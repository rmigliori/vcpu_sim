# Lo scheduler, dedotto — il nucleo fattuale

> **Questo è metà di un nucleo fattuale, e l'altra metà è generata.**
>
> | | dove | come si mantiene |
> |---|---|---|
> | numeri **misurati** | [`generated/scheduler-measures.md`](generated/scheduler-measures.md) | `python3 tools/scheduler_facts.py` |
> | numeri **dedotti** | **questo file** | a mano, e non c'è altro modo |
>
> I quattro documenti sullo scheduler — i due didattici e i due per il collega —
> **citano** questi due file invece di ripetere i numeri. Quattro testi che
> ripetono lo stesso numero sono quattro verità, e la prosa non ha un
> compilatore.

## Il contratto di questa pagina

Le tre regole di costruzione decise il 12/09/2026 (§«I QUATTRO DOCUMENTI» in
[`stato-lavori.md`](stato-lavori.md)), qui nella forma che le rende
controllabili riga per riga:

1. **un nucleo fattuale solo.** Un numero che compare in un documento e non qui
   è un difetto del documento;
2. **il didattico è la sorgente, quello per il collega è una compressione**, e
   le versioni nell'altra lingua si **rigenerano**, non si editano;
3. **ogni numero o è misurato, e si dice come, o è dedotto, e si dice da cosa.**
   Questa pagina è il posto dei secondi, ed è per questo che esiste separata:
   un'inferenza non ha un comando che la riproduca, e mescolarla ai numeri
   generati la farebbe sembrare misurata.

La terza regola non è una precauzione teorica. Il 12/09 è successo **due volte**
di riportare come misura un'inferenza, e uno dei due casi è qui sotto per esteso
perché è l'esempio migliore che il progetto abbia.

> **Lo scopo è LO SCHEDULER, non il progetto.** §13 è chiusa dal 12/09, quindi
> c'è un oggetto concluso da descrivere. Il sincronizzatore, il linker e `vc`
> non lo sono e non lo saranno per il 1° ottobre 2026.

---

## 1. Il prezzo di §13.7, e perché due terzi non si possono più misurare

`mutex_unlock` **arma e cede**, invece di armare e aspettare il tick. Il costo
su `test_mutex`, che ha **quattro** unlock (§3.41):

| variante | istruzioni | cicli | cessioni |
|---|---:|---:|---:|
| senza cessione (prima di §13.7) | 3038 | 6321 | 0 |
| arma e cede **sempre** | 3679 | 7700 | 4 |
| **chiede, poi cede** (oggi) | 3434 | 7143 | 2 |

**Solo l'ultima riga è misurabile oggi**, e infatti è la sola che compare anche
in [`generated/scheduler-measures.md`](generated/scheduler-measures.md), dove un
comando la riproduce. Le prime due sono **storiche**: quelle varianti del codice
non esistono più, quindi i loro numeri non si rifanno, si citano — e questa
pagina è il posto dove la differenza fra le due cose si vede.

**Dedotte da quella tabella**, e non misurate a parte:

- **+13%** è il prezzo di §13.7 come sta in `master`: `7143 / 6321 − 1`;
- **+22%** era il prezzo della versione incondizionata: `7700 / 6321 − 1`;
- **−7,2%** è ciò che il ramo `mutex-cede-solo-se-serve` ha tolto:
  `7143 / 7700 − 1`;
- le due cessioni evitate — quelle che rientravano su sé stesse — valgono
  **245 istruzioni** (`3679 − 3434`) e **557 cicli** (`7700 − 7143`), cioè
  **278 cicli l'una**.

> **Una correzione, ed è il primo frutto di questa pagina.** §3.41 scrive «i
> **245 cicli** recuperati sono le loro». 245 è la differenza delle
> **istruzioni**; i cicli sono **557**. Nessuna conclusione di §3.41 cambia — il
> −7% è calcolato sui cicli e resta — ma il numero è nella colonna sbagliata, e
> un documento che lo copiasse direbbe che una cessione a vuoto costa 122 cicli
> invece di 278. È esattamente il modo in cui quattro testi che ripetono un
> numero diventano quattro verità, colto mentre succedeva.

> **Due cessioni su quattro è una proprietà di QUESTO test, non del rimedio.**
> `test_mutex` è costruito per avere un contendente pronto durante la sezione
> critica, cioè è il **caso peggiore**. §13.5 sostiene che sotto un ceiling
> corretto quella coda sia *sempre* vuota: in un sistema dichiarato bene la
> risposta sarebbe «nessuno» quasi sempre e il risparmio quasi totale. Questa è
> una **deduzione dalla specifica**, non un'estrapolazione dalla misura, e i
> documenti devono presentarla così.

## 2. I «37 cicli di latenza» NON SONO UN NUMERO, e vanno raccontati

