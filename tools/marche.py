#!/usr/bin/env python3
"""marche.py — il catalogo delle misure: lo GENERA per l'assembler, lo LEGGE per te.

    python3 tools/marche.py genera marche.conf -o out/vasm/marche/marche.vinc
    python3 tools/marche.py leggi  marche.conf registrazione.txt [--vx prog.vx]

--- PERCHE' UN FILE SOLO CON DUE MODI ---

Il catalogo (marche.conf) ha due consumatori: l'assembler, che vuole degli .equ,
e il lettore, che vuole i nomi. Tenerli in due programmi vorrebbe dire due
parser dello stesso formato, cioe' due occasioni di interpretarlo diversamente.
Qui il parser e' uno e la sorgente e' una: e' l'intera ragione per cui il
catalogo esiste separato dal .vasm (vedi la testa di marche.conf).

--- COSA SA, E COSA IMPARA DAL FILE CHE LEGGE ---

I canali 0 e 1 sono della MACCHINA (esecuzione e device) e questo programma NON
li ha scritti da nessuna parte: li legge dall'intestazione della registrazione,
che il simulatore emette. Un elenco qui dentro sarebbe esattamente la seconda
verita' che il catalogo esiste per evitare.

I proprietari sono indirizzi di TCB. Per dargli un nome serve la tabella dei
simboli del programma (--vx), e ci finiscono solo i .global: un TCB che il test
non pubblica resta un numero. E' lo stesso vincolo che traccia.py dichiara per
le etichette di routine.
"""

import argparse, collections, os, re, subprocess, sys

RADICE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MARCA_BASE   = 0x100100          # deve coincidere con include/vcpu.h
MARCA_CANALI = 32
RISERVATI    = 2                 # i canali 0 e 1 li scrive la macchina


class Categoria:
    def __init__(self, simbolo, canale, nome):
        self.simbolo, self.canale, self.nome = simbolo, canale, nome
        self.marker = {}          # valore -> (simbolo, nome)


def leggi_catalogo(path):
    """Il catalogo, con i controlli che il formato rende possibili.

    Ogni errore qui e' un errore di CONFIGURE: meglio fermarsi adesso che
    scoprire a valle che due categorie condividono un canale e che le finestre
    dell'una chiudono quelle dell'altra.
    """
    cat, per_canale, corrente = {}, {}, None
    riga_re = re.compile(r'^\s*(categoria|marker)\s+(\w+)\s+(\d+)\s+"([^"]*)"\s*$')

    for n, linea in enumerate(open(path), 1):
        if not linea.strip() or linea.lstrip().startswith("#"):
            continue
        m = riga_re.match(linea)
        if not m:
            sys.exit(f"{path}:{n}: riga non riconosciuta: {linea.rstrip()}")
        tipo, simbolo, numero, nome = m.group(1), m.group(2), int(m.group(3)), m.group(4)

        if tipo == "categoria":
            if numero < RISERVATI:
                sys.exit(f"{path}:{n}: il canale {numero} e' riservato alla macchina "
                         f"(i canali dell'applicazione partono da {RISERVATI})")
            if numero >= MARCA_CANALI:
                sys.exit(f"{path}:{n}: il canale {numero} non esiste "
                         f"(ce ne sono {MARCA_CANALI})")
            if numero in per_canale:
                sys.exit(f"{path}:{n}: il canale {numero} e' gia' di "
                         f"'{per_canale[numero].simbolo}'")
            if simbolo in cat:
                sys.exit(f"{path}:{n}: la categoria '{simbolo}' e' gia' dichiarata")
            corrente = Categoria(simbolo, numero, nome)
            cat[simbolo] = per_canale[numero] = corrente
        else:
            if corrente is None:
                sys.exit(f"{path}:{n}: un marker prima di qualunque categoria")
            if numero == 0:
                sys.exit(f"{path}:{n}: il valore 0 CHIUDE una finestra, "
                         f"non puo' essere un marker")
            if numero in corrente.marker:
                sys.exit(f"{path}:{n}: il valore {numero} e' gia' di "
                         f"'{corrente.marker[numero][0]}' in {corrente.simbolo}")
            corrente.marker[numero] = (simbolo, nome)

    if not cat:
        sys.exit(f"{path}: nessuna categoria dichiarata")
    return cat, per_canale


# --- genera: il .vinc per l'assembler ---------------------------------------
def genera(path, cat, uscita):
    os.makedirs(os.path.dirname(uscita), exist_ok=True)
    with open(uscita, "w") as f:
        f.write(f"; GENERATO da {os.path.basename(path)} — NON MODIFICARE.\n"
                f"; La sorgente e' quel file: modificare qui vuol dire vedere le\n"
                f"; modifiche sparire al prossimo configure, e nel frattempo avere\n"
                f"; nomi che non corrispondono a quelli che lo strumento mostra.\n"
                f";\n"
                f"; Il marker e' qualificato dalla categoria perche' lo spazio di nomi\n"
                f"; dell'assembler e' PIATTO: M_<CATEGORIA>_<MARKER>.\n\n"
                f'.include "hal/marca.vinc"\n\n')
        for c in sorted(cat.values(), key=lambda c: c.canale):
            f.write(f"; --- {c.nome} (canale {c.canale}) ---\n")
            f.write(f".equ MARCA_{c.simbolo:<12} 0x{MARCA_BASE + c.canale*4:x}\n")
            for v, (sim, nome) in sorted(c.marker.items()):
                f.write(f".equ M_{c.simbolo}_{sim:<12} {v:<6}; {nome}\n")
            f.write("\n")
    print(f"{uscita}: {len(cat)} categorie, "
          f"{sum(len(c.marker) for c in cat.values())} marker")


