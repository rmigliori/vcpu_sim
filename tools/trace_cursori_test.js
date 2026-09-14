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

print("--- CHE COSA E' GIRATO FRA I CURSORI ---");
// Chiesto dall'utente il 14/09/2026 (§3.64): fra due cursori, i NOMI di cio'
// che ha girato. Il dato e' la linea temporale delle etichette fini, che
// trace.py calcolava gia' a ogni corsa e buttava via tenendone due cablate.
//
// L'asserzione che conta e' la stessa dei proprietari: i cicli devono sommare
// ESATTAMENTE alla durata. Un profilo che non torna non e' un profilo.
{
  ok(!!D.proc && D.proc.nomi.length > 0,
     "la linea temporale c'e' (" + (D.proc ? D.proc.t.length : 0) + " tratti, " +
     (D.proc ? D.proc.nomi.length : 0) + " nomi)");
  ok(D.proc.t.every((p, i, a) => i === 0 || a[i-1][0] < p[0]),
     "i tratti sono ordinati e non si sovrappongono");
  ok(D.proc.t.every(p => p[1] >= 0 && p[1] < D.proc.nomi.length),
     "ogni tratto punta a un nome che esiste");
  ok(D.proc.t[0][0] === 0, "il primo tratto comincia a zero: non c'e' tempo orfano");

  const tutto = routineFra(0, D.fine);
  const somma = tutto.reduce((s, [, c]) => s + c, 0);
  ok(somma === D.fine,
     "su tutta la corsa i cicli sommano ESATTAMENTE alla durata (" + somma + ")");
  ok(tutto.every((r, i, a) => i === 0 || a[i-1][1] >= r[1]),
     "e l'elenco e' ordinato per cicli, il piu' caro per primo");

  // Additivita': spezzare l'intervallo non crea ne' perde cicli. E' lo stesso
  // controllo dei proprietari, sulla stessa proprieta'.
  const m2 = Math.floor(D.fine / 2);
  const s1 = routineFra(0, m2).reduce((s, [, c]) => s + c, 0);
  const s2 = routineFra(m2, D.fine).reduce((s, [, c]) => s + c, 0);
  ok(s1 + s2 === D.fine, "spezzando a meta', i cicli tornano (" + s1 + " + " + s2 + ")");

  // Un intervallo dentro UN SOLO tratto: il caso che una ricerca binaria
  // sbagliata sbaglia in silenzio, restituendo il tratto successivo o niente.
  if (D.proc.t.length > 2) {
    const a = D.proc.t[1][0], z = D.proc.t[2][0];
    if (z - a > 2) {
      const dentro = routineFra(a + 1, z - 1);
      ok(dentro.length === 1 && dentro[0][1] === (z - 1) - (a + 1),
         "un intervallo interno a un solo tratto da' quel tratto e basta");
    }
  }
  ok(routineFra(100, 100).length === 0, "due cursori sullo stesso punto: niente");
  ok(routineFra(D.fine, D.fine + 500).length === 0, "e oltre la fine: niente");

  // fraICursori la espone, o il righello non la vedrebbe.
  ok(Array.isArray(fraICursori(0, D.fine).routine),
     "e fraICursori la porta con se'");
}

print("--- il contesto e' contato per INTERO ---");
// Erano due nomi, ctx_save e ctx_restore, e la pagina ne faceva "il costo del
// contesto". Ma quelli sono i PREAMBOLI: il lavoro sta in cs_clean e cr_scalar.
// Su test_tmgr_marks la pagina diceva 2633 dove la verita' e' il doppio.
{
  const nomi = Object.keys(D.ctx);
  ok(nomi.length === 4, "quattro voci, non due (" + nomi.join(", ") + ")");
  ok(typeof D.nsw === "number" && D.nsw >= 0,
     "le commutazioni si contano invece di dividere un totale (" + D.nsw + ")");
  // Se il programma commuta, il contesto deve costare qualcosa: il contrario
  // vorrebbe dire che uno di quei quattro nomi non esiste piu'.
  if (D.nsw > 0)
    ok(Object.values(D.ctx).reduce((s, v) => s + v, 0) > 0,
       "e con delle commutazioni il contesto costa cicli veri");
  else
    print("  saltato: questo programma non commuta");
}

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

