#!/usr/bin/env python3
"""tools/fft_tables.py -- le tabelle costanti di dsp/fft (29/09/2026).

    python3 tools/fft_tables.py > dsp/fft/impl/src/fft_tables.vinc

L'uscita si COMMITTA: l'assembler non calcola seni, e la FFT non deve farlo a
runtime. Non c'e' un --check come per scheduler_facts, e non serve: una tabella
che divergesse dal codice farebbe sbagliare lo spettro, e test_fft lo vede.

Tre tabelle, tutte in .float -- anche gli indici, perche' vloadx e vstorex
leggono la corsia dell'indice come float e la troncano a intero (vcpu.c):

  fft_brev   64 indici 2*rev6(i). Il gather dal blocco dell'ADC fa in un colpo
             il bit reversal (rev6) e il deinterleave (il fattore 2 salta le
             Q, che stanno alla parola dopo)
  fft_top    per ognuno dei 6 stadi, i 32 indici dell'elemento ALTO di ogni
             farfalla. Il basso e' alto + h, e lo fa una vadds
  fft_wre    per ogni stadio, i 32 twiddle W = exp(-2 pi i m / 64) della
  fft_wim    farfalla j, parte reale e immaginaria, CONTIGUI: si caricano
             con una vload (12+P) invece di un gather (12+2P)

Decimation in time di Cooley-Tukey: ingresso in ordine bit-reversed, uscita in
ordine naturale. Allo stadio s (h = 2^s) la farfalla j ha il gruppo g = j / h,
la posizione k = j % h, l'alto in g*2h + k e il basso h piu' in la', e il
twiddle W^(k * N/(2h)).

Nove cifre significative bastano perche' strtof ricostruisca esatto il float32.
"""

import math

N = 64
STAGES = 6
HALF = N // 2


def rev6(i):
  return int(format(i, "06b")[::-1], 2)


def floats(label, values, per_line=8):
  out = []
  for n in range(0, len(values), per_line):
    chunk = ", ".join(values[n:n + per_line])
    head = f"{label}:" if n == 0 else ""
    out.append(f"{head:<9} .float {chunk}")
  return out


def idx(v):
  return str(v)


def num(v):
  # cos(pi/2) in double vale 6.1e-17, non 0: gli zeri esatti devono restarlo
  if abs(v) < 1e-12:
    v = 0.0
  s = f"{v:.9g}"
  return "0" if s in ("0", "-0") else s


def main():
  brev = [idx(2 * rev6(i)) for i in range(N)]
  top, wre, wim = [], [], []
  for s in range(STAGES):
    h = 1 << s
    for j in range(HALF):
      g, k = divmod(j, h)
      top.append(idx(g * 2 * h + k))
      m = k * (N // (2 * h))
      a = 2.0 * math.pi * m / N
      wre.append(num(math.cos(a)))
      wim.append(num(-math.sin(a)))

  lines = [
    "; dsp/fft/impl/src/fft_tables.vinc -- GENERATO da tools/fft_tables.py:",
    "; non si modifica a mano. Le tre tabelle e il loro perche' sono",
    "; nell'intestazione dello script.",
    "",
    "; 2*rev6(i): bit reversal e deinterleave in un gather solo",
  ]
  lines += floats("fft_brev", brev)
  lines += ["", "; per stadio (6 x 32): l'indice ALTO di ogni farfalla"]
  lines += floats("fft_top", top)
  lines += ["", "; per stadio (6 x 32): Re W, W = exp(-2 pi i m / 64)"]
  lines += floats("fft_wre", wre, 4)
  lines += ["", "; per stadio (6 x 32): Im W"]
  lines += floats("fft_wim", wim, 4)
  print("\n".join(lines))


if __name__ == "__main__":
  main()
