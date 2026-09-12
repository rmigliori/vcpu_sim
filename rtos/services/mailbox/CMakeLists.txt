# ---------------------------------------------------------------------------
#  lib_messaggi — send e receive: muovere i messaggi, e bloccare chi aspetta.
#
#  Ha un interface/ dal 07/09/2026, e la nota che stava qui prevedeva il caso
#  ("il giorno che la mailbox avesse dei codici di esito propri") indovinando il
#  quando e sbagliando il cosa: non sono codici di esito, e' un TIPO. La mailbox
#  E' una TESTA, ma il suo terzo campo ha una convenzione sua (§8.2), e finche'
#  messageHandling.vasm scriveva "TESTA.count" quella convenzione stava in un
#  commento invece che nel codice. Ora mailbox.vinc la nomina.
#
#  Resta vero il resto: il FORMATO di un messaggio e' costanti senza codice
#  (generic/message/, sola interfaccia), il SERVIZIO che li muove e' codice, e
#  le sue entry sono due simboli che viaggiano nel .va. Il giorno che nascesse
#  anche un mailbox_api.vinc, sarebbe per pubblicare il contratto di chiamata di
#  send e receive, non per gli .extern in se' (§3.26 dell'handoff).
#
#  Che le due meta' fossero una libreria sola era il residuo da sciogliere:
#  stavano insieme perche' parlano della stessa cosa, non perche' abbiano le
#  stesse dipendenze.
# ---------------------------------------------------------------------------
add_subdirectory(interface)
add_subdirectory(impl)
