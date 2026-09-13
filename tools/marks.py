#!/usr/bin/env python3
"""marks.py — il catalogue delle misure: lo GENERA per l'assembler, lo LEGGE per te.

    python3 tools/marks.py generate marks.conf -o out/vasm/marche/marche.vinc
    python3 tools/marks.py read  marks.conf recording.txt [--vx prog.vx]

--- PERCHE' UN FILE SOLO CON DUE MODI ---

Il catalogue (marks.conf) ha due consumatori: l'assembler, che vuole degli .equ,
e il lettore, che vuole i nomi. Tenerli in due programmi vorrebbe dire due
parser dello stesso formato, cioe' due occasioni di interpretarlo diversamente.
Qui il parser e' uno e la sorgente e' una: e' l'intera ragione per cui il
catalogue esiste separato dal .vasm (vedi la testa di marks.conf).

--- COSA SA, E COSA IMPARA DAL FILE CHE LEGGE ---

I canali 0 e 1 sono della MACCHINA (esecuzione e device) e questo programma NON
li ha scritti da nessuna parte: li legge dall'intestazione della recording,
che il simulatore emette. Un elenco qui dentro sarebbe esattamente la seconda
verita' che il catalogue esiste per evitare.

I proprietari sono indirizzi di TCB. Per dargli un nome serve la tabella dei
simboli del programma (--vx), e ci finiscono solo i .global: un TCB che il test
non pubblica resta un numero. E' lo stesso vincolo che trace.py dichiara per
le etichette di routine.
"""

import argparse, collections, os, re, subprocess, sys

RADICE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MARK_BASE   = 0x100100          # deve coincidere con include/vcpu.h
MARK_CHANNELS = 32
RISERVATI    = 2                 # i canali 0 e 1 li scrive la macchina


class Categoria:
    """Un canale dichiarato, e la SPECIE di cio' che ci passa sopra.

    Due specie, e non e' una raffinatezza:

      category  FINESTRE. Un valore != 0 apre, uno 0 chiude, e cio' che si
                misura e' la DURATA. Costa 3 istruzioni ad aprire e 1-2 a
                chiudere.
      event     PUNTUALI. Un valore != 0 e basta: non c'e' niente da chiudere
                perche' la cosa non dura. Costa 3 istruzioni e nient'altro.

    Una preemption e' l'esempio che ha reso la distinzione necessaria: e' un
    ISTANTE -- la CPU ti e' stata tolta adesso -- e forzarla nel modello a
    finestre vorrebbe dire o finestre lunghe zero (che il lettore
    riporterebbe come "0 cicli", cioe' rumore) o finestre mai chiuse (che il
    lettore dichiara come misure MANCANTI, cioe' un errore falso).
    """
    def __init__(self, simbolo, canale, nome, puntuale=False):
        self.simbolo, self.canale, self.nome = simbolo, canale, nome
        self.puntuale = puntuale
        self.marker = {}          # valore -> (simbolo, nome)


def read_catalogue(path):
    """Il catalogue, con i controlli che il formato rende possibili.

    Ogni errore qui e' un errore di CONFIGURE: meglio fermarsi adesso che
    scoprire a valle che due categorie condividono un canale e che le finestre
    dell'una chiudono quelle dell'altra.
    """
    cat, by_channel, corrente = {}, {}, None
    riga_re = re.compile(r'^\s*(category|event|marker)\s+(\w+)\s+(\d+)\s+"([^"]*)"\s*$')

    for n, linea in enumerate(open(path), 1):
        if not linea.strip() or linea.lstrip().startswith("#"):
            continue
        m = riga_re.match(linea)
        if not m:
            sys.exit(f"{path}:{n}: riga non riconosciuta: {linea.rstrip()}")
        tipo, simbolo, numero, nome = m.group(1), m.group(2), int(m.group(3)), m.group(4)

        if tipo in ("category", "event"):
            if numero < RISERVATI:
                sys.exit(f"{path}:{n}: il canale {numero} e' riservato alla macchina "
                         f"(i canali dell'applicazione partono da {RISERVATI})")
            if numero >= MARK_CHANNELS:
                sys.exit(f"{path}:{n}: il canale {numero} non esiste "
                         f"(ce ne sono {MARK_CHANNELS})")
            if numero in by_channel:
                sys.exit(f"{path}:{n}: il canale {numero} e' gia' di "
                         f"'{by_channel[numero].simbolo}'")
            if simbolo in cat:
                sys.exit(f"{path}:{n}: la categoria '{simbolo}' e' gia' dichiarata")
            corrente = Categoria(simbolo, numero, nome, puntuale=(tipo == "event"))
            cat[simbolo] = by_channel[numero] = corrente
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
    return cat, by_channel


