#!/usr/bin/env python3
"""trace.py — LA TRACCIA TEMPORALE: chi gira, e da quando a quando.

    python3 tools/trace.py out/vasm/test_tmgr.vx        # -> out/trace.html

Un programma guidato da un DEVICE non e' autosufficiente: senza il suo
alimentatore resta a pollare un flag che nessuno alzera' e non termina mai. Le
opzioni della macchina si passano dopo un --, e sono le stesse che vasm_check
mette in ARGS (§3.37):

    python3 tools/trace.py out/vasm/test_events.vx -- --kbd "2000:a,6000:b"

Nasce il 07/09/2026 per chiudere un debito che l'handoff portava da §3.29: senza
uno strumento che dica chi gira e in quale intervallo, ogni numero prodotto dai
test resta un'osservazione invece che una misura. I contatori di un test dicono
QUANTO ha girato un task; questo dice QUANDO, e soprattutto quanto di quel tempo
sia finito nel kernel per conto suo.

--- COME FA, VISTO CHE IL PROPRIETARIO NON E' NEL TRACE ---

Il simulatore stampa `pc` e ciclo per ogni istruzione, e basta: `current` vive in
memoria e non compare da nessuna parte. Il proprietario di un intervallo si
DEDUCE, con una regola sola:

    il proprietario cambia solo quando il pc entra nel CORPO di un task o
    dell'ISR; tutto cio' che sta in mezzo -- scheduler, mailbox, pool, HAL -- e'
    kernel A CARICO DI CHI GIRAVA.

E' corretto perche' una libreria si esegue sempre per conto di chi l'ha chiamata,
ma resta un'inferenza e va detto: le fasce tratteggiate della pagina sono dedotte,
non lette.

--- I CORPI SI RICONOSCONO PER CONVENZIONE, NON PER ELENCO ---

Un elenco scritto a mano sarebbe una seconda verita' da tenere allineata ai test.
Valgono invece i nomi che i programmi di questo progetto usano gia':

    main            il boot
    timer_isr       l'ISR del tick
    task<X>         il task X   (taskI e' l'idle per convenzione)
    <nome>_task     il task di sistema <nome> (tmgr_task -> "gestore")

Un corpo si estende dalla sua etichetta a quella del corpo successivo dentro lo
STESSO modulo: cosi' le etichette interne (loopI, tmgr_drain, isr_manda) non
vanno elencate e non vengono scambiate per kernel.

--- PERCHE' SERVONO I LABEL LOCALI, E NON BASTA `nm` ---

`nm` pubblica solo i simboli .global. Con quelli soli, ctx_save -- che globale non
e' -- finisce contata dentro _trap_entry, e i rami dello scheduler dentro il
simbolo che li precede: il bilancio per routine diventa una somma di cose
diverse. Quindi ogni modulo si ri-assembla con --emit-expanded per averne le
etichette, e la base di ciascuno si ricava da un simbolo globale presente sia nel
listato sia nel programma linkato.

Il costo e' un `asm` per modulo, che su questo progetto e' istantaneo.

--- COSA PRODUCE ---

Una pagina HTML autosufficiente (i dati sono dentro; le uniche risorse esterne
sono i font, che senza rete cadono sui fallback dichiarati). Il modello e'
tools/trace.template.html: si modifica quello per cambiare il disegno, e questo
file non si tocca.
"""

import argparse, bisect, collections, datetime, glob, json, os, re, subprocess, sys

RADICE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QUI    = os.path.dirname(os.path.abspath(__file__))
TIMEOUT = 60      # secondi: un programma di questo progetto ne impiega meno di uno


def sim():
    """Il simulatore, che e' anche assembler e nm. Costruito da CMake in out/."""
    for p in ("out/vcpu_sim", "build/vcpu_sim"):
        c = os.path.join(RADICE, p)
        if os.path.exists(c):
            return c
    sys.exit("vcpu_sim non trovato: cmake -B out -S . && cmake --build out -j")