Una prima lettura del 12/09 aveva riportato «37 cicli di guadagno di latenza»
dal passaggio a `task_yield`. **Era deriva di fase, non una misura.** Le
commutazioni sono **sette in ogni variante** — senza cessione, con cessione
incondizionata, con la domanda — e uno spostamento di quell'ordine si spiega con
la diversa lunghezza del percorso, non con una preemption anticipata.

Per misurare davvero quel guadagno servirebbe sapere **quando** il contendente è
diventato pronto, cioè un tag dentro `task_ready`: strumentazione di kernel, che
dal 13/09 ha il suo prerequisito (`.ifdef` e `-D`, §3.44) ma non è ancora
scritta.

**La giustificazione di §13.7 non è quindi un numero su questo test**, ed è così
che i quattro documenti devono metterla: la latenza dell'unlock diventa
**deterministica** — il costo si paga sempre e si conosce — mentre l'attesa
evitata era variabile e limitata solo dal periodo del timer.

Questa voce sta qui, e non fra i rifiuti, perché è la migliore illustrazione
della regola 3 che il progetto possieda: un numero plausibile, in una tabella di
numeri veri, che non misurava ciò che diceva di misurare.

## 3. Cosa `ctest` asserisce, e cosa i cicli non asseriscono

La tabella in [§2 del file generato](generated/scheduler-measures.md) mette
fianco a fianco due cose che nei documenti conviene non confondere:

- i valori attesi dei `vasm_check` sono **stati** — sequenze di `dumps` che
  dicono *cosa è successo*;
- i cicli sono **costi**, e **nessun test li guarda**.

Per metà dei programmi dello scheduler è una scelta scritta accanto alla
dichiarazione (*«nessuno dei cinque numeri dipende dal periodo del timer»*), e
quella scelta è ciò che rende i test insensibili al tick. La conseguenza, che è
il motivo per cui il file generato esiste: **un cambiamento che sposta i cicli
lascia `ctest` verde**, e un numero copiato a mano in un documento diverge in
silenzio.

## 4. `test_chain` e la saturazione: un limite dedotto, non asserito

`test_chain` mette in fila A→B→C. A **800 cicli di periodo** del timer
l'ultimo anello **muore di fame senza che nessuna asserzione scatti** (§3.32).

È un fatto dedotto dal confronto fra il costo della catena e il periodo, non un
valore che un test pretende: la suite non ha, e per come è costruita non può
avere, un `EXPECT` che dica «C ha girato abbastanza». Un documento che presenti
la catena deve dire **entrambe** le cose, o racconta un sistema che si accorge
di una cosa di cui non si accorge.

## 5. Le tre attese, e quale delle tre è una deduzione

| | cosa aspetti | timeout | fonte |
|---|---|---|---|
| **mailbox** | il **mondo**, che può non rispondere | sì | §9, *scritto* |
| **semaforo** | una **risorsa** che un altro task restituirà entro un tempo calcolabile | no | §13.8, **dedotto** |
| **mutex** | niente: sotto un ceiling corretto non blocca mai | — | §13.5, **dedotto** |

Le ultime due righe non sono osservazioni sul codice: sono **conseguenze della
specifica**, e il codice le rispetta. Vanno citate come tali — con il numero di
sezione — e non come «il semaforo non ha il timeout», che è vero e dice l'altra
metà.

---

## 6. Il costo della scansione — misurato, ma su un ALTRO programma

Dal 13/09 `scheduler` porta un tag, nella sola configurazione strumentata
(§3.47). Su `test_tmgr_marks`, diciotto scansioni:

| | n | min | max | media | **jitter** |
|---|---:|---:|---:|---:|---:|
| la scelta di chi gira | 18 | 51 | 177 | 118 | **126** |
| *gli stessi, @ 100 MHz* | | *0,51 µs* | *1,77 µs* | *1,18 µs* | ***1,26 µs*** |

```bash
vcpu_sim run build/vasm/test_tmgr_marks.vx --marks rec.txt
python3 tools/marks.py read marks.conf rec.txt --vx build/vasm/test_tmgr_marks.vx
```

E **undici preemption**, che sono un fatto *letto* dal kernel e non dedotto —
`sp_preempted` è l'unico ramo che riempie uno slot `PCB.preempted` (§3.48):

| | n | fuori CPU | @ 100 MHz |
|---|---:|---|---|
| preemption | 11 | **909** (×6) e **2108** (×5) cicli, alternati | **9,09 µs** e **21,1 µs** |

L'alternanza non è rumore: sono le due quantità di lavoro diverse che il tick
innesca. È il tempo in cui un task è stato tolto dalla CPU **involontariamente**,
e va detto così — non è latenza di preemption (quella è il ritardo fra «qualcuno
di più prioritario è pronto» e «gira»), è la sua conseguenza sulla vittima.

