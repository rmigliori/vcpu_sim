<!-- GENERATO DA tools/scheduler_facts.py — NON MODIFICARE A MANO. -->

# Lo scheduler, misurato — il nucleo fattuale

> **Questo file è generato.** Si rifà con
> `python3 tools/scheduler_facts.py`, e
> `python3 tools/scheduler_facts.py --check` pretende che rigenerarlo non lo
> cambi. Una modifica scritta qui a mano sopravvive fino alla prima
> rigenerazione, e non oltre.
>
> **Ogni numero di questa pagina è MISURATO**, e sotto ogni tabella c'è il
> comando che lo produce. I numeri **dedotti** — quelli che non si eseguono ma
> si derivano — non stanno qui: stanno in
> [`scheduler-facts.md`](../scheduler-facts.md), con scritto da cosa.
>
> Quali programmi compaiono è l'unica cosa che questa pagina non deduce, ed è
> dichiarata in [`scheduler-facts.conf`](../../scheduler-facts.conf). Il `.vx`,
> gli argomenti della macchina e i valori attesi vengono dal build
> (`ctest --show-only=json-v1`), non da una copia tenuta qui.

## 1. Il costo di ogni programma

`cicli` è il modello di timing (§6 del manuale), non un tempo reale.

> **Il tempo della colonna accanto è una LETTURA dei cicli, non una misura in
> più.** I cicli li decide `vcpu.c`, e questa macchina non ha un sistema di
> memoria: niente cache miss, niente contesa DMA, niente conflitti di banco.
> Moltiplicarli per una frequenza dà un numero che *sembra* più reale di quello
> da cui viene — «118 cicli» si legge come un numero di modello, «1,18 µs» si
> legge come una misura. Per questo il tempo non compare mai da solo, e la
> frequenza è sempre scritta accanto: è un'ipotesi dichiarata.
>
> La frequenza è quella che la macchina dichiara in `include/vcpu.h` e stampa
> accanto ai cicli — qui non ce n'è una copia.

| programma | istruzioni | vec-elem-ops | cicli | tempo @ 100 MHz |
|---|---:|---:|---:|---:|
| `scheduler` | 2032 | 0 | 4458 | 44,6 µs |
| `block` | 2549 | 0 | 5503 | 55,0 µs |
| `chain` | 7293 | 0 | 16632 | 166,3 µs |
| `mailbox` | 288 | 0 | 639 | 6,39 µs |
| `semaphore` | 3639 | 0 | 7555 | 75,5 µs |
| `mutex` | 3446 | 0 | 7173 | 71,7 µs |
| `tmgr` | 22216 | 0 | 49834 | 498,3 µs |
| `coop` | 4866 | 0 | 10531 | 105,3 µs |
| `events` | 9265 | 0 | 18767 | 187,7 µs |
| `vectors` | 92611 | 43712 | 159668 | 1,60 ms |

Misurati così, uno per riga:

```bash
vcpu_sim run <build>/vasm/test_scheduler.vx
vcpu_sim run <build>/vasm/test_block.vx
vcpu_sim run <build>/vasm/test_chain.vx
vcpu_sim run <build>/vasm/test_mailbox.vx
vcpu_sim run <build>/vasm/test_semaphore.vx
vcpu_sim run <build>/vasm/test_mutex.vx
vcpu_sim run <build>/vasm/test_tmgr.vx
vcpu_sim run <build>/vasm/test_coop.vx
vcpu_sim run <build>/vasm/test_events.vx --kbd 2000:a,6000:b,10000:c,14000:d,18000:e
vcpu_sim run <build>/vasm/test_vectors.vx
```

## 2. Cosa asserisce `ctest`, e su cosa tace

La colonna **atteso** è la dichiarazione che il build confronta a ogni `ctest`.
Vale la pena leggerla accanto alla tabella sopra: sono **stati**, non cicli.
Nessuno dei programmi qui sotto asserisce un numero di cicli, e per metà di essi
è una scelta scritta accanto alla dichiarazione (*«nessuno dei cinque numeri
dipende dal periodo del timer»*). Da cui la ragione per cui questa pagina
esiste: **i cicli non li guarda nessun test**, e in prosa divergerebbero in
silenzio.

