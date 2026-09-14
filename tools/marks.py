#!/usr/bin/env python3
"""marks.py — il catalogue delle misure: lo GENERA per l'assembler, lo LEGGE per te.

    python3 tools/marks.py generate marks.conf -o out/vasm/marche/marche.vinc
    python3 tools/marks.py read  marks.conf recording.txt [--vx prog.vx] [--mhz 250]

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

Dalla stessa intestazione viene la FREQUENZA ("# frequenza <hz>"), ed e' per la
stessa ragione: e' la macchina che sa a che velocita' gira. Da li' i cicli si
leggono anche come tempo -- mai al posto loro, sempre accanto, e sempre con la
frequenza a vista. Con --mhz si rilegge la stessa registrazione a un'altra
velocita': i cicli non cambiano, cambia la loro lettura.

I proprietari sono indirizzi di TCB. Per dargli un nome serve la tabella dei
simboli del programma (--vx), e ci finiscono solo i .global: un TCB che il test
non pubblica resta un numero. E' lo stesso vincolo che trace.py dichiara per
le etichette di routine.
"""

import argparse, collections, os, re, subprocess, sys

RADICE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

MARK_BASE   = 0x100100          # deve coincidere con include/vcpu.h
MARK_CHANNELS = 32
MARKD_BASE  = MARK_BASE + MARK_CHANNELS * 4
MARKN_BASE  = MARKD_BASE + MARK_CHANNELS * 4
RISERVATI    = 2
MARK_EXEC_CH = 0                 # il canale dell'esecuzione, come in vcpu.h                 # i canali 0 e 1 li scrive la macchina


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
    def __init__(self, simbolo, canale, nome, puntuale=False, dato=None,
                 owner="task"):
        self.simbolo, self.canale, self.nome = simbolo, canale, nome
        self.puntuale = puntuale
        self.dato = dato          # il nome del valore sul porto dati, o None
        # CHI PRODUCE le marche di questo canale, e quindi che relazione ha la
        # marca con `current`. Non e' la stessa per tutti, ed e' una proprieta'
        # del CANALE: chi scrive il tag sa dov'e' messo.
        #
        #   task    la sw la esegue il task: current l'ha PRODOTTA
        #   kernel  la esegue il kernel per conto di qualcuno: current e' il
        #           task PER CONTO DEL QUALE (o, per una preemption, la vittima)
        #   isr     la esegue l'ISR: current e' chi e' stato INTERROTTO
        #
        # Senza, il lettore sarebbe costretto a una frase buona per tutti --
        # "girava X" -- che descrive il possesso della CPU e non la paternita'.
        self.owner = owner
        self.marker = {}          # valore -> (simbolo, nome)


class Nomi:
    """Uno spazio di nomi per i VALORI di un canale.

    Il canale 0 porta indirizzi di TCB, e un indirizzo non ha un nome finche'
    qualcuno non glielo da'. Il programma lo REGISTRA a runtime scrivendo un id
    sul porto dei nomi (vedi MARKN_BASE in include/vcpu.h); l'id -> nome
    visualizzato sta qui, perche' questo file e' l'unico posto in cui vivono i
    nomi da mostrare -- se stesse nel programma sarebbe un secondo posto.
    """
    def __init__(self, simbolo, canale, nome):
        self.simbolo, self.canale, self.nome = simbolo, canale, nome
        self.id = {}              # id -> (simbolo, nome visualizzato)