# --- generate: il .vinc per l'assembler ---------------------------------------
def generate(path, cat, out_path):
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w") as f:
        f.write(f"; GENERATO da {os.path.basename(path)} — NON MODIFICARE.\n"
                f"; La sorgente e' quel file: modificare qui vuol dire vedere le\n"
                f"; modifiche sparire al prossimo configure, e nel frattempo avere\n"
                f"; nomi che non corrispondono a quelli che lo strumento mostra.\n"
                f";\n"
                f"; Il marker e' qualificato dalla categoria perche' lo spazio di nomi\n"
                f"; dell'assembler e' PIATTO: M_<CATEGORIA>_<MARKER>.\n\n"
                f'.include "hal/marker.vinc"\n\n')
        for c in sorted(cat.values(), key=lambda c: c.canale):
            f.write(f"; --- {c.nome} (canale {c.canale}) ---\n")
            f.write(f".equ MARK_{c.simbolo:<12} 0x{MARK_BASE + c.canale*4:x}\n")
            for v, (sim, nome) in sorted(c.marker.items()):
                f.write(f".equ M_{c.simbolo}_{sim:<12} {v:<6}; {nome}\n")
            f.write("\n")
    print(f"{out_path}: {len(cat)} categorie, "
          f"{sum(len(c.marker) for c in cat.values())} marker")


# --- read: la recording, con i nomi ------------------------------------
def simboli_dati(vx):
    """indirizzo -> nome, per i soli simboli DATI globali del programma."""
    if not vx:
        return {}
    # Gli stessi due posti in cui li cerca tools/trace.py, e nello stesso
    # ordine: guardarne uno solo lasciava i proprietari senza nome su un albero
    # costruito in build/, che e' il caso normale di chi usa CMake.
    for p in ("out/vcpu_sim", "build/vcpu_sim"):
        sim = os.path.join(RADICE, p)
        if os.path.exists(sim):
            break
    else:
        return {}
    out = subprocess.run([sim, "nm", vx], capture_output=True, text=True).stdout
    return {int(m.group(1)): m.group(2)
            for m in (re.match(r"\s*(\d+)\s+D\s+(\S+)", l) for l in out.splitlines()) if m}