def sorgenti(vx):
    """I moduli .vasm del programma: le librerie, piu' l'APPLICAZIONE.

    Le librerie si possono prendere tutte -- i loro simboli globali sono unici,
    quindi una che non fa parte del programma non aggancia niente. L'applicazione
    no: ogni test definisce `main` a indice 0, quindi prenderle tutte darebbe a
    tutte la stessa base e le etichette si sovrapporrebbero. Si ricava dal nome
    del programma, che e' l'unico legame affidabile fra il .vx e il sorgente.
    """
    fuori = []
    for pat in ("hal/impl/src/*.vasm", "generic/*/impl/src/*.vasm",
                "rtos/*/impl/src/*.vasm", "rtos/*/*/impl/src/*.vasm"):
        fuori += glob.glob(os.path.join(RADICE, pat))
    nome = os.path.splitext(os.path.basename(vx))[0] + ".vasm"
    app = [p for p in glob.glob(os.path.join(RADICE, "**", nome), recursive=True)
           if os.sep + "impl" + os.sep not in p
           and os.path.relpath(p, RADICE).split(os.sep)[0] not in IGNORA]
    if not app:
        print(f"attenzione: non trovo il sorgente dell'applicazione ({nome}): "
              f"i corpi dei task non saranno riconosciuti", file=sys.stderr)
    return sorted(fuori) + app[:1]


IGNORA = ("out", "build")     # le cartelle di build, che rispecchiano l'albero


def includes():
    """Le cartelle interface/ delle librerie: sono i -I dell'assembler.

    Le cartelle di build vanno escluse, e non per pulizia: CMake ci rispecchia
    dentro la stessa gerarchia, quindi ogni interface/ comparirebbe due volte e
    si sfonderebbe il tetto di 16 -I dell'assembler (MAX_INC_DIRS) con dei
    doppioni.
    """
    fuori = []
    for pat in ("*/interface", "*/*/interface", "*/*/*/interface"):
        for d in glob.glob(os.path.join(RADICE, pat)):
            rel = os.path.relpath(d, RADICE)
            if os.path.isdir(d) and rel.split(os.sep)[0] not in IGNORA:
                fuori.append(d)
    return sorted(set(fuori))


def globali(vx):
    out = subprocess.run([sim(), "nm", vx], capture_output=True, text=True).stdout
    return {m.group(2): int(m.group(1))
            for m in (re.match(r"\s*(\d+)\s+T\s+(\S+)", l) for l in out.splitlines()) if m}


def listato(src, inc, tmp):
    """(indice, etichetta) per ogni label di codice del modulo, piu' la sua lunghezza."""
    lst = os.path.join(tmp, "traccia.lst")
    cmd = [sim(), "asm", src, "-o", os.path.join(tmp, "traccia.vo"), "--emit-expanded", lst]
    for d in inc:
        cmd += ["-I", d]
    if subprocess.run(cmd, capture_output=True, text=True).returncode:
        return [], 0          # non assembla da solo: non e' un modulo del programma
    fuori, pend, ultimo = [], [], 0
    for line in open(lst):
        m = re.match(r"^(\w+):", line)
        if m:
            pend.append(m.group(1)); continue
        m = re.match(r"^\s+(\d+)\s+", line)
        if m:
            ultimo = int(m.group(1))
            for n in pend:
                fuori.append((ultimo, n))
            pend = []
    return fuori, ultimo + 1


def proprietario(etichetta):
    """La convenzione dei nomi -> il proprietario, o None se non e' un corpo."""
    if etichetta == "main":
        return "boot"
    if etichetta == "timer_isr":
        return "ISR"
    if etichetta == "taskI":
        return "idle"
    m = re.fullmatch(r"task([A-Z]\w*)", etichetta)
    if m:
        return m.group(1)
    m = re.fullmatch(r"(\w+)_task", etichetta)
    if m:
        return m.group(1)
    return None


