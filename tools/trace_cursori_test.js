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
ok(PRE.every(e => NOTEVOLI.includes(e.c)), "ogni istante letto e' un notevole");
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
ok(tutto.eventi === PRE.length, "conta tutti gli istanti letti (" + PRE.length + ")");
ok(fraICursori(D.fine, 0).durata === D.fine, "i cursori invertiti danno lo stesso");
ok(fraICursori(100, 100).durata === 0, "due cursori sullo stesso punto: zero");
// additivita': spezzare l'intervallo non crea ne' perde cicli
const m = aggancia(D.fine/2);
ok(fraICursori(0,m).durata + fraICursori(m,D.fine).durata === D.fine, "spezzando, i cicli tornano");
const s1 = fraICursori(0,m).proprietari.reduce((s,[,c])=>s+c,0);
const s2 = fraICursori(m,D.fine).proprietari.reduce((s,[,c])=>s+c,0);
ok(s1 + s2 === D.fine, "e tornano anche sommando i proprietari dei due pezzi");

print("--- gli eventi non si confondono fra loro ---");
// Fino al 13/09 il readout diceva "PREEMPTION" su qualunque evento: appena il
// marcatore ne ha avuto un secondo, una ricezione veniva etichettata come una
// preemption. Nome e colore devono venire dai DATI, uno per categoria.
ok(PRE.every(e => e.canale && e.marker), "ogni evento porta la sua categoria e il suo marker");
ok(PRE_CAT.length === new Set(PRE.map(e => e.canale)).size,
   "le categorie di evento sono quelle che compaiono (" + PRE_CAT.join(", ") + ")");
ok(PRE_CAT.every(c => PRE_COL[c]), "ognuna ha un colore");
ok(PRE.every(e => e.frase && e.frase.length), "ogni evento porta la frase del suo OWNER");
// La corsia su cui cade un evento dev'essere una che ESISTE nel diagramma, o
// il segno finirebbe in nessun posto -- silenziosamente.
ok(PRE.every(e => e.corsia), "ogni evento sa su quale corsia sta");
ok(PRE.every(e => ORDINE.some(o => o.k === e.corsia)),
   "e quella corsia esiste davvero fra quelle disegnate");
// La riga degli istanti c'e' se la corsia ne produce in TUTTA la corsa, non
// solo in vista: altrimenti zoomando il diagramma salterebbe sotto il puntatore.
{
  const conEventi = [...new Set(PRE.map(e => e.corsia))];
  ok(conEventi.every(k => ORDINE.some(o => o.k === k)),
     "le corsie che avranno una riga di istanti sono " +
     (conEventi.length ? conEventi.join(", ") : "(nessuna)"));
}
ok(new Set(PRE_CAT.map(c => PRE_COL[c])).size === PRE_CAT.length,
   "e due categorie non hanno lo stesso colore");

print("--- una traccia per MARKER, non per categoria ---");
// Oggi ogni categoria ha un marker solo, quindi sui dati veri non si
// distinguerebbe. La regola si prova su dati sintetici, che e' l'unico modo di
// vedere il caso che conta: PIU' marker sullo stesso canale devono dare PIU'
// righe, o le relazioni fra punti diversi non si leggono in verticale.
{
  const finti = [
    { corsia: "A", ch: 6, canale: "recv", marker: "uno",  c: 1 },
    { corsia: "A", ch: 6, canale: "recv", marker: "due",  c: 2 },
    { corsia: "A", ch: 6, canale: "recv", marker: "uno",  c: 3 },
    { corsia: "B", ch: 6, canale: "recv", marker: "uno",  c: 4 },
    { corsia: "A", ch: 5, canale: "pre",  marker: "tre",  c: 5 },
  ];
  const t = tracceDa(finti);
  ok(t.length === 4, "due marker sullo stesso canale danno DUE righe (" + t.length + " in tutto)");
  ok(t.find(x => x.corsia === "A" && x.marker === "uno").n === 2, "e ognuna conta le sue");
  ok(t.filter(x => x.corsia === "A").length === 3, "le righe di una corsia sono le sue");
  ok(t[0].ch <= t[t.length-1].ch, "ordinate per canale, quindi stabili fra due corse");
  ok(JSON.stringify(tracceDa(finti)) === JSON.stringify(t), "e rifarlo da' lo stesso");
}
ok(TRACCE.every(t => ORDINE.some(o => o.k === t.corsia)),
   "ogni traccia vera sta su una corsia che esiste (" + TRACCE.length + " tracce)");

