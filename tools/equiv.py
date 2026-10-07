#!/usr/bin/env python3
"""tools/equiv.py -- la stessa macchina AL CICLO? (29/09/2026)

    python3 tools/equiv.py <build> <simulatore> <cartella_uscite>

Rilancia ogni programma che ctest esegue -- con i suoi --kbd, --adc, -I --
aggiungendo --trace e --marks, e scrive per ognuno <nome>.out (stdout, stderr,
exit code: la traccia stampa il ciclo di ogni istruzione e di ogni trap) e
<nome>.marks (la registrazione del marcatore). Due simulatori si confrontano
con un diff delle due cartelle:

    git worktree add --detach /tmp/head HEAD
    (cd /tmp/head && cmake -B out -S . && cmake --build out -j)
    python3 tools/equiv.py out /tmp/head/out/vcpu_sim /tmp/eq_head
    python3 tools/equiv.py out $PWD/out/vcpu_sim      /tmp/eq_nuovo
    diff -rq /tmp/eq_head /tmp/eq_nuovo               # vuoto = stessa macchina

--- PERCHE' ESISTE, e cosa aggiunge a ctest e alle impronte ---
ctest guarda i numeri DICHIARATI; tools/fingerprint.sh guarda i .vx, cioe'
l'uscita della TOOLCHAIN. Quando cambia il SIMULATORE nessuno dei due basta:
una ristrutturazione che spostasse un evento di un ciclo lascerebbe verdi
quasi tutti gli EXPECT e ferme tutte le impronte. Tracce identiche vogliono
dire che ogni istruzione, ogni trap e ogni marca cadono nello stesso ciclo.
E' nato per la ristrutturazione pura del 29/09 (core, periferiche, scheda:
§3.78 dell'handoff), che doveva cambiare la forma e non il comportamento.

--- PRIMA DI FIDARSI, vederlo ROSSO ---
Il 29/09 l'uguaglianza e' venuta al primo colpo, e il confronto e' stato
mutato due volte per vedere che sapesse vedere una differenza: il divisore
delle periferiche a 2 ha cambiato SOLO i 5 programmi con l'ADC, e `>` al posto
di `>=` nella tastiera SOLO i 4 con la tastiera.

Le uscite stanno nella cartella data, e i percorsi dentro l'uscita sono
relativi: il programma gira con quella cartella come directory corrente,
altrimenti "marche: N in <percorso>" farebbe differire due cartelle diverse.
I test trace_* (la pagina sotto gjs) e i passi asm/ld non sono programmi, e
si saltano. Due corse dello stesso simulatore danno uscite identiche byte per
byte: e' la prima cosa da verificare su una macchina nuova.

--- IL TETTO, e perche' l'uscita non passa dalla memoria ---
Il 29/09 sera il confronto ha fatto intervenire l'OOM killer: test_bases, nato
insieme a CMP_CYC, sul simulatore di HEAD non riceve mai lo sparo sui cicli e
gira in tondo su `bne`, e --trace stampa ogni giro. In 120 secondi sono GIGA,
e capture_output li teneva tutti in RAM (5,6 GB su 6,8). Ora l'uscita va nel
file a blocchi e si ferma a TETTO byte; la piu' grande legittima era 21 MB
(clock.out). Anche la cartella delle uscite conta: /tmp e' tmpfs, cioe' RAM.
Un'uscita troncata finisce con "--- troncata a TETTO byte ---", cosi' un
programma appeso si riconosce invece di sembrare solo diverso.

--- OLTRE IL TETTO, L'HASH (07/10/2026, §3.85) ---
Il tetto rendeva il confronto CIECO dopo i primi 64 MB: il programma veniva
ucciso li', e due simulatori diversi solo da quel punto in poi davano due file
identici -- "stessa macchina" detto di due macchine diverse. Le tracce della
macchina a stati (tappa 4) arrivano a ~77 MB, quindi il buco avrebbe morso al
primo uso vero. Adesso il flusso si legge fino alla fine e passa tutto da uno
SHA-256: su disco restano i primi TETTO byte, che servono a chi deve leggere
DOVE due tracce divergono, e in coda la lunghezza e l'impronta del flusso
intero, che dicono SE divergono. La riga c'e' solo oltre il tetto: sotto, il
file e' gia' il flusso intero, e le uscite restano quelle di prima byte per
byte. Un programma appeso non lo ferma piu' il tetto ma il timer dei 120
secondi, e lo dice ("--- fermato dopo 120 secondi ---").

Visto rosso il 07/10, prima di fidarsi: un programma da ~80 MB di traccia con
una sola `srai` in fondo, e un simulatore in cui `srai` costa 2 cicli invece
di 1. Il confronto di prima diceva "identici"; questo no.
"""
import hashlib, os, re, shlex, subprocess, sys, tempfile, threading

