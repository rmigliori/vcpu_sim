#!/usr/bin/env python3
"""scheduler_facts.py — IL NUCLEO FATTUALE MISURATO, generato dalla build.

    python3 tools/scheduler_facts.py            # -> docs/generated/scheduler-measures.md
    python3 tools/scheduler_facts.py --check    # rigenera e confronta: esce 1 se diverge

I quattro documenti sullo scheduler citano dei numeri, e la prima regola di
costruzione e' che quei numeri stanno in UN POSTO SOLO con scritto COME sono
stati ottenuti. Questo e' quel posto per la meta' MISURATA: i numeri che si
ottengono eseguendo un programma. L'altra meta' -- i numeri DEDOTTI, che non si
eseguono ma si derivano -- sta in docs/scheduler-facts.md, scritta a mano
perche' un'inferenza non si misura.

--- PERCHE' GENERATO, E NON UNA TABELLA SCRITTA BENE ---

Il 13/09/2026 i cicli di test_mutex comparivano in prosa in tre punti
dell'handoff. Nessuno dei tre e' controllato da niente: i vasm_check di
rtos/test/ asseriscono gli STATI (le sequenze di dumps), e per scelta
dichiarata non i cicli -- "nessuno dei cinque numeri dipende dal periodo del
timer" sta scritto accanto a meta' di quei test. Quindi oggi un cambiamento che
sposta i cicli lascia ctest verde e i documenti sbagliati, in silenzio.

Quattro testi che ripetono lo stesso numero sono quattro verita', e la prosa non
ha un compilatore. Questa pagina gliene da' uno: --check la rigenera e pretende
un diff vuoto, che e' la stessa forma di prova di tools/fingerprint.sh.

--- NON DUPLICA NIENTE, E CHIEDE AL BUILD ---

Di suo questo strumento sa una cosa sola, e la legge da scheduler-facts.conf:
QUALI programmi sono in argomento. Tutto il resto lo sa gia' il build e glielo
si chiede con `ctest --show-only=json-v1`:

    il .vx da eseguire, gli argomenti della MACCHINA (--kbd e simili, che
    vivono in ARGS di vasm_check) e i valori attesi dichiarati.

Riscriverli qui sarebbe la seconda verita' che l'esercizio esiste per togliere.

--- NON C'E' UNA DATA NELL'INTESTAZIONE, ED E' VOLUTO ---

Una data di generazione farebbe divergere il file da se' stesso a ogni
esecuzione, e --check non potrebbe esistere. Quando e' stato rigenerato lo dice
git, che e' il posto dove quella domanda ha gia' una risposta.
"""

import json
import os
import re
import subprocess
import sys

ROOT  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONF  = os.path.join(ROOT, "scheduler-facts.conf")
OUT   = os.path.join(ROOT, "docs", "generated", "scheduler-measures.md")
TRACE = os.path.join(ROOT, "tools", "trace.py")


def die(msg):
    print("scheduler_facts: " + msg, file=sys.stderr)
    sys.exit(1)


def read_conf(path):
    """I nomi dei test in argomento, nell'ordine in cui sono dichiarati."""
    names = []
    with open(path, encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) != 2 or parts[0] != "program":
                die("%s:%d: si scrive «program <nome>», non %r" % (path, lineno, line))
            names.append(parts[1])
    if not names:
        die("%s non dichiara nessun programma" % path)
    return names


def read_configs(build):
    """programma -> configurazione, dal manifesto che scrive il build.

    IL NUCLEO FATTUALE E' PER DEFINIZIONE L'ALBERO PULITO. Un programma col
    kernel strumentato costa di piu' -- misurato: +18 istruzioni su test_tmgr,
    abbastanza da spostare un valore atteso -- e i suoi cicli sono veri ma sono
    di un altro programma. Citarli nei quattro documenti direbbe il falso sul
    sistema che si spedisce.

    La configurazione la dichiara il BUILD (vasm_write_manifest), non il
    suffisso di un nome: una convenzione sui nomi e' una cosa che nessuno
    garantisce, ed e' la famiglia di difetto che questo progetto ha gia' pagato
    tre volte. Se il manifesto non c'e', l'albero e' vecchio e si dice.
    """
    path = os.path.join(build, "vasm", "configs.txt")
    if not os.path.exists(path):
        die("%s non c'e': ricostruisci l'albero (cmake -S . -B %s)" % (path, build))
    out = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.rstrip("\n").split(" ", 1)
            out[parts[0]] = (parts[1].strip() if len(parts) > 1 else "")
    return out


