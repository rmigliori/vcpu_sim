# Vuota dal 07/09/2026: scheduler_demo.vasm e' stato RITIRATO, non adattato.
#
# Verificava che due task si alternassero a ogni tick (l'invariante 97/64, poi
# 98/65), e con lo scheduler a priorita' quel comportamento non esiste piu': al
# tick l'uscente va nello slot del suo livello e lo slot batte la coda (§4).
# Un test che sopravvive al modello che verificava passa, e dice il falso.
#
# Il suo posto lo prende rtos/test/test_scheduler.vasm, che verifica proprieta'
# dello scheduler invece di due contatori. Questa cartella resta perche' una
# demo -- un esempio d'uso che non sia un test -- avra' senso di nuovo quando
# ci sara' qualcosa da mostrare: la simulazione completa di §3.28.
