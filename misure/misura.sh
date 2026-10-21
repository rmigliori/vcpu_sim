#!/bin/sh
# misura.sh <nome> [args di run...] -- assembla misure/<nome>.vasm, linka
# con le librerie del foreground e lo fa girare. Uscita limitata (OOM, §3.81).
# I prodotti (.vo .vx) vanno in out/misure/, fuori da git: il build e' in out/.
#
# Non e' in ctest: sono misure di COSTO, e ogni modifica del kernel le muove
# legittimamente. I numeri attesi sono in docs/stato-lavori.md (§3.86, §0).
set -e
M=$(cd "$(dirname "$0")" && pwd)
R=$(dirname "$M")
O=$R/out/misure
V=$R/out/vasm
S=$R/out/vcpu_sim
N=$1; shift
# lib_hal_banco e' un HAL ALTERNATIVO, non un'aggiunta: definisce tutto cio'
# che definisce lib_hal. Va AL POSTO di lib_hal, come CONFIG banco in CMake;
# messa davanti a tutti, m_attivazione esce con un load fuori dai limiti.
case $N in
  m_banco) HAL=lib_hal_banco ;;
  *)       HAL=lib_hal ;;
esac
mkdir -p "$O"
$S asm -I$R/generic/queue/interface -I$R/rtos/scheduler/interface \
       -I$R/rtos/services/wait/interface -I$R/hal/interface \
       -I$R/dsp/fft/interface -I$R/dsp/eco/interface \
       $M/$N.vasm -o $O/$N.vo
$S ld $O/$N.vo $V/lib_fft.va $V/lib_wait.va $V/lib_kernel.va \
      $V/lib_queue.va $V/$HAL.va -o $O/$N.vx
timeout 60 $S run $O/$N.vx "$@" 2>&1 | head -c 20000