def read_build(build):
    """Cosa il build sa di ogni test: comando, .vx, argomenti macchina, attesi.

    Il comando di un vasm_check ha questa forma, e si legge da destra:
        cmake -P vasm_check.cmake -- <MODE> <EXPECT> <vcpu_sim> run <vx> [args...]
    Il percorso legacy a file singolo non ha «run»: quei test non sono
    programmi linkati e non sono in argomento qui, quindi si scartano.
    """
    try:
        js = subprocess.run(["ctest", "--show-only=json-v1"], cwd=build,
                            capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        die("«ctest --show-only=json-v1» in %s non risponde (%s). Build fatta?" % (build, e))

    tests = {}
    for t in json.loads(js).get("tests", []):
        cmd = t.get("command", [])
        if "--" not in cmd or "run" not in cmd:
            continue
        rest = cmd[cmd.index("--") + 1:]
        if len(rest) < 4:
            continue
        mode, expect = rest[0], rest[1]
        run = rest[2:]
        if run[1] != "run":
            continue
        tests[t["name"]] = {
            "sim":    run[0],
            "vx":     run[2],
            "args":   run[3:],
            "mode":   mode,
            "expect": expect,
        }
    return tests


STATS = {
    "instructions executed": "instr",
    "vector element ops":    "vecops",
    "cycles (timing model)": "cycles",
}


def measure(t):
    """Esegue il programma e ne legge le tre statistiche."""
    out = subprocess.run([t["sim"], "run", t["vx"]] + t["args"],
                         capture_output=True, text=True).stdout
    got = {}
    for line in out.splitlines():
        if ":" not in line:
            continue
        key, _, val = line.partition(":")
        name = STATS.get(key.strip())
        if name:
            got[name] = int(val.strip())
    if len(got) != len(STATS):
        die("%s non ha stampato le statistiche attese" % t["vx"])
    return got


# «  B             3360   31.9%» — le righe di occupazione sullo stdout di trace.py
OCC = re.compile(r"^\s+(\S+)\s+(\d+)\s+([\d.,]+)%\s*$")


def occupancy(t):
    """La ripartizione per proprietario, dallo stesso strumento che disegna la pagina.

    Gli argomenti della MACCHINA vanno dopo un «--», ed e' la ragione per cui
    trace.py li vuole cosi': sono gli stessi che il build mette in ARGS.
    """
    cmd = [sys.executable, TRACE, t["vx"]]
    if t["args"]:
        cmd += ["--"] + t["args"]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        return []
    rows = []
    for line in r.stdout.splitlines():
        m = OCC.match(line)
        if m:
            rows.append((m.group(1), int(m.group(2)), m.group(3).replace(",", ".")))
    return rows


HEAD = """<!-- GENERATO DA tools/scheduler_facts.py — NON MODIFICARE A MANO. -->

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

| programma | istruzioni | vec-elem-ops | cicli |
|---|---:|---:|---:|
"""

MID = """
Misurati così, uno per riga:

```bash
{cmds}
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
"""

TAIL = """
## 3. Chi gira, e per quanto

Dedotto dalla traccia con la regola di `tools/trace.py`: il proprietario cambia
solo quando il `pc` entra nel **corpo** di un task o dell'ISR, e tutto ciò che
sta in mezzo — scheduler, mailbox, pool, HAL — è kernel **a carico di chi
girava**. È corretto perché una libreria si esegue per conto di chi l'ha
chiamata, ma resta un'**inferenza**, e per questo la sezione lo dice invece di
lasciarlo capire: le percentuali qui sotto sono lette da una traccia reale e
attribuite da una regola.

Un programma che non compare non ha prodotto una traccia leggibile.

"""


def render(names, tests):
    out = [HEAD]
    cmds = []
    for n in names:
        t = tests[n]
        m = measure(t)
        vx = os.path.relpath(t["vx"], ROOT)
        out.append("| `%s` | %d | %d | %d |\n" % (n, m["instr"], m["vecops"], m["cycles"]))
        cmds.append("vcpu_sim run %s%s" %
                    (vx, (" " + " ".join(t["args"])) if t["args"] else ""))
    out.append(MID.format(cmds="\n".join(cmds)))

    for n in names:
        t = tests[n]
        args = " ".join(t["args"])
        out.append("| `%s` | %s | `%s` | %s |\n" %
                   (n, t["mode"], t["expect"], ("`%s`" % args) if args else "—"))
    out.append(TAIL)

    for n in names:
        rows = occupancy(tests[n])
        if not rows:
            continue
        out.append("**`%s`**\n\n| proprietario | cicli | quota |\n|---|---:|---:|\n" % n)
        for who, cyc, pct in rows:
            out.append("| %s | %d | %s%% |\n" % (who, cyc, pct))
        out.append("\n")
    return "".join(out)


def main(argv):
    check = "--check" in argv
    rest  = [a for a in argv if not a.startswith("--")]
    build = rest[0] if rest else os.path.join(ROOT, "build")

    names = read_conf(CONF)
    tests = read_build(build)
    missing = [n for n in names if n not in tests]
    if missing:
        die("scheduler-facts.conf nomina test che il build non ha: %s" % ", ".join(missing))

    # La regola, applicata invece che raccomandata: niente numeri strumentati.
    cfg = read_configs(build)
    sporchi = []
    for n in names:
        prog = os.path.splitext(os.path.basename(tests[n]["vx"]))[0]
        if cfg.get(prog):
            sporchi.append("%s (%s, configurazione '%s')" % (n, prog, cfg[prog]))
    if sporchi:
        die("questi sono STRUMENTATI e il nucleo fattuale e' l'albero pulito:\n  "
            + "\n  ".join(sporchi)
            + "\nTogli le loro righe da scheduler-facts.conf: i loro cicli sono veri,"
              "\nma sono di un altro programma.")

    text = render(names, tests)

    if check:
        try:
            with open(OUT, encoding="utf-8") as f:
                have = f.read()
        except OSError:
            die("%s non c'e': rigeneralo" % os.path.relpath(OUT, ROOT))
        if have != text:
            print("scheduler_facts: %s NON e' aggiornato — rigeneralo" %
                  os.path.relpath(OUT, ROOT), file=sys.stderr)
            return 1
        print("scheduler_facts: %s e' aggiornato" % os.path.relpath(OUT, ROOT))
        return 0

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write(text)
    print("scheduler_facts: scritto %s (%d programmi)" %
          (os.path.relpath(OUT, ROOT), len(names)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
