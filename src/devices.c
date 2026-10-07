// ---------------------------------------------------------------------------
//  devices.c — le periferiche (29/09/2026, la ristrutturazione pura)
//
//  Il codice e i commenti vengono da vcpu.c, dove stavano fino al 29/09, e il
//  comportamento e' lo stesso: cambia da DOVE arriva lo stato (la struttura
//  del device invece della CPU) e da dove arriva il TEMPO (un argomento, in
//  tick del clock master, invece di cpu->cycles). Il contratto e' in cima a
//  devices.h.
// ---------------------------------------------------------------------------
#include "machine.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
//  Il marcatore: annota "all'istante t il canale C prende il valore V".
//
//  Il timbro di CHI girava lo mette qui la macchina e non il programma, ed e'
//  la meta' che rende il modello a due assi: la CATEGORIA la dichiara chi
//  scrive il tag, il PROPRIETARIO lo sa solo il sistema. E' anche cio' che
//  permette a del codice CONDIVISO -- una mailbox, un mutex -- di marcarsi
//  senza sapere per conto di chi sta girando. Il timbro e' della CPU che
//  tocca il marcatore, e arriva come argomento.
//
//  Oltre il tetto non si annota piu' e si CONTA quanto si e' perso: una
//  finestra mancante che non si sappia mancante e' peggio di nessuna misura.
// ---------------------------------------------------------------------------
void marker_mark(VMarker* mk, const VCpu* timbro, uint64_t t, int canale, int32_t valore)
{
  if (!mk->on) return;
  if (mk->len >= MARCHE_MAX) { mk->lost += 1; return; }
  Marca* m = &mk->marche[mk->len++];
  m->cycle   = t;
  m->canale  = canale;
  m->valore  = valore;
  m->current = timbro->mark_current;
  // Il dato armato sul porto: la marca se lo porta via e il canale torna
  // disarmato, cosi' un armamento serve UNA marca e non resta appiccicato a
  // quelle dopo.
  m->dato    = mk->pend[canale];
  m->ha_dato = mk->armed[canale];
  m->in_trap = timbro->trap_depth;
  mk->pend[canale]  = 0;
  mk->armed[canale] = 0;
}

int marker_is_channel(int64_t addr)
{
  return addr >= MARK_BASE && addr < MARK_BASE + MARK_CHANNELS * 4;
}

static int is_marca_dato(int64_t addr)
{
  return addr >= MARKD_BASE && addr < MARKD_BASE + MARK_CHANNELS * 4;
}

static int is_marca_nome(int64_t addr)
{
  return addr >= MARKN_BASE && addr < MARKN_BASE + MARK_CHANNELS * 4;
}

// "Sul canale C, il valore che stiamo guardando si chiama <id>."
static void marker_name(VMarker* mk, const VCpu* timbro, int canale, int32_t id)
{
  int32_t valore;
  if (canale == MARK_EXEC)
    valore = timbro->mark_current;   // "chiunque io sia adesso"
  else if (mk->armed[canale])
    valore = mk->pend[canale];       // armato sul porto dati: e' quello
  else
  {
    // Nominare un valore senza aver detto QUALE e' un errore, non un caso da
    // indovinare: senza il porto armato si nominerebbe uno zero qualunque.
    fprintf(stderr, "runtime error: nome sul canale %d senza un valore armato "
                    "sul porto dati\n", canale);
    return;
  }
  if (mk->names_len >= MARK_NAMES_MAX) { mk->names_lost += 1; return; }
  // Un valore gia' nominato si RI-nomina: l'ultima vince, ed e' l'unica regola
  // che non richieda di ricordarsi se l'avevi gia' fatto.
  for (int i = 0; i < mk->names_len; ++i)
    if (mk->names[i].canale == canale && mk->names[i].valore == valore)
    { mk->names[i].id = id; return; }
  MarcaNome* n = &mk->names[mk->names_len++];
  n->canale = canale; n->valore = valore; n->id = id;
}

