#!/usr/bin/env python3
"""tools/grafici_cpuometro.py -- il cpuometro, in grafici (11/10/2026, §3.88)

    python3 tools/grafici_cpuometro.py <uscita.html> [--sim out/vcpu_sim]

Esegue test_mondo_cpuometro con il registratore e con la traccia -- le due
misure di tools/cpuometro.py -- e scrive una pagina AUTOSUFFICIENTE: niente
librerie, l'SVG lo disegnano poche righe di JavaScript, e i dati sono dentro.
Si apre anche senza rete. Il template e' accanto,
tools/grafici_cpuometro.template.html.

Tre cose sulla pagina: i numeri dei due strumenti, la CPU libera tick per tick
(il cpuometro a barre, trace.py come trattino sopra), e l'onda quadra vera con
sotto il corpo dell'idle secondo trace.py -- dove l'onda e' fitta l'idle gira,
dove e' piatta la CPU e' di qualcun altro.

La pagina generata non sta in git (docs/grafici/ e' in .gitignore): la si
rifa da qui, dopo `cmake --build out`.
"""
import json, os, sys

QUI = os.path.dirname(os.path.abspath(__file__))
RADICE = os.path.dirname(QUI)
sys.path.insert(0, QUI)
import cpuometro

PROGRAMMA = os.path.join(RADICE, "out", "vasm", "test_mondo_cpuometro.vx")
H = 23                                   # 2*CPU_N + 3, dal sorgente
ARGS = ["--kbd", "24000:a,64000:b"]      # quelli del test


def main():
    a = sys.argv[1:]
    sim = os.path.join(RADICE, "out", "vcpu_sim")
    if "--sim" in a:
        i = a.index("--sim")
        sim = a[i + 1]
        del a[i:i + 2]
    if len(a) != 1:
        sys.exit("uso: grafici_cpuometro.py <uscita.html> [--sim <vcpu_sim>]")

    an, fasce = cpuometro.misura(sim, PROGRAMMA, H, ARGS)
    k = an["cpuometro"]
    if fasce is None:
        sys.exit("\n".join(an["errori"]) or "nessuna onda del CPUOMETRO")

    corpo = [[x, y] for x, y, chi, specie, _ in fasce
             if chi == "idle" and specie == "task"]
    tick = [{"a": t["a"], "b": t["b"], "cpu": t["libera"],
             "trace": cpuometro.idle_corpo(fasce, t["a"], t["b"])} for t in k["tick"]]
    dati = {
        "programma": "test_mondo_cpuometro", "hz": an["hz"], "H": k["H"],
        "da": k["da"], "a": k["a"], "span": k["span"],
        "libera": k["libera"],
        "libera_trace": cpuometro.idle_corpo(fasce, k["da"], k["a"]),
        "tick": tick, "onda": k["onda"], "corpo": corpo,
        "tasti": [p["c"] for p in an["puntuali"] if p["canale"] == "tasto"],
    }
    pagina = open(os.path.join(QUI, "grafici_cpuometro.template.html")).read()
    pagina = pagina.replace("/*DATI*/null", json.dumps(dati, separators=(",", ":")))
    os.makedirs(os.path.dirname(os.path.abspath(a[0])), exist_ok=True)
    open(a[0], "w").write(pagina)
    print(f"{a[0]}: {len(tick)} tick, {len(k['onda'])} marche, "
          f"CPU libera {dati['libera']} / {dati['libera_trace']}")


if __name__ == "__main__":
    main()
