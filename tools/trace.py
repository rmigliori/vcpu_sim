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

--- E DAL 13/09/2026 ANCHE IL MARCATORE ---

La stessa corsa produce la REGISTRAZIONE delle marche (--marks), e la pagina la
disegna: una corsia per categoria, ogni finestra rigata dai tratti di chi
possedeva la CPU dentro di essa, e la sovrapposizione allineata al trigger dove
il jitter si vede. Una corsa sola e non due, perche' allineare a occhio la
finestra di un'esecuzione alle commutazioni di un'altra e' esattamente cio' che
il disegno esiste per non dover chiedere. E' neutrale: con e senza --marks
test_events fa gli stessi 9259/18785.

L'analisi NON e' qui: si importa da tools/marks.py, che e' anche cio' che
stampa `marks.py read`. Un programma senza tag non e' un caso limite -- oggi e'
la norma -- e la sezione si nasconde da sola.

--- COSA PRODUCE ---

Una pagina HTML autosufficiente (i dati sono dentro; le uniche risorse esterne
sono i font, che senza rete cadono sui fallback dichiarati). Il modello e'
tools/trace.template.html: si modifica quello per cambiare il disegno, e questo
file non si tocca.

E SI VERIFICA: tools/trace_dom.js piu' gjs eseguono lo script della pagina fuori
dal browser. La ricetta e' in testa a quel file, e va fatta su PIU' programmi --
i difetti di questa pagina sono tutti della forma "funziona su quello per cui e'
stata scritta".
"""

import argparse, bisect, collections, datetime, glob, json, os, re, subprocess, sys

RADICE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QUI    = os.path.dirname(os.path.abspath(__file__))
TIMEOUT = 60      # secondi: un programma di questo progetto ne impiega meno di uno

# L'analisi delle marche e la lettura dei cicli in tempo stanno in marks.py, e
# si importano invece di rifarle: due implementazioni della stessa aritmetica
# direbbero due numeri diversi sugli stessi dati -- la forma precisa del difetto
# che marks.conf esiste per togliere.
sys.path.insert(0, QUI)
import marks


G_SIM = None      # --sim: lo dice il chiamante invece di farlo indovinare


def sim():
    """Il simulatore, che e' anche assembler e nm.

    Si cerca in out/ e build/, che sono i due nomi usati nella documentazione.
    Ma `cmake -B <dir>` non impone un nome, quindi chi ne usa un altro deve
    poterlo dire: --sim. Senza, questo strumento fallisce su un albero
    costruito altrove -- e chi lo chiama da dentro un programma rischia di
    prendere il fallimento per "nessuna traccia" (e' successo a
    tools/scheduler_facts.py, che perdeva un terzo della pagina in silenzio).
    """
    if G_SIM:
        return G_SIM
    for p in ("out/vcpu_sim", "build/vcpu_sim"):
        c = os.path.join(RADICE, p)
        if os.path.exists(c):
            return c
    sys.exit("vcpu_sim non trovato: cmake -B out -S . && cmake --build out -j\n"
             "  (se l'albero di build ha un altro nome, dillo con --sim <percorso>)")


def manifesto(vx):
    """(base, configurazione, [-D...]) per il programma, dal manifesto del build.

    Il nome del programma NON basta a ri-assemblarne i moduli: se e' stato
    costruito in una configurazione, i suoi .ifdef erano accesi, e un listato
    prodotto senza gli stessi -D e' PIU' CORTO di cio' che gira. Le etichette
    dopo il primo blocco condizionale slittano, i corpi dei task cadono nel
    posto sbagliato, e la pagina attribuisce tutto a un modulo solo -- senza
    dirlo, perche' listato() scarta i fallimenti apposta.
    """
    nome = os.path.splitext(os.path.basename(vx))[0]
    man = os.path.join(os.path.dirname(os.path.abspath(vx)), "configs.txt")
    if not os.path.exists(man):
        return nome, "", []
    for riga in open(man):
        if riga.startswith("#") or not riga.strip():
            continue
        parti = riga.rstrip("\n").split("|")
        if parti[0] != nome:
            continue
        cfg  = parti[1].strip() if len(parti) > 1 else ""
        defs = parti[2].split() if len(parti) > 2 else []
        base = nome[: -(len(cfg) + 1)] if cfg else nome
        return base, cfg, defs
    return nome, "", []


def base_programma(vx):
    """Il nome del programma SENZA la sua configurazione.

    Serve perche' l'applicazione si ritrova dal nome del .vx, ed e' l'unico
    legame fra il programma e il suo sorgente. Dal 13/09/2026 lo stesso sorgente
    puo' produrre piu' programmi -- test_tmgr e test_tmgr_marks (§3.47) -- e il
    secondo non ha un test_tmgr_marks.vasm: e' lo stesso t_tmgr.vo con un kernel
    diverso sotto.

    Il suffisso NON si indovina: lo dice il manifesto che scrive il build,
    accanto ai .vx. Tagliare un "_qualcosa" finale a naso funzionerebbe finche'
    un programma non si chiama test_due_task, e poi sbaglierebbe in silenzio --
    che e' il modo in cui questo strumento ha gia' perso un task intero.
    """
    return manifesto(vx)[0]


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
    nome = base_programma(vx) + ".vasm"
    app = [p for p in glob.glob(os.path.join(RADICE, "**", nome), recursive=True)
           if os.sep + "impl" + os.sep not in p
           and os.path.relpath(p, RADICE).split(os.sep)[0] not in IGNORA]
    if not app:
        print(f"attenzione: non trovo il sorgente dell'applicazione ({nome}): "
              f"i corpi dei task non saranno riconosciuti", file=sys.stderr)
    # (moduli, applicazione o None). Il secondo e' separato perche' su di lui il
    # fallimento non e' ammesso, mentre sulle librerie si': se ne prendono tutte
    # e una che non fa parte del programma non aggancia niente. Un programma
    # senza sorgente d'applicazione (multi, che nasce da due moduli) non ne ha,
    # e allora non c'e' niente da pretendere.
    return sorted(fuori) + app[:1], (app[0] if app else None)


IGNORA = ("out", "build")     # le cartelle di build, che rispecchiano l'albero
MAX_INC_DIRS = 16             # il tetto dell'assembler: src/assembler.c


def includes():
    """Le cartelle interface/ delle librerie: sono i -I dell'assembler.

    Le cartelle di build vanno escluse, e non per pulizia: CMake ci rispecchia
    dentro la stessa gerarchia, quindi ogni interface/ comparirebbe due volte e
    si sfonderebbe il tetto di 16 -I dell'assembler (MAX_INC_DIRS) con dei
    doppioni.

    CON UNA ECCEZIONE, e non e' un'eccezione alla regola ma il suo complemento:
    le interfacce GENERATE nell'albero di build non esistono nel sorgente, e
    nessun doppione le rispecchia. Sono in <build>/vasm/<libreria>/, una
    cartella per libreria generata, e senza di loro un modulo che le include non
    si ri-assembla affatto -- che e' un fallimento SILENZIOSO qui dentro: il
    listato resta vuoto, nessun corpo viene riconosciuto e tutto il programma
    finisce attribuito al boot.
    E' successo: dal 12/09/2026 test_events include "marks/marks.vinc", che
    tools/marks.py genera da marks.conf, e la sua ripartizione e' diventata
    "boot 100%" senza che niente dicesse perche'.
    """
    # L'albero di build si esclude per QUELLO CHE E', non per come si chiama.
    # IGNORA elenca i due nomi della documentazione, ma `cmake -B <dir>` non ne
    # impone nessuno: con un terzo nome ogni interface/ compariva due volte e i
    # -I passavano da 10 a 18, sopra il tetto di 16 dell'assembler. Ogni
    # ri-assemblaggio falliva, il listato restava vuoto, nessun corpo veniva
    # riconosciuto e la pagina diceva "boot 100%" -- in silenzio, perche'
    # listato() scarta i fallimenti apposta (un modulo puo' non assemblare da
    # solo, ed e' lecito). Trovato il 13/09/2026 da un worktree con la build
    # in 'b'. Qui la cartella la sappiamo: e' quella del simulatore.
    salta = set(IGNORA)
    rel_build = os.path.relpath(os.path.dirname(sim()), RADICE).split(os.sep)[0]
    if rel_build not in ("", "."):
        salta.add(rel_build)

    fuori = []
    for pat in ("*/interface", "*/*/interface", "*/*/*/interface"):
        for d in glob.glob(os.path.join(RADICE, pat)):
            rel = os.path.relpath(d, RADICE)
            if os.path.isdir(d) and rel.split(os.sep)[0] not in salta:
                fuori.append(d)
    fuori += generate()
    fuori = sorted(set(fuori))

    # Sopra il tetto NESSUN assemblaggio riesce, quindi non e' il caso lecito
    # che listato() e' fatto per assorbire: e' una diagnosi, e va detta.
    if len(fuori) > MAX_INC_DIRS:
        sys.exit(f"{len(fuori)} cartelle -I, ma l'assembler ne accetta "
                 f"{MAX_INC_DIRS} (MAX_INC_DIRS in src/assembler.c): nessun "
                 f"modulo si ri-assemblerebbe e la pagina direbbe 'boot 100%'.\n"
                 f"  " + "\n  ".join(os.path.relpath(d, RADICE) for d in fuori))
    return fuori


def generate():
    """Le interfacce GENERATE: <build>/vasm/<libreria>/, accanto al simulatore.

    Si prende l'albero di build da cui viene il simulatore, non tutti e due, o i
    -I raddoppierebbero. Una cartella entra solo se contiene davvero un .vinc,
    cosi' i .vo e i .va che le stanno accanto non diventano dei -I inutili.
    """
    base = os.path.join(os.path.dirname(sim()), "vasm")
    fuori = []
    for d in sorted(glob.glob(os.path.join(base, "*"))):
        if os.path.isdir(d) and glob.glob(os.path.join(d, "**", "*.vinc"), recursive=True):
            fuori.append(d)
    return fuori


def globali(vx):
    out = subprocess.run([sim(), "nm", vx], capture_output=True, text=True).stdout
    return {m.group(2): int(m.group(1))
            for m in (re.match(r"\s*(\d+)\s+T\s+(\S+)", l) for l in out.splitlines()) if m}


def listato(src, inc, tmp, defs=()):
    """(indice, etichetta) per ogni label di codice del modulo, piu' la sua lunghezza."""
    lst = os.path.join(tmp, "traccia.lst")
    cmd = [sim(), "asm", src, "-o", os.path.join(tmp, "traccia.vo"), "--emit-expanded", lst]
    for d in inc:
        cmd += ["-I", d]
    # GLI STESSI -D con cui il programma e' stato costruito: un listato prodotto
    # con .ifdef diversi e' piu' corto di cio' che gira, e ogni etichetta dopo
    # il primo blocco condizionale slitta. Li dice il manifesto del build.
    for d in defs:
        cmd += ["-D", d]
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
    defs = manifesto(vx)[2]
    fini = [(a, n) for n, a in G.items()]     # mappa fine: ogni etichetta nota
    grezzi = []                               # (inizio, proprietario, fine modulo)
    # L'APPLICAZIONE e' l'ultimo della lista, e su di lei il silenzio non e'
    # ammesso: le librerie possono legittimamente non agganciarsi -- se ne
    # prendono tutte e una che non fa parte del programma non aggancia niente --
    # ma il modulo dell'applicazione e' stato scelto PER NOME dal .vx. Se quello
    # non si assembla, i corpi dei task non esistono e la pagina attribuisce
    # tutto al boot: "boot 100%", plausibile e falso. E' successo due volte il
    # 13/09/2026, e tutte e due in silenzio.
    srcs, app = sorgenti(vx)
    for src in srcs:
        loc, lung = listato(src, includes(), tmp, defs)
        basi = {G[n] - i for i, n in loc if n in G}
        if len(basi) != 1:                    # nessun aggancio, o agganci
            if app is not None and src == app:
                sys.exit(f"il modulo dell'applicazione non si aggancia: {src}\n"
                         f"  ri-assemblato con: {' '.join('-D ' + d for d in defs) or '(nessun -D)'}\n"
                         f"  senza i suoi corpi la pagina direbbe 'boot 100%', che e'\n"
                         f"  plausibile e falso. Cause viste: i -D non combaciano con\n"
                         f"  quelli del programma (lo dice il manifesto), oppure un\n"
                         f"  albero di build stale accanto a quello buono.")
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


