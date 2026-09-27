# ---------------------------------------------------------------------------
#  lib_wait — la coppia dedicata di §3.74: `wait` sospende il titolare,
#  `wait_signal` lo sveglia da un'ISR, e un CONTATORE fa si' che una sveglia
#  arrivata prima della sospensione non si perda.
#
#  Stessa forma di semaforo e mailbox: un interface/ col TIPO e un impl/ col
#  codice. Il tipo qui e' una .struct propria e non un .equ derivato da HEAD,
#  perche' una WAIT non e' una testa di lista: l'attendente e' uno solo.
# ---------------------------------------------------------------------------
add_subdirectory(interface)
add_subdirectory(impl)
