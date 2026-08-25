# Toolchain multi-file — specifica dei formati `.vo` e `.vx`

> Stato: **IMPLEMENTATA** (Fasi 1-3 + estensioni). Proposte del §11 tutte adottate.
> Pipeline `assembler → oggetto .vo → linker → eseguibile .vx → loader` con
> **compilazione separata**, **simboli globali/locali/esterni** e **rilocazioni**.
> Comandi: `vcpu_sim asm|ld|run|nm|ar` (con `ld -e`, `nm` ordinato, addend
> `sym+off`, librerie `.va`). Il percorso legacy `vcpu_sim <prog.vasm>` è invariato.

---

## 1. Perché servono le rilocazioni

Oggi l'assembler è a due passi e **risolve i simboli subito**: in pass 2,
`parse_int()` chiama `find_symbol()` e scrive nel campo dell'istruzione il
valore assoluto (indice di istruzione per le label di codice, indirizzo per
quelle di dati). Questo funziona solo perché *tutto il programma è in un file*
e quindi le basi (indice 0 del codice, indirizzo 0 dei dati) sono note.

Con la compilazione separata questo salta per due motivi:

1. **Simboli esterni**: un modulo può riferirsi a `printf` o `sum` definiti in
   *un altro* file. Al momento dell'assemblaggio quel valore **non esiste**.
2. **Basi mobili**: anche i simboli *locali* non hanno più un valore assoluto.
   Quando il linker concatena i moduli, il codice del modulo B parte dall'indice
   di istruzione `N` (non 0) e i suoi dati partono dall'indirizzo `D` (non 0).

La soluzione, come nei linker reali (stile ELF), è **non risolvere** al momento
dell'assemblaggio i riferimenti a simboli, ma:

- lasciare un **segnaposto** (0) nel campo dell'istruzione;
- registrare una **rilocazione**: "il campo _X_ dell'istruzione _i_ va
  poi riempito col valore del simbolo _S_ (più un addend)".

Il **linker** assegna a ogni modulo una base di codice e una di dati, calcola il
valore finale di ogni simbolo e **applica** (patch) le rilocazioni.

---

## 2. Modello di indirizzamento

La macchina ha **due spazi separati**:

| Spazio | Unità        | Dove vive                              | Base al link            |
|--------|--------------|----------------------------------------|-------------------------|
| Codice | **indice istruzione** (`pc`) | array `Instr prog[]`     | `text_base` (indice)    |
| Dati   | **byte**     | `cpu->mem[]` (1 MiB, byte-addressable) | `data_base` (indirizzo) |

> Nota didattica: nella CPU reale il `pc` conta byte; qui il `pc` è un **indice
> di istruzione**. Perciò il codice si riloca sommando un *offset di indice*, i
> dati sommando un *indirizzo base*. Sono due aritmetiche diverse → due tipi di
> rilocazione.

Valore finale di un simbolo, calcolato dal linker:

```
S.is_code == 1  ->  valore = text_base(modulo_di_S) + S.offset
S.is_code == 0  ->  valore = data_base(modulo_di_S) + S.offset
```

dove `S.offset` è la posizione **relativa al modulo** (indice istruzione entro
il proprio `.text`, oppure byte entro il proprio `.data`).

---

## 3. Scope dei simboli (binding)

Nuove direttive nell'assembler:

| Direttiva     | Binding  | Significato                                                    |
|---------------|----------|----------------------------------------------------------------|
| *(nessuna)*   | `local`  | Default. Visibile solo nel modulo. Non entra nella mappa globale. |
| `.global sym` | `global` | Esportato. Entra nella mappa globale; risolve gli `extern` altrui. |
| `.extern sym` | `extern` | Importato/indefinito qui. Deve essere risolto da un `global` altrove. |

Regole:

- Un `.extern` non risolto da nessun `.global` a fine link → **errore**
  (`undefined reference to 'sym'`).
