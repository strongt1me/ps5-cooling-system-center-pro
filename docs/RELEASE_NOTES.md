## Release Notes

Version: v1.53.0
Datum: 2026-10-08

Neu seit 1.52.0: der neue Lüfter in der Oberfläche.

### Neu

- **Der Lüfter in der Oberfläche ist neu** (Entwurf des Users, Komponente `<ps5-cooling-fan>`, Datei `web/ps5-fan.js`). Auf der Seite **Kühlung** sitzt er als leuchtende Kachel:
  ein gezeichneter Radiallüfter mit 23 Lamellen und Lichtring. Er zeigt den **gemessenen** Lüfterwert. Die **Farbe** geht stufenlos von Blau (bis 25 %) über Grün (um 55 %)
  zu Rot (ab 65 %) und färbt auch Rand, Glühen und Zahl der Kachel. Die **Drehung** wird mit dem Wert schneller (etwa 0,15 bis 1 Umdrehung pro Sekunde), beschleunigt und bremst
  weich und steht bei 0 %; die Nabe in der Mitte steht still. Ohne Messwert ist der Lüfter grau und steht. Die Drehung ist eine Darstellung, keine gemessene Drehzahl.
- **Kleiner Lüfter in der Kopfleiste** neben dem Verbindungsstatus mit Wert und Zone („Kühl“, „Normal“, „Hohe Last“) auf jeder Seite, **außer auf Kühlung**, wo der große zu sehen ist.
- **Einstellung „Animation“** in der Kachel: „voll“, „reduziert“ (keine Drehung, Farbe bleibt) oder „aus“ (auch ohne Lichteffekte). Der Browser merkt sich die Wahl.
- **Schutz für den alten Browser der Konsole:** Wird ein Bild zu langsam, schaltet der Lüfter zuerst die Lichteffekte ab und danach die Drehung.

### Wichtig

- **Die Anzeige des Lüfters ist am Fernseher der Konsole beurteilt worden** (Rückmeldung des Users vom 08.10.2026: Drehung und Nabe passen). Zuerst drehte die Nabe mit und die Lamellen sprangen bei
  hoher Drehzahl; beides ist behoben (Höchstdrehzahl 1 Umdrehung pro Sekunde, Nabe fest).
- **Firmware:** Getestet auf einer PS5 Pro (CFI-7021) mit **12.00**. Der Nutzer meldet, dass die App auch auf einer PS5 mit **13.60** läuft (nicht von mir geprüft). Neuere Firmware als 13.60 startet erst mit einem neueren SDK.
- Noch nicht an der Konsole benutzt: Profile ausführen und als Startprofil, der Import vom PC bei den Profilen, die Backport-Erkennung mit `fakelib2`/`backports` und das **Löschen von Spielständen**.
  Probiere das Löschen zuerst mit einem unwichtigen Titel. **Löschen ist endgültig** – bei Spielen, Sicherungen und im Dateimanager.
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**
- **Was auf Deutsch bleibt:** die Meldungen, die die Konsole selbst auf dem Fernseher einblendet, die Entwicklerdokumente und die Schnittstellenbeschreibung.

### Technisches

- Neu: `web/ps5-fan.js`; entfernt: `web/img/fan-rotor.png`, `web/img/fan-frame.png`. Keine neuen Endpunkte, keine Änderung am C-Teil außer der Versionsnummer.
- Tests: Browser-Suite 54 Szenarien ohne Befund (mit Content-Security-Policy), Lüfter-Szenario FN.