def analizza(cat, by_channel, recording, vx):
    """La registrazione, letta e decomposta. Ritorna DATI, non testo.

    E' separata dalla stampa perche' ha due consumatori: `read` qui sotto, che
    la scrive nel terminale, e tools/trace.py, che la disegna nella pagina. La
    ragione e' la stessa per cui marks.conf esiste separato dal .vasm -- due
    analisi della stessa registrazione divergerebbero in silenzio, e il disegno
    direbbe un numero diverso dal testo sugli stessi dati.
    """
    riservati, ev = {}, []
    for linea in open(recording):
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
        sys.exit(f"{recording}: nessuna marca")

    nomi = simboli_dati(vx)
    def chi(a):
        return nomi.get(a, "?" if a else "—") + (f" ({a})" if a and a not in nomi else "")

    def nome_canale(ch):
        if ch in riservati:  return riservati[ch] + " [macchina]"
        if ch in by_channel: return by_channel[ch].nome
        return f"canale {ch} NON DICHIARATO"

    def nome_marker(ch, v):
        c = by_channel.get(ch)
        if c and v in c.marker: return c.marker[v][1]
        return str(v)

    # Il canale 0 e' una PARTIZIONE del tempo: in ogni istante la CPU ha un
    # proprietario e uno solo, quindi da qui si decompone qualunque finestra.
    esec = [(c, v) for c, ch, v, _ in ev if ch == 0]
    fine = ev[-1][0]
    def segmenti(a, b):
        """I tratti di [a,b), uno per possessore, IN ORDINE DI TEMPO."""
        out = []
        for i, (c, v) in enumerate(esec):
            f = esec[i+1][0] if i+1 < len(esec) else fine
            lo, hi = max(c, a), min(f, b)
            if hi > lo: out.append((lo, hi, v))
        return out

    def possesso(a, b):
        """Gli stessi tratti, sommati per possessore.

        Derivato da segmenti() e non calcolato a parte: sono la stessa
        decomposizione vista in due modi, e il totale in fondo al disegno deve
        per forza tornare con i tratti disegnati sopra. Due conti separati
        potrebbero non tornare, e nessuno se ne accorgerebbe.
        """
        out = collections.Counter()
        for lo, hi, v in segmenti(a, b):
            out[v] += hi - lo
        return out

    # Chi possiede la CPU dal ciclo c in poi, e quando 'a' la riprende: due
    # domande sulla stessa partizione (il canale 0), e servono all'evento
    # puntuale piu' sotto per dire non solo CHE c'e' stata una preemption, ma a
    # favore di chi e per quanto.
    def succ(c):
        for cc, v in esec:
            if cc > c: return chi(v)
        return None

    def rientro(c, a):
        for cc, v in esec:
            if cc > c and v == a: return cc
        return None

    aperte, finestre, puntuali, eventi, errori = {}, [], [], [], []
    for c, ch, v, cur in ev:
        if ch in riservati:
            if ch != 0: puntuali.append((c, ch, v, cur))
            continue
        # Un canale dichiarato EVENT non ha finestre: il valore e' l'istante.
        c_ = by_channel.get(ch)
        if c_ is not None and c_.puntuale:
            if not v:
                errori.append(f"ciclo {c}: il canale {ch} ({nome_canale(ch)}) e' un "
                              f"EVENT e non si chiude: lo 0 scritto qui non vuol dire niente")
                continue
            eventi.append((c, ch, v, cur))
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

    categorie = []
    for ch in sorted({f[0] for f in finestre}):
        d = [f[3] - f[2] for f in finestre if f[0] == ch]
        categorie.append({"ch": ch, "nome": nome_canale(ch), "n": len(d),
                          "min": min(d), "max": max(d), "media": sum(d) // len(d),
                          "jitter": max(d) - min(d), "totale": sum(d)})

    fin = []
    for ch, v, a, z, acur, zcur in finestre:
        fin.append({"ch": ch, "canale": nome_canale(ch), "marker": nome_marker(ch, v),
                    "a": a, "z": z, "d": z - a, "cross": acur != zcur,
                    "poss": [[chi(o), n] for o, n in possesso(a, z).most_common()],
                    # I tratti per il disegno: stessa decomposizione di 'poss',
                    # con le posizioni. Li calcola qui e non il disegno, o
                    # sarebbero due implementazioni della stessa aritmetica.
                    "tratti": [[lo, hi, chi(o)] for lo, hi, o in segmenti(a, z)]})

    # Gli eventi dell'APPLICAZIONE (oggi: la preemption). Ognuno porta con se'
    # chi girava -- che la registrazione ha gia' nella quarta colonna, gratis --
    # piu' le due cose che il canale 0 sa dire: chi ha preso la CPU subito dopo,
    # e quando il preemptato l'ha ripresa. Il secondo e' il tempo in cui e'
    # stato fuori INVOLONTARIAMENTE, che e' il numero realtime della cosa.
    evs = []
    for c, ch, v, cur in eventi:
        a = chi(cur)
        r = rientro(c, cur)
        evs.append({"c": c, "ch": ch, "canale": nome_canale(ch),
                    "marker": nome_marker(ch, v), "chi": a,
                    "succ": succ(c), "rientro": r,
                    "fuori": (r - c) if r is not None else None})

    return {
        "recording": recording,
        "nmarche":   len(ev),
        "fine":      fine,
        "puntuali":  [{"c": c, "ch": ch, "canale": riservati[ch], "v": v, "cur": chi(cur)}
                      for c, ch, v, cur in puntuali],
        "eventi":    evs,
        "categorie": categorie,
        "finestre":  fin,
        # La partizione del tempo secondo il canale 0: il possesso della CPU
        # LETTO, non dedotto dal pc come fa trace.py. E' il dato che il
        # marcatore aggiunge, ed e' quello che riga le barre del disegno.
        "esec":      [[c, chi(v)] for c, v in esec],
        "errori":    errori,
    }