def raccogli(vx, tmp):
    """(mappa fine, corpi con i loro estremi, mappa dei soli globali)."""
    G = globali(vx)
    fini = [(a, n) for n, a in G.items()]     # mappa fine: ogni etichetta nota
    grezzi = []                               # (inizio, proprietario, fine modulo)
    for src in sorgenti(vx):
        loc, lung = listato(src, includes(), tmp)
        basi = {G[n] - i for i, n in loc if n in G}
        if len(basi) != 1:                    # nessun aggancio, o agganci
            continue                          # incoerenti: non e' di questo programma
        base = basi.pop()
        for i, n in loc:
            fini.append((base + i, n))
            o = proprietario(n)
            if o:
                grezzi.append((base + i, o, base + lung))
    fini.sort()
    grezzi.sort()

    # --- DOVE FINISCE UN CORPO ---
    # Al primo simbolo PUBBLICATO che lo segue, o al corpo successivo, o a fine
    # modulo. Il criterio del .global non e' un espediente: le etichette interne
    # di un corpo sono locali (loopI, tmgr_drain, isr_manda) e devono restarci
    # dentro, mentre cio' che il modulo pubblica e' un'altra routine -- il caso
    # che conta e' tmgr_tick, che sta nello stesso modulo del gestore ma gira
    # dentro l'ISR, ed e' del chiamante e non suo.
    pubb = sorted(G.values())
    corpi = []
    for k, (a, o, fine_mod) in enumerate(grezzi):
        fine = fine_mod
        if k + 1 < len(grezzi):
            fine = min(fine, grezzi[k + 1][0])
        j = bisect.bisect_right(pubb, a)
        if j < len(pubb):
            fine = min(fine, pubb[j])
        corpi.append((a, o, fine))
    return fini, corpi, sorted((a, n) for n, a in G.items())


def analizza(vx, tmp, argomenti=()):
    fini, corpi, glob_ = raccogli(vx, tmp)
    af, ac, ag = [a for a, _ in fini], [a for a, _, _ in corpi], [a for a, _ in glob_]

    def dove(pc):
        """(proprietario se e' un corpo, etichetta fine, routine globale)."""
        i = bisect.bisect_right(ac, pc) - 1
        own = corpi[i][1] if i >= 0 and pc < corpi[i][2] else None
        j = bisect.bisect_right(af, pc) - 1
        k = bisect.bisect_right(ag, pc) - 1
        return own, (fini[j][1] if j >= 0 else "?"), (glob_[k][1] if k >= 0 else "?")

    # Le opzioni della MACCHINA (--kbd, oggi) vanno passate qui dentro: un
    # programma guidato da un device non e' autosufficiente, e senza il suo
    # alimentatore non termina -- resta a pollare un flag che nessuno alzera'.
    # Da cui il timeout, che trasforma una sospensione muta in una diagnosi.
    try:
        trace = subprocess.run([sim(), "run", vx, "--trace", *argomenti],
                               capture_output=True, text=True, timeout=TIMEOUT).stdout
    except subprocess.TimeoutExpired:
        sys.exit(f"il programma non e' terminato in {TIMEOUT}s. Se dipende da un "
                 f"device, il suo alimentatore va passato dopo un --:\n"
                 f"    python3 tools/trace.py {vx} -- --kbd \"2000:a,6000:b\"")
    ev, tick, cur = [], [], "boot"
    for line in trace.splitlines():
        if "timer trap" in line:
            m = re.search(r"cyc=\s*(\d+)", line)
            if m:
                tick.append(int(m.group(1)))
            continue
        m = re.match(r"^\[pc=\s*(\d+)\s+cyc=\s*(\d+)\]", line)
        if not m:
            continue
        pc, cyc = int(m.group(1)), int(m.group(2))
        own, fine_l, glob_l = dove(pc)
        if own:
            cur, strato, nome = own, "task", fine_l
        else:
            strato, nome = "kernel", glob_l
        ev.append((cyc, cur, strato, nome, fine_l))
    if not ev:
        sys.exit("nessuna istruzione tracciata: il programma gira?")

    fine = ev[-1][0] + 1
    durata = lambda i: (ev[i + 1][0] if i + 1 < len(ev) else fine) - ev[i][0]

    loc_rt = collections.Counter()            # per etichetta fine: ctx_save & C.
    for i, e in enumerate(ev):
        loc_rt[e[4]] += durata(i)

    fasce = []
    for i, (cyc, o, l, n, _) in enumerate(ev):
        d = durata(i)
        if fasce and fasce[-1]["o"] == o and fasce[-1]["l"] == l:
            fasce[-1]["e"] += d
            fasce[-1]["r"][n] = fasce[-1]["r"].get(n, 0) + d
        else:
            fasce.append({"b": cyc, "e": cyc + d, "o": o, "l": l, "r": {n: d}})

    own, rt = collections.Counter(), collections.Counter()
    for f in fasce:
        own[(f["o"], f["l"])] += f["e"] - f["b"]
        for k, v in f["r"].items():
            rt[k] += v

    return {
        "fine": fine, "tick": tick,
        "fasce": [[f["b"], f["e"], f["o"], f["l"],
                   sorted(f["r"].items(), key=lambda x: -x[1])[:6]] for f in fasce],
        "own": [[o, l, c] for (o, l), c in own.items()],
        "rt": rt.most_common(18),
        "ctx": {"ctx_save": loc_rt["ctx_save"], "ctx_restore": loc_rt["ctx_restore"]},
    }


