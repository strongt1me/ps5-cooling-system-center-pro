# PS5 Cooling & System Center – Pro v1.76.5

## Highlights
- **Offline-Konto wie in OffAct:** Ein vorhandener Benutzer wird ausgewählt, nicht mehr getippt; Kennzeichen 4098 statt 1
- **Dateimanager:** Zielwahl im Stil des Dateimanagers, gelbe Ordner, der Dialog „Namen schon vorhanden“ schließt sich selbst

## Neu

### Profil: Offline-Konto
- Ein Vergleich mit dem Quelltext von OffAct (`master` ist weiter v0.4.2) ergab zwei Abweichungen unseres bisherigen Einbaus, die jetzt behoben sind: OffAct **schreibt nie einen Namen**, sondern macht einen **vorhandenen Benutzer** zum Offline-Konto (Konto-ID, Typ `np`, Kennzeichen), und es setzt die Kennzeichen auf **4098** (wir hatten 1). Ein Name mit Umlaut bekommt jetzt dieselbe Konto-ID wie dort.
- **Browser:** Statt des Namensfelds steht eine Liste der vorhandenen Benutzer (Platz, Typ, Konto-ID) mit „Aktivieren“; die aktive Zeile leuchtet grün.
- **Vollbild-App:** „Offline-Konto aktivieren“ öffnet eine Auswahl der Benutzer (bei nur einem wird er sofort aktiviert); die Tastatur dafür entfällt.
- „Aktiv“ hieß bisher nur „Platz 1 hat einen Namen“, was auf jeden Benutzer zutrifft; jetzt heißt es „Konto-ID gesetzt und Typ `np`“.

### Dateimanager
- „Kopieren nach“: Die Ordnerzeilen haben Größe und Schrift wie im Dateimanager (vorher riesige Symbole mit kleinem Namen). Ordner sind überall **gelb**, Dateien bleiben grau. Bei `/data` leuchtet nur noch „Interne SSD“, nicht zugleich „Ganzes System“.
- Das Fenster „Namen schon vorhanden“ schließt sich nach dem Hochladen von selbst.

## Hinweise
- Wer das Offline-Konto schon mit einer früheren Fassung aktiviert hat, hat Kennzeichen 1. „Erneut aktivieren“ setzt sie auf 4098, wie OffAct es tut. Die Dokumentation von OffAct verlangt danach keinen Neustart; ob die Oberfläche der Konsole die Änderung sofort übernimmt, ist nicht belegt.
- Die Kacheln FAKE86921 (Vollbild) und FAKE86922 (Browser) aus diesem Release neu kopieren.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.76.5.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.76.4...v1.76.5
