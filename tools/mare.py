#!/usr/bin/env python3
"""tools/mare.py -- il MONDO dell'altimetro: l'eco dell'acqua (07/10/2026, §3.84)

    python3 tools/mare.py <scena>

Lo lancia il simulatore con --eco-cmd, e parla con lui su stdin/stdout. E' la
copia del simulatore di bersaglio da banco: riceve i TX da un accoppiatore e
rimette l'eco nel ricevitore. La macchina e il mondo non si conoscono (§3.82):
questo programma non sa niente di CPU, e la macchina non sa niente di mare.

--- IL TEMPO E' DEL SIMULATORE ---
Lockstep: il simulatore domanda, aspetta la risposta, e va avanti. Questo
programma e' una FUNZIONE PURA delle domande: niente orologio, niente casualita'
non seminata. Gli istanti sono in tick del clock master, interi.

    CLOCK <hz>                        la prima riga: quanto vale un tick
    TX <istante> <segno>              a ogni TX, segno +1 o -1 (il Barker);
                                      nessuna risposta
    BLOCCO <istante_1> <periodo> <n>  all'avvio di un blocco dell'ADC: il
                                      campione i e' all'istante_1 + i*periodo.
                                      Risposta: n righe "I Q"

I e Q tornano REALI: arrotondare e saturare a 12 bit e' dell'ADC, cioe' della
macchina. Un errore (riga ignota, scena sbagliata) esce con codice 1, e per il
simulatore e' un guasto: lo dice e si ferma.

--- IL MODELLO, per ogni campione all'istante t ---
    il ritardo del TX k    tau_k = 2 (R0 + v t_k) / (c - v), ESATTO per il moto
                           lineare: l'impulso tocca l'acqua a meta' viaggio. t_k
                           e' l'istante ASSOLUTO del TX, in secondi
    dove cade il campione  x = c (t - t_k - tau_k) / 2, in metri; 0 = META' fronte
    l'inviluppo            a(x) = A (1 + erf(x / (sqrt2 sf))) / 2 * exp(-max(x,0) / L)
                           erf: il fronte (Brown, a memoria); sf lo stato del
                           mare. exp: la coda, senza la quale ogni eco entra in
                           tutte le finestre dopo
    I, Q                   a s_k cos(fase), a s_k sin(fase); s_k = il segno del TX
    la somma               su TUTTI i TX: l'eco di seconda passata deve esistere
    il clutter             al TX n, un impulso gaussiano di ampiezza B e sigma
                           1,5 m (un gate a 100 MHz), d metri DOPO il fronte
                           vero: d negativo e' un falso fronte che arriva prima
    la perdita             dal TX a al TX b, inclusi, il contributo e' zero
    il rumore              gaussiano su I e su Q, sigma = rumore: Box-Muller su
                           un HASH di (seme, t). Funzione del TEMPO, non della
                           storia: con un generatore che avanza, spostare una
                           finestra di un ciclo cambierebbe tutti gli sweep dopo

I TX si contano da 1, nell'ordine in cui arrivano. NON modellati, e dichiarati
in §3.82: lo speckle, il Doppler, i lobi laterali.

--- LA SCENA, un file ---
    quota 90000   velocita 375   ampiezza 1000   fronte 4.5   coda 1000
    fase 0.785    rumore 20      seme 1
    clutter 3 -6 1500        al TX n. 3, un falso fronte 6 m prima
    perdita 14 16            dal TX 14 al 16 l'eco non c'e'
Coppie chiave-valore in qualunque ordine e su qualunque riga; '#' commenta.
Le prime otto sono obbligatorie: una scena a meta' e' un errore, non un default.
"""
import math, sys

C = 299792458.0                                       # m/s
M64 = (1 << 64) - 1
SIGMA_CLUTTER = 1.5                                   # m

def fallisci(msg):
  sys.stderr.write("mare: " + msg + "\n")
  sys.exit(1)