- Due `.global` con lo stesso nome → **errore** (`duplicate global 'sym'`).
- Un simbolo può essere dichiarato `.global` *e* definito nello stesso modulo
  (caso normale dell'export). L'ordine di dichiarazione/definizione è libero
  (l'assembler è già a due passi).

---

## 4. Tipi di rilocazione

Tre, che si distinguono per **quale campo** dell'istruzione patchano:

| Tipo      | Campo patchato    | Uso tipico                                  |
|-----------|-------------------|---------------------------------------------|
| `R_CODE`  | `Instr.target`    | `beq/bne/blt/j <label>` (indice istr.)      |
| `R_DATA`  | `Instr.imm`       | simbolo dato come operando (displacement)    |
| `R_ADDR`  | `Instr.imm`       | `li rX, <label>` (indirizzo-di: codice o dato) |

Formula di patch applicata dal linker:

```
campo = valore_finale(simbolo) + addend
```

L'`addend` (di solito 0) serve per riferimenti tipo `li rX, arr+8`. La sintassi
`simbolo+N` / `simbolo-N` (N decimale o hex) è supportata dall'assembler: per i
dati l'offset è in **byte**, per il codice in **indici di istruzione**. Vale sia
in compilazione separata sia in modo legacy.

> `R_CODE` richiede `is_code == 1` (saltare a un dato è un errore), `R_DATA`
> richiede `is_code == 0`. `R_ADDR` invece è **polimorfo**: `li rX, sym` prende
> l'indirizzo di `sym` qualunque sia la sua natura e il linker sceglie il valore
> in base a `is_code` — indice di istruzione per il codice (puntatore a
> funzione), indirizzo in byte per i dati. Il linker verifica comunque la
> coerenza di `R_CODE`/`R_DATA` per intercettare bug tipo "label di codice usata
> come displacement dati".

---

## 5. Formato oggetto `.vo` (testo)

Formato **testo ispezionabile con `cat`** (coerente col taglio didattico). Una
sezione per riga-intestazione `.<nome> <conteggio>`, seguita dalle righe di
contenuto. Righe vuote e `; commento` ignorate.

```
VO1                              ; magic + versione formato

.text 3                          ; 3 istruzioni; op come intero, campi grezzi
; idx op  a  b  c        imm            fimm  target   ; <disasm indicativo>
0    0    1  0  0          0             0        0     ; li r1, 0        (imm ← reloc)
1    17   0  1  0          0             0        0     ; vload v0, r1
2    12   0  0  0          0             0        0     ; j loop          (target ← reloc)

.data 40                         ; 40 byte di immagine dati del modulo
; offset: 16 byte esadecimali per riga (little-endian, come in memoria)
0000: 00 00 20 41 00 00 a0 41 00 00 f0 41 00 00 20 42
0010: 00 00 48 42 cd cc 6c 42 00 00 8c 42 9a 99 a1 42
0020: 00 00 b4 42 00 00 c8 42
; forma compatta per zeri da .space:  .zero <n>

.symtab 3                        ; nome  sez   offset  binding  is_code
sum      text  0       global   1
arr      data  0       local    0
printf   ----  -       extern   -        ; indefinito: sez/offset non significativi

.reloc 2                         ; tipo   site_idx  simbolo  addend
R_ADDR   0        arr      0               ; patch prog[0].imm  = addr(arr)+0
R_CODE   2        loop     0               ; patch prog[2].target = idx(loop)+0

.end
```

### Note sul formato `.vo`

- **`.text`**: ogni istruzione è dumpata coi campi **grezzi** della `struct Instr`
  (`op a b c imm fimm target`). `op` è l'intero dell'enum `OpCode`; il commento
  finale è solo per l'occhio umano e viene ignorato dal loader.
  - `fimm` è stampato con `%.17g` per **round-trip esatto** del `double`.
  - I campi che saranno riempiti da una rilocazione contengono `0` (segnaposto).
- **`.data`**: immagine byte del modulo (non più scritta direttamente in
  `cpu->mem`, ma in un buffer del modulo). Le run di zeri prodotte da `.space`
  possono usare la forma compatta `.zero <n>` per non gonfiare il file.
- **`.symtab`**: per ogni simbolo `nome | sezione | offset | binding | is_code`.
  Gli `extern` hanno sezione/offset non significativi (`----` / `-`).
- **`.reloc`**: `tipo | indice_istruzione | nome_simbolo | addend`.

---

## 6. Formato eseguibile `.vx` (testo)

Come il `.vo`, ma **completamente linkato**: rilocazioni già applicate, nessun
binding/extern residuo, tutti i simboli con **valore finale assoluto**. Aggiunge
un **entry point** e una **mappa simboli globale** (per `nm` e per i breakpoint
cross-modulo del debugger).

```
VX1                              ; magic + versione
.entry 0                         ; indice istruzione di partenza (vedi §7)

.text 17                         ; immagine codice finale, tutti i campi risolti
0    0   1  0  0   40   0   0     ; li r1, 40    (imm risolto: addr di arr)
...

.data 80                         ; immagine dati finale (concatenazione moduli)
0000: ...

.symmap 4                        ; mappa GLOBALE: nome  valore  is_code
main     0        1
saxpy    5        1
arr      0        0
out      40       0

.end
```

### Note sul formato `.vx`

- **`.entry`**: indice istruzione da cui parte l'esecuzione (`cpu->pc`).
- **`.text` / `.data`**: immagini finali. Il loader copia `.data` in `cpu->mem`
  a partire dall'indirizzo base (0) e carica `.text` in `prog[]`.
- **`.symmap`**: solo simboli **globali** risolti; alimenta `nm` (Fase 2) e i
  breakpoint per nome nel debugger.

---

## 7. Entry point

Convenzione: l'esecuzione parte dal simbolo globale **`main`**. Regole:

1. Se esiste un `.global main` → `.entry = valore(main)`.
2. Altrimenti, entry = **0** (primo modulo, prima istruzione) con un *warning*.
3. Con `ld -e <sym>` si sceglie un entry point diverso: deve essere un simbolo
   di **codice globale**, altrimenti il link fallisce con errore.

---

## 8. CLI a sottocomandi (unico binario `vcpu_sim`)

```
vcpu_sim asm  <in.vasm> -o <out.vo>            # assembla → oggetto rilocabile
vcpu_sim ld   <a.vo|lib.va> ... [-e <sym>] -o <out.vx>  # linka → eseguibile
vcpu_sim run  <prog.vx> [--trace|--debug]     # carica ed esegue
vcpu_sim nm   [-n|-p] [-r] <prog.vx | obj.vo>  # elenca i simboli (ordinati)
vcpu_sim ar   <lib.va> <o1.vo> ...             # crea archivio/libreria
```

**Compatibilità all'indietro**: `vcpu_sim <prog.vasm>` (senza sottocomando)
resta valido e fa asm+run in memoria come oggi — così tutti gli esempi e
l'invariante di regressione saxpy (17 istr / 40 vec-elem-ops / 94 cicli)
continuano a funzionare senza modifiche.

---

## 8b. Librerie: formato archivio `.va` e inclusione selettiva

Un archivio `.va` è un **bundle di oggetti** `.vo` in formato testo, prodotto da
`ar`. Ogni membro è preceduto da `.member <nome>` e contiene il blocco `.vo`
verbatim (`VO1` … `.end`):

```
VA1
.member msaxpy.vo
VO1
.text 12
...
.end
.member unused.vo
VO1
...
.end
```

**Inclusione selettiva** (linker): gli oggetti passati *esplicitamente* a `ld`
sono sempre inclusi; i **membri di un archivio entrano solo su richiesta**. Il
linker mantiene l'insieme dei simboli *referenziati ma non ancora definiti* e
include un membro se **esporta un simbolo globale** che serve. Il processo si
ripete fino a **punto fisso**, così l'inclusione è anche **transitiva** (se A
tira dentro B, e B referenzia C, anche C viene incluso). I membri mai richiesti
restano fuori dall'eseguibile.

---

## 9. Esempio worked-through a due moduli

**`math.vasm`** (esporta `saxpy`, definisce dati locali? no, solo codice):

```asm
.text
.global saxpy
saxpy:
    ; ... corpo ...
    halt
```

**`main.vasm`** (usa `saxpy` e dati propri):

```asm
.data
arr:   .float 10 20 30 40 5
out:   .space 5
.text
.extern saxpy
.global main
main:
    li   r1, arr        ; R_ADDR su 'arr' (indirizzo dato)
    li   r2, out        ; R_ADDR su 'out'
    j    saxpy          ; R_CODE su 'saxpy' (extern)
```

Pipeline:

```
vcpu_sim asm math.vasm -o math.vo
vcpu_sim asm main.vasm -o main.vo
vcpu_sim ld  main.vo math.vo -o prog.vx
vcpu_sim run prog.vx
```

Al link (ordine `main.vo` poi `math.vo`):

| Modulo   | text_base | data_base |
|----------|-----------|-----------|
| main.vo  | 0         | 0         |
| math.vo  | 3         | 40        |

Risoluzione simboli:

| Simbolo | Modulo | sez  | offset | valore finale        |
|---------|--------|------|--------|----------------------|
| main    | main   | text | 0      | 0 + 0  = 0           |
| arr     | main   | data | 0      | 0 + 0  = 0           |
| out     | main   | data | 20     | 0 + 20 = 20          |
| saxpy   | math   | text | 0      | 3 + 0  = 3           |

Patch delle rilocazioni di `main.vo`:

| Reloc   | site (istr) | campo   | = valore(sym)+addend |
|---------|-------------|---------|----------------------|
| R_ADDR  | 0 (`li r1`) | imm     | 0                    |
| R_ADDR  | 1 (`li r2`) | imm     | 20                   |
| R_CODE  | 2 (`j`)     | target  | 3                    |

`.entry` = valore(`main`) = 0.

---

## 10. Impatti sul codice esistente (per la Fase 1)

1. **`Symbol`** (assembler): aggiungere `enum Binding {LOCAL, GLOBAL, EXTERN}` e
   un campo `section` (TEXT/DATA) + `offset` relativo al modulo. Oggi c'è già
   `is_code`; si deriva da `section`.
2. **Assembler**: separare `assemble()` in una funzione che produce una
   *struttura oggetto in memoria* (istruzioni con segnaposto + tabella
   rilocazioni + symtab + immagine dati in buffer proprio, non in `cpu->mem`).
   - `parse_int()` per un simbolo **non** risolve più: emette segnaposto +
     registra la rilocazione, con `field` dedotto dall'istruzione
     (`target` per branch/jump → `R_CODE`; `imm` per `li` → `R_ADDR`;
     simbolo dato come operando → `R_DATA`).
   - Nuove direttive `.global` / `.extern`.
3. **Emitter `.vo`**: serializza la struttura oggetto nel formato §5.
4. **Linker**: legge N `.vo`, assegna `text_base`/`data_base`, costruisce la
   mappa globale, verifica extern/duplicati, applica le rilocazioni, emette `.vx`.
5. **Loader `.vx`**: legge l'immagine, riempie `prog[]` e `cpu->mem`, imposta
   `cpu->pc = entry`, poi `vcpu_run_ex()`.
6. **`main.c`**: dispatch dei sottocomandi (`asm`/`ld`/`run`), mantenendo il
   percorso legacy `vcpu_sim <prog.vasm>`.

Nessuna modifica all'interprete (`execute()`), al timing o all'ISA: la toolchain
lavora **prima** dell'esecuzione. L'invariante saxpy resta il test di non
regressione.

---

## 11. Domande aperte per la revisione

1. **Ordine dati/basi**: concatenare i `.data` nell'ordine dei `.vo` sulla riga
   di `ld` (proposto), oppure ordinare per nome/modulo? Proposto: ordine di
   comando, semplice e prevedibile.
2. **`.space` come byte o come elementi?** Oggi `.space N` riserva `N*4` byte
   (N *elementi* float). Manteniamo questa semantica anche nell'immagine dati.
3. **Addend**: ~~introdurre subito la sintassi `sym+off` in `li`/branch, o
   rimandarla (addend sempre 0 in Fase 1)?~~ **Implementato**: sintassi
   `simbolo+N` / `simbolo-N` supportata (offset in byte per i dati, in indici di
   istruzione per il codice).
4. **Entry point**: `main` va bene come nome convenzionale, o preferisci
   `_start`? Proposto: `main`.
5. **`op` nel `.vo`**: intero dell'enum (proposto, robusto) o mnemonico testuale
   (più leggibile ma richiede tabella inversa)? Proposto: intero + commento.
```