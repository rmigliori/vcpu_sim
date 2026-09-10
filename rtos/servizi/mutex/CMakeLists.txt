# ---------------------------------------------------------------------------
#  lib_mutex — la sezione critica con un limite: priority ceiling (ICPP).
#
#  Terzo servizio, e l'unico dei tre che sotto uso corretto NON BLOCCA MAI: la
#  sua coda d'attesa esiste perche' un ceiling dichiarato male DEGRADI invece di
#  appendere (§13.5), non per essere usata. E' anche la ragione per cui il tipo
#  qui e' una .struct vera e non dei .equ derivati come mailbox e semaforo: il
#  mutex ha tre campi in piu' di una TESTA, e il primo di quelli — owner — e'
#  cio' che lo separa da un semaforo binario (§13.2).
# ---------------------------------------------------------------------------
add_subdirectory(interface)
add_subdirectory(impl)
