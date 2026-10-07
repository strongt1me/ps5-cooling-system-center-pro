/* Übersetzung der Oberfläche zur Laufzeit (07.10.2026).
 *
 * Die Quelltexte der Seite sind deutsch. Für jede andere Sprache liegt ein Wörterbuch unter /lang/<xx>.json (es wird
 * erst geladen, wenn die Sprache gewählt ist): {"v":1, "t": {"deutscher Text": "Übersetzung", ...}}. Texte, in die das
 * Programm Werte setzt, stehen mit Platzhaltern {0}, {1}, … im Wörterbuch ("Nicht lesbar: {0}"); sie werden als Muster
 * erkannt. Dieses Skript übersetzt, was auf der Seite steht und was später dazukommt (MutationObserver): Textknoten
 * und die Attribute title, aria-label, placeholder und alt.
 *
 * Wie ein Text gefunden wird (lookup): erst als Ganzes (exakt), dann gegen die Muster (mit den eingesetzten Werten, die
 * ihrerseits übersetzt werden, wenn sie selbst im Wörterbuch stehen: ein Wort wie „Dateien“ in „{0} {1} gelöscht“),
 * zuletzt an „ · “ zerlegt, jeder Teil für sich (Zeilen wie „4 Sitzungen · im Schnitt 2 Min. · zuletzt …“). Was nicht
 * gefunden wird, bleibt deutsch. Ein führendes oder folgendes „·“, „,“ oder „;“ und Leerraum gehören nicht zum Schlüssel
 * und bleiben, wie sie sind. Nicht übersetzt werden: Skripte, Stile, Textfelder, <pre>, <code> und alles, was
 * data-no-i18n trägt (Dateinamen, Kernel-Log): Das sind Daten, keine Oberfläche.
 *
 * Zahlen und Zeiten formatiert app.js selbst nach window.PS5_LOCALE (i18n-boot.js). Die Sprache wechselt man mit der
 * Auswahl oben (#lang): Die Wahl wird gemerkt und die Seite lädt neu (so ist alles frisch in der neuen Sprache).
 *
 * Für Tests: window.PS5I18N.misses() liefert die deutsch aussehenden Texte, für die es keine Übersetzung gab. */