# --- leggi: la registrazione, con i nomi ------------------------------------
def simboli_dati(vx):
    """indirizzo -> nome, per i soli simboli DATI globali del programma."""
    if not vx:
        return {}
    sim = os.path.join(RADICE, "out", "vcpu_sim")
    if not os.path.exists(sim):
        return {}
    out = subprocess.run([sim, "nm", vx], capture_output=True, text=True).stdout
    return {int(m.group(1)): m.group(2)
            for m in (re.match(r"\s*(\d+)\s+D\s+(\S+)", l) for l in out.splitlines()) if m}


def leggi(cat, per_canale, registrazione, vx):
    riservati, ev = {}, []
    for linea in open(registrazione):
        if linea.startswith("#"):
            # I canali della macchina li dichiara LA REGISTRAZIONE, non questo
            # programma: "# riservato <n> <nome>".
            m = re.match(r"#\s*riservato\s+(\d+)\s+(\S+)", linea)
            if m:
                riservati[int(m.group(1))] = m.group(2)
            continue
        c, ch, v, cur = (int(x) for x in linea.split())
        ev.append((c, ch, v, cur))
    if not ev:
        sys.exit(f"{registrazione}: nessuna marca")

    nomi = simboli_dati(vx)
    def chi(a):
        return nomi.get(a, "?" if a else "—") + (f" ({a})" if a and a not in nomi else "")

    def nome_canale(ch):
        if ch in riservati:  return riservati[ch] + " [macchina]"
        if ch in per_canale: return per_canale[ch].nome
        return f"canale {ch} NON DICHIARATO"

    def nome_marker(ch, v):
        c = per_canale.get(ch)
        if c and v in c.marker: return c.marker[v][1]
        return str(v)

    # Il canale 0 e' una PARTIZIONE del tempo: in ogni istante la CPU ha un
    # proprietario e uno solo, quindi da qui si decompone qualunque finestra.
    esec = [(c, v) for c, ch, v, _ in ev if ch == 0]
    fine = ev[-1][0]
    def possesso(a, b):
        out = collections.Counter()
        for i, (c, v) in enumerate(esec):
            f = esec[i+1][0] if i+1 < len(esec) else fine
            lo, hi = max(c, a), min(f, b)
            if hi > lo: out[v] += hi - lo
        return out

    aperte, finestre, puntuali, errori = {}, [], [], []
    for c, ch, v, cur in ev:
        if ch in riservati:
            if ch != 0: puntuali.append((c, ch, v, cur))
            continue
        if v:
            if ch in aperte:
                errori.append(f"ciclo {c}: canale {ch} ({nome_canale(ch)}) riaperto "
                              f"mentre era ancora aperto dal ciclo {aperte[ch][0]}")
            aperte[ch] = (c, v, cur)
        elif ch in aperte:
            a, av, acur = aperte.pop(ch)
            finestre.append((ch, av, a, c, acur, cur))
        else:
            errori.append(f"ciclo {c}: canale {ch} ({nome_canale(ch)}) chiuso "
                          f"senza essere stato aperto")
    for ch, (a, v, _) in aperte.items():
        errori.append(f"canale {ch} ({nome_canale(ch)}) aperto al ciclo {a} e "
                      f"MAI CHIUSO: quella misura non c'e'")

    print(f"\n=== {registrazione}: {len(ev)} marche, {fine} cicli ===")

    print("\n--- eventi puntuali (la macchina) ---")
    for c, ch, v, cur in puntuali:
        print(f"  ciclo {c:>7}  {riservati[ch]:<12} {v:<6} mentre girava {chi(cur)}")
    if not puntuali:
        print("  nessuno")

    print("\n--- per categoria ---")
    print(f"  {'categoria':<24} {'n':>3} {'min':>7} {'max':>7} {'media':>7} "
          f"{'jitter':>7} {'totale':>8}")
    for ch in sorted({f[0] for f in finestre}):
        d = [f[3] - f[2] for f in finestre if f[0] == ch]
        print(f"  {nome_canale(ch):<24} {len(d):>3} {min(d):>7} {max(d):>7} "
              f"{sum(d)//len(d):>7} {max(d)-min(d):>7} {sum(d):>8}")

    print("\n--- finestra per finestra, e dove sono finiti i cicli ---")
    for ch, v, a, z, acur, zcur in finestre:
        p = possesso(a, z)
        dett = "  ".join(f"{chi(o)} {n}" for o, n in p.most_common())
        cross = " ATTRAVERSA" if acur != zcur else ""
        print(f"  {nome_canale(ch):<20} {nome_marker(ch, v):<30} "
              f"@{a:<7} {z-a:>6} cicli{cross}   [{dett}]")

    if errori:
        print("\n--- ERRORI ---")
        for e in errori:
            print("  " + e)


def main():
    ap = argparse.ArgumentParser(description="Il catalogo delle misure: genera e legge")
    sub = ap.add_subparsers(dest="modo", required=True)
    g = sub.add_parser("genera", help="il .vinc con gli .equ, per l'assembler")
    g.add_argument("catalogo")
    g.add_argument("-o", "--out", required=True)
    l = sub.add_parser("leggi", help="una registrazione, con i nomi")
    l.add_argument("catalogo")
    l.add_argument("registrazione")
    l.add_argument("--vx", help="il programma, per dare un nome ai proprietari")
    a = ap.parse_args()

    cat, per_canale = leggi_catalogo(a.catalogo)
    if a.modo == "genera":
        genera(a.catalogo, cat, a.out)
    else:
        leggi(cat, per_canale, a.registrazione, a.vx)


if __name__ == "__main__":
    main()