def read_catalogue(path):
    """Il catalogue, con i controlli che il formato rende possibili.

    Ogni errore qui e' un errore di CONFIGURE: meglio fermarsi adesso che
    scoprire a valle che due categorie condividono un canale e che le finestre
    dell'una chiudono quelle dell'altra.
    """
    cat, by_channel, corrente = {}, {}, None
    nomi, nomi_corrente = {}, None
    riga_re = re.compile(r'^\s*(category|event|marker|names|name)\s+(\w+)\s+(\d+)\s+"([^"]*)"'
                         r'(?:\s+owner\s+(task|kernel|isr))?'
                         r'(?:\s+data\s+"([^"]*)")?\s*$')

    for n, linea in enumerate(open(path), 1):
        if not linea.strip() or linea.lstrip().startswith("#"):
            continue
        m = riga_re.match(linea)
        if not m:
            sys.exit(f"{path}:{n}: riga non riconosciuta: {linea.rstrip()}")
        tipo, simbolo, numero, nome = m.group(1), m.group(2), int(m.group(3)), m.group(4)
        owner, dato = m.group(5), m.group(6)
        if owner is not None and tipo == "marker":
            sys.exit(f"{path}:{n}: `owner` si dichiara sulla CATEGORIA, non su un "
                     f"marker: chi esegue la sw e' una proprieta' del canale")
        if dato is not None and tipo == "marker":
            sys.exit(f"{path}:{n}: `data` si dichiara sulla CATEGORIA, non su un "
                     f"marker: e' una proprieta' del canale, e tutti i marker "
                     f"che ci passano sopra portano lo stesso genere di valore")

        if tipo == "names":
            # Uno spazio di nomi PUO' stare sul canale 0: non dichiara un canale
            # di marche -- quello e' della macchina -- ma come si chiamano i
            # valori che ci passano sopra.
            if numero >= MARK_CHANNELS:
                sys.exit(f"{path}:{n}: il canale {numero} non esiste")
            if numero in nomi:
                sys.exit(f"{path}:{n}: i nomi del canale {numero} sono gia' "
                         f"dichiarati da '{nomi[numero].simbolo}'")
            nomi_corrente = Nomi(simbolo, numero, nome)
            nomi[numero] = nomi_corrente
            continue
        if tipo == "name":
            if nomi_corrente is None:
                sys.exit(f"{path}:{n}: un `name` prima di qualunque `names`")
            if numero in nomi_corrente.id:
                sys.exit(f"{path}:{n}: l'id {numero} e' gia' di "
                         f"'{nomi_corrente.id[numero][0]}' in {nomi_corrente.simbolo}")
            nomi_corrente.id[numero] = (simbolo, nome)
            continue

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
            corrente = Categoria(simbolo, numero, nome,
                                 puntuale=(tipo == "event"), dato=dato,
                                 owner=(owner or "task"))
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
    return cat, by_channel, nomi


# --- generate: il .vinc per l'assembler ---------------------------------------
def generate(path, cat, out_path, nomi=None):
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
            if c.dato:
                f.write(f".equ MARKD_{c.simbolo:<11} "
                        f"0x{MARK_BASE + MARK_CHANNELS*4 + c.canale*4:x}"
                        f"  ; il porto dati: {c.dato}\n")
            for v, (sim, nome) in sorted(c.marker.items()):
                f.write(f".equ M_{c.simbolo}_{sim:<12} {v:<6}; {nome}\n")
            f.write("\n")
        for nm in sorted((nomi or {}).values(), key=lambda x: x.canale):
            f.write(f"; --- nomi dei valori di {nm.nome} (canale {nm.canale}) ---\n")
            f.write(f".equ MARKN_{nm.simbolo:<11} "
                    f"0x{MARKN_BASE + nm.canale*4:x}  ; il porto dei nomi\n")
            for i, (sim, nome) in sorted(nm.id.items()):
                f.write(f".equ N_{nm.simbolo}_{sim:<12} {i:<6}; {nome}\n")
            f.write("\n")
    print(f"{out_path}: {len(cat)} categorie, "
          f"{sum(len(c.marker) for c in cat.values())} marker")


# --- il CLOCK: i cicli letti anche come tempo --------------------------
# La frequenza NON e' scritta qui: la dichiara la macchina in testa alla
# registrazione ("# frequenza <hz>"), come gia' fa per i canali riservati. Un
# numero tenuto qui sarebbe una seconda verita', e leggerebbe in microsecondi
# sbagliati una registrazione prodotta da un'altra macchina -- in silenzio,
# perche' i cicli resterebbero giusti.
#
# E QUESTE FUNZIONI STANNO QUI perche' i consumatori sono tre (`read`,
# tools/trace.py, tools/scheduler_facts.py) e la regola di scrittura e' una
# sola. Vedi l'avvertimento in include/vcpu.h: il tempo non va MAI da solo,
# perche' "1,18 us" si legge come una misura mentre "118 cicli" si legge per
# quello che e', l'uscita di un modello senza sistema di memoria.
RE_FREQUENZA = re.compile(r"#\s*frequenza\s+(\d+)")


