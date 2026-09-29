#ifndef LOCATOR_H
#define LOCATOR_H

#include <stdint.h>
#include <stddef.h>

// ---------------------------------------------------------------------------
//  locator.h — IL FILE DI COLLOCAZIONE: le memorie della scheda, e in quale
//  memoria finisce ogni sezione (29/09/2026, la tappa 3).
//
//  --- PERCHE' ESISTE ---
//  Fino al 29/09 il linker concatenava in command order con le basi cablate in
//  C: il codice da 0, i dati da NULL_GUARD, nessuna nozione di memoria. Con due
//  CPU e una RAM CONDIVISA quella forma non basta piu', perche' due programmi
//  linkati separatamente devono mettersi d'accordo sull'indirizzo di cio' che
//  sta in mezzo -- e l'accordo non puo' stare in due posti, o divergono in
//  silenzio. Sta qui, in un file solo, che tutti i link leggono.
//
//  --- L'UNITA' E' DICHIARATA, E NON E' UN VEZZO ---
//  Questa macchina e' Harvard: lo spazio del codice e' indicizzato per
//  ISTRUZIONE (vx_load copia il testo in prog[], il PC indicizza quell'array) e
//  non e' raggiungibile con una lw. Una regione che dicesse ORIGIN e LENGTH in
//  byte per il codice dichiarerebbe una cosa falsa, quindi ogni regione dice su
//  quale spazio sta: CODE in istruzioni, DATA in byte. E' la distinzione
//  TYPE(PM RAM) / TYPE(DM RAM) del .ldf degli SHARC, che esiste per lo stesso
//  motivo; il linker script di GNU non ce l'ha perche' su un von Neumann non
//  serve.
//
//  Da cui un vincolo che il file puo' esprimere e la macchina non sa fare, e
//  allora e' un ERRORE e non una bugia silenziosa: l'origine di una regione
//  CODE puo' essere solo 0, perche' il PC indicizza prog[] da zero.
//
//  E il guard del NULL smette di essere una costante C: una regione DATA che
//  cominciasse a 0 metterebbe il primo dato all'indirizzo 0, dove non si
//  distingue da un puntatore nullo. Anche questo e' un errore, e adesso e' un
//  fatto scritto nella mappa di memoria invece che nel linker.
//
//  --- L'EREDITA': UN BLOCCO DICE SOLO CIO' CHE DIFFERISCE ---
//  La scheda dichiara le memorie e le collocazioni; un blocco PROCESSOR dice
//  soltanto cio' che per quella CPU e' diverso, e un blocco vuoto -- o assente
//  -- vale "come la scheda". Serve a non avere due dichiarazioni dello stesso
//  fatto nel giorno in cui le CPU sono identiche, che e' oggi: il file della
//  tappa 3 non ha nessun blocco. Il posto dove dire "questa CPU no" c'e' dal
//  primo giorno, e non costa una riga.
//
//  --- LA FORMA ---
//    # un commento, come in marks.conf
//    MEMORY
//    {
//      PM     : CODE  ORIGIN = 0,        LENGTH = 4096      # in ISTRUZIONI
//      LOCAL  : DATA  ORIGIN = 4,        LENGTH = 0xFFFFC   # 4 = il guard
//      SHARED : DATA  ORIGIN = 0x200000, LENGTH = 0x10000
//    }
//    SECTIONS
//    {
//      .text > PM
//      .data > LOCAL
//    }
//    PROCESSOR cpu0 { }     # niente da dire: prende la scheda
//
//  --- COSA QUESTO FILE NON CONTROLLA, e va detto ---
//  Che una regione sia davvero coperta da RAM sulla scheda. Un file che
//  dichiarasse SHARED a 0x300000 passerebbe di qui e darebbe indirizzi che la
//  macchina non serve. E' un controllo del caricamento, non della sintassi, e
//  non c'e' ancora.
// ---------------------------------------------------------------------------

#define LOC_MAX_REGIONS 8
#define LOC_MAX_PLACE   8
#define LOC_MAX_PROC    8
#define LOC_NAME        32

// L'unita' di una regione: lo SPAZIO su cui sta, non una preferenza.
typedef enum { LOC_CODE, LOC_DATA } LocUnit;

typedef struct
{
  char    name[LOC_NAME];
  int     unit;            // LocUnit
  int64_t origin;          // istruzioni (CODE) o byte (DATA)
  int64_t length;          // idem
} LocRegion;

typedef struct
{
  char name[LOC_NAME];     // ".text", ".data", ...
  char region[LOC_NAME];   // il nome della regione: risolto da loc_region()
} LocPlace;

// La scheda o una CPU: la stessa forma, perche' un blocco PROCESSOR e' una
// scheda parziale. 'name' e' vuoto per la scheda.
typedef struct
{
  char      name[LOC_NAME];
  LocRegion regions[LOC_MAX_REGIONS];
  int       region_count;
  LocPlace  places[LOC_MAX_PLACE];
  int       place_count;
} LocScope;

typedef struct
{
  LocScope board;
  LocScope proc[LOC_MAX_PROC];
  int      proc_count;
} Locator;

// Il file di collocazione INCORPORATO, scritto nella sua stessa lingua e letto
// dallo stesso parser: e' il layout di sempre (codice da 0, dati da
// NULL_GUARD), e serve perche' `ld` senza -T continui a funzionare senza che
// quel layout sia dichiarato due volte, una in C e una in un file. GNU ld ha
// un default script per lo stesso motivo, e lo stampa con --verbose.
const char* loc_builtin(void);

// Legge un file di collocazione (o il testo incorporato, con loc_parse).
int loc_read(const char* path, Locator* loc, char* err, size_t errsz);
int loc_parse(const char* text, const char* origin, Locator* loc,
              char* err, size_t errsz);

// La scheda piu' cio' che quella CPU cambia. 'cpu' NULL o "" = solo la scheda.
// Qui avvengono i controlli che vogliono la vista intera: che una collocazione
// nomini una regione che esiste, e che due regioni dello stesso spazio non si
// sovrappongano.
int loc_resolve(const Locator* loc, const char* cpu, LocScope* out,
                char* err, size_t errsz);

const LocRegion* loc_region(const LocScope* s, const char* name);
// La regione in cui finisce una sezione, NULL se la sezione non e' collocata.
const LocRegion* loc_section_region(const LocScope* s, const char* section);

#endif // LOCATOR_H
