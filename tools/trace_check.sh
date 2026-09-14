#!/bin/sh
# tools/trace_check.sh — ESEGUE la pagina di trace.py, fuori dal browser.
#
#   tools/trace_check.sh <build> <prog.vx> [-- opzioni della macchina...]
#
# Genera la pagina, ne estrae lo script, e lo fa girare sotto gjs con il DOM
# finto di trace_dom.js piu' le asserzioni di trace_cursori_test.js. Esce != 0
# se gjs solleva un'eccezione o se un'asserzione fallisce.
#
# --- PERCHE' E' UN TEST E NON PIU' UNA RICETTA ---
#
# Fino al 13/09/2026 il modo di verificare questa pagina era scritto in testa a
# tools/trace_dom.js e si eseguiva a mano. Ha funzionato -- ha stanato quattro
# difetti -- ma una verifica che si esegue a mano e' una verifica che qualcuno
# prima o poi non esegue, ed e' esattamente cio' che §4 dell'handoff dice delle
# invarianti prima che diventassero ctest: "un commento e la disciplina di chi
# lo esegue".
#
# --- COSA COPRE E COSA NO ---
#
# Copre la LOGICA SUI DATI: che lo script non esploda su un programma di forma
# diversa da quello per cui e' stato scritto, e che il calcolo dei cursori sia
# giusto. NON copre i pixel e gli eventi -- il DOM finto non ha layout -- e non
# copre come la pagina APPARE. Per quello si apre nel browser.
#
# --- gjs E' OPZIONALE ---
#
# Il CMakeLists aggiunge questi test solo se gjs c'e' (find_program). Su una
# macchina senza, `ctest` resta verde e questi test semplicemente non esistono:
# meglio un test che manca di un test che fallisce per l'ambiente.
set -eu

BUILD=$1; shift
VX=$1;    shift
[ "${1:-}" = "--" ] && shift || true

QUI=$(dirname "$0")
RADICE=$(dirname "$QUI")
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# --sim: il simulatore glielo diciamo, perche' la cartella di build puo'
# chiamarsi in qualunque modo (e nominarla per nome e' costato due difetti).
python3 "$QUI/trace.py" "$VX" --sim "$BUILD/vcpu_sim" -o "$TMP/p.html" \
        ${1:+--} "$@" >/dev/null

python3 - "$TMP/p.html" > "$TMP/p.js" <<'FINE'
import re, sys
h = open(sys.argv[1]).read()
m = re.search(r"<script>(.*)</script>", h, re.S)
if not m:
    sys.exit("la pagina non ha uno <script>: trace.template.html e' cambiato?")
print(m.group(1))
FINE

# I tre pezzi in un FILE, e gjs lo esegue da li'.
#
# Prima si passavano a `gjs -c "$(cat)"`, cioe' dentro UN argomento, e il
# 14/09/2026 la pagina ha sfondato il tetto: Linux limita un singolo argomento a
# 128 KB (MAX_ARG_STRLEN, 32 pagine) e la linea temporale delle routine ha
# portato la pagina da ~120 a ~155. L'errore era "Elenco degli argomenti troppo
# lungo", che non dice niente su cosa sia cresciuto.
#
# Un file non ha quel tetto, e la pagina continuera' a crescere.
cat "$QUI/trace_dom.js" "$TMP/p.js" "$QUI/trace_cursori_test.js" > "$TMP/tutto.js"
OUT=$(gjs "$TMP/tutto.js" 2>&1) || {
  echo "$OUT" >&2
  echo "trace_check: gjs e' uscito in errore su $(basename "$VX")" >&2
  exit 1
}
echo "$OUT" | grep -v '^###' || true

# Un'eccezione non sempre fa uscire gjs in errore: si controlla anche il testo.
if echo "$OUT" | grep -qi "JS ERROR"; then
  echo "trace_check: eccezione JS su $(basename "$VX")" >&2
  exit 1
fi
if ! echo "$OUT" | grep -q "TUTTO VERDE"; then
  echo "trace_check: asserzioni fallite su $(basename "$VX")" >&2
  exit 1
fi
