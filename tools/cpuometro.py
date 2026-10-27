#!/usr/bin/env python3
"""tools/cpuometro.py -- il CPUOMETRO contro trace.py (11/10/2026, §3.88)

    python3 tools/cpuometro.py <vcpu_sim> <prog.vx> --h <H> [-- opzioni della macchina]
    python3 tools/cpuometro.py <vcpu_sim> <prog.vx> --h <H> --frame <P> <T0> --idle <TCB>
                               [-- opzioni della macchina]

Due strumenti INDIPENDENTI della stessa grandezza, la CPU libera:

  il cpuometro   l'idle genera un'onda quadra sul marcatore, e un semiperiodo
                 lungo d contiene d - H cicli tolti all'idle. Lo legge
                 marks.py, dalla registrazione, senza sapere niente del pc
  trace.py       attribuisce ogni ciclo dal pc: il corpo dell'idle e' la fascia
                 `task` del proprietario `idle`. NON quella `kernel`: e'
                 l'ingresso della trap e il salvataggio del contesto mentre
                 l'idle viene interrotto, cioe' CPU tolta, e il cpuometro la
                 conta giustamente come tale

Fra la prima e l'ultima marca devono dare lo STESSO numero, al ciclo. Tick per
tick il cpuometro conta un semiperiodo nel tick in cui finisce, quindi lo
scarto ammesso e' sotto H. E i tick senza CPU libera devono essere gli stessi.

Esce 0 se i due strumenti sono d'accordo, 1 se no, e stampa i numeri in tutti
e due i casi.

PER FRAME, SENZA trace.py (27/10/2026, §3.107). L'altimetro non ha tick: la sua
unita' e' il frame, t0 + k * periodo. E trace.py li' vorrebbe la traccia di
~1,5 milioni di istruzioni per due CPU, centinaia di MB: dopo l'OOM del 25/10
no. Il secondo strumento allora e' il canale 0, il possesso della CPU LETTO dal
marcatore: la CPU libera dell'onda non puo' superare il tempo in cui l'idle
possiede la CPU (la differenza e' il costo delle trap che lo interrompono). Si
controllano anche l'onda (alterna, H come dichiarato, nessuna marca persa) e che
nessun frame resti senza CPU libera. Con due CPU l'onda e' quella della CPU 0. Le uscite intermedie stanno in una cartella temporanea: con un
programma lungo la traccia e' grande, e per questo il cpuometro esiste.
"""
import json, os, subprocess, sys, tempfile

RADICE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(RADICE, "tools"))
import marks


def idle_corpo(fasce, a, b):
    """I cicli del CORPO dell'idle in [a, b), secondo trace.py."""
    s = 0
    for x, y, chi, specie, _ in fasce:
        if chi == "idle" and specie == "task":
            lo, hi = max(x, a), min(y, b)
            if hi > lo:
                s += hi - lo
    return s


def misura(sim, vx, h, extra, frame=None):
    """Le due misure della stessa corsa: (analisi di marks.py, fasce di
    trace.py). Le usa anche tools/grafici_cpuometro.py, che le disegna. Con
    `frame` la seconda non c'e' (None): basta l'analisi, col possesso."""
    with tempfile.TemporaryDirectory() as tmp:
        rec = os.path.join(tmp, "rec.txt")
        r = subprocess.run([sim, "run", vx, "--marks", rec] + extra,
                           capture_output=True, text=True, timeout=120)
        if "PERSE" in r.stdout:
            sys.exit("il registratore ha perso delle marche: il cpuometro non copre "
                     "la corsa, alza CPU_N")
        if not os.path.exists(rec):
            rec = os.path.join(tmp, "rec.cpu0.txt")   # due CPU: un file per CPU
        cat, by_channel, nomi = marks.read_catalogue(os.path.join(RADICE, "marks.conf"))
        a = marks.analizza(cat, by_channel, rec, vx if frame else None, nomi,
                           h_cpu=h, frame=frame)
        if a["errori"] or not a["cpuometro"] or frame:
            return a, None

        tj = os.path.join(tmp, "trace.json")
        subprocess.run([sys.executable, os.path.join(RADICE, "tools", "trace.py"), vx,
                        "--sim", sim, "-o", os.path.join(tmp, "trace.html"),
                        "--json", tj, "--"] + extra,
                       check=True, capture_output=True, timeout=300)
        return a, json.load(open(tj))["fasce"]


