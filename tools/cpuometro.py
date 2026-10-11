#!/usr/bin/env python3
"""tools/cpuometro.py -- il CPUOMETRO contro trace.py (11/10/2026, §3.88)

    python3 tools/cpuometro.py <vcpu_sim> <prog.vx> --h <H> [-- opzioni della macchina]

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
e due i casi. Le uscite intermedie stanno in una cartella temporanea: con un
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


def misura(sim, vx, h, extra):
    """Le due misure della stessa corsa: (analisi di marks.py, fasce di
    trace.py). Le usa anche tools/grafici_cpuometro.py, che le disegna."""
    with tempfile.TemporaryDirectory() as tmp:
        rec = os.path.join(tmp, "rec.txt")
        r = subprocess.run([sim, "run", vx, "--marks", rec] + extra,
                           capture_output=True, text=True, timeout=120)
        if "PERSE" in r.stdout:
            sys.exit("il registratore ha perso delle marche: il cpuometro non copre "
                     "la corsa, alza CPU_N")
        cat, by_channel, nomi = marks.read_catalogue(os.path.join(RADICE, "marks.conf"))
        a = marks.analizza(cat, by_channel, rec, None, nomi, h_cpu=h)
        if a["errori"] or not a["cpuometro"]:
            return a, None

        tj = os.path.join(tmp, "trace.json")
        subprocess.run([sys.executable, os.path.join(RADICE, "tools", "trace.py"), vx,
                        "--sim", sim, "-o", os.path.join(tmp, "trace.html"),
                        "--json", tj, "--"] + extra,
                       check=True, capture_output=True, timeout=300)
        return a, json.load(open(tj))["fasce"]


def main():
    argv = sys.argv[1:]
    extra = []
    if "--" in argv:
        i = argv.index("--")
        argv, extra = argv[:i], argv[i + 1:]
    if len(argv) != 4 or argv[2] != "--h":
        sys.exit("uso: cpuometro.py <vcpu_sim> <prog.vx> --h <H> [-- opzioni]")
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