def report(a):
    """La stessa analisi, nel terminale."""
    print(f"\n=== {a['recording']}: {a['nmarche']} marche, {a['fine']} cicli ===")

    print("\n--- eventi puntuali (la macchina) ---")
    for p in a["puntuali"]:
        print(f"  ciclo {p['c']:>7}  {p['canale']:<12} {p['v']:<6} "
              f"mentre girava {p['cur']}")
    if not a["puntuali"]:
        print("  nessuno")

    print("\n--- per categoria ---")
    print(f"  {'categoria':<24} {'n':>3} {'min':>7} {'max':>7} {'media':>7} "
          f"{'jitter':>7} {'totale':>8}")
    for c in a["categorie"]:
        print(f"  {c['nome']:<24} {c['n']:>3} {c['min']:>7} {c['max']:>7} "
              f"{c['media']:>7} {c['jitter']:>7} {c['totale']:>8}")

    if a["eventi"]:
        print("\n--- eventi (il programma) ---")
        for e in a["eventi"]:
            fuori = ("%d cicli fuori" % e["fuori"]) if e["fuori"] is not None \
                    else "non rientra piu'"
            print(f"  ciclo {e['c']:>7}  {e['canale']:<14} {e['marker']:<24} "
                  f"{e['chi']:<8} -> {str(e['succ']):<8} {fuori}")

    print("\n--- finestra per finestra, e dove sono finiti i cicli ---")
    for f in a["finestre"]:
        dett = "  ".join(f"{o} {n}" for o, n in f["poss"])
        cross = " ATTRAVERSA" if f["cross"] else ""
        print(f"  {f['canale']:<20} {f['marker']:<30} "
              f"@{f['a']:<7} {f['d']:>6} cicli{cross}   [{dett}]")

    if a["errori"]:
        print("\n--- ERRORI ---")
        for e in a["errori"]:
            print("  " + e)


def main():
    ap = argparse.ArgumentParser(description="Il catalogue delle misure: generate e legge")
    sub = ap.add_subparsers(dest="mode", required=True)
    g = sub.add_parser("generate", help="il .vinc con gli .equ, per l'assembler")
    g.add_argument("catalogue")
    g.add_argument("-o", "--out", required=True)
    l = sub.add_parser("read", help="una recording, con i nomi")
    l.add_argument("catalogue")
    l.add_argument("recording")
    l.add_argument("--vx", help="il programma, per dare un nome ai proprietari")
    l.add_argument("--json", help="scrive l'analisi qui invece di stamparla")
    a = ap.parse_args()

    cat, by_channel = read_catalogue(a.catalogue)
    if a.mode == "generate":
        generate(a.catalogue, cat, a.out)
        return
    dati = analizza(cat, by_channel, a.recording, a.vx)
    if a.json:
        import json
        json.dump(dati, open(a.json, "w"))
    else:
        report(dati)


if __name__ == "__main__":
    main()
