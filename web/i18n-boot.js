/* Sprache wählen, bevor die Seite gezeichnet wird (07.10.2026).
 *
 * Läuft synchron im <head>, ist klein und hat keine Abhängigkeiten. Es legt window.PS5_LANG und window.PS5_LOCALE fest
 * (app.js formatiert Zahlen und Zeiten damit), setzt <html lang> und verbirgt die Seite, bis i18n.js übersetzt hat,
 * damit nicht erst deutscher Text aufblitzt. Ein Sicherheitsnetz zeigt die Seite nach 4 Sekunden in jedem Fall.
 *
 * Welche Sprache: die gemerkte Wahl (localStorage "lang"). Ohne Wahl (oder mit „Auto“) gilt die Systemsprache, wenn die App sie
 * hat, sonst Englisch (12.11.2026): im Browser der PS5 die Sprache der Konsole (app.js merkt sie als "lang_sys", sobald die App
 * sie von der Konsole erfahren hat), sonst die Sprachen des Browsers in ihrer Reihenfolge. Deutsch ist die Sprache der Quelltexte
 * und braucht kein Wörterbuch. */
(function () {
  var LOCALES = { de: "de-DE", en: "en-GB", it: "it-IT", es: "es-ES", fr: "fr-FR", ru: "ru-RU", sv: "sv-SE" };
  var lang = "";
  try { lang = localStorage.getItem("lang") || ""; } catch (e) { /* ohne Speicher: Browsersprache */ }
  var auto = !LOCALES[lang];
  if (auto) {
    lang = "";
    var ps5 = false;
    try { ps5 = /PlayStation/i.test(navigator.userAgent); } catch (e) { /* ohne Kennung: Browsersprache */ }
    if (ps5) {
      try { var sys = localStorage.getItem("lang_sys") || ""; if (LOCALES[sys]) lang = sys; } catch (e) { /* noch nicht bekannt */ }
    }
    if (!lang) {
      var list = [];
      try { list = (navigator.languages && navigator.languages.length) ? Array.prototype.slice.call(navigator.languages) : [navigator.language || ""]; } catch (e) { /* ignorieren */ }
      for (var i = 0; i < list.length && !lang; i++) {
        var n = String(list[i] || "").slice(0, 2).toLowerCase();
        if (LOCALES[n]) lang = n;
      }
    }
    if (!lang) lang = "en";                                   /* die Sprache gibt es in der App nicht: Englisch */
  }
  window.PS5_LANG_AUTO = auto;
  window.PS5_LANG = lang;
  window.PS5_LOCALE = LOCALES[lang];
  window.PS5_LOCALES = LOCALES;
  document.documentElement.lang = lang;
  if (lang !== "de") {
    document.documentElement.classList.add("i18n-wait");
    setTimeout(function () { document.documentElement.classList.remove("i18n-wait"); }, 4000);
  }
})();