TETTO = 64 << 20                                      # byte di stdout per programma

if len(sys.argv) != 4:
  sys.exit(__doc__.split("\n\n")[1])
build, sim, outdir = sys.argv[1], os.path.abspath(sys.argv[2]), sys.argv[3]
os.makedirs(outdir, exist_ok=True)
txt = subprocess.run(["ctest", "--test-dir", build, "-N", "-V"],
                     capture_output=True, text=True).stdout

names = dict(re.findall(r"^\s*Test\s+#(\d+):\s+(\S+)", txt, re.M))
done = 0
for num, cmd in re.findall(r"^(\d+): Test command: (.*)$", txt, re.M):
  argv = shlex.split(cmd)
  if "vasm_check.cmake" in cmd:
    k = argv.index("--")
    argv = argv[k + 3:]                               # salta MODE e EXPECT
    argv = [a for x in argv for a in x.split(";")]    # le liste CMake, come vasm_check
  if not argv or not argv[0].endswith("vcpu_sim"):
    continue                                          # trace_*: non un programma
  if len(argv) > 1 and argv[1] in ("asm", "ld", "ar", "nm"):
    continue
  name = names.get(num, "t" + num)
  marks = name + ".marks"                             # relativo: vedi sopra
  if len(argv) > 1 and argv[1] == "run":
    new = [sim, "run", argv[2], "--trace", "--marks", marks] + argv[3:]
  else:
    new = [sim, "--trace", "--marks", marks] + argv[1:]
  with open(os.path.join(outdir, name + ".out"), "wb") as f, \
       tempfile.TemporaryFile() as err:
    p = subprocess.Popen(new, stdout=subprocess.PIPE, stderr=err, cwd=outdir)
    scaduto = threading.Event()
    def ferma():                                      # appeso: lo ferma il tempo
      scaduto.set()
      p.kill()
    orologio = threading.Timer(120, ferma)
    orologio.start()
    # Il flusso si legge TUTTO e passa TUTTO dall'hash; su disco ne vanno i
    # primi TETTO byte. Memoria e disco costanti, a qualunque lunghezza.
    impronta = hashlib.sha256()
    n = 0
    for blocco in iter(lambda: p.stdout.read(1 << 16), b""):
      impronta.update(blocco)
      if n < TETTO:
        f.write(blocco[:TETTO - n])
      n += len(blocco)
    p.stdout.close()
    rc = p.wait()
    orologio.cancel()
    if n > TETTO:
      f.write(b"\n--- troncata a %d byte; il flusso intero: %d byte, sha256 %s ---\n"
              % (TETTO, n, impronta.hexdigest().encode()))
    if scaduto.is_set():
      f.write(b"--- fermato dopo 120 secondi ---\n")
    err.seek(0)
    f.write(b"--- stderr ---\n" + err.read(TETTO))
    f.write(b"--- exit %d ---\n" % rc)
  done += 1
print("programmi eseguiti:", done)