def frequenza(recording):
    """Gli Hz dichiarati dalla registrazione, o None se non ne dichiara.

    Sta separata da analizza() perche' ha un chiamante in piu': tools/trace.py
    la vuole anche quando le marche non ci sono. Un programma senza tag non
    produce finestre -- e oggi e' la norma -- ma i suoi cicli la pagina li
    mostra lo stesso, e senza questa il tempo sparirebbe proprio dai programmi
    piu' comuni.
    """
    try:
        with open(recording) as f:
            for linea in f:
                if not linea.startswith("#"):
                    return None       # l'intestazione e' finita
                m = RE_FREQUENZA.match(linea)
                if m:
                    return int(m.group(1))
    except OSError:
        pass
    return None


UNITA = ((1e9, "ns"), (1e6, "µs"), (1e3, "ms"), (1, "s"))


def unita_per(cicli, hz):
    """L'unita' in cui leggere questi cicli: la piu' grande che li tiene sopra 1."""
    for scala, u in UNITA:
        if abs(cicli / hz) * scala < 1000 or u == "s":
            return u
    return "s"


def tempo(cicli, hz, unita=None):
    """I cicli come TEMPO: '134,3 µs'. Stringa vuota senza una frequenza.

    Mai da solo -- chi chiama lo mette ACCANTO ai cicli, che restano la cosa
    misurata -- e mai senza dire a quale frequenza: vedi clock().

    `unita` forza la scala, e serve ai GRUPPI. Una riga di cinque numeri in cui
    il primo e' in ns e gli altri in µs e' leggibile numero per numero ma non si
    confronta a colpo d'occhio, che e' l'unica ragione per cui quei cinque
    numeri stanno sulla stessa riga: chi legge deve poter scorrere min e max
    senza convertire in testa. L'unita' la sceglie il valore piu' GRANDE del
    gruppo -- unita_per() -- cosi' nessuno finisce sotto lo zero virgola.
    """
    if not hz or cicli is None:
        return ""
    unita = unita or unita_per(cicli, hz)
    scala = dict((u, s) for s, u in UNITA)[unita]
    x = cicli / hz * scala
    return f"{x:.{2 if abs(x) < 10 else 1}f}".replace(".", ",") + " " + unita


def clock(hz):
    """'@ 100 MHz', l'ipotesi dichiarata accanto a ogni conversione."""
    if not hz:
        return ""
    mhz = hz / 1e6
    return "@ %s MHz" % (("%g" % mhz).replace(".", ","))


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


