# ---------------------------------------------------------------------------
#  generic — tutto cio' che non usa lo scheduler.
#
#  Il criterio e' quello, letterale, e da' una partizione senza residui: un
#  modulo sta qui se non nomina un simbolo del kernel. Le code e il pool ne
#  sono l'esempio -- una lista doppiamente concatenata e un allocatore a
#  blocchi fissi non hanno niente a che vedere con la schedulazione, e
#  l'unico contratto che chiedono al mondo sotto e' la sezione critica.
#
#  Non sta sotto rtos/ APPOSTA. Se ci stesse, l'albero affermerebbe un
#  contenimento che il grafo delle dipendenze nega: questa roba si solleva di
#  peso in un progetto che di scheduler non ne ha, purche' quel progetto
#  fornisca irq_save e irq_restore. E' il senso della parola "generic", e la
#  cartella deve poterlo dimostrare invece di dichiararlo.
#
#  Ne segue una regola per chi aggiunge qui dentro: se un modulo di generic/
#  finisce per nominare un simbolo o un header di rtos/, non e' generic. Vale
#  anche per i suoi test — ed e' esattamente il difetto che test_queue aveva,
#  sanato prima di cominciare a spostare.
# ---------------------------------------------------------------------------
# In ordine di dipendenza, come ovunque nell'albero: messaggio prima di
# timeout, che dal 07/09/2026 ne usa il formato per comporre il messaggio di
# scadenza (timeout_expired).
add_subdirectory(queue)
add_subdirectory(pool)
add_subdirectory(message)
add_subdirectory(timeout)
add_subdirectory(test)
