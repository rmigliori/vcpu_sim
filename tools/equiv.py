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
"""
import os, re, shlex, subprocess, sys

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
  r = subprocess.run(new, capture_output=True, text=True, timeout=120, cwd=outdir)
  with open(os.path.join(outdir, name + ".out"), "w") as f:
    f.write(r.stdout)
    f.write("--- stderr ---\n" + r.stderr)
    f.write("--- exit %d ---\n" % r.returncode)
  done += 1
print("programmi eseguiti:", done)
