# ---------------------------------------------------------------------------
#  servizi — i clienti del kernel: chi blocca e risveglia i task.
#
#  Un servizio si riconosce da una firma sola: chiama task_ready e task_block.
#  Non e' parte del kernel (§3.22) e non e' generic (usa lo scheduler), quindi
#  ha un livello suo. Oggi c'e' la sola mailbox; semafori e mutex nasceranno
#  qui accanto, e con la stessa forma -- un formato generico pubblicato in
#  generic/ e un servizio che blocca pubblicato qui.
# ---------------------------------------------------------------------------
add_subdirectory(mailbox)
