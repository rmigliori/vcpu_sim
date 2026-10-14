# ---------------------------------------------------------------------------
#  lib_eco — le attivita' dell'altimetro sul blocco dell'ADC (14/10/2026).
#
#  Le procedure del super task (§3.89): FRONTE per ora, poi TRACKER e BARKER.
#  Stanno in dsp/ perche' nessuna nomina rtos/: ricevono un blocco e uno
#  scratch e rendono numeri, come la FFT. Una libreria sola e non una per
#  attivita' perche' FRONTE e BARKER_DEC finiranno sulla stessa rivelazione.
# ---------------------------------------------------------------------------
add_subdirectory(interface)
add_subdirectory(impl)
