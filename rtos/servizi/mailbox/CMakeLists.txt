# ---------------------------------------------------------------------------
#  lib_messaggi — send e receive: muovere i messaggi, e bloccare chi aspetta.
#
#  NESSUN interface/, ed e' l'immagine speculare di generic/messaggio/, che
#  invece e' sola interfaccia. La ragione e' la stessa vista dai due lati: il
#  FORMATO di un messaggio e' fatto di costanti e non ha codice, il SERVIZIO
#  che li muove e' fatto di codice e non pubblica una costante -- la sua
#  interfaccia sono due simboli, send e receive, e i simboli viaggiano nel .va.
#  Il giorno che la mailbox avesse dei codici di esito propri, nascerebbe qui
#  un interface/mailbox/ e questa nota andrebbe via.
#
#  Che le due meta' fossero una libreria sola era il residuo da sciogliere:
#  stavano insieme perche' parlano della stessa cosa, non perche' abbiano le
#  stesse dipendenze.
# ---------------------------------------------------------------------------
add_subdirectory(impl)
