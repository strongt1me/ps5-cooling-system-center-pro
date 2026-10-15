/* Das Design der Oberfläche (15.10.2026): Aktuell, Hell, Dunkel oder Optimal.
 *
 * Läuft synchron im <head> wie i18n-boot.js, damit die Seite gleich im gewählten Design gezeichnet wird und nicht erst
 * in „Aktuell“ aufblitzt. Die Wahl gilt je Gerät (localStorage "theme"): am Fernseher etwa „Optimal“, am Handy „Hell“.
 * Das Design steht als data-theme an <html>; „Aktuell“ ist die Vorgabe und braucht keines. Die Farben stehen in
 * style.css (Abschnitt „Vier Designs“). Der Wähler oben (#theme) wechselt ohne Neuladen. */
(function () {
  var THEMES = { aktuell: 1, hell: 1, dunkel: 1, optimal: 1 };
  var root = document.documentElement;
  function apply(t) {
    if (!THEMES[t]) t = "aktuell";
    if (t === "aktuell") root.removeAttribute("data-theme"); else root.setAttribute("data-theme", t);
    return t;
  }
  var cur = "aktuell";
  try { cur = localStorage.getItem("theme") || "aktuell"; } catch (e) { /* ohne Speicher: die Vorgabe */ }
  cur = apply(cur);
  document.addEventListener("DOMContentLoaded", function () {
    var sel = document.getElementById("theme");
    if (!sel) return;
    sel.value = cur;
    sel.addEventListener("change", function () {
      cur = apply(sel.value);
      try { localStorage.setItem("theme", cur); } catch (e) { /* ohne Speicher gilt die Wahl bis zum Neuladen */ }
    });
  });
})();