// I tre porti del marcatore: i canali, i dati, i nomi.
int marker_store(VMarker* mk, const VCpu* timbro, uint64_t t, int64_t addr, int32_t value)
{
  if (marker_is_channel(addr))
  {
    int canale = (int) ((addr - MARK_BASE) / 4);
    // I primi MARK_RISERVATI canali li scrive la MACCHINA (esecuzione, tasto,
    // timer). Che un programma ci scriva non e' uno stato da gestire: e' un
    // errore di costruzione, e va detto. La soglia invece dell'elenco perche'
    // l'elenco si dimentica: il terzo canale e' arrivato il 15/09/2026 e questi
    // guard erano tre, in tre punti diversi del file.
    if (canale < MARK_RISERVATI)
      fprintf(stderr, "runtime error: il canale %d e' riservato alla macchina\n",
              canale);
    else
      marker_mark(mk, timbro, t, canale, value);
    return 1;
  }
  if (is_marca_nome(addr))
  {
    marker_name(mk, timbro, (int) ((addr - MARKN_BASE) / 4), value);
    return 1;
  }
  if (is_marca_dato(addr))
  {
    // ARMA il porto dati del canale: la prossima marca su QUEL canale se lo
    // porta via. Non emette niente da solo -- se nessuna marca segue, il dato
    // resta li' e il lettore lo dice (un armamento che nessuno consuma e' un
    // tag scritto a meta').
    int canale = (int) ((addr - MARKD_BASE) / 4);
    if (canale < MARK_RISERVATI)
      fprintf(stderr, "runtime error: il canale %d e' riservato alla macchina\n",
              canale);
    else
    {
      mk->pend[canale]  = value;
      mk->armed[canale] = 1;
    }
    return 1;
  }
  return 0;
}

// ---------------------------------------------------------------------------
//  La tastiera
// ---------------------------------------------------------------------------

// Leggere KBD_DATA CONSUMA il carattere: e' l'effetto collaterale che la
// memoria non ha, e il protocollo sta tutto li'. Chi legge lo status e poi il
// dato non ha bisogno di nessun altro handshake, e un secondo carattere
// arrivato nel frattempo si vede in KBD_OVERRUN invece di sparire in silenzio.
int kbd_load(VKbd* k, int64_t addr, int32_t* out)
{
  if (addr == KBD_STATUS)
  {
    *out = (k->ready ? KBD_READY : 0) | (k->overrun ? KBD_OVERRUN : 0);
    return 1;
  }
  if (addr == KBD_CTRL)
  {
    *out = k->ie ? KBD_IE : 0;
    return 1;
  }
  if (addr == KBD_DATA)
  {
    *out = k->data;
    k->ready   = 0;
    k->overrun = 0;
    return 1;
  }
  return 0;
}

// KBD_CTRL arma l'interrupt della tastiera (14/09/2026, §3.67).
int kbd_store(VKbd* k, int64_t addr, int32_t value)
{
  if (addr == KBD_CTRL)
  {
    k->ie = (value & KBD_IE) ? 1 : 0;
    return 1;
  }
  return 0;
}

// L'alimentatore deterministico, chiamato a ogni confine d'istruzione: il
// device vive nel tempo SIMULATO, quindi una traccia rigiocata da' sempre gli
// stessi numeri. Piu' eventi possono maturare nello stesso ciclo, e il
// risultato e' un overrun -- che e' esattamente cio' che farebbe una UART.
void kbd_advance(VKbd* k, uint64_t t, VMarker* mk, const VCpu* timbro)
{
  while (k->trace_pos < k->trace_len && t >= k->trace[k->trace_pos].cycle)
  {
    if (k->ready) k->overrun = 1;
    k->data  = k->trace[k->trace_pos].ch;
    k->ready = 1;
    k->trace_pos += 1;
    // MARK_KEY: l'istante in cui il mondo ha bussato. Il programma non puo'
    // marcarlo -- sa solo quando se n'e' accorto -- e la differenza fra i due
    // E' il ritardo del polling, cioe' una delle cose da misurare.
    marker_mark(mk, timbro, t, MARK_KEY, k->data);
  }
}

// A LIVELLO: la condizione e' ready, che resta alzato finche' qualcuno non
// legge KBD_DATA. Quindi un carattere arrivato con IE=0 non si perde.
int kbd_irq(const VKbd* k)
{
  return k->ie && k->ready;
}

