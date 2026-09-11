# Proposta: un sincronizzatore per più VM e modelli di hardware

> Scritta l'**11/09/2026**. **Niente di questo è implementato**, ed è
> intenzionale: il documento esiste per congelare decisioni prese mentre erano
> fresche, non per descrivere codice. Quello che c'è oggi è un device singolo in
> MMIO alimentato da una traccia a cicli (§3.1 del manuale, §3.37 dell'handoff):
> deterministico, ma con il tempo posseduto dal simulatore e i device dentro di
> lui.
>
> Ogni sezione dice se la scelta è **DECISA** dalla discussione o **APERTA**.

---

## 1. Il problema, in una riga: chi possiede il tempo

Un simulatore ha un tempo suo — qui i **cicli**, contati dal modello di costo —
che non ha niente a che vedere con il tempo dell'orologio da parete. Appena
esiste più di un partecipante (una CPU simulata, un modello di periferica,
un'altra CPU simulata) bisogna decidere chi lo governa, e le risposte possibili
sono tre:

| Chi possiede il tempo | Conseguenza |
|---|---|
| **il simulatore**, che chiama i device come funzioni | deterministico, ma i device non sono entità separate: vivono dentro `vcpu.c`. **È ciò che c'è oggi** |
| **l'orologio di parete**, con i device in thread | i device sono veri processi concorrenti, ma l'istante in cui agiscono lo decide lo scheduler di Linux: gli stessi ingressi danno risultati diversi |
| **un arbitro esterno**, e tutti gli altri chiedono il permesso di avanzare | deterministico **e** con partecipanti separati. È quello che fanno i simulatori seri (in SystemC il tempo è del kernel di simulazione, non dei moduli) |

La terza è la proposta. Il resto del documento è cosa comporta.

---

## 2. Il tempo comune è ASSOLUTO, non in cicli — **DECISA**

L'unità condivisa fra i partecipanti sono **nanosecondi simulati**, non cicli.
Ogni partecipante converte nei propri.

Non è pedanteria: due VM possono avere frequenze diverse, ed è la configurazione
normale di un apparato vero (un DSP veloce accanto a un controllore lento). Se
l'unità comune fossero i cicli, «ciclo 1000» vorrebbe dire due istanti diversi
per due partecipanti diversi, e l'intero protocollo sarebbe sbagliato.

È una decisione da prendere alla prima riga di codice, perché cambiarla dopo
significa riscrivere ogni messaggio.

> **APERTA**: la frequenza di ciascun partecipante è un parametro di
> configurazione — dove si dichiara (riga di comando, file, handshake iniziale)
> non è deciso.

---

## 3. L'arbitro è un processo separato — **DECISA**

Con una VM sola si poteva farle possedere il tempo e trattare i device come
richiedenti: meno pezzi, meno messaggi. **Con due VM quella strada è chiusa**,
perché nessuna delle due può essere insieme arbitro e parte.

L'arbitro non simula niente: tiene il tempo corrente, sa chi sono i
partecipanti, concede i quanti e inoltra gli eventi.

---

## 4. Barriera a quanti, e il quanto lo detta la fisica — **DECISA**

I partecipanti non avanzano liberamente: avanzano di un **quanto** per volta.

```
    partecipante                     arbitro
    ------------                     -------
    READY(t = fine quanto)     ->
                               <-    GRANT(fino a t + Q)
    ... simula fino a t + Q ...
    READY(t + Q)               ->
```

Sincronizzarsi a ogni ciclo simulato costerebbe uno scambio di messaggi per
ciclo, e un simulatore che oggi fa milioni di istruzioni al secondo ne farebbe
decine di migliaia. Il quanto è ciò che rende la cosa praticabile — e non è
un'approssimazione che ci si concede, perché ha un limite superiore **esatto**:

> **Q non può superare la latenza minima con cui un partecipante può
> influenzarne un altro.**

Se la seriale fra due VM impiega δ a consegnare un byte, nessuna delle due può
avere effetto sull'altra prima di δ, quindi possono simulare indipendentemente
per δ senza che nessuno osservi niente fuori ordine. È il *lookahead* della
simulazione a eventi distribuita, ed è la ragione per cui **più il canale è
lento, più veloce va la simulazione**.

> Ha la stessa forma del tick del kernel (§3.30 dell'handoff: «il tick è il
> quanto»): non si misura ciò che una grandezza già esistente misura da sé.

---

## 5. Gli eventi generati in un quanto si applicano nel successivo — **DECISA**

Un partecipante che durante il quanto `[t, t+Q)` produce un evento lo consegna
all'arbitro con la sua **data**, e l'arbitro lo recapita ai destinatari perché lo
applichino quando ci arrivano. Siccome la data non può cadere prima di `t + δ` e
`Q ≤ δ`, l'evento non finisce mai nel passato di chi lo riceve.

È la disciplina *conservativa*: nessuno torna indietro, nessuno deve essere in
grado di annullare ciò che ha già fatto.

---

## 6. A parità di istante, l'ordine è per identificatore — **DECISA**

Due eventi datati allo stesso nanosecondo devono essere applicati in un ordine
**stabile**, e l'unico stabile è quello dichiarato: l'identificatore del
partecipante che li ha prodotti, assegnato all'handshake. Mai l'ordine di arrivo
sulla socket, che dipende da come Linux ha schedulato i processi.

Senza questa regola tutto il resto non serve a niente: si sarebbe conservato il
tempo e perso il determinismo.

---

## 7. I messaggi — **APERTA nella forma, DECISA nella sostanza**

Cinque, e i campi sono quelli che le sezioni precedenti impongono:

| Messaggio | Direzione | Campi | Quando |
|---|---|---|---|
| `HELLO` | partecipante → arbitro | nome, frequenza, latenza minima offerta | all'avvio |
| `WELCOME` | arbitro → partecipante | id assegnato, quanto Q, istante iniziale | dopo che tutti si sono presentati |
| `READY` | partecipante → arbitro | id, istante raggiunto, eventi prodotti | a fine quanto |
| `GRANT` | arbitro → partecipante | istante fino a cui avanzare, eventi in arrivo | quando **tutti** hanno detto READY |
| `BYE` | partecipante → arbitro | id, motivo | terminazione (`halt`, errore) |

Il trasporto sono socket UNIX o pipe: i messaggi sono di poche decine di byte e
due per quanto per partecipante, quindi la memoria condivisa non aggiungerebbe
niente se non complicazione.

Q lo calcola l'arbitro come **minimo** delle latenze offerte negli `HELLO`: è
l'unico che le conosce tutte, ed è la regola di §4 applicata.

---

## 8. Il canale seriale fra due VM — **APERTA**

Il primo cliente vero del sincronizzatore, e il motivo per cui vale la pena
farlo: due nodi che si scambiano messaggi sono il modello di un apparato vero,
dove processori distinti comunicano su un bus.

Quello che è **deciso** è che il canale ha una **latenza dichiarata** e che
quella latenza vive nel tempo simulato: il byte parte a `t` e arriva a `t + δ`.
Una seriale istantanea sarebbe irrealistica e nasconderebbe proprio i difetti
che un sistema distribuito ha.

Resta da decidere quasi tutto il resto: se δ si ricava da una velocità in baud o
si dichiara direttamente; se il canale ha una profondità (quanti byte in volo) e
cosa succede quando è pieno; se la consegna è a byte o a messaggio; se serve
modellare gli errori di linea. **Nessuna di queste va decisa prima di avere
un'applicazione che ci parli sopra.**

---

## 9. Due modi di avanzare, stessa architettura — **DECISA**

L'arbitro concede i quanti; **quanto in fretta** li concede è un parametro:

- **libero** — appena tutti sono pronti. Massima velocità, ed è il modo dei
  test;
- **ancorato** — un nanosecondo simulato vale un tempo di parete fissato, e
  l'arbitro aspetta prima di concedere. È il modo delle demo, ed è l'unico in
  cui un essere umano che preme un tasto ha senso.

È il vero guadagno rispetto a oggi: **il rapporto fra tempo simulato e tempo
reale si sceglie** invece di subirlo, e i modelli di hardware sotto sono gli
stessi.

---

## 10. Cosa si rompe, e va previsto prima — **DECISA che vada gestito**

- **il determinismo dipende da Q.** Cambiare il quanto cambia i risultati:
  quindi Q va **dichiarato** e fissato nei test, esattamente come il periodo del
  timer. Un test che non dice il proprio Q non è riproducibile;
- **un partecipante che muore blocca tutti.** Serve un timeout sull'attesa dei
  `READY` e un messaggio che nomini chi manca, altrimenti si guarda un prompt
  fermo senza sapere perché;
- **il debugger ferma il sistema, non la VM.** `--debug` sospende il tempo
  simulato per tutti, ed è corretto; ma il timeout di cui sopra deve saperlo, o
  accusa un blocco mentre stai leggendo un registro;
- **`ctest` vuole un comando solo.** Un test che avvia tre processi a mano è
  fragile. La via è che il simulatore (o l'arbitro) lanci i partecipanti come
  figli — `--device ./kbd_hw`, `--vm ./nodo_b.vx` — così ogni test resta una
  riga.

---

## 11. Cosa questo documento NON copre

- **l'interrupt della tastiera.** La macchina ha una sola sorgente di interrupt
  e un solo vettore; una seconda richiede una causa leggibile o un secondo
  vettore, più la scelta sull'annidamento (oggi l'HAL entra in trap con `IE` a
  zero, quindi una seconda sorgente resterebbe pendente per tutta l'ISR). È una
  decisione sul **modello della macchina**, indipendente dalla
  sincronizzazione;
- **il thread che legge `stdin`.** Resta il modo più economico di avere
  interattività senza niente di tutto questo, e si innesta sugli stessi campi del
  device (§3.37 dell'handoff);
- **il locator.** L'indirizzo base dei device è cablato in un `.equ` perché il
  linker concatena in command order e non conosce regioni. Con più partecipanti e
  più device la cosa peggiora, e i device sono il primo cliente vero che il
  locator avrà.

---

## 12. Quando farlo

Un sincronizzatore è **infrastruttura**, e vale in proporzione a ciò che ci gira
sopra. Costruito oggi, ci girerebbero due VM che si scambiano byte con dentro,
in ciascuna, lo stesso kernel che non sa ancora fare la cosa per cui esiste: i
registri vettoriali sono dichiarati volatili attraverso la preemption
([`machine.vasm:17-19`](../hal/impl/src/machine.vasm#L17-L19)), quindi due task
che usassero `v0..v7` si corromperebbero a vicenda — e nessun test se ne accorge
perché sotto `rtos/` e `hal/` non c'è **una sola** istruzione vettoriale.

Chiuso quel fronte e scritta un'applicazione che fa un lavoro vero, metterla su
due nodi che si parlano è il passo naturale, e il sincronizzatore nasce **con un
cliente**. È il criterio che ha reso buone le decisioni di questo progetto: il
ceiling ha una forma precisa perché c'era un caso da decidere, non perché era
nella lista.
