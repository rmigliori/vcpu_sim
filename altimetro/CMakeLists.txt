# ---------------------------------------------------------------------------
#  altimetro — la macchina a stati della time line (25/10/2026, §3.99).
#
#  Un'APPLICAZIONE, e per questo in cima all'albero: usa dsp/ (le attivita' e
#  la FFT) e rtos/ (il kernel, `wait`), e dsp/ non puo' dipendere da rtos/.
#  Il motore degli slot sembrerebbe un servizio del kernel -- la schedule
#  table di OSEK -- ma ha un'ancora di dominio, la finestra: resta qui finche'
#  non ha un secondo cliente.
# ---------------------------------------------------------------------------
add_subdirectory(interface)
add_subdirectory(src)
add_subdirectory(test)