int kbd_set_trace(VKbd* k, const char* spec, char* err, size_t errsz)
{
  k->trace_len = 0;
  k->trace_pos = 0;

  const char* p = spec;
  uint64_t last = 0;
  while (*p)
  {
    char* end = NULL;
    unsigned long long cyc = strtoull(p, &end, 0);
    if (end == p || *end != ':')
    {
      snprintf(err, errsz, "traccia tastiera: atteso <ciclo>:<carattere> in \"%s\"", p);
      return -1;
    }
    if ((uint64_t) cyc < last)
    {
      snprintf(err, errsz, "traccia tastiera: i cicli devono essere non decrescenti (%llu dopo %llu)",
               cyc, (unsigned long long) last);
      return -1;
    }
    if (!end[1])
    {
      snprintf(err, errsz, "traccia tastiera: manca il carattere dopo il ciclo %llu", cyc);
      return -1;
    }
    if (k->trace_len >= KBD_TRACE_MAX)
    {
      snprintf(err, errsz, "traccia tastiera: troppi eventi (max %d)", KBD_TRACE_MAX);
      return -1;
    }

    k->trace[k->trace_len].cycle = (uint64_t) cyc;
    k->trace[k->trace_len].ch    = (unsigned char) end[1];
    k->trace_len += 1;
    last = (uint64_t) cyc;

    p = end + 2;
    if (*p == ',') p += 1;
    else if (*p)
    {
      snprintf(err, errsz, "traccia tastiera: atteso ',' fra due eventi, trovato \"%s\"", p);
      return -1;
    }
  }
  return 0;
}

// ---------------------------------------------------------------------------
//  I comparatori (15/09/2026, §3.72)
// ---------------------------------------------------------------------------
int cmp_load(const VCmp* c, int64_t addr, int32_t* out)
{
  if (addr == CMP_CTRL)
  {
    // Armamento E base: cosi' un read-modify-write che arma un canale non
    // azzera la base di un altro. (Che il read-modify-write sia di per se' una
    // corsa fra la trap e la `sw` resta vero, ed e' il buco dichiarato in
    // §3.74 dell'handoff: qui si evita almeno di perdere un bit per costruzione.)
    *out = (int32_t) (c->armed | (c->cycles << CMP_CYCLES_SHIFT));
    return 1;
  }
  if (addr >= CMP_BASE && addr < CMP_BASE + CMP_CHANNELS * 4)
  {
    *out = c->val[(addr - CMP_BASE) / 4];
    return 1;
  }
  return 0;
}

int cmp_store(VCmp* c, int64_t addr, int32_t value)
{
  // Solo i bit dei canali che esistono: un bit oltre CMP_CHANNELS armerebbe un
  // canale che nessuno confronta, cioe' un armamento che non succede mai -- e
  // il silenzio e' il modo peggiore di dirlo.
  if (addr == CMP_CTRL)
  {
    uint32_t armabili = (1u << CMP_CHANNELS) - 1u;
    uint32_t basi     = armabili << CMP_CYCLES_SHIFT;   // la base di ogni canale
    if ((uint32_t) value & ~(armabili | basi))
      fprintf(stderr, "runtime error: CMP_CTRL, nessun canale oltre il %d\n",
              CMP_CHANNELS - 1);
    c->armed  = (uint32_t) value & armabili;
    c->cycles = ((uint32_t) value >> CMP_CYCLES_SHIFT) & armabili;
    return 1;
  }

  // Scrivere la scadenza ABBASSA la richiesta di quel canale, perche' la
  // richiesta E' il confronto: non c'e' un flag da azzerare. Riarmare costa
  // quindi una `sw` sola, ed e' il percorso caldo del tickless.
  if (addr >= CMP_BASE && addr < CMP_BASE + CMP_CHANNELS * 4)
  {
    c->val[(addr - CMP_BASE) / 4] = value;
    return 1;
  }
  return 0;
}

// LA DIFFERENZA, MAI L'ORDINE. `(int32_t)(adesso - scadenza) >= 0` in aritmetica
// wrappante: una scadenza gia' passata spara SUBITO invece di non sparare
// mai, che e' cio' che succederebbe con `==`. I canali in ordine: vince il
// primo, che e' il posto del foreground.
//
// E "adesso" dipende dal CANALE: millisecondi o cicli (CMP_CYCLES). Il wrap e'
// diverso -- 49 giorni e 43 secondi -- ma la formula e' la stessa, ed e' per
// questo che la seconda base non costa un secondo percorso.
int cmp_pending(const VCmp* c, int32_t ms, int32_t cycles)
{
  for (int n = 0; n < CMP_CHANNELS; n++)
  {
    if (!(c->armed & CMP_ARM(n))) continue;
    int32_t adesso = (c->cycles & CMP_ARM(n)) ? cycles : ms;
    if ((int32_t) ((uint32_t) adesso - (uint32_t) c->val[n]) >= 0) return n;
  }
  return -1;
}

// ---------------------------------------------------------------------------
//  L'ADC (28/09/2026)
// ---------------------------------------------------------------------------