def per_frame(sim, vx, h, frame, idle, extra):
    """Il cpuometro per frame, col possesso del canale 0 come secondo strumento."""
    a, _ = misura(sim, vx, h, extra, frame=frame)
    k = a["cpuometro"]
    if a["errori"] or not k:
        print("\n".join(a["errori"]) or "nessuna onda del CPUOMETRO")
        return 1
    ok = True
    print(f"H = {k['H']}, {k['semiperiodi']} semiperiodi, dal ciclo {k['da']} al {k['a']}")
    print(f"CPU libera   {k['libera']} cicli ({100.0 * k['libera'] / k['span']:.1f}%)")
    lib = [t["libera"] for t in k["tick"]]
    if not lib:
        print("nessun frame misurato per intero")
        return 1
    w = min(k["tick"], key=lambda t: t["libera"])
    print(f"per frame ({len(lib)}): minimo {w['libera']} su {w['durata']} "
          f"({100.0 * w['libera'] / w['durata']:.1f}%), al frame dal ciclo {w['a']}")
    if 0 in lib:
        print("  un frame SENZA CPU libera")
        ok = False
    poss = dict((o, n) for o, n in k["possesso"])
    print("possesso (canale 0): " + "  ".join(f"{o} {n}" for o, n in k["possesso"]))
    if idle not in poss:
        print(f"  l'idle '{idle}' non possiede mai la CPU: il nome e' giusto?")
        return 1
    print(f"l'idle possiede {poss[idle]}, l'onda ne dice libera {k['libera']}: "
          f"le trap mentre l'idle e' interrotto costano {poss[idle] - k['libera']}")
    if k["libera"] > poss[idle]:
        print("  la CPU libera SUPERA il possesso dell'idle: uno dei due mente")
        ok = False
    print("D'ACCORDO" if ok else "NON D'ACCORDO")
    return 0 if ok else 1


def main():
    argv = sys.argv[1:]
    extra = []
    if "--" in argv:
        i = argv.index("--")
        argv, extra = argv[:i], argv[i + 1:]
    if len(argv) == 9 and argv[2] == "--h" and argv[4] == "--frame" and argv[7] == "--idle":
        return per_frame(argv[0], argv[1], int(argv[3]), (int(argv[5]), int(argv[6])),
                         argv[8], extra)
    if len(argv) != 4 or argv[2] != "--h":
        sys.exit("uso: cpuometro.py <vcpu_sim> <prog.vx> --h <H> "
                 "[--frame <P> <T0> --idle <TCB>] [-- opzioni]")
    sim, vx, h = argv[0], argv[1], int(argv[3])

    a, fasce = misura(sim, vx, h, extra)
    k = a["cpuometro"]
    if fasce is None:
        print("\n".join(a["errori"]) or "nessuna onda del CPUOMETRO")
        return 1

    ok = True
    tr = idle_corpo(fasce, k["da"], k["a"])
    print(f"H = {k['H']}, {k['semiperiodi']} semiperiodi, dal ciclo {k['da']} al {k['a']}")
    print(f"CPU libera   cpuometro {k['libera']}   trace.py {tr}"
          f"   ({100.0 * k['libera'] / k['span']:.1f}%)")
    if k["libera"] != tr:
        print(f"  DIVERSI di {k['libera'] - tr}: devono coincidere al ciclo")
        ok = False

    scarti, zeri_c, zeri_t = [], [], []
    for t in k["tick"]:
        x = idle_corpo(fasce, t["a"], t["b"])
        scarti.append(t["libera"] - x)
        if t["libera"] == 0: zeri_c.append(t["a"])
        if x == 0: zeri_t.append(t["a"])
    if scarti:
        peggio = max(scarti, key=abs)
        print(f"per tick ({len(scarti)}): scarto peggiore {peggio} cicli, ammesso sotto H")
        if abs(peggio) >= k["H"]:
            ok = False
        print(f"tick senza CPU libera   cpuometro {zeri_c}   trace.py {zeri_t}")
        if zeri_c != zeri_t:
            ok = False
    print("D'ACCORDO" if ok else "NON D'ACCORDO")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