def analizza(vx, tmp, argomenti=(), hz=None):
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
    # --marks nella STESSA corsa, non in una seconda: due esecuzioni dello
    # stesso programma sarebbero due storie, e allineare a occhio la finestra
    # dell'una alle commutazioni dell'altra e' esattamente cio' che il disegno
    # deve evitare di chiedere al lettore. Verificato che sia neutrale: con e
    # senza --marks test_events fa gli stessi 9259/18785 -- la macchina annota,
    # non esegue. (Il costo dei TAG invece sta nel programma e si paga sempre,
    # ed e' un'altra cosa: lo dice hal/marker.vinc.)
    rec = os.path.join(tmp, "marks.txt")
    try:
        trace = subprocess.run([sim(), "run", vx, "--trace", "--marks", rec, *argomenti],
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
        own, fine_l, _glob = dove(pc)
        # L'ETICHETTA FINE ANCHE PER IL KERNEL (14/09/2026, §3.64).
        #
        # Fino a oggi il codice di un task era attribuito all'etichetta fine e
        # quello di kernel a quella GLOBALE, e la seconda meta' diceva il falso:
        # `scheduler`, `dispatcher`, `sched_scan` e `sched_preempt` non sono
        # .global, quindi i loro cicli finivano sotto `sched_isr_exit`, che e'
        # solo il simbolo che li precede. Su test_tmgr_marks erano 3088 cicli
        # sotto un nome che ne vale 231.
        #
        # Il commento in testa a questo file lo dichiarava gia' -- «con quelli
        # soli, i rami dello scheduler finiscono dentro il simbolo che li
        # precede» -- ed e' la ragione per cui ogni modulo si ri-assembla con
        # --emit-expanded. Le etichette c'erano, pagate a ogni corsa, e venivano
        # buttate via tranne due nomi cablati.
        #
        # NIENTE RAGGRUPPAMENTO: queste etichette sono piu' fini di una
        # procedura (sched_scan_queue, deq_empty, cr_scalar sono rami interni), e
        # il linguaggio non dichiara quali etichette SIANO procedure -- .proc sta
        # su 26 etichette di 283. Dedurlo dai bersagli di `call` non funziona:
        # a `dispatcher` ci si arriva con `j`, e finirebbe assorbito da
        # `scheduler`, cioe' la stessa bugia in piccolo. Si mostrano come sono.
        cur = own or cur
        strato = "task" if own else "kernel"
        ev.append((cyc, cur, strato, fine_l, fine_l))
    if not ev:
        sys.exit("nessuna istruzione tracciata: il programma gira?")

    fine = ev[-1][0] + 1
    durata = lambda i: (ev[i + 1][0] if i + 1 < len(ev) else fine) - ev[i][0]

    loc_rt = collections.Counter()            # per etichetta fine: ctx_save & C.
    for i, e in enumerate(ev):
        loc_rt[e[4]] += durata(i)

    # LA LINEA TEMPORALE DELLE ROUTINE (14/09/2026, §3.64): i tratti di etichetta
    # fine, compressi. Serve ai CURSORI -- «fra A e B che cosa e' girato» -- e
    # non si puo' ricavare dalle fasce, che sono per proprietario e dentro
    # portano solo le prime sei voci di un totale.
    #
    # Compressa in due pezzi: i nomi una volta sola, e la linea come coppie
    # [ciclo, indice]. Su test_tmgr_marks fa 2710 tratti e 85 nomi, cioe' ~35 KB
    # invece degli ~62 della forma con i nomi ripetuti. La pagina intera ne pesa
    # 120, quindi e' un terzo in piu' per una misura che prima non esisteva.
    proc_n, proc_i, proc_t = [], {}, []
    for i, e in enumerate(ev):
        if proc_t and proc_t[-1][1] == proc_i.get(e[3]):
            continue
        if e[3] not in proc_i:
            proc_i[e[3]] = len(proc_n)
            proc_n.append(e[3])
        proc_t.append([e[0], proc_i[e[3]]])

    # LE COMMUTAZIONI, contate invece che divise. La pagina le ricavava da
    # `rt["ctx_restore"] / 84`, cioe' da un totale diviso per un costo medio
    # scritto a mano -- e con l'etichetta fine quel totale e' cambiato, perche'
    # `ctx_restore` adesso e' il solo preambolo. Un ingresso in ctx_restore E'
    # una commutazione: si contano gli ingressi.
    nsw = sum(1 for c, i in proc_t if proc_n[i] == "ctx_restore")

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

    m = marche(rec, vx, hz)
    if m:
        assegna_corsia(m, fasce)

    # La frequenza la dichiara la REGISTRAZIONE, e va letta anche quando le
    # marche non ci sono: un programma senza tag non produce finestre -- ed e'
    # la norma -- ma i suoi cicli la pagina li mostra lo stesso. Presa da
    # marche() soltanto, il tempo sparirebbe proprio dai casi piu' comuni.
    return {
        "hz": hz or marks.frequenza(rec),
        "fine": fine, "tick": tick,
        "fasce": [[f["b"], f["e"], f["o"], f["l"],
                   sorted(f["r"].items(), key=lambda x: -x[1])[:6]] for f in fasce],
        "own": [[o, l, c] for (o, l), c in own.items()],
        "rt": rt.most_common(18),
        # IL CONTESTO, per intero e non per meta'. Erano due nomi, `ctx_save` e
        # `ctx_restore`, e la pagina ne faceva la somma dichiarandola «il costo
        # del contesto». Ma quei due sono solo i PREAMBOLI: il lavoro vero sta
        # in `cs_clean` e soprattutto in `cr_scalar`, che da solo vale 2407 cicli
        # su test_tmgr_marks contro i 203 di `ctx_restore`. La pagina diceva 2633
        # dove la verita' e' il doppio abbondante.
        #
        # L'ELENCO E' SCRITTO A MANO, e va detto perche' e' il genere di cosa che
        # questo progetto cancella di solito. Non si puo' dedurre: vorrebbe dire
        # sapere quali etichette appartengono a quale procedura, e il linguaggio
        # non lo dichiara (vedi il commento su .proc piu' su). Quattro nomi in un
        # posto solo, e il giorno che l'HAL cresce vanno rivisti -- il test che
        # se ne accorge e' il gradino dei cicli in rtos/test/CMakeLists.txt.
        "ctx": {n: loc_rt[n] for n in ("ctx_save", "cs_clean",
                                       "ctx_restore", "cr_scalar")},
        "nsw": nsw,
        "proc": {"nomi": proc_n, "t": proc_t},
        "marche": m,
    }


def assegna_corsia(m, fasce):
    """Su quale CORSIA va disegnato ogni evento: quella di chi lo PRODUCE.

    Un istante non e' del sistema, e' di qualcuno: una ricezione la produce il
    task che riceve, una preemption la produce il kernel che gira nell'ISR. Il
    segno va sulla riga di quello, e li' soltanto -- come su un analizzatore di
    stati logici, dove un evento sta sul canale che lo emette.

    Chi lo produce e' CHI STAVA GIRANDO in quell'istante, e lo si prende dalle
    FASCE, cioe' dalla stessa deduzione che disegna le corsie. Non da `current`,
    che e' un indirizzo di TCB: quello vive in un altro spazio di nomi (`tcbA`
    contro `A`) e farli combaciare vorrebbe dire una convenzione sui nomi che
    nessuno garantisce. Cosi' invece la corsia scelta e' per COSTRUZIONE una
    che esiste nel diagramma, e le due cose non possono discordare.

    Quale sia l'entita' resta comunque dichiarato -- `owner` nel catalogo dice
    se current e' l'autore, la vittima o l'interrotto -- e il readout lo usa.
    Qui si decide solo DOVE cade il segno.
    """
    ini = [f["b"] for f in fasce]
    for e in m.get("eventi", []):
        i = bisect.bisect_right(ini, e["c"]) - 1
        e["corsia"] = fasce[i]["o"] if i >= 0 else None


def marche(rec, vx, hz=None):
    """L'analisi della registrazione, se ce n'e' una da leggere.

    Il calcolo NON e' qui: si importa da tools/marks.py, che e' anche cio' che
    stampa `marks.py read`. Rifarlo qui darebbe due analisi della stessa
    registrazione, e il disegno direbbe un numero diverso dal testo sugli stessi
    dati -- la forma precisa del difetto che marks.conf esiste per togliere.

    Un programma senza tag non e' un errore: la macchina riempie comunque il
    canale 0, e cio' che manca sono le finestre. La pagina lo nota da sola.
    """
    if not os.path.exists(rec):
        return None
    cat = os.path.join(RADICE, "marks.conf")
    if not os.path.exists(cat):
        return None
    catalogue, by_channel, nomi = marks.read_catalogue(cat)
    try:
        return marks.analizza(catalogue, by_channel, rec, vx, nomi, hz=hz)
    except SystemExit:
        return None      # «nessuna marca»: un programma che non ne emette


def main():
    ap = argparse.ArgumentParser(description="Traccia temporale di un programma vcpu_sim")
    ap.add_argument("programma", help="il .vx da tracciare (es. out/vasm/test_tmgr.vx)")
    ap.add_argument("-o", "--out", help="la pagina da scrivere (default: out/trace.html)")
    ap.add_argument("--json", help="scrive anche i dati grezzi qui")
    ap.add_argument("--sim", help="il vcpu_sim da usare (default: out/ o build/)")
    # I cicli si leggono anche come TEMPO, e la frequenza la dichiara la
    # macchina. Questa opzione rilegge la STESSA corsa a un'altra velocita':
    # i cicli non cambiano -- sono la cosa misurata -- cambia la loro lettura.
    ap.add_argument("--mhz", type=float,
                    help="legge i cicli a un'altra frequenza (default: quella "
                         "che la macchina dichiara)")
    ap.add_argument("argomenti", nargs="*", metavar="-- OPZIONI",
                    help="dopo un --: opzioni per la MACCHINA, non per questo "
                         "strumento (es. -- --kbd \"2000:a,6000:b\"). Servono ai "
                         "programmi guidati da un device, che senza alimentatore "
                         "non terminano")
    a = ap.parse_args()

    global G_SIM
    # Il simulatore e le interfacce GENERATE devono venire dallo stesso albero
    # del .vx, non da una ricerca: un out/ vecchio accanto a un build/ fresco ha
    # un catalogo delle marche STALE, il ri-assemblaggio fallisce su una
    # costante che non c'e' ancora, il listato resta vuoto e la pagina dice
    # "boot 100%". Il .vx sta in <build>/vasm/, quindi l'albero lo sappiamo.
    G_SIM = a.sim
    if not G_SIM:
        cand = os.path.join(os.path.dirname(os.path.dirname(
                            os.path.abspath(a.programma))), "vcpu_sim")
        if os.path.exists(cand):
            G_SIM = cand

    vx = a.programma
    out_path = a.out or os.path.join(RADICE, "out", "trace.html")
    tmp = os.path.dirname(os.path.abspath(out_path)) or "."
    os.makedirs(tmp, exist_ok=True)

    dati = analizza(vx, tmp, a.argomenti, hz=int(a.mhz * 1e6) if a.mhz else None)
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
    # Il tempo ACCANTO ai cicli e con la frequenza a vista, che e' la regola di
    # tutti gli strumenti: "497,0 µs" si legge come una misura, e questa e'
    # l'uscita di un modello senza sistema di memoria. Vedi include/vcpu.h.
    hz = dati.get("hz")
    durata = (f" · {marks.tempo(dati['fine'], hz)} {marks.clock(hz)}") if hz else ""
    print(f"{out_path}: {len(dati['fasce'])} fasce, {len(dati['tick'])} tick, "
          f"{dati['fine']} cicli{durata}")
    print(f"  la pagina deve dire «generata {stamp}»: se ne dice un'altra, "
          f"il browser mostra una copia in cache (ctrl-shift-R)")
    per = collections.Counter()
    for o, l, c in dati["own"]:
        per[o] += c
    for o, c in per.most_common():
        print(f"  {o:<10}{c:>8}  {100*c/gran:5.1f}%")


if __name__ == "__main__":
    main()