// Leggere lo stato ABBASSA l'overrun, come leggere KBD_DATA abbassa quello
// della tastiera: chi l'ha visto una volta l'ha visto.
int adc_load(VAdc* a, int64_t addr, int32_t* out)
{
  if (addr == ADC_STATUS)
  {
    uint32_t s = (a->done << ADC_DONE_SHIFT)
               | (a->overrun ? ADC_OVERRUN : 0u)
               | (a->busy ? ADC_BUSY : 0u);
    a->overrun = 0;
    *out = (int32_t) s;
    return 1;
  }
  if (addr == ADC_ADDR)   { *out = a->addr;   return 1; }
  if (addr == ADC_COUNT)  { *out = a->count;  return 1; }
  if (addr == ADC_PERIOD) { *out = a->period; return 1; }
  return 0;
}

// Tre registri di configurazione, e l'avvio. Il primo campione e' preso un
// periodo DOPO l'istante dell'avvio, e il periodo e' in tick PERIFERICHE.
int adc_store(VAdc* a, int64_t addr, int32_t value, uint64_t t, const VClock* ck)
{
  if (addr == ADC_ADDR)   { a->addr   = value; return 1; }
  if (addr == ADC_COUNT)  { a->count  = value; return 1; }
  if (addr == ADC_PERIOD) { a->period = value; return 1; }
  if (addr != ADC_CTRL) return 0;

  if (!(value & ADC_START)) return 1;
  // Mentre acquisisce l'avvio e' IGNORATO e si alza l'overrun: e' un errore
  // di tempo della time line, e lo deve vedere il programma.
  if (a->busy) { a->overrun = 1; return 1; }
  // Una configurazione impossibile invece e' un errore di COSTRUZIONE, come
  // un canale del comparatore che non esiste: si dice, e non si parte.
  //
  // LA DMA SCRIVE SOLO NELLA RAM CONDIVISA (decisione dell'utente, 29/09/2026):
  // nessuna periferica scrive nella memoria privata di una CPU. Un blocco che
  // non ci sta tutto dentro e' una configurazione impossibile.
  int64_t fine = (int64_t) a->addr + 8 * (int64_t) a->count;
  int in_shared = a->addr >= SHARED_BASE && fine <= SHARED_BASE + SHARED_SIZE;
  if (a->count <= 0 || a->period <= 0 || !in_shared)
  {
    fprintf(stderr, "runtime error: ADC, configurazione impossibile "
                    "(addr 0x%x, count %d, period %d)\n",
            (unsigned) a->addr, a->count, a->period);
    return 1;
  }
  a->cur_addr   = a->addr;
  a->cur_count  = a->count;
  a->cur_period = a->period;
  a->i    = 0;
  a->next = t + (uint64_t) a->period * ck->div_periph;
  a->busy = 1;
  return 1;
}

// Un canale dell'ADC: arrotonda e SATURA a 12 bit. Fuori scala resta
// all'estremo, non riparte dall'altro -- il ferro fa cosi', e un ritorno
// dall'estremo opposto sarebbe un segnale che non esiste.
static int32_t adc_quantizza(double x)
{
  double r = floor(x + 0.5);
  if (r > ADC_MAX) return ADC_MAX;
  if (r < ADC_MIN) return ADC_MIN;
  return (int32_t) r;
}

// L'ADC al confine d'istruzione, come la tastiera: prende i campioni maturati
// e li scrive in RAM -- la DMA, che non ruba cicli. Il tempo del campione e'
// l'istante in cui e' PRESO (next), non quello in cui arriva in memoria -- il
// segnale non sa niente di quanto era lunga l'istruzione in corso.
//
// Un campione preso col TX acceso, o entro la commutazione T/R, esce
// SATURATO su tutti e due i canali (§3.83): il ricevitore e' abbagliato, e il
// segnale non c'entra.
void adc_advance(VAdc* a, uint64_t t, const VClock* ck, uint8_t* shared, VTx* tx)
{
  while (a->busy && t >= a->next)
  {
    int32_t iv, qv;
    if (tx_blinds(tx, a->next))
    {
      iv = qv = ADC_MAX;
      tx->blind = 1;
    }
    else if (a->eco_on)
    {
      // Il mondo esterno ha gia' risposto per tutto il blocco: qui si
      // converte e basta, perche' arrotondare e saturare e' dell'ADC.
      iv = adc_quantizza(a->eco[2 * a->i]);
      qv = adc_quantizza(a->eco[2 * a->i + 1]);
    }
    else
    {
      double ts = (double) a->next / (double) ck->hz;
      double fi = 2.0 * M_PI * a->freq * ts;
      iv = adc_quantizza(a->amp * cos(fi));
      qv = adc_quantizza(a->amp * sin(fi));
    }

    int64_t p = a->cur_addr + 8 * (int64_t) a->i;
    uint8_t* dst = &shared[p - SHARED_BASE];
    memcpy(dst,     &iv, sizeof iv);
    memcpy(dst + 4, &qv, sizeof qv);

    a->i    += 1;
    a->next += (uint64_t) a->cur_period * ck->div_periph;
    if (a->i == a->cur_count)
    {
      a->busy  = 0;
      a->done += 1;
    }
  }
}

