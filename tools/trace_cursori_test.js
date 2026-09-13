// tools/trace_cursori_test.js — il NUCLEO PURO dei cursori, provato.
//
// Si esegue con tools/trace_check.sh, ed e' un test di ctest (trace_cursori_*).
// Gira dentro il DOM finto di trace_dom.js, dopo lo script della pagina: da li'
// vede NOTEVOLI, aggancia() e fraICursori() come le vede la pagina.
//
// --- PERCHE' ESISTE, E PERCHE' PROVA SOLO META' DELLA COSA ---
//
// I cursori sono fatti di due parti, e la divisione e' voluta:
//
//   il CALCOLO   gli istanti notevoli, l'aggancio al piu' vicino, cosa c'e'
//                fra due cursori. Niente pixel, niente eventi: provabile, ed
//                e' quello che questo file prova.
//   la COLLA     leggere un clientX e scrivere nel DOM. Venti righe, e non
//                provabili qui -- il DOM finto stubba addEventListener come
//                no-op e non ha layout.
//
// Se la colla sbaglia, sbaglia in modo VISIBILE: un cursore nel posto sbagliato,
// e chi guarda se ne accorge. Se sbagliasse il calcolo, sbaglierebbe un NUMERO,
// e nessuno se ne accorgerebbe. La divisione passa di li' apposta, ed e' la
// stessa di analizza()/report() in marks.py.
//
// Va eseguito su PIU' programmi: i difetti di questa pagina sono tutti della
// forma "funziona su quello per cui e' stata scritta" (§3.39).

// Prova del NUCLEO PURO dei cursori: niente pixel, niente eventi.
let ko = 0;
function ok(c, m) { print((c ? "  ok   " : "  FALLITO ") + m); if (!c) ko++; }

print("--- gli istanti notevoli ---");
ok(NOTEVOLI.length > 0, "ce ne sono (" + NOTEVOLI.length + ")");
ok(NOTEVOLI.every(Number.isInteger), "sono tutti INTERI: un ciclo non e' frazionario");
ok(NOTEVOLI.every((v,i,a) => i===0 || a[i-1] < v), "ordinati e senza doppioni");
ok(NOTEVOLI[0] === 0 && NOTEVOLI[NOTEVOLI.length-1] === D.fine, "vanno da 0 a fine");
ok(MARK.every(t => NOTEVOLI.includes(t)), "ogni tick e' un notevole");
ok(PRE.every(e => NOTEVOLI.includes(e.c)), "ogni preemption e' un notevole");
ok(D.fasce.every(f => NOTEVOLI.includes(f[0])), "ogni bordo di fascia e' un notevole");

print("--- l'aggancio ---");
ok(NOTEVOLI.every(v => aggancia(v) === v), "un notevole aggancia se stesso");
ok(aggancia(-1e9) === NOTEVOLI[0], "sotto il minimo -> il primo");
ok(aggancia(1e9) === NOTEVOLI[NOTEVOLI.length-1], "sopra il massimo -> l'ultimo");
ok(NOTEVOLI.includes(aggancia(12345.67)), "un valore qualunque cade SU un notevole");
// il piu' vicino e' davvero il piu' vicino: forza bruta contro ricerca binaria
let peggio = 0;
for (let x = 0; x <= D.fine; x += Math.max(1, Math.floor(D.fine/997))) {
  const a = aggancia(x);
  const b = NOTEVOLI.reduce((p,c) => Math.abs(c-x) < Math.abs(p-x) ? c : p, NOTEVOLI[0]);
  if (Math.abs(a-x) !== Math.abs(b-x)) peggio++;
}
ok(peggio === 0, "la ricerca binaria da' lo stesso della forza bruta su 997 punti");

print("--- la misura fra i cursori ---");
const tutto = fraICursori(0, D.fine);
ok(tutto.durata === D.fine, "da 0 a fine la durata e' tutta la corsa");
const somma = tutto.proprietari.reduce((s,[,c]) => s+c, 0);
ok(somma === D.fine, "i proprietari sommano ESATTAMENTE alla durata (" + somma + ")");
ok(tutto.preemption === PRE.length, "conta tutte le preemption (" + PRE.length + ")");
ok(fraICursori(D.fine, 0).durata === D.fine, "i cursori invertiti danno lo stesso");
ok(fraICursori(100, 100).durata === 0, "due cursori sullo stesso punto: zero");
// additivita': spezzare l'intervallo non crea ne' perde cicli
const m = aggancia(D.fine/2);
ok(fraICursori(0,m).durata + fraICursori(m,D.fine).durata === D.fine, "spezzando, i cicli tornano");
const s1 = fraICursori(0,m).proprietari.reduce((s,[,c])=>s+c,0);
const s2 = fraICursori(m,D.fine).proprietari.reduce((s,[,c])=>s+c,0);
ok(s1 + s2 === D.fine, "e tornano anche sommando i proprietari dei due pezzi");

print(ko === 0 ? "\nTUTTO VERDE" : "\n" + ko + " FALLITI");
