# ---------------------------------------------------------------------------
#  gestore_timeout — il TASK che fa scadere i timeout (§9.2).
#
#  Cartella al primo livello di rtos/, e non sotto servizi/, per il criterio
#  letterale che quella cartella dichiara: "un servizio si riconosce da una
#  firma sola: chiama task_ready e task_block". Il gestore non li chiama --
#  non blocca nessuno, si blocca LUI sulla propria mailbox come un task
#  qualunque. E' il primo TASK DI SISTEMA del progetto, che e' una terza
#  specie dopo il kernel e i servizi.
#
#  Non poteva stare in generic/timeout accanto a timeout_arm/timeout_cancel:
#  chiama receive e send, cioe' simboli di rtos/, e la regola in fondo a
#  generic/CMakeLists.txt e' senza appello. La riga di confine e'
#  timeout_scaduto, che formatta il messaggio ma non lo manda.
# ---------------------------------------------------------------------------
add_subdirectory(interface)
add_subdirectory(impl)