(function () {
  "use strict";
  var lang = window.PS5_LANG || "de";
  var root = document.documentElement;
  var waiting = function (on) { root.classList[on ? "add" : "remove"]("i18n-wait"); };
  var api = { lang: lang, ready: lang === "de", misses: function () { return []; }, stats: function () { return {}; } };
  window.PS5I18N = api;

  /* Mehrzahlformen: {0|Form1|Form2|Form3} in einer Übersetzung wird durch das EINE Wort ersetzt, das zum Wert von {0} passt
     (die Zahl selbst steht getrennt als {0}). Russisch: 1, 21 … -> Form 1; 2–4, 22–24 … -> Form 2; sonst Form 3 (ein Bruch:
     Form 2). Sprachen mit zwei Formen: 1 -> Form 1, sonst Form 2 (Französisch: auch 0). */
  function pluralWord(lng, value, forms) {
    var v = String(value).replace(/[\s\u00a0\u202f]/g, "");
    var frac = /\d[.,]\d{1,2}$/.test(v);
    var n = Math.abs(parseInt(v.replace(/[.,]\d{1,2}$/, "").replace(/[^\d]/g, ""), 10));
    if (!isFinite(n)) n = 0;
    var i;
    if (lng === "ru") {
      var m10 = n % 10, m100 = n % 100;
      if (frac) i = 1;
      else if (m10 === 1 && m100 !== 11) i = 0;
      else if (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) i = 1;
      else i = 2;
    } else if (lng === "fr") {
      i = !frac && n < 2 ? 0 : 1;
    } else {
      i = !frac && n === 1 ? 0 : 1;
    }
    if (i >= forms.length) i = forms.length - 1;
    return forms[i];
  }
  api.plural = pluralWord;

  /* die Sprachauswahl und die Links zu Handbuch und FAQ in der gewählten Sprache */
  var sel = document.getElementById("lang");
  if (sel) {
    sel.value = lang;
    sel.addEventListener("change", function () {
      try { localStorage.setItem("lang", sel.value); } catch (e) { /* ohne Speicher gilt die Wahl nur bis zum Neuladen nicht */ }
      try {
        var page = sessionStorage.getItem("ps5page");
        if (page) sessionStorage.setItem("ps5restore", page);
      } catch (e) { /* ignorieren */ }
      location.reload();
    });
  }
  var suffix = lang === "de" ? "" : "." + lang;
  Array.prototype.forEach.call(document.querySelectorAll("a[data-doc]"), function (a) {
    a.setAttribute("href", "/" + a.getAttribute("data-doc") + suffix + ".html");
  });

  if (lang === "de") { waiting(false); return; }

  var SEP = " · ";
  var ATTRS = ["title", "aria-label", "placeholder", "alt"];
  var SKIP_TAGS = { SCRIPT: 1, STYLE: 1, TEXTAREA: 1, PRE: 1, CODE: 1 };
  var SKIP_CLASS = ["fm-name", "fm-crumb"];
  var exact = new Map();
  var floating = [];
  var byLead = new Map();
  var cache = new Map();
  var misses = new Map();
  var mineText = new WeakMap();      /* Textknoten -> was dieses Skript hineingeschrieben hat */
  var mineAttr = new WeakMap();      /* Element -> {Attribut: was dieses Skript hineingeschrieben hat} */
  var GERMAN = /[äöüÄÖÜß]|\b(?:und|ist|nicht|wird|für|mit|auf|bei|von|zum|zur|ein|eine|keine|noch|nur|auch|über|unter|dann|wenn|dem|den|der|das|Datei|Dateien|Spiel|Spiele|Konsole|Laufwerk|Ordner|Fehler|wurde|werden|kann|nach|aus|oder|sind|sich|wieder|bitte)\b/;
  var HAS_LETTERS = /[A-Za-zÄÖÜäöüßÀ-ÿА-Яа-я]{2}/;

  function escRe(s) { return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&"); }

  function build(t) {
    var pats = [];
    Object.keys(t).forEach(function (src) {
      var tr = t[src];
      if (typeof tr !== "string") return;
      if (!/\{\d+\}/.test(src)) { exact.set(src, tr); return; }
      var pieces = src.split(/\{\d+\}/);
      var order = [];
      var re = /\{(\d+)\}/g;
      var m;
      while ((m = re.exec(src))) order.push(+m[1]);
      var need = "";
      pieces.forEach(function (p) { if (p.length > need.length) need = p; });
      pats.push({
        rx: new RegExp("^" + pieces.map(escRe).join("(.*?)") + "$", "s"),
        order: order, tr: tr, lead: pieces[0], need: need.length >= 3 ? need : "", weight: pieces.join("").length
      });
    });
    pats.sort(function (a, b) { return b.weight - a.weight; });
    pats.forEach(function (p) {
      if (p.lead.length >= 2) {
        var k = p.lead.slice(0, 2).toLowerCase();
        var list = byLead.get(k);
        if (!list) { list = []; byLead.set(k, list); }
        list.push(p);
      } else {
        floating.push(p);
      }
    });
  }

  function tryList(list, core, depth) {
    for (var i = 0; i < list.length; i++) {
      var p = list[i];
      if (p.need && core.indexOf(p.need) < 0) continue;
      var m = p.rx.exec(core);
      if (!m) continue;
      var tr = p.tr;
      if (tr.indexOf("|") > 0) {
        tr = tr.replace(/\{(\d+)\|([^{}]*)\}/g, function (_, n, forms) {
          var k = p.order.indexOf(+n);
          return k >= 0 ? pluralWord(lang, m[k + 1], forms.split("|")) : "";
        });
      }
      return tr.replace(/\{(\d+)\}/g, function (_, n) {
        var k = p.order.indexOf(+n);
        return k >= 0 ? inner(m[k + 1], depth) : "";
      }).replace(/([^.])\.\.(?!\.)/g, "$1.");
    }
    return undefined;
  }

  /* ein eingesetzter Wert, der selbst ein Text sein kann („Dateien“, „Sicherung“) */
  function inner(v, depth) {
    if (depth >= 3 || !HAS_LETTERS.test(v)) return v;
    var parts = /^(\s*)([\s\S]*?)(\s*)$/.exec(v);
    var t = lookup(parts[2].replace(/\s+/g, " "), depth + 1);
    return t === null ? v : parts[1] + t + parts[3];
  }

  function viaSegments(core, depth) {
    var changed = false;
    var out = core.split(SEP).map(function (part) {
      var t = lookup(part.trim(), depth + 1);
      if (t === null) return part;
      changed = true;
      return t;
    });
    return changed ? out.join(SEP) : undefined;
  }

  /* ein Satzende oder eine offene Klammer, die die Seite aus einem anderen Stück anklebt (". Am häufigsten zieht", "als Erster nach (") */
  var LEAD_MARK = /^[.\s]+/;
  var TAIL_MARK = /[\s.(:]+$/;
  function viaMarks(core, depth) {
    var lead = LEAD_MARK.exec(core);
    var tail = TAIL_MARK.exec(core);
    if (!lead && !tail) return undefined;
    var mid = core.slice(lead ? lead[0].length : 0, tail ? core.length - tail[0].length : core.length);
    if (!mid || mid === core || !HAS_LETTERS.test(mid)) return undefined;
    var t = lookup(mid, depth + 1);
    return t === null ? undefined : (lead ? lead[0] : "") + t + (tail ? tail[0] : "");
  }

  function lookup(core, depth) {
    var hit = cache.get(core);
    if (hit !== undefined) return hit;
    var out = exact.get(core);
    if (out === undefined) {
      var list = byLead.get(core.slice(0, 2).toLowerCase());
      if (list) out = tryList(list, core, depth);
      if (out === undefined) out = tryList(floating, core, depth);
    }
    if (out === undefined && depth < 3 && core.indexOf(SEP) > 0) out = viaSegments(core, depth);
    if (out === undefined && depth < 3) out = viaMarks(core, depth);
    if (out === undefined) out = null;
    if (cache.size > 8000) cache.clear();
    cache.set(core, out);
    return out;
  }

  /* der Text eines Knotens oder Attributs: Rand (Leerraum, ein „·“ oder „,“) bleibt, der Kern wird übersetzt */
  var EDGES = /^(\s*(?:[·•,;]\s*)?)([\s\S]*?)(\s*[·,;]?\s*)$/;
  function translateText(s) {
    var m = EDGES.exec(s);
    var core = m[2].replace(/\s+/g, " ").trim();
    if (!core || !HAS_LETTERS.test(core)) return null;
    var t = lookup(core, 0);
    if (t === null) {
      if (GERMAN.test(core) && misses.size < 4000) misses.set(core, (misses.get(core) || 0) + 1);
      return null;
    }
    return m[1] + t + m[3];
  }

  function skipEl(el) {
    if (SKIP_TAGS[el.tagName] || (el.hasAttribute && el.hasAttribute("data-no-i18n"))) return true;
    for (var i = 0; i < SKIP_CLASS.length; i++) if (el.classList && el.classList.contains(SKIP_CLASS[i])) return true;
    return false;
  }
  function inSkip(el) {
    for (; el; el = el.parentElement) if (skipEl(el)) return true;
    return false;
  }

  function doText(n) {
    var v = n.nodeValue;
    if (mineText.get(n) === v) return;
    var out = translateText(v);
    if (out === null) return;
    mineText.set(n, out);
    n.nodeValue = out;
  }

  function doAttr(el, name) {
    var v = el.getAttribute(name);
    if (!v) return;
    var mine = mineAttr.get(el);
    if (mine && mine[name] === v) return;
    var out = translateText(v);
    if (out === null) return;
    if (!mine) { mine = {}; mineAttr.set(el, mine); }
    mine[name] = out;
    el.setAttribute(name, out);
  }

  function doAttrs(el) {
    for (var i = 0; i < ATTRS.length; i++) if (el.hasAttribute(ATTRS[i])) doAttr(el, ATTRS[i]);
  }

  function walk(node) {
    if (node.nodeType === 3) { if (!inSkip(node.parentElement)) doText(node); return; }
    if (node.nodeType !== 1 || inSkip(node)) return;
    doAttrs(node);
    var w = document.createTreeWalker(node, 5, { acceptNode: function (n) { return n.nodeType === 1 && skipEl(n) ? 2 : 1; } });
    var n;
    while ((n = w.nextNode())) {
      if (n.nodeType === 3) doText(n); else doAttrs(n);
    }
  }

  var observer = new MutationObserver(function (records) {
    for (var i = 0; i < records.length; i++) {
      var r = records[i];
      if (r.type === "childList") {
        if (inSkip(r.target)) continue;
        for (var k = 0; k < r.addedNodes.length; k++) walk(r.addedNodes[k]);
      } else if (r.type === "characterData") {
        if (!inSkip(r.target.parentElement)) doText(r.target);
      } else if (r.type === "attributes") {
        if (!inSkip(r.target)) doAttr(r.target, r.attributeName);
      }
    }
  });

  api.misses = function () { return Array.from(misses.keys()); };
  api.stats = function () { return { exact: exact.size, patterns: floating.length + Array.from(byLead.values()).reduce(function (a, l) { return a + l.length; }, 0), cache: cache.size }; };

  fetch("/lang/" + lang + ".json", { cache: "no-store" })
    .then(function (r) { if (!r.ok) throw new Error("HTTP " + r.status); return r.json(); })
    .then(function (d) {
      build(d.t || {});
      walk(document.body);
      observer.observe(root, { childList: true, subtree: true, characterData: true, attributes: true, attributeFilter: ATTRS });
      api.ready = true;
      waiting(false);
    })
    .catch(function (e) {
      waiting(false);                  /* ohne Wörterbuch bleibt die Seite deutsch, und das ist nicht schlimm */
      if (window.console) console.warn("Sprache " + lang + " nicht geladen:", e && e.message);
    });
})();