int adc_set_signal(VAdc* a, const char* spec, char* err, size_t errsz)
{
  char* end = NULL;
  double f = strtod(spec, &end);
  if (end == spec || *end != ',')
  {
    snprintf(err, errsz, "segnale ADC: atteso <frequenza_Hz>,<ampiezza> in \"%s\"", spec);
    return -1;
  }
  const char* p = end + 1;
  double amp = strtod(p, &end);
  if (end == p || *end != '\0')
  {
    snprintf(err, errsz, "segnale ADC: ampiezza non valida in \"%s\"", spec);
    return -1;
  }
  a->freq = f;
  a->amp  = amp;
  return 0;
}

// ---------------------------------------------------------------------------
//  Il trasmettitore (07/10/2026, §3.83). Il perche' e' in devices.h, accanto
//  ai registri.
// ---------------------------------------------------------------------------
static int tx_on(const VTx* x, uint64_t t)
{
  return x->n > 0 && t < x->end[0];
}

// Leggere lo stato abbassa BLIND e OVERRUN: chi l'ha visto una volta l'ha
// visto, come ADC_STATUS. TX_ON invece e' il tempo, e non si abbassa.
int tx_load(VTx* x, int64_t addr, uint64_t t, int32_t* out)
{
  if (addr == TX_STATUS)
  {
    *out = (tx_on(x, t) ? TX_ON : 0)
         | (x->blind ? TX_BLIND : 0)
         | (x->overrun ? TX_OVERRUN : 0);
    x->blind   = 0;
    x->overrun = 0;
    return 1;
  }
  if (addr == TX_LEN) { *out = x->len; return 1; }
  return 0;
}

int tx_store(VTx* x, int64_t addr, int32_t value, uint64_t t, const VClock* ck)
{
  if (addr == TX_LEN) { x->len = value; return 1; }
  if (addr != TX_CTRL) return 0;

  if (!(value & TX_START)) return 1;
  if (tx_on(x, t)) { x->overrun = 1; return 1; }
  if (x->len <= 0)
  {
    fprintf(stderr, "runtime error: TX, durata impossibile (len %d)\n", x->len);
    return 1;
  }
  x->start[1] = x->start[0];
  x->end[1]   = x->end[0];
  x->start[0] = t;
  x->end[0]   = t + (uint64_t) x->len * ck->div_periph;
  x->neg      = (value & TX_NEG) ? 1 : 0;
  x->n       += 1;
  return 1;
}

// Dal primo istante del TX alla fine della commutazione, esclusa: alla fine
// esatta il ricevitore e' pronto.
int tx_blinds(const VTx* x, uint64_t ts)
{
  for (uint32_t k = 0; k < 2 && k < x->n; ++k)
    if (ts >= x->start[k] && ts < x->end[k] + x->tsw) return 1;
  return 0;
}

// ---------------------------------------------------------------------------
//  La mailbox (29/09/2026, §3.79). Il perche' e il protocollo sono in
//  devices.h, accanto ai registri.
// ---------------------------------------------------------------------------
int mbox_load(VMbox* mine, const VMbox* peer, int64_t addr, int32_t* out)
{
  if (addr == MBOX_STATUS)
  {
    uint32_t s = ((uint32_t) mine->count & MBOX_COUNT_MASK)
               | (peer->count == MBOX_DEPTH ? MBOX_PEER_FULL : 0u)
               | (mine->overrun ? MBOX_OVERRUN : 0u);
    mine->overrun = 0;          // chi l'ha visto una volta l'ha visto
    *out = (int32_t) s;
    return 1;
  }
  if (addr == MBOX_RECV)
  {
    if (mine->count == 0)
    {
      fprintf(stderr, "runtime error: MBOX_RECV da una mailbox vuota\n");
      *out = 0;
      return 1;
    }
    *out = (int32_t) mine->fifo[mine->head];
    mine->head   = (mine->head + 1) % MBOX_DEPTH;
    mine->count -= 1;
    return 1;
  }
  if (addr == MBOX_CTRL)
  {
    *out = mine->ie ? MBOX_IE : 0;
    return 1;
  }
  return 0;
}