def analizza(cat, by_channel, recording, vx, nomi=None, hz=None):
    """La registrazione, letta e decomposta. Ritorna DATI, non testo.

    E' separata dalla stampa perche' ha due consumatori: `read` qui sotto, che
    la scrive nel terminale, e tools/trace.py, che la disegna nella pagina. La
    ragione e' la stessa per cui marks.conf esiste separato dal .vasm -- due
    analisi della stessa registrazione divergerebbero in silenzio, e il disegno
    direbbe un numero diverso dal testo sugli stessi dati.
    """
    riservati, registrati, ev = {}, {}, []
    # hz: quella DICHIARATA dalla registrazione, salvo che il chiamante ne
    # imponga un'altra (--mhz) per rileggere la STESSA corsa a un'altra
    # velocita'. I cicli non cambiano, e sono loro la cosa misurata.
    hz_reg = None
    for linea in open(recording):
        if linea.startswith("#"):
            m = RE_FREQUENZA.match(linea)
            if m:
                hz_reg = int(m.group(1))
            # I canali della macchina li dichiara LA REGISTRAZIONE, non questo
            # programma: "# riservato <n> <nome>".
            m = re.match(r"#\s*riservato\s+(\d+)\s+(\S+)", linea)
            if m:
                riservati[int(m.group(1))] = m.group(2)
            # I nomi che il PROGRAMMA ha registrato a runtime: "sul canale C il
            # valore V si chiama <id>", e l'id lo traduce il catalogo.
            m = re.match(r"#\s*nome\s+(-?\d+)\s+(-?\d+)\s+(-?\d+)", linea)
            if m:
                registrati[(int(m.group(1)), int(m.group(2)))] = int(m.group(3))
            continue
        campi = [int(x) for x in linea.split()]
        # Le due colonne del porto dati sono in fondo. Una registrazione
        # prodotta prima che esistessero non le ha, e va letta lo stesso:
        # significa "nessun dato", che e' la verita'.
        c, ch, v, cur = campi[0], campi[1], campi[2], campi[3]
        dato    = campi[4] if len(campi) > 4 else 0
        ha_dato = campi[5] if len(campi) > 5 else 0
        in_trap = campi[6] if len(campi) > 6 else 0
        ev.append((c, ch, v, cur, dato, ha_dato, in_trap))
    if not ev:
        sys.exit(f"{recording}: nessuna marca")

    simboli = simboli_dati(vx)
    sp_nomi = (nomi or {}).get(MARK_EXEC_CH)

    def chi(a):
        """Il nome di un proprietario, e le tre strade in ordine di forza.

        1. quello che il PROGRAMMA ha registrato (MARKN_*): non dipende dalla
           tabella dei simboli, quindi c'e' anche dove la toolchain non ne
           pubblica una -- che e' il caso per cui la registrazione esiste;
        2. la tabella dei simboli, se quel TCB e' .global;
        3. il numero nudo, che almeno non finge.
        """
        idn = registrati.get((MARK_EXEC_CH, a))
        if idn is not None and sp_nomi and idn in sp_nomi.id:
            return sp_nomi.id[idn][1]
        if idn is not None:
            return f"id {idn}?"      # registrato ma non dichiarato nel catalogo
        return simboli.get(a, "?" if a else "—") + (f" ({a})" if a and a not in simboli else "")

    def nome_canale(ch):
        if ch in riservati:  return riservati[ch] + " [macchina]"
        if ch in by_channel: return by_channel[ch].nome
        return f"canale {ch} NON DICHIARATO"

    def frase_owner(ch, cur, succ, in_trap=0):
        """Come si dice, per QUESTO canale, il rapporto fra la marca e current.

        Una frase sola per tutti -- "girava X" -- descriverebbe il possesso
        della CPU e non la paternita', che per meta' dei canali e' un'altra
        cosa. Il canale lo dichiara, e qui si traduce.
        """
        c = by_channel.get(ch)
        o = c.owner if c else "task"
        if o == "isr":
            return f"prodotta dall'ISR, mentre girava {chi(cur)}"
        if o == "kernel":
            # In quale dei due contesti il kernel stesse girando lo sa la
            # macchina, e vale la pena dirlo: distingue una preemption arrivata
            # dal TICK da una arrivata da un blocco volontario.
            dove = "nell'ISR" if in_trap else "da task"
            return (f"dal kernel ({dove}) per conto di {chi(cur)}"
                    + (f", poi {succ}" if succ else ""))
        return f"prodotta da {chi(cur)}" + (f", poi {succ}" if succ else "")

    def etichetta_dato(ch, d, hd):
        """'codice=17', o None se quel canale non porta dati."""
        c = by_channel.get(ch)
        if not c or not c.dato or not hd: return None
        return f"{c.dato}={d}"

    def nome_marker(ch, v):
        c = by_channel.get(ch)
        if c and v in c.marker: return c.marker[v][1]
        return str(v)

    # Il canale 0 e' una PARTIZIONE del tempo: in ogni istante la CPU ha un
    # proprietario e uno solo, quindi da qui si decompone qualunque finestra.
    esec = [(c, v) for c, ch, v, _, _, _, _ in ev if ch == 0]
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
    for c, ch, v, cur, dato, ha_dato, in_trap in ev:
        # Il porto dati e la sua dichiarazione devono essere d'accordo, e le due
        # discordanze sono difetti diversi: una marca senza il dato che la sua
        # categoria promette e' una misura che manca; un dato armato su un
        # canale che non lo dichiara e' un armamento che nessuno leggera' --
        # quasi sempre il canale sbagliato.
        decl = by_channel.get(ch)
        if decl is not None and decl.dato and not ha_dato:
            errori.append(f"ciclo {c}: {nome_canale(ch)} dichiara il dato "
                          f"'{decl.dato}' e questa marca non ce l'ha: non e' 0, "
                          f"e' MANCANTE (nessuno ha armato il porto)")
        # L'owner DICHIARATO contro il contesto che la macchina ha VISTO. La
        # macchina la profondita' di trap la sa esatta (e' lei che prende la
        # trap e che esegue reti), quindi non e' una stima: e' il controllo che
        # rende la dichiarazione una promessa invece di un commento. Stesso
        # schema di `data`/`ha_dato`: si dichiara, la macchina registra, il
        # lettore confronta.
        # Solo `isr` e `task` sono STRETTI. `kernel` sta in entrambi i contesti
        # ed e' corretto che ci stia: `scheduler` e' chiamato da sched_isr_exit
        # (dentro il tick) e da task_block (da task), che e' precisamente il
        # disegno di questo kernel. Pretendere un contesto solo darebbe un
        # errore su ogni preemption -- l'ha dato, la prima volta.
        if decl is not None:
            if decl.owner == "isr" and not in_trap:
                errori.append(f"ciclo {c}: {nome_canale(ch)} dichiara `owner isr` "
                              f"ma questa marca e' stata prodotta in contesto di "
                              f"TASK: il tag non e' dentro l'ISR")
            if decl.owner == "task" and in_trap:
                errori.append(f"ciclo {c}: {nome_canale(ch)} dichiara `owner task` "
                              f"ma questa marca e' stata prodotta DENTRO una trap: "
                              f"il tag non e' dove la dichiarazione dice")
        if decl is not None and not decl.dato and ha_dato:
            errori.append(f"ciclo {c}: armato un dato sul canale {ch} "
                          f"({nome_canale(ch)}), che non ne dichiara: nessuno "
                          f"lo leggera'")
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
            eventi.append((c, ch, v, cur, dato, ha_dato, in_trap))
            continue
        if v:
            if ch in aperte:
                errori.append(f"ciclo {c}: canale {ch} ({nome_canale(ch)}) riaperto "
                              f"mentre era ancora aperto dal ciclo {aperte[ch][0]}")
            aperte[ch] = (c, v, cur, dato, ha_dato, in_trap)
        elif ch in aperte:
            a, av, acur, ad, ahd, _ = aperte.pop(ch)
            finestre.append((ch, av, a, c, acur, cur, ad, ahd))
        else:
            errori.append(f"ciclo {c}: canale {ch} ({nome_canale(ch)}) chiuso "
                          f"senza essere stato aperto")
    for ch, (a, v, _, _, _, _) in aperte.items():
        errori.append(f"canale {ch} ({nome_canale(ch)}) aperto al ciclo {a} e "
                      f"MAI CHIUSO: quella misura non c'e'")

    categorie = []
    for ch in sorted({f[0] for f in finestre}):
        d = [f[3] - f[2] for f in finestre if f[0] == ch]
        categorie.append({"ch": ch, "nome": nome_canale(ch), "n": len(d),
                          "min": min(d), "max": max(d), "media": sum(d) // len(d),
                          "jitter": max(d) - min(d), "totale": sum(d)})

    fin = []
    for ch, v, a, z, acur, zcur, ad, ahd in finestre:
        fin.append({"dato": etichetta_dato(ch, ad, ahd),
                    "ch": ch, "canale": nome_canale(ch), "marker": nome_marker(ch, v),
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
    for c, ch, v, cur, dato, ha_dato, in_trap in eventi:
        a = chi(cur)
        r = rientro(c, cur)
        evs.append({"c": c, "ch": ch, "canale": nome_canale(ch),
                    "dato": etichetta_dato(ch, dato, ha_dato),
                    "owner": (by_channel[ch].owner if ch in by_channel else "task"),
                    "frase": frase_owner(ch, cur, succ(c), in_trap),
                    "marker": nome_marker(ch, v), "chi": a,
                    "succ": succ(c), "rientro": r,
                    "fuori": (r - c) if r is not None else None})

    return {
        "recording": recording,
        "nmarche":   len(ev),
        "fine":      fine,
        # La frequenza viaggia CON l'analisi, non accanto: chi disegna questi
        # numeri deve poterli convertire senza andare a ricercarla, e senza la
        # possibilita' di prenderne un'altra.
        "hz":        hz or hz_reg,
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
    hz = a.get("hz")
    t = lambda c: (" · " + tempo(c, hz)) if hz else ""
    print(f"\n=== {a['recording']}: {a['nmarche']} marche, "
          f"{a['fine']} cicli{t(a['fine'])} {clock(hz)} ===")

    print("\n--- eventi puntuali (la macchina) ---")
    for p in a["puntuali"]:
        print(f"  ciclo {p['c']:>7}  {p['canale']:<12} {p['v']:<6} "
              f"mentre girava {p['cur']}")
    if not a["puntuali"]:
        print("  nessuno")

    print("\n--- per categoria ---")
    print(f"  {'categoria':<24} {'n':>3} {'min':>9} {'max':>9} {'media':>9} "
          f"{'jitter':>9} {'totale':>9}")
    for c in a["categorie"]:
        print(f"  {c['nome']:<24} {c['n']:>3} {c['min']:>9} {c['max']:>9} "
              f"{c['media']:>9} {c['jitter']:>9} {c['totale']:>9}")
        # La riga del tempo sotto quella dei cicli, e non al posto suo: i cicli
        # restano la misura, il tempo e' la lettura. La frequenza sta in testa
        # alla riga che converte, cosi' si legge come l'ipotesi che e'.
        if hz:
            campi = ("min", "max", "media", "jitter", "totale")
            u = unita_per(max(c[k] for k in campi), hz)
            print(f"  {clock(hz):<24} {'':>3} "
                  + " ".join(f"{tempo(c[k], hz, u):>9}" for k in campi))

    if a["eventi"]:
        print("\n--- eventi (il programma) ---")
        for e in a["eventi"]:
            # "riprende dopo" e non "cicli fuori": la seconda e' vera solo per
            # una preemption, dove la CPU e' stata TOLTA. Per un evento
            # qualunque -- una ricezione -- chi girava non e' stato messo fuori
            # da nessuno, e chiamarlo cosi' direbbe una cosa falsa su un numero
            # vero.
            fuori = ("riprende dopo %d%s" % (e["fuori"], t(e["fuori"]))) \
                    if e["fuori"] is not None else "non riprende piu'"
            d = ("  [" + e["dato"] + "]") if e.get("dato") else ""
            print(f"  ciclo {e['c']:>7}  {e['canale']:<20} {e['marker']:<26} "
                  f"{e['frase']:<44} {fuori}{d}")

    print("\n--- finestra per finestra, e dove sono finiti i cicli ---")
    for f in a["finestre"]:
        dett = "  ".join(f"{o} {n}" for o, n in f["poss"])
        cross = " ATTRAVERSA" if f["cross"] else ""
        dd = ("  " + f["dato"]) if f.get("dato") else ""
        print(f"  {f['canale']:<20} {f['marker']:<30} "
              f"@{f['a']:<7} {f['d']:>6} cicli{t(f['d'])}{cross}{dd}   [{dett}]")

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
    # Rileggere la STESSA registrazione a un'altra frequenza: i cicli non
    # cambiano -- sono la cosa misurata -- cambia solo la loro lettura in
    # tempo. Serve a chiedersi "e a 250 MHz?" senza rieseguire niente.
    l.add_argument("--mhz", type=float,
                   help="rilegge questa registrazione a un'altra frequenza "
                        "(default: quella che la registrazione dichiara)")
    a = ap.parse_args()

    cat, by_channel, nomi = read_catalogue(a.catalogue)
    if a.mode == "generate":
        generate(a.catalogue, cat, a.out, nomi)
        return
    dati = analizza(cat, by_channel, a.recording, a.vx, nomi,
                    hz=int(a.mhz * 1e6) if a.mhz else None)
    if a.json:
        import json
        json.dump(dati, open(a.json, "w"))
    else:
        report(dati)


if __name__ == "__main__":
    main()
