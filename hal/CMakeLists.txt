# ---------------------------------------------------------------------------
#  hal — lo strato che conosce la macchina, e il solo.
#
#  NON sta sotto rtos/, e non e' una svista: machine.vasm non nomina un solo
#  simbolo del kernel (§12.2 della proposta), e la prova e' che `ld machine.vo`
#  da solo si chiude. Chi dipende da lui — le code, i timeout — dipende dal suo
#  CONTRATTO e non dalla sua implementazione: irq_save e irq_restore, due
#  simboli. E' questo che ne fa un fratello di generic/ e di rtos/ invece che un
#  pezzo di uno dei due, e che lascia il posto a un secondo port accanto a
#  machine.vasm senza toccare una riga altrove.
#
#  L'ordine dei due add_subdirectory non e' libero: impl/ dichiara
#  INTERFACES vinc_hal, e in CMake un target dev'essere gia' definito quando lo
#  si nomina. L'interfaccia viene prima, sempre — vale per ogni libreria di
#  questo albero.
# ---------------------------------------------------------------------------
add_subdirectory(interface)
add_subdirectory(impl)