int mbox_store(VMbox* mine, VMbox* peer, int64_t addr, int32_t value)
{
  if (addr == MBOX_SEND)
  {
    if (peer->count == MBOX_DEPTH) { mine->overrun = 1; return 1; }
    peer->fifo[(peer->head + peer->count) % MBOX_DEPTH] = (uint32_t) value;
    peer->count += 1;
    return 1;
  }
  if (addr == MBOX_CTRL)
  {
    mine->ie = (value & MBOX_IE) ? 1 : 0;
    return 1;
  }
  return 0;
}

// A LIVELLO: la propria FIFO non vuota, e l'interrupt armato.
int mbox_irq(const VMbox* mine)
{
  return mine->ie && mine->count > 0;
}

// ---------------------------------------------------------------------------
//  Lo spinlock hardware (29/09/2026, §3.79)
// ---------------------------------------------------------------------------
static int hwlock_index(int64_t addr)
{
  if (addr < HWLOCK_BASE || addr >= HWLOCK_BASE + HWLOCK_COUNT * 4) return -1;
  return (int) ((addr - HWLOCK_BASE) / 4);
}

// LEGGERE E' IL TEST-AND-SET: libero -> preso da chi legge, e torna 0.
//
// NON RICORSIVO: chi lo tiene e lo richiede lo trova occupato, e se gira ad
// aspettarlo aspetta se stesso per sempre. La lettura resta 1, ma l'errore di
// costruzione si DICE, una volta per presa: senza, il simulatore resterebbe
// appeso in silenzio (osservazione dell'utente, 29/09/2026).
int hwlock_load(VHwLock* h, int id, int64_t addr, int32_t* out)
{
  int n = hwlock_index(addr);
  if (n < 0) return 0;
  if (h->owner[n] == 0)
  {
    h->owner[n] = id + 1;
    h->detto[n] = 0;
    *out = 0;
  }
  else
  {
    if (h->owner[n] == id + 1 && !h->detto[n])
    {
      fprintf(stderr, "runtime error: spinlock %d richiesto dalla CPU %d, che lo tiene "
                      "gia' (non e' ricorsivo)\n", n, id);
      h->detto[n] = 1;
    }
    *out = 1;
  }
  return 1;
}

// SCRIVERE 0 LO RILASCIA, e solo chi lo tiene puo' farlo: rilasciare un lock
// altrui, o scrivere altro che 0, e' un errore di costruzione e va detto.
int hwlock_store(VHwLock* h, int id, int64_t addr, int32_t value)
{
  int n = hwlock_index(addr);
  if (n < 0) return 0;
  if (value != 0)
    fprintf(stderr, "runtime error: spinlock %d, si rilascia scrivendo 0 (scritto %d)\n",
            n, value);
  else if (h->owner[n] != id + 1)
    fprintf(stderr, "runtime error: spinlock %d rilasciato dalla CPU %d, che non lo tiene\n",
            n, id);
  else
    h->owner[n] = 0;
  return 1;
}

// ---------------------------------------------------------------------------
//  Il distributore delle interruzioni (29/09/2026, §3.79): il perche' e' in
//  devices.h, accanto ai registri.
// ---------------------------------------------------------------------------
static int intd_index(int64_t addr)
{
  if (addr < INTD_TARGET || addr >= INTD_TARGET + INTD_LINES * 4) return -1;
  return (int) ((addr - INTD_TARGET) / 4);
}

int intd_load(const VIntd* d, int64_t addr, int32_t* out)
{
  int n = intd_index(addr);
  if (n < 0) return 0;
  *out = d->target[n];
  return 1;
}

int intd_store(VIntd* d, int ncpu, int64_t addr, int32_t value)
{
  int n = intd_index(addr);
  if (n < 0) return 0;
  if (value < 0 || value >= ncpu)
    fprintf(stderr, "runtime error: INTD, la linea %d non puo' andare alla CPU %d "
                    "(la scheda ne ha %d)\n", n, value, ncpu);
  else
    d->target[n] = value;
  return 1;
}
