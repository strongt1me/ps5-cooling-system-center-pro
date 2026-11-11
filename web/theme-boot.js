/* Das Design der Oberfläche (15.10.2026): Standard, Hell, Dunkel oder Optimal.
 *
 * Läuft synchron im <head> wie i18n-boot.js, damit die Seite gleich im gewählten Design gezeichnet wird und nicht erst
 * in „Standard“ aufblitzt. Die Wahl gilt je Gerät (localStorage "theme"): am Fernseher etwa „Optimal“, am Handy „Hell“.
 * Das Design steht als data-theme an <html>; „Standard“ ist die Vorgabe und braucht keines (gemerktes „aktuell“, der
 * frühere Name, gilt als „standard“). Die Farben stehen in
 * style.css (Abschnitt „Vier Designs“). Der Wähler oben (#theme) wechselt ohne Neuladen.
 *
 * Ebenso die Darstellung der Spielkarten (10.11.2026): „groß (wie bisher)“ oder „kompakt“, gemerkt je Gerät (localStorage "gmview"),
 * als data-gmview="compact" an <html>; „kompakt“ ist die Vorgabe (seit 11.11.2026; wer „groß“ gewählt hat, behält es), „groß“ braucht kein Attribut. Der Wähler #gm-view steht auf der
 * Spiele-Seite. */
(function () {
  var THEMES = { standard: 1, hell: 1, dunkel: 1, optimal: 1 };
  var root = document.documentElement;
  /* Browser der PS5 (Kennung „PlayStation“): Klasse ps5 an <html>. Sie schaltet nur PS5-eigene Dinge (Titelbilder der kompakten Ansicht,
     Zeichenbereich je Spielkarte, Scroll-Messung); die Schrift ist dort wie in jedem anderen Browser (die größere Schrift wurde am
     10.11.2026 wieder zurückgenommen). */
  try { if (/PlayStation/i.test(navigator.userAgent)) root.classList.add("ps5"); } catch (e) { /* ohne Kennung: nichts */ }
  function apply(t) {
    if (!THEMES[t]) t = "standard";
    if (t === "standard") root.removeAttribute("data-theme"); else root.setAttribute("data-theme", t);
    return t;
  }
  var cur = "standard";
  try { cur = localStorage.getItem("theme") || "standard"; } catch (e) { /* ohne Speicher: die Vorgabe */ }
  cur = apply(cur);
  var view = "compact";
  try { view = localStorage.getItem("gmview") === "classic" ? "classic" : "compact"; } catch (e) { /* ohne Speicher: die Vorgabe */ }
  function applyView(v) {
    if (v === "compact") root.setAttribute("data-gmview", "compact"); else root.removeAttribute("data-gmview");
    return v;
  }
  view = applyView(view);
  document.addEventListener("DOMContentLoaded", function () {
    var vsel = document.getElementById("gm-view");
    if (vsel) {
      vsel.value = view;
      vsel.addEventListener("change", function () {
        view = applyView(vsel.value);
        try { localStorage.setItem("gmview", view); } catch (e) { /* ohne Speicher gilt die Wahl bis zum Neuladen */ }
      });
    }
    var sel = document.getElementById("theme");
    if (!sel) return;
    sel.value = cur;
    sel.addEventListener("change", function () {
      cur = apply(sel.value);
      try { localStorage.setItem("theme", cur); } catch (e) { /* ohne Speicher gilt die Wahl bis zum Neuladen */ }
    });
  });
})();