| programma | modo | atteso | argomenti della macchina |
|---|---|---|---|
| `scheduler` | DUMPS | `65 3 8 56` | — |
| `block` | DUMPS | `3 25` | — |
| `chain` | DUMPS | `3 3 3 629` | — |
| `mailbox` | DUMPS | `0 1 2 11 22 0 33 0 0` | — |
| `semaphore` | DUMPS | `123456 2 1 -6` | — |
| `mutex` | DUMPS | `0 0 1 1 1` | — |
| `tmgr` | DUMPS | `5 0 2318` | — |
| `coop` | DUMPS | `0 5 5 5 0` | — |
| `events` | DUMPS | `0 5 495 5 718` | `--kbd 2000:a,6000:b,10000:c,14000:d,18000:e` |
| `vectors` | DUMPS | `0 0 20 19 39` | — |

## 3. Chi gira, e per quanto

Dedotto dalla traccia con la regola di `tools/trace.py`: il proprietario cambia
solo quando il `pc` entra nel **corpo** di un task o dell'ISR, e tutto ciò che
sta in mezzo — scheduler, mailbox, pool, HAL — è kernel **a carico di chi
girava**. È corretto perché una libreria si esegue per conto di chi l'ha
chiamata, ma resta un'**inferenza**, e per questo la sezione lo dice invece di
lasciarlo capire: le percentuali qui sotto sono lette da una traccia reale e
attribuite da una regola.

Un programma che non compare non ha prodotto una traccia leggibile.

**`scheduler`**

| proprietario | cicli | quota |
|---|---:|---:|
| ISR | 2007 | 45.0% |
| H | 1050 | 23.6% |
| idle | 765 | 17.2% |
| boot | 321 | 7.2% |
| D | 180 | 4.0% |
| M | 135 | 3.0% |

**`block`**

| proprietario | cicli | quota |
|---|---:|---:|
| ISR | 2571 | 46.7% |
| W | 2160 | 39.3% |
| idle | 455 | 8.3% |
| boot | 317 | 5.8% |

**`chain`**

| proprietario | cicli | quota |
|---|---:|---:|
| idle | 6791 | 40.8% |
| B | 2808 | 16.9% |
| A | 2442 | 14.7% |
| ISR | 2230 | 13.4% |
| C | 1860 | 11.2% |
| boot | 501 | 3.0% |

**`mailbox`**

| proprietario | cicli | quota |
|---|---:|---:|
| boot | 639 | 100.0% |

**`semaphore`**

| proprietario | cicli | quota |
|---|---:|---:|
| boot | 4258 | 56.4% |
| ISR | 2955 | 39.1% |
| idle | 342 | 4.5% |

**`mutex`**

| proprietario | cicli | quota |
|---|---:|---:|
| ISR | 2404 | 33.5% |
| U | 1782 | 24.8% |
| S | 1176 | 16.4% |
| W | 953 | 13.3% |
| boot | 573 | 8.0% |
| T | 285 | 4.0% |

**`tmgr`**

| proprietario | cicli | quota |
|---|---:|---:|
| idle | 24385 | 48.9% |
| tmgr | 12833 | 25.8% |
| ISR | 6858 | 13.8% |
| A | 4375 | 8.8% |
| boot | 1383 | 2.8% |

**`coop`**

| proprietario | cicli | quota |
|---|---:|---:|
| B | 3360 | 31.9% |
| A | 3302 | 31.4% |
| C | 2967 | 28.2% |
| boot | 902 | 8.6% |

**`events`**

| proprietario | cicli | quota |
|---|---:|---:|
| idle | 16238 | 86.5% |
| E | 2219 | 11.8% |
| boot | 310 | 1.7% |

**`vectors`**

| proprietario | cicli | quota |
|---|---:|---:|
| A | 57582 | 36.1% |
| B | 55577 | 34.8% |
| ISR | 41911 | 26.2% |
| idle | 4094 | 2.6% |
| boot | 504 | 0.3% |

