# ---------------------------------------------------------------------------
#  lib_pool — allocazione statica a blocchi fissi, sei classi (§10).
#
#  Il modulo piu' lontano dalla macchina che ci sia qui dentro: non nomina un
#  solo simbolo dell'HAL. La sezione critica gli arriva gia' confezionata dai
#  wrapper _s delle code, quindi la sua unica dipendenza e' lib_coda.
# ---------------------------------------------------------------------------
add_subdirectory(interface)
add_subdirectory(impl)
