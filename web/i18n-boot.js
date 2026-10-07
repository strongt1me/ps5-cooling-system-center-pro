/* Sprache wählen, bevor die Seite gezeichnet wird (07.10.2026).
 *
 * Läuft synchron im <head>, ist klein und hat keine Abhängigkeiten. Es legt window.PS5_LANG und window.PS5_LOCALE fest
 * (app.js formatiert Zahlen und Zeiten damit), setzt <html lang> und verbirgt die Seite, bis i18n.js übersetzt hat,
 * damit nicht erst deutscher Text aufblitzt. Ein Sicherheitsnetz zeigt die Seite nach 4 Sekunden in jedem Fall.
 *
 * Welche Sprache: die gemerkte Wahl (localStorage "lang"); beim ersten Besuch die Sprache des Browsers, wenn die App sie
 * hat, sonst Englisch. Deutsch ist die Sprache der Quelltexte und braucht kein Wörterbuch. */
(function () {
  var LOCALES = { de: "de-DE", en: "en-GB", it: "it-IT", es: "es-ES", fr: "fr-FR", ru: "ru-RU" };
  var lang = "";
  try { lang = localStorage.getItem("lang") || ""; } catch (e) { /* ohne Speicher: Browsersprache */ }
  if (!LOCALES[lang]) {
    var n = String(navigator.language || "de").slice(0, 2).toLowerCase();
    lang = LOCALES[n] ? n : "en";
  }
  window.PS5_LANG = lang;
  window.PS5_LOCALE = LOCALES[lang];
  window.PS5_LOCALES = LOCALES;
  document.documentElement.lang = lang;
  if (lang !== "de") {
    document.documentElement.classList.add("i18n-wait");
    setTimeout(function () { document.documentElement.classList.remove("i18n-wait"); }, 4000);
  }
})();
