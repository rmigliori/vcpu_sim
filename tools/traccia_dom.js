// tools/traccia_dom.js — un DOM finto, per ESEGUIRE lo script di traccia.html.
//
// Nasce il 12/09/2026 da un problema pratico: la pagina prodotta da traccia.py
// e' l'unico pezzo di questo progetto che non si poteva verificare. Non c'e'
// node sulla macchina e Firefox headless non parte (l'istanza interattiva
// tiene il profilo), quindi ogni correzione al template veniva spedita LETTA e
// non provata — ed e' cosi' che due difetti sono passati: `own.gestore.kernel`
// che lanciava una TypeError su ogni programma senza un task chiamato gestore,
// e l'elenco ORDINE scritto a mano che faceva sparire dalla pagina i task di
// cui non conosceva il nome.
//
// Ma un motore JS c'e': gjs (SpiderMonkey), che arriva con GNOME. Non ha un
// DOM, e non serve: quello che va verificato e' la LOGICA SUI DATI, cioe'
// esattamente cio' che esplode quando un programma ha una forma diversa da
// test_gestore. Bastano getElementById e due setter.
//
// --- COME SI USA ---
//
//   python3 tools/traccia.py out/vasm/test_mondo.vx -- --kbd "2000:a,6000:b"
//   python3 - <<'FINE' > /tmp/pagina.js
//   import re; h=open('out/traccia.html').read()
//   print(re.search(r'<script>(.*)</script>', h, re.S).group(1) + "\n_dump();")
//   FINE
//   gjs -c "$(cat tools/traccia_dom.js /tmp/pagina.js)"
//
// Un'eccezione la stampa gjs; _dump() mostra cosa e' finito in ogni elemento,
// che e' come si controlla che una corsia o una riga di tabella ci sia davvero.
// Va fatto su PIU' programmi: i difetti di questa pagina sono tutti della forma
// "funziona su quello per cui e' stata scritta".

const _visti = {};
function _el(id) {
  if (!_visti[id]) _visti[id] = {
    id,
    set innerHTML(v) { this._html = String(v); },
    get innerHTML() { return this._html || ""; },
    set textContent(v) { this._text = String(v); },
    get textContent() { return this._text || ""; },
    hidden: true,            // come nella pagina: chi si mostra lo dice
    dataset: {},
    closest() { return null; },
  };
  return _visti[id];
}

globalThis.document = {
  getElementById: _el,
  querySelector: (s) => _el(s),
  addEventListener: () => {},
};

globalThis._dump = () => {
  for (const k of Object.keys(_visti)) {
    const e = _visti[k];
    const v = e._text !== undefined ? e._text : (e._html || "");
    print("### " + k + (e.hidden === false ? " [visibile]" : "") + ": " +
          String(v).replace(/\s+/g, " ").slice(0, 300));
  }
};
