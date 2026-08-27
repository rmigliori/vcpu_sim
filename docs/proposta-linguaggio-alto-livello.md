# Proposta per un linguaggio ad alto livello sopra `vcpu_sim`

> Documento di discussione (bozza). Nessuna implementazione: serve solo a fissare
> le idee prima di decidere se e come procedere.

Glossario rapido delle sigle usate più avanti, così restano chiare:

- **albero sintattico** (in inglese *AST*, "abstract syntax tree"): la
  rappresentazione ad albero di un'espressione dopo che è stata letta.
- **lato destro / lato sinistro** (in inglese *RHS* / *LHS*): rispettivamente la
  parte a destra e a sinistra del segno di assegnamento.
- **BNF / EBNF**: la Backus-Naur Form e la sua versione *estesa* (con `{ }` per la
  ripetizione e `[ ]` per l'opzionale), la stessa notazione dei diagrammi
  sintattici del Pascal.
- **elemento per elemento** (in inglese *elementwise*): un'operazione applicata a
  ogni corsia del vettore, `x[0] op y[0]`, `x[1] op y[1]`, ecc.

---

## 1. Idea di fondo: un linguaggio *centrato sugli array*

Il valore didattico del simulatore è il modello vettoriale (strip-mining, VL,
maschere, gather/scatter, riduzioni). Un linguaggio C-like "normale" sprecherebbe
questo. L'idea è rendere il **vettore un tipo di prima classe**: si scrive la
matematica su interi array e il compilatore genera da solo il ciclo
`setvl`/`vload`/.../`bne` che oggi si scrive a mano.

È esattamente il salto da `standalone/saxpy_scalar.vasm` a `standalone/saxpy.vasm`, ma
automatico.

L'ispirazione "array-first" viene dal **Fortran 90/NumPy**: operazioni su interi
array (`Y = a*X + Y`), sezioni di array con i due punti (`X(:)`, la nostra `x[:]`)
e il costrutto `WHERE` per il mascheramento. Sono tutte idee nate nel Fortran 90 e
riprese da NumPy; qui cadono esatte sul modello vettoriale del simulatore.

Nome provvisorio del linguaggio: **`vc`**.

---

## 2. Sintassi di esempio

```
// tipi scalari: int, float ; tipi array: int[], float[]
func saxpy(a: float, x: float[], y: float[]) {
    y[:] = a * x[:] + y[:];        // op su interi array -> strip-mined
}

func relu(x: float[]) {
    x[:] = max(x[:], 0.0);         // vmax elemento per elemento
}

func norm(x: float[]) -> float {
    return sqrt(sum(x[:] * x[:]));  // sum() -> riduzione vredsum
}

func clamp_pos(x: float[]) {
    where (x[:] > 10.0) {           // predicazione -> vmask + vaddm/vstorem
        x[:] += 100.0;
    }
}
```

---

## 3. Mappa dei costrutti sulla ISA (tutto già esistente)

| Costrutto ad alto livello                 | Istruzioni ISA generate                                              |
|-------------------------------------------|---------------------------------------------------------------------|
| `a[:] = f(b[:], c[:])` (op su array)      | ciclo `setvl`/`vload`/`v*`/`vstore` + avanzamento puntatori + `bne`  |
| `sum/max/min(v[:])`                       | `vredsum`/`vredmax`/`vredmin` (+ accumulo tra i blocchi)             |
| `where(cond) { ... }`                     | `vmslt`/`vmsgt`/`vmseq` -> `vmask`, poi `vaddm`/`vsubm`/`vmulm`/`vstorem` |
| `a[idx[:]]` (indicizzazione con array)    | `vloadx`/`vstorex` (gather/scatter)                                 |
| scalari, `if`, `while`, `func`            | codice scalare + `beq`/`bne`/`blt`/`j` + convenzione a registri     |

---

## 4. Precedenza degli operatori

Dalla precedenza più alta (lega più forte) alla più bassa:

| Liv. | Operatori                              | Descrizione                          | Associatività |
|------|----------------------------------------|--------------------------------------|---------------|
| 1    | `()` `f(...)` `a[i]` `a[:]` `a[i:j]`    | raggruppamento, chiamata, indice, slice | sinistra   |
| 2    | `-x` `!x` (unari), `sqrt(x)` ...        | negazione aritmetica/logica          | destra        |
| 3    | `*` `/` `%`                             | moltiplicativi                       | sinistra      |
| 4    | `+` `-`                                 | additivi                             | sinistra      |
| 5    | `<` `<=` `>` `>=`                       | relazionali                          | sinistra      |
| 6    | `==` `!=`                               | uguaglianza                          | sinistra      |
| 7    | `&&`                                    | AND logico (con corto-circuito)      | sinistra      |
| 8    | `\|\|`                                  | OR logico (con corto-circuito)       | sinistra      |
| 9    | `=` `+=` `-=` `*=` `/=`                 | assegnamento                         | **destra**    |

Note di progettazione:

1. **Relazionali sopra uguaglianza**, come in C: `a < b == c` significa
   `(a < b) == c`.
2. **Assegnamento a precedenza minima e associativo a destra**: in
   `y[:] = a*x[:] + y[:]` tutto il lato destro si calcola prima, poi si scrive nel
   lato sinistro. Questo delimita esattamente la parte che il compilatore deve
   vettorizzare.
3. **Niente operatori bit-a-bit `&` `|` all'inizio**: si usano solo i logici
   `&&` / `||` per le condizioni, per evitare la classica trappola del C (in cui
   `&` ha precedenza più bassa di `==`). Se in futuro servissero i bit-a-bit
   interi (`and`/`or`/`xor` della ISA), li si aggiunge con nomi espliciti e una
   precedenza scelta a mente fredda.
4. **Lo slice `[:]` lega più forte di tutto**: `a[:] * b[:]` è
   `(a[:]) * (b[:])`, cioè moltiplicazione elemento per elemento di due vettori.
5. **Unari a destra**: siccome l'indicizzazione (livello 1) lega più forte
   dell'unario (livello 2), `-x[:]` viene letto correttamente come `-(x[:])`.

---

## 5. Grammatica in EBNF

Convenzioni: `{ }` = zero o più ripetizioni, `[ ]` = opzionale, `|` = alternativa,
in MAIUSCOLO le categorie lessicali (identificatori, numeri).

```
programma        ::= { funzione }

funzione         ::= "func" IDENT "(" [ parametri ] ")" [ "->" tipo ] blocco

parametri        ::= parametro { "," parametro }
parametro        ::= IDENT ":" tipo
tipo             ::= "int" | "float" | "int" "[]" | "float" "[]"

blocco           ::= "{" { istruzione } "}"

istruzione       ::= dichiarazione
                   | assegnamento
                   | istruzione_if
                   | istruzione_while
                   | istruzione_where
                   | istruzione_return
                   | chiamata ";"

dichiarazione    ::= "var" IDENT ":" tipo [ "=" espressione ] ";"
assegnamento     ::= destinazione operatore_assegna espressione ";"
destinazione     ::= IDENT | IDENT "[" espressione "]" | IDENT "[" ":" "]"

istruzione_if    ::= "if" "(" espressione ")" blocco [ "else" blocco ]
istruzione_while ::= "while" "(" espressione ")" blocco
istruzione_where ::= "where" "(" espressione ")" blocco
istruzione_return::= "return" [ espressione ] ";"

operatore_assegna::= "=" | "+=" | "-=" | "*=" | "/="
```

Espressioni, scritte "a scala" per livelli di precedenza (ogni regola richiama
quella di precedenza più alta: più una regola sta in basso, più forte lega —
stesso schema di `Expression -> SimpleExpression -> Term -> Factor` del Pascal):

```
espressione      ::= or_logico
or_logico        ::= and_logico   { "||" and_logico }
and_logico       ::= uguaglianza  { "&&" uguaglianza }
uguaglianza      ::= relazionale  { ( "==" | "!=" ) relazionale }
relazionale      ::= additivo     { ( "<" | "<=" | ">" | ">=" ) additivo }
additivo         ::= moltiplic    { ( "+" | "-" ) moltiplic }
moltiplic        ::= unario       { ( "*" | "/" | "%" ) unario }
unario           ::= ( "-" | "!" ) unario | postfisso
postfisso        ::= primario { "[" espressione "]" | "[" ":" "]" | "(" [ argomenti ] ")" }
primario         ::= NUMERO | IDENT | "(" espressione ")"

argomenti        ::= espressione { "," espressione }
```

Nota: il `where(cond) { ... }` è una **istruzione**, non un operatore, quindi non
entra nella tabella di precedenza. La `cond` al suo interno è una normale
espressione booleana (livelli 5-8). Questo tiene la predicazione fuori dalla
grammatica delle espressioni e la rende esplicita.

---

## 6. Una scelta da fissare: la lunghezza degli array

Due opzioni:

- **(a) lunghezza nota staticamente** — es. `float[10]`.
- **(b) array come coppia (puntatore, lunghezza)** passata a runtime.

Consiglio la **(b)**: è più realistica, permette funzioni generiche come `saxpy`
su array di qualunque dimensione, e combacia con la convenzione già usata negli
esempi (il `r3 = n` che gira nei registri). La lunghezza vive in un registro
intero, esattamente come oggi.

---

## 7. Architettura del compilatore, per fasi

Rispecchia la progressione già seguita per la toolchain (assembler -> oggetti ->
linker):

1. **Analisi lessicale e sintattica**: dal testo all'albero sintattico
   (espressioni, `func`, `if`/`while`, slice `[:]`, `where`).
2. **Controllo dei tipi**: scalare contro array; verifica che gli array in una
   stessa espressione abbiano lunghezze compatibili.
3. **Abbassamento** ("lowering"): espande `a[:] = espr` nel ciclo di strip-mining
   canonico; `where` in maschera; `sum` in riduzione. Qui vive tutta
   l'intelligenza vettoriale.
4. **Allocazione dei registri** semplice (sui 16 interi + 16 float; spill in
   memoria se servono più temporanei) e **generazione del codice** che stampa
   `.vasm`.
5. (Opzionale) **convenzione di chiamata** formalizzata — di fatto è il
   "prototipo" già documentato negli esempi in `linked/multi/`: argomenti in
   `r1..`, valore di ritorno in `r0`/`f0`.

Il compilatore produce `.vasm`, che poi passa per la toolchain esistente:
`asm` -> `.vo` -> `ld` -> `.vx` -> `run`. Nessun backend nuovo, solo un front-end.

---

## 8. Estensioni naturali (fase successiva)

- **Fusione di operazioni**: riconoscere `y[:] = a*x[:] + y[:]` come un unico
  schema e generare `vmacc` invece di `vmul` + `vadd`. Un piccolo ottimizzatore
  che insegna la fusione di operazioni.
- **Emissione dell'assembly commentato** (`vc --emit-asm`) come strumento
  didattico: affianca sorgente ad alto livello e assembly generato.

---

## 9. Primo passo minimo e soddisfacente

Analizzatore sintattico + **un solo costrutto** (`a[:] = espr` con `+ - *`
elemento per elemento) che genera `.vasm`. Criterio di successo: il `saxpy`
scritto in `vc` deve produrre lo stesso conteggio del `saxpy.vasm` scritto a mano
(17 istruzioni / 40 operazioni su elementi / 94 cicli). Sarebbe, per il
front-end, l'equivalente dell'invariante di regressione già usato per il resto del
progetto.

---

## 10. Variante in stile Pascal (alternativa di sintassi)

Il compilatore sottostante non cambia; cambia solo la "buccia" sintattica.

| Costrutto        | Stile C (proposto qui)          | Stile Pascal                              |
|------------------|---------------------------------|-------------------------------------------|
| assegnamento     | `y = a * x;`                    | `y := a * x;`                             |
| blocco           | `{ ... }`                       | `begin ... end`                          |
| funzione         | `func saxpy(...) { }`           | `procedure saxpy(...); begin ... end`     |
| dichiarazione    | `var i: int;`                   | `var i: integer;`                        |
| array intero     | `int[]`                         | `array of integer`                       |
