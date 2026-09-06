# ---------------------------------------------------------------------------
#  rtos — cio' che USA lo scheduler, e solo quello.
#
#  Il criterio e' dell'utente ed e' letterale: un modulo sta qui se nomina un
#  simbolo del kernel. Applicato ai simboli veri, e non alle intenzioni, da'
#  una partizione senza residui -- machine.vasm, coda.vasm, pool.vasm,
#  timeout.vasm e messaggio.vinc non ne nominano nessuno e stanno fuori;
#  scheduler.vasm e' il kernel, messageHandling.vasm chiama task_ready,
#  task_block e current.
#
#  Ne segue un rovesciamento rispetto a com'era l'albero prima: kernel/ era la
#  cartella che conteneva cinque librerie su sei, e adesso lo scheduler e' UNA
#  delle sei. Non e' una perdita, e' la correzione -- §3.22 aveva gia'
#  stabilito che i messaggi sono un CLIENTE del kernel e non parte sua, e la
#  vecchia cartella contraddiceva il grafo.
#
#  L'ordine: scheduler prima di servizi, che lo linka.
# ---------------------------------------------------------------------------
add_subdirectory(scheduler)
add_subdirectory(servizi)
add_subdirectory(demo)