def main():
    ap = argparse.ArgumentParser(description="Traccia temporale di un programma vcpu_sim")
    ap.add_argument("programma", help="il .vx da tracciare (es. out/vasm/test_tmgr.vx)")
    ap.add_argument("-o", "--out", help="la pagina da scrivere (default: out/trace.html)")
    ap.add_argument("--json", help="scrive anche i dati grezzi qui")
    ap.add_argument("argomenti", nargs="*", metavar="-- OPZIONI",
                    help="dopo un --: opzioni per la MACCHINA, non per questo "
                         "strumento (es. -- --kbd \"2000:a,6000:b\"). Servono ai "
                         "programmi guidati da un device, che senza alimentatore "
                         "non terminano")
    a = ap.parse_args()

    vx = a.programma
    out_path = a.out or os.path.join(RADICE, "out", "trace.html")
    tmp = os.path.dirname(os.path.abspath(out_path)) or "."
    os.makedirs(tmp, exist_ok=True)

    dati = analizza(vx, tmp, a.argomenti)
    if a.json:
        json.dump(dati, open(a.json, "w"))

    # Lo stamp serve a distinguere la pagina appena scritta da quella che il
    # browser tiene in cache su file://, e per quello va CONFRONTATO: stampato
    # qui e scritto nell'occhiello della pagina. Leggerlo dal file (un grep) non
    # prova niente -- dice cosa c'e' sul disco, che e' proprio la meta' che non
    # era in dubbio. La prova e' che i due coincidano a schermo.
    stamp = datetime.datetime.now().strftime("%d/%m %H:%M:%S")
    modello = open(os.path.join(QUI, "trace.template.html")).read()
    pagina = (modello
              .replace("/*__DATI__*/{}", json.dumps(dati))
              .replace("__PROGRAMMA__", os.path.basename(vx))
              .replace("__GENERATA__", stamp))
    open(out_path, "w").write(pagina)

    gran = sum(c for _, _, c in dati["own"])
    print(f"{out_path}: {len(dati['fasce'])} fasce, {len(dati['tick'])} tick, "
          f"{dati['fine']} cicli")
    print(f"  la pagina deve dire «generata {stamp}»: se ne dice un'altra, "
          f"il browser mostra una copia in cache (ctrl-shift-R)")
    per = collections.Counter()
    for o, l, c in dati["own"]:
        per[o] += c
    for o, c in per.most_common():
        print(f"  {o:<10}{c:>8}  {100*c/gran:5.1f}%")


if __name__ == "__main__":
    main()
