#!/usr/bin/env python3
"""tools/grafici_tappa4.py -- l'alfa/beta e il Barker-5, in grafici (07/10/2026, §3.84)

    python3 tools/grafici_tappa4.py <uscita.html> [--chartjs <chart.umd.min.js>]

Scrive una pagina con cinque grafici: la calibrazione (posizione e residui,
cinque semi), la risposta al gradino e alla rampa, l'autocorrelazione del
Barker-5 e l'integrazione coerente dei cinque blocchi. Il template e' accanto,
tools/grafici_tappa4.template.html.

--- CHE COSA SONO QUESTI NUMERI ---
Una simulazione in Python, fatta PRIMA della macchina a stati: il tracker qui
non gira sulla CPU simulata. L'eco del Barker invece viene dal modello vero,
tools/mare.py, chiamato direttamente. I numeri sono quelli di §3.82: gate
1,5 m, sweep 1 ms, bersaglio a 0,25 gate a sweep, alfa 0,6 e beta di
Benedict-Bordner, rumore di misura 0,2 gate. Al passo 4 si rigenera e si
confronta con cio' che la macchina misura davvero.

--- CHART.JS: in rete, o dentro la pagina ---
Senza --chartjs la pagina carica Chart.js 4.4.1 da cdnjs, e senza rete non
disegna. Con --chartjs lo incorpora, e la pagina si apre ovunque:

    curl -o /tmp/chart.umd.min.js \\
      https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.4.1/chart.umd.min.js
    python3 tools/grafici_tappa4.py docs/grafici/alfabeta-barker.html \\
      --chartjs /tmp/chart.umd.min.js

La copia minificata di cdnjs non porta la nota di licenza, e la MIT la chiede:
la aggiunge questo script. La pagina generata non sta in git (.gitignore):
la si rifa da qui.
"""
import json, math, os, random, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mare

QUI = os.path.dirname(os.path.abspath(__file__))
CDN = "https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.4.1/chart.umd.min.js"
LICENZA = ("/*! Chart.js v4.4.1 | https://www.chartjs.org | (c) 2024 Chart.js "
           "Contributors | Released under the MIT License */")

# --- l'alfa/beta della calibrazione -----------------------------------------
V, SIGMA, ERRORE0 = 0.25, 0.2, 10.0   # gate/sweep, gate, distanza iniziale dall'eco
SWEEP = 12                            # CAL ne usa 5: il resto mostra dove va la stima
SEMI = range(1, 6)

def beta(alfa):
  return alfa * alfa / (2 - alfa)     # Benedict-Bordner, citato a memoria in §3.82

def calibrazione(alfa, due_punti, seme):
  """Posizione vera, misure, stime e residui. Il residuo e' misura - predizione,
  None negli sweep in cui il filtro non gira (i primi due, a due punti)."""
  rnd = random.Random(seme)
  b = beta(alfa)
  vera = [ERRORE0 + V * k for k in range(SWEEP)]
  z = [p + rnd.gauss(0, SIGMA) for p in vera]
  stima, residui = [], []
  if due_punti:
    x, v = z[0], 0.0                  # sweep 1: la posizione
    stima.append(x); residui.append(None)
    x, v = z[1], z[1] - z[0]          # sweep 2: la velocita'
    stima.append(x); residui.append(None)
    primo = 2
  else:
    x, v, primo = 0.0, 0.0, 0         # da zero: la finestra a priori, ferma
  for k in range(primo, SWEEP):
    xp = x + v
    r = z[k] - xp
    x, v = xp + alfa * r, v + b * r
    stima.append(x); residui.append(r)
  return vera, z, stima, residui

def risposta(tipo, alfa=0.6, n=16):
  """Il filtro a regime e senza rumore: un gradino di 4 gate, o una rampa di 1
  gate a sweep, dallo sweep 4."""
  b = beta(alfa)
  x = v = 0.0
  vero, stima = [], []
  for k in range(n):
    t = 0.0 if k < 3 else (4.0 if tipo == "gradino" else float(k - 2))
    xp = x + v
    r = t - xp
    x, v = xp + alfa * r, v + b * r
    vero.append(t); stima.append(x)
  return vero, stima