print("--- i segmenti fra tre cursori ---");
// I tre punti si prendono dai NOTEVOLI e non da tre frazioni della corsa: su un
// programma corto -- multi e' 95 cicli con due soli istanti notevoli -- tre
// frazioni si agganciano tutte allo stesso punto e i segmenti degenerano. Il
// caso non e' esercitabile li', e va DETTO invece di fallire o di passare per
// caso.
if (NOTEVOLI.length < 3) {
  print("  saltato: servono almeno 3 istanti notevoli, ce ne sono " + NOTEVOLI.length);
} else {
  const n0 = NOTEVOLI[0],
        n1 = NOTEVOLI[Math.floor(NOTEVOLI.length / 2)],
        n2 = NOTEVOLI[NOTEVOLI.length - 1];
  const tre = segmentiFra({ A: n0, B: n1, C: n2 });
  ok(tre.length === 2, "tre cursori danno DUE segmenti");
  ok(tre[0].da === "A" && tre[0].a === "B" && tre[1].da === "B" && tre[1].a === "C",
     "e sono A..B e B..C");
  ok(tre[0].m.durata + tre[1].m.durata === fraICursori(n0, n2).durata,
     "le due meta' sommano al totale A..C");
  // piantati in disordine: il segmento resta fra chi e' ADIACENTE NEL TEMPO
  const dis = segmentiFra({ A: n0, B: n2, C: n1 });
  ok(dis[0].da === "A" && dis[0].a === "C" && dis[1].da === "C" && dis[1].a === "B",
     "con C prima di B i segmenti seguono il TEMPO, non il nome");
  ok(dis.every(sg => sg.x0 <= sg.x1), "e nessun segmento e' rovesciato");
  ok(segmentiFra({ A: n0, B: null, C: null }).length === 0, "un cursore solo: nessun segmento");
  ok(segmentiFra({ A: n0, B: n1, C: null }).length === 1, "due cursori: un segmento");
  ok(segmentoDi(n0, tre) === 0, "il primo cursore cade nel primo segmento");
  ok(segmentoDi(n1, tre) >= 0, "un punto interno cade in un segmento");
  ok(segmentoDi(n0 - 1, tre) === -1, "e fuori da tutti non ne cade in nessuno");
}

print("--- la finestra dello zoom ---");
const dentro = ([a,z]) => a >= 0 && z <= D.fine && z > a;
ok(dentro(limitaFinestra(0, D.fine)), "tutta la corsa e' una finestra valida");
ok(dentro(limitaFinestra(-1e9, 1e9)), "fuori scala da entrambe le parti: rientra");
ok(dentro(limitaFinestra(D.fine - 5, D.fine + 500)), "a cavallo della fine: rientra");
ok(limitaFinestra(100, 101)[1] - limitaFinestra(100, 101)[0] >= Math.min(MIN_SPAN, D.fine),
   "non si zooma sotto il fondo scala (" + MIN_SPAN + " cicli)");
ok(limitaFinestra(0, 1e9)[1] - limitaFinestra(0, 1e9)[0] === D.fine,
   "e non si zooma oltre la corsa intera");
ok(limitaFinestra(0, D.fine).every(Number.isInteger), "gli estremi sono interi");

// L'ancora non scappa: e' la proprieta' che rende naturale la rotella.
{
  let scappate = 0;
  for (const q of [0.1, 0.25, 0.5, 0.75, 0.9]) {
    const anc = Math.round(D.fine * q);
    const [a, z] = nuovaFinestra(0, D.fine, 0.5, anc);
    if (z > a && a > 0 && z < D.fine) {          // solo dove non ha tagliato ai bordi
      const q2 = (anc - a) / (z - a);
      if (Math.abs(q2 - q) > 0.02) scappate++;
    }
  }
  ok(scappate === 0, "zoomando, il ciclo sotto il puntatore resta dov'e'");
}
{
  // avvicinare e poi allontanare dello stesso fattore torna al punto di partenza
  const anc = Math.round(D.fine / 3);
  const [a1, z1] = nuovaFinestra(0, D.fine, 0.5, anc);
  const [a2, z2] = nuovaFinestra(a1, z1, 2, anc);
  ok(Math.abs((z2 - a2) - D.fine) <= 2, "avvicinare e allontanare torna alla corsa intera");
}
{
  const a = aggancia(D.fine * 0.3), b = aggancia(D.fine * 0.4);
  const [f0, f1] = finestraFra(a, b);
  ok(f0 <= a && f1 >= b, "la finestra fra i cursori li CONTIENE entrambi");
  ok(dentro([f0, f1]), "ed e' una finestra valida");
  ok(finestraFra(b, a)[0] === f0, "i cursori invertiti danno la stessa finestra");
}

print(ko === 0 ? "\nTUTTO VERDE" : "\n" + ko + " FALLITI");
