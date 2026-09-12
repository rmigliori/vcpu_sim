# mailbox.vinc: il tipo MAILBOX, cioe' una TESTA il cui terzo campo ha la
# convenzione della mailbox e non quella delle code (§8.2, §8.3).
get_filename_component(_iface_dir ${CMAKE_CURRENT_SOURCE_DIR} DIRECTORY)

vasm_interface(vinc_mailbox
  DIR        ${_iface_dir}
  HEADERS    mailbox/mailbox.vinc
  INTERFACES vinc_queue)