# --- il Barker-5 sui cinque blocchi, col modello vero ------------------------
CODICE = [1, 1, 1, -1, 1]
# Ampiezza 40 contro rumore 20, APPOSTA: con la scena di §3.82 (1000) l'eco
# sovrasta il rumore gia' in un blocco solo, e il guadagno non si vedrebbe.
SCENA = dict(quota=90000.0, velocita=375.0, ampiezza=40.0, fronte=4.5,
             coda=1000.0, fase=0.785, rumore=20.0, seme=1, clutter=[], perdita=[])
HZ, T0, PRI, W = 1e8, 10000, 100000, 64

def barker():
  """I cinque blocchi decodificati (moltiplicati per il segno del loro TX),
  proiettati sulla fase dell'eco. La finestra e' centrata sul ritardo VERO di
  ogni sweep: un tracker ideale."""
  txs, ricevuti = [], []
  for k, s in enumerate(CODICE):
    tk = T0 + k * PRI
    txs.append((tk, s))
    tau = 2 * (SCENA["quota"] + SCENA["velocita"] * tk / HZ) / (mare.C - SCENA["velocita"]) * HZ
    inizio = tk + round(tau) - W // 2
    y = []
    for i in range(W):
      ci, cq = mare.campione(SCENA, HZ, txs, inizio + i)
      y.append(ci * math.cos(SCENA["fase"]) + cq * math.sin(SCENA["fase"]))
    ricevuti.append(y)
  blocchi = [[s * v for v in y] for s, y in zip(CODICE, ricevuti)]   # decodificati
  media = [sum(b[i] for b in blocchi) / 5 for i in range(W)]
  grezza = [sum(y[i] for y in ricevuti) / 5 for i in range(W)]       # senza codice: 3/5
  return blocchi, media, grezza

def autocorrelazione(c):
  return [sum(c[i] * c[i + m] for i in range(len(c) - m)) for m in range(len(c))]

def arrotonda(o):
  if isinstance(o, dict):  return {k: arrotonda(v) for k, v in o.items()}
  if isinstance(o, (list, tuple)): return [arrotonda(v) for v in o]
  if isinstance(o, float): return round(o, 3)
  return o

def dati():
  cal, ing5, ing7 = {}, {}, {}
  for s in SEMI:
    vera, z, e2, r2 = calibrazione(0.6, True, s)
    _, _, e0, r0 = calibrazione(0.6, False, s)
    cal[s] = dict(true=vera, z=z, est2=e2, res2=r2, est0=e0, res0=r0)
    ing5[s] = calibrazione(0.5, False, s)[3]
    ing7[s] = calibrazione(0.7, False, s)[3]
  blocchi, media, grezza = barker()
  return arrotonda(dict(cal=cal, ing5=ing5, ing7=ing7,
                        gradino=risposta("gradino"), rampa=risposta("rampa"),
                        autocorr=autocorrelazione(CODICE),
                        barker=dict(blocks=blocchi, avg=media, plain=grezza)))

def main():
  a = sys.argv[1:]
  chartjs = None
  if "--chartjs" in a:
    i = a.index("--chartjs")
    if i + 1 >= len(a):
      sys.exit("--chartjs vuole il file di Chart.js")
    chartjs = a[i + 1]
    del a[i:i + 2]
  if len(a) != 1:
    sys.exit(__doc__.split("\n\n")[1])
  pagina = open(os.path.join(QUI, "grafici_tappa4.template.html")).read()
  if chartjs:
    lib = open(chartjs).read()
    if "</script" in lib.lower():
      sys.exit("%s contiene '</script': non si puo' incorporare" % chartjs)
    tag = "<script>\n" + LICENZA + "\n" + lib + "\n</script>"
  else:
    tag = '<script src="%s"></script>' % CDN
  pagina = pagina.replace("<!--CHARTJS-->", tag, 1)
  pagina = pagina.replace("/*DATI*/null", json.dumps(dati(), separators=(",", ":")), 1)
  os.makedirs(os.path.dirname(os.path.abspath(a[0])), exist_ok=True)
  open(a[0], "w").write(pagina)
  print("scritta %s (%d byte, Chart.js %s)" % (a[0], len(pagina), "incorporato" if chartjs else "da cdnjs"))

if __name__ == "__main__":
  main()