def leggi_scena(path):
  try:
    tok = []
    for riga in open(path):
      tok += riga.split("#")[0].split()
  except OSError as e:
    fallisci("scena: %s" % e)
  s = {"clutter": [], "perdita": []}
  semplici = ("quota", "velocita", "ampiezza", "fronte", "coda", "fase",
              "rumore", "seme")
  i = 0
  try:
    while i < len(tok):
      k = tok[i]
      if k in semplici:
        s[k] = int(tok[i + 1]) if k == "seme" else float(tok[i + 1])
        i += 2
      elif k == "clutter":
        s[k].append((int(tok[i + 1]), float(tok[i + 2]), float(tok[i + 3])))
        i += 4
      elif k == "perdita":
        s[k].append((int(tok[i + 1]), int(tok[i + 2])))
        i += 3
      else:
        fallisci("scena: chiave sconosciuta '%s'" % k)
  except (IndexError, ValueError):
    fallisci("scena: valore mancante o non valido dopo '%s'" % tok[i])
  for k in semplici:
    if k not in s:
      fallisci("scena: manca '%s'" % k)
  if s["fronte"] <= 0 or s["coda"] <= 0:
    fallisci("scena: fronte e coda devono essere positivi")
  return s

# splitmix64: un mescolatore, non un generatore -- non ha stato.
def mescola(z):
  z = (z + 0x9E3779B97F4A7C15) & M64
  z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
  z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
  return z ^ (z >> 31)

def uniforme(seme, t, canale):                        # in (0, 1), mai 0
  h = mescola(mescola(mescola(seme & M64) ^ t) ^ canale)
  return ((h >> 11) + 0.5) / float(1 << 53)

def rumore(s, t):
  if s["rumore"] == 0:
    return 0.0, 0.0
  u1, u2 = uniforme(s["seme"], t, 0), uniforme(s["seme"], t, 1)
  r = s["rumore"] * math.sqrt(-2.0 * math.log(u1))
  return r * math.cos(2 * math.pi * u2), r * math.sin(2 * math.pi * u2)

def perso(s, k):
  return any(a <= k <= b for a, b in s["perdita"])

def campione(s, hz, txs, t):
  """L'inviluppo complesso all'istante t (tick master): la somma su tutti i TX."""
  v, A, sf, L = s["velocita"], s["ampiezza"], s["fronte"], s["coda"]
  ts = t / hz
  somma = 0.0
  for k, (tk, segno) in enumerate(txs, start=1):
    if tk > t or perso(s, k):
      continue
    tks = tk / hz
    tau = 2.0 * (s["quota"] + v * tks) / (C - v)
    x = C * (ts - tks - tau) / 2.0
    a = A * (1.0 + math.erf(x / (math.sqrt(2.0) * sf))) / 2.0 * math.exp(-max(x, 0.0) / L)
    for n, d, B in s["clutter"]:
      if n == k:
        a += B * math.exp(-((x - d) ** 2) / (2.0 * SIGMA_CLUTTER ** 2))
    somma += segno * a
  ni, nq = rumore(s, t)
  return somma * math.cos(s["fase"]) + ni, somma * math.sin(s["fase"]) + nq

def main():
  if len(sys.argv) != 2:
    fallisci("uso: mare.py <scena>")
  s = leggi_scena(sys.argv[1])
  hz = None
  txs = []                                            # (istante, segno)
  for riga in sys.stdin:
    p = riga.split()
    if not p:
      continue
    try:
      if p[0] == "CLOCK" and len(p) == 2 and hz is None:
        hz = float(int(p[1]))
        if hz <= 0:
          fallisci("CLOCK non positivo")
      elif hz is None:
        fallisci("la prima riga dev'essere CLOCK, e' arrivato '%s'" % riga.strip())
      elif p[0] == "TX" and len(p) == 3:
        segno = int(p[2])
        if segno not in (1, -1):
          fallisci("TX: il segno e' +1 o -1, non %d" % segno)
        txs.append((int(p[1]), segno))
      elif p[0] == "BLOCCO" and len(p) == 4:
        t1, periodo, n = int(p[1]), int(p[2]), int(p[3])
        out = []
        for i in range(n):
          ci, cq = campione(s, hz, txs, t1 + i * periodo)
          out.append("%r %r\n" % (ci, cq))
        sys.stdout.write("".join(out))
        sys.stdout.flush()
      else:
        fallisci("riga non riconosciuta: '%s'" % riga.strip())
    except ValueError:
      fallisci("numero non valido in '%s'" % riga.strip())

if __name__ == "__main__":
  main()
