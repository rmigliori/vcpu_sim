# ---------------------------------------------------------------------------
#  lib_coda — liste doppiamente concatenate con testa contata.
#
#  Il .vinc piu' basso e piu' incluso del progetto: TCB, MESSAGGIO e BLOCCO
#  sono tutti nodi di lista, quindi tre interfacce su sei includono questa.
#
#  L'interfaccia prima dell'implementazione: impl/ dichiara INTERFACES
#  vinc_coda, e in CMake un target dev'essere definito prima di essere nominato.
# ---------------------------------------------------------------------------
add_subdirectory(interface)
add_subdirectory(impl)