print("--- LE RIGHE NON SPARISCONO ZOOMANDO ---");
// Il difetto, segnalato dall'utente il 14/09/2026: la corsia di un proprietario
// si disegnava solo se aveva fasce DENTRO la vista, quindi mano a mano che si
// zoomava le righe sparivano una per una e il diagramma si riassestava sotto il
// puntatore -- proprio mentre stai mirando un istante.
//
// La regola era gia' scritta venti righe piu' sotto, per le sotto-righe degli
// istanti, e diceva l'opposto: le righe ci sono se la corsia produce IN TUTTA
// LA CORSA. Qui si prova che ora vale anche per le corsie dei proprietari.
//
// E' un'asserzione sul CONTEGGIO, non sui pixel: il DOM finto non ha layout, ma
// l'HTML che disegna() produce lo vede tutto, ed e' li' che la riga spariva.
{
  const corsieIn = (b0, b1) => {
    const ln = document.getElementById("_prova_lanes");
    disegna(document.getElementById("_prova_axis"), ln, b0, b1, t => String(t));
    return (ln.innerHTML.match(/class="lane[ "]/g) || []).length;
  };
  // Una per proprietario, una per traccia, una per CATEGORIA di finestre e una
  // per gli eventi puntuali della macchina: sono tutti elenchi di TUTTA la
  // corsa, quindi il numero non dipende dalla vista.
  const nCat  = (M && M.finestre.length) ? M.categorie.length : 0;
  const nPunt = (M && M.puntuali.length) ? 1 : 0;
  const attese = ORDINE.length + TRACCE.length + nCat + nPunt;
  ok(corsieIn(0, D.fine) === attese,
     "a tutta la corsa le righe sono " + attese +
     " (" + ORDINE.length + " proprietari + " + TRACCE.length + " istanti + " +
     nCat + " finestre + " + nPunt + " macchina)");

  // Al FONDO SCALA, cioe' dove il difetto si vedeva peggio: una finestra di
  // MIN_SPAN cicli contiene quasi sempre un proprietario solo.
  let variate = 0, minimo = attese;
  for (const q of [0, 0.15, 0.3, 0.45, 0.6, 0.75, 0.9, 1]) {
    const [a, z] = limitaFinestra(D.fine * q, D.fine * q + MIN_SPAN);
    const n = corsieIn(a, z);
    if (n !== attese) variate++;
    if (n < minimo) minimo = n;
  }
  ok(variate === 0,
     "e restano " + attese + " al fondo scala in otto punti della corsa" +
     (variate ? " -- scese a " + minimo : ""));

  // La prova che il conteggio non e' vacuo: le FASCE, a differenza delle righe,
  // devono continuare a dipendere dalla vista. Senza questa, un disegna() che
  // ignorasse [b0,b1] passerebbe le due asserzioni qui sopra.
  //
  // Il confronto e' con il numero ESATTO -- quante fasce toccano la finestra --
  // e non con "meno di prima": su multi, che e' 95 cicli con una fascia sola,
  // non c'e' niente da escludere e un confronto comparativo fallirebbe su un
  // programma sano. E' lo stesso motivo per cui i tre cursori sono saltati li'.
  const bandeIn = (b0, b1) => {
    const ln = document.getElementById("_prova_lanes2");
    disegna(document.getElementById("_prova_axis2"), ln, b0, b1, t => String(t));
    return (ln.innerHTML.match(/class="band /g) || []).length;
  };
  let sbagliate = 0;
  for (const q of [0, 0.25, 0.5, 0.75, 1]) {
    const [a, z] = limitaFinestra(D.fine * q, D.fine * q + MIN_SPAN);
    if (bandeIn(a, z) !== D.fasce.filter(f => f[1] > a && f[0] < z).length) sbagliate++;
  }
  ok(sbagliate === 0,
     "mentre le FASCE disegnate sono esattamente quelle che toccano la finestra");
}

print("--- LE FINESTRE STANNO SULLO STESSO RIGHELLO DELLE FASCE ---");
// Il difetto, segnalato dall'utente il 14/09/2026: le finestre del marcatore si
// disegnavano in una sezione a parte, su una scala FISSA di tutta la corsa,
// mentre le fasce stavano sul diagramma zoomabile. Due righelli per la stessa
// corsa, quindi la correlazione che questi dati esistono per dare -- QUESTA
// latenza cade sotto quale fascia, e di chi era la CPU dentro -- non si poteva
// leggere in verticale.
//
// Qui si prova che ora le finestre passano da disegna(), cioe' sono funzione
// della stessa [b0,b1] delle fasce.
if (!M || !M.finestre.length) {
  print("  saltato: questo programma non emette finestre");
} else {
  const reso = (b0, b1) => {
    const ln = document.getElementById("_prova_lanes3");
    disegna(document.getElementById("_prova_axis3"), ln, b0, b1, t => String(t));
    return ln.innerHTML;
  };
  const quante = h => (h.match(/class="win[ "]/g) || []).length;

  ok(quante(reso(0, D.fine)) === M.finestre.length,
     "a tutta la corsa ci sono tutte (" + M.finestre.length + ")");

  // ESATTAMENTE quelle che toccano la vista: ne' una in piu' (disegnata fuori
  // schermo) ne' una in meno (persa perche' comincia prima del bordo).
  let sbagliate = 0, tagliate = 0;
  for (const q of [0, 0.2, 0.4, 0.6, 0.8, 1]) {
    const [a, z] = limitaFinestra(D.fine * q, D.fine * q + MIN_SPAN);
    const h = reso(a, z);
    if (quante(h) !== M.finestre.filter(f => f.z > a && f.a < z).length) sbagliate++;
    tagliate += (h.match(/tglio-(sx|dx)/g) || []).length;
  }
  ok(sbagliate === 0, "e in vista ci sono esattamente quelle che la toccano");

  // Una finestra troncata dal bordo DEVE dirlo, o si legge come una finestra
  // corta e il numero che uno crede di vedere e' sbagliato. Al fondo scala
  // (40 cicli) qualcosa di troncato c'e' per forza, se ci sono finestre.
  ok(tagliate > 0,
     "e chi esce dalla vista porta il bordo tratteggiato (" + tagliate + " bordi)");

  // I tratti dentro una finestra tagliata vanno rifatti sulla parte VISIBILE:
  // riusare le percentuali della finestra intera li farebbe scivolare fuori
  // dalla barra -- un disegno che mente di poco mentre stai misurando.
  {
    const f = M.finestre.reduce((p, c) => c.d > p.d ? c : p, M.finestre[0]);
    // una vista che taglia la piu' lunga a meta', da entrambi i lati
    const [a, z] = limitaFinestra(f.a + f.d / 4, f.a + f.d / 4 + Math.max(MIN_SPAN, f.d / 2));
    const h = reso(a, z);
    const perc = [...h.matchAll(/class="seg" style="--sc:[^;]+;left:([-\d.]+)%;width:([\d.]+)%/g)]
      .map(m => [parseFloat(m[1]), parseFloat(m[2])]);
    ok(perc.length > 0, "la finestra piu' lunga tagliata mostra comunque i suoi tratti");
    ok(perc.every(([l, w]) => l >= -0.001 && l + w <= 100.001),
       "e ogni tratto sta DENTRO la barra ritagliata (0..100%)");
  }

  // La prova che non e' rimasta una seconda copia su un'altra scala: la
  // sezione in fondo non disegna piu' corsie, solo la sovrapposizione.
  ok(document.getElementById("mlanes").innerHTML === "",
     "e la vecchia copia a scala fissa non si disegna piu'");
}

print("--- A MANI NUDE SI NAVIGA, COL MODIFICATORE SI MISURA ---");
// Deciso con l'utente il 14/09/2026 (§3.61). Prima il click NUDO piantava A, e
// lo stesso gesto voleva dire anche "scorri": la convivenza stava su una soglia
// di quattro pixel che INDOVINAVA l'intenzione -- sotto, un nudge da 1-3 px
// mentre scorrevi spostava A; sopra, un trascinamento da 5 px che volevi fosse
// un click non piantava niente.
//
// La riga che decideva il gesto stava nella colla, dove il DOM finto non
// arriva. Ora e' cursoreDi(): due booleani, un nome o niente, e si prova.
{
  ok(cursoreDi(false, false) === null,
     "il click NUDO non pianta niente: e' navigazione");
  ok(cursoreDi(true,  false) === "A", "shift -> A");
  ok(cursoreDi(false, true)  === "B", "ctrl -> B");
  ok(cursoreDi(true,  true)  === "C", "ctrl+shift -> C");

  // Ogni cursore raggiungibile, e da un gesto SOLO: se due combinazioni dessero
  // lo stesso nome, uno dei tre sarebbe irraggiungibile senza che niente lo dica.
  const resi = [[false,false],[true,false],[false,true],[true,true]]
    .map(([s,c]) => cursoreDi(s,c)).filter(n => n !== null);
  ok(resi.length === 3, "tre gesti piantano, uno no (" + resi.join(", ") + ")");
  ok(new Set(resi).size === 3, "e sono tre cursori DIVERSI");
  ok(["A","B","C"].every(n => resi.includes(n)), "A, B e C sono tutti raggiungibili");

  // I suggerimenti a schermo si derivano da cursoreDi, non da un secondo elenco:
  // e' la stessa mossa di marks.conf, un posto solo dove la cosa e' scritta.
  ok(["A","B","C"].every(n => GESTI[n]),
     "ogni cursore sa come si chiama il suo gesto (" +
     ["A","B","C"].map(n => n + "=" + GESTI[n]).join(", ") + ")");
  ok(new Set(["A","B","C"].map(n => GESTI[n])).size === 3,
     "e due cursori non annunciano lo stesso gesto");
}

print("--- IL COMANDO E IL DATO NON SI CHIAMANO ALLO STESSO MODO ---");
// Segnalato dall'utente il 14/09/2026: shift+click non piantava piu' il cursore
// B. Il gestore del click trova i bottoni dello zoom con
//
//     ev.target.closest("[data-z]")      ... e se lo trova, ZOOMA e ritorna
//
// e le finestre appena entrate nel diagramma portavano `data-z` = il loro ciclo
// di fine. Ogni click su una barra diventava un comando di zoom, e il cursore
// non si piantava mai: due significati per lo stesso nome, che e' la specie di
// difetto che questo progetto toglie dappertutto tranne, quel giorno, qui.
//
// La colla del click non e' provabile nel DOM finto -- addEventListener e'
// no-op, closest() ritorna null -- ma la COLLISIONE si': e' una proprieta' del
// markup, non degli eventi. E la forma generale e' migliore di "non c'e'
// data-zoom nelle corsie": qualunque nome condiviso fra i due insiemi rende un
// pezzo di diagramma cliccabile come un bottone.
{
  const nomi = h => new Set(
    [...String(h).matchAll(/\sdata-([a-z0-9-]+)\s*=/g)].map(m => m[1]));
  const comandi = nomi(document.getElementById("zoomctl").innerHTML);
  const dati    = nomi(document.getElementById("lanes").innerHTML);
  const comuni  = [...comandi].filter(n => dati.has(n));
  ok(comandi.size > 0,
     "i controlli dello zoom portano un attributo di comando (data-" +
     [...comandi].join(", data-") + ")");
  ok(dati.size > 0,
     "e il diagramma porta i suoi dati (data-" + [...dati].join(", data-") + ")");
  ok(comuni.length === 0,
     comuni.length ? "MA UN NOME E' CONDIVISO: data-" + comuni.join(", data-")
                   : "e nessuno dei due nomi finisce nell'altro insieme");
}

print(ko === 0 ? "\nTUTTO VERDE" : "\n" + ko + " FALLITI");