> **Questi due numeri sono stati per un giorno 915 e 2120, ed è la storia di un
> difetto che si è visto solo leggendo i cicli come tempo.** `cadb40b` fa
> **registrare il proprio nome** a ogni task: tre istruzioni sotto
> `.ifdef MARKS`, che il commento dichiarava «una volta sola». Per l'idle lo
> erano — `taskI` rientra in `loopI`, cioè **sotto** il blocco. Per `taskA` e
> `tmgr_task` **no**: rientravano con `j` sulla propria etichetta, che sta
> *sopra*, quindi si ripresentavano a ogni giro e pagavano **6 cicli ogni
> volta**. Innocuo nell'effetto — riscrivevano lo stesso id sullo stesso canale
> — ma dentro la misura, su un programma che esiste per misurare.
>
> Il 14/09 il rientro è stato spostato sotto il blocco (`loopA`, `tmgr_loop`), e
> i due numeri sono **tornati esatti**: 909 e 2108 sono i valori misurati a
> `6314e19`, cioè al commit **prima** che la registrazione esistesse. Verificato
> eseguendo quel commit in un worktree, non dedotto. La finestra corta conteneva
> una ri-registrazione (6 cicli), quella lunga due (12).
>
> Resta una differenza con il **2093** che i documenti riportavano il 13/09: è
> più vecchia di tutto questo, ed è il tag **RECV col porto dati** (§3.51), che
> in quella finestra ci sta per progetto. I 18 numeri della scansione non si
> sono mai mossi, in nessuno dei tre stati.

**Questi numeri non stanno nel file generato, e la ragione è una regola.** Il
nucleo fattuale misurato è per definizione l'albero **pulito**: un programma
strumentato costa di più — `test_tmgr` passa da 21925 a 21943 istruzioni — e i
suoi cicli sono veri ma sono di un altro programma.
`tools/scheduler_facts.py` **rifiuta** di raccoglierli, e non lo deduce dal nome:
lo chiede al manifesto che scrive il build.

Quindi la riga qui sopra va citata **dicendo che è strumentata**. Il jitter è
più del doppio del minimo, ed è il genere di numero che una media nasconde: 118
di media su un massimo di 177 racconta un sistema diverso da quello vero.

## 7. Il periodo del tick è COMPRESSO, e col tempo a schermo qualcuno lo leggerà come una scelta di progetto

Dal 14/09 gli strumenti leggono i cicli **anche come tempo** (§6.1 del manuale:
`CPU_HZ` = 100 MHz, un ciclo = 10 ns). Questo rende visibile una cosa che finché
i numeri erano cicli non guardava nessuno: **i tick di questa suite sono
lontanissimi da un tick realtime vero.**

| periodo armato | programmi | @ 100 MHz |
|---:|---|---:|
| 500 cicli | `scheduler`, `block`, `mutex`, `semaphore` | **5 µs** |
| 2000 cicli | `chain` | **20 µs** |
| 4000 cicli | `tmgr`, `vectors` | **40 µs** |

Un tick realtime vero sta fra **1 e 10 ms**. Questi stanno fra 5 e 40 µs, cioè
**da 25 a 2000 volte più veloci**.

**È una compressione deliberata, e la ragione è che i test devono finire in
fretta**: `ctest` intero gira in cinque secondi perché il più lungo dei
programmi simula 1,60 ms di tempo-macchina e tutti gli altri stanno sotto i
200 µs. I periodi non sono nemmeno scelti
come frequenze — sono scelti come **soglie**, cioè in rapporto al costo del giro
che devono interrompere (§4 qui sopra, e la tabella in §3.32 dell'handoff). A
`800` cicli `test_chain` misura la saturazione invece della propagazione: è un
numero rispetto al *lavoro*, non rispetto al *tempo*.

Il rischio è nuovo e nasce proprio dal tempo a schermo: finché la pagina diceva
«4000 cicli» nessuno ci leggeva una scelta di sistema, mentre «40 µs» somiglia a
una specifica. **Non lo è.** Un documento che mostri questi tempi deve dire che
il periodo è compresso, o racconta un RTOS che gira a 25 kHz di tick.

## Cosa manca ancora qui dentro

Elencato perché un nucleo fattuale incompleto è utile e un nucleo fattuale che
finge di essere completo no. In ordine di quanto serve ai quattro documenti:

- **la latenza di preemption**, che è il numero che un lettore realtime cerca
  per primo e che ancora **non esiste**: richiede un tag in `task_ready` e uno
  nel dispatcher. Dal 13/09 non è più bloccato da niente — il vocabolario del
  build c'è (`vasm_config`, §3.47) e il disegno pure (§3.46) — è lavoro da
  fare, non una decisione da prendere;
- **il costo di un context switch**, idem: oggi si legge solo come differenza
  fra totali, che è la forma in cui §3.41 ha già prodotto un'inferenza
  sbagliata;
- **il costo del salvataggio del contesto vettoriale**, che `test_vectors`
  esercita dal 12/09 e che il salvataggio **pigro** (ancora da decidere)
  renderebbe evitabile nel caso comune;
- **la ripartizione fra kernel e task** per programma: `tools/trace.py` la sa
  distinguere — è la parte tratteggiata della pagina — ma il suo stdout oggi dà
  solo il totale per proprietario, quindi il file generato non la riporta.
