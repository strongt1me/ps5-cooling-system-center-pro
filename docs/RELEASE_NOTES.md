## Release Notes

Version: v1.49.1
Datum: 2026-10-06

Neu seit 1.49.0: Benutzerhandbuch und FAQ direkt in der App.

### Neu

- **Handbuch und FAQ in der App.** Unter „Credits“ stehen in der Seitenleiste zwei neue Einträge: **Handbuch** öffnet
  das Benutzerhandbuch (Installation, alle Seiten der App, Sicherheit, Hilfe bei Problemen), **FAQ** die häufigen Fragen
  mit Antworten. Am PC öffnen sie sich in einem neuen Tab, oben steht „← Zurück zur App“. Die eingebauten Fassungen
  kommen ohne Bildschirmfotos aus, damit die App klein bleibt (rund 150 KB mehr).

### Verbessert

- Credits: mehr Abstand zwischen dem Schlusssatz und den Karten darüber; bei zecoxao ein doppelter Link entfernt.
- Persönliche Angaben aus Kommentaren und Beispielen der Dokumentation entfernt (Konsolenname, WLAN-Name,
  Benutzerkennung).

### Wichtig

- **Getestet ist nur Firmware 12.00** auf einer PS5 Pro (CFI-7021).
- **Löschen ist endgültig** – bei Spielen, Sicherungen und im Dateimanager.
- **Pakete aufteilen** wurde an der Konsole noch nicht ausprobiert.
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**
- **„Spiel beenden“ fragt nicht nach.**

### Technisches

- Neue Dateien `web/handbuch.html` und `web/faq.html` (eigenständig, ohne Skripte). In der Seitenleiste sind die
  Einträge Links (`a.tab`), keine Seiten der App.
- Enthält alle Änderungen aus 1.49.0 (Dateimanager, Spiele und Sicherungen löschen, Credits, Paket-Installation bis
  zum Ende, Meldung bei abgemeldeten Laufwerken).
