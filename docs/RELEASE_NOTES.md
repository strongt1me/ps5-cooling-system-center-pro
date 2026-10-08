## Release Notes

Version: v1.52.0
Datum: 2026-10-08

Neu seit 1.51.0: Pakete direkt vom PC installieren und ein QR-Code mit der Adresse der Weboberfläche.

### Neu

- **Pakete direkt vom PC installieren.** Auf dem Reiter **Pakete** steht oben die Karte **„Vom PC installieren“**: eine `.pkg`-Datei wählen oder auf die Karte
  ziehen, den Plan lesen, „Jetzt installieren“. Die Datei wird **nicht auf der Konsole gespeichert**: Der Browser schickt sie in Stücken zu einem Megabyte,
  während die Konsole sie installiert. Die Installation braucht darum nur den Platz des installierten Spiels, nicht noch einmal die Größe der Datei.
  Die Konsole liest das Paket nicht der Reihe nach; die App hält einen Ring von 64 MB im Arbeitsspeicher und liefert zuerst, was die Konsole gerade braucht.
  **Die Seite muss offen bleiben**, bis die Installation fertig ist (sie arbeitet auch in einem Hintergrund-Reiter weiter). Meldet sich der Browser 40 Sekunden
  lang nicht, bricht die Konsole mit einem klaren Satz ab, statt zu hängen. Geteilte Pakete (`.part1` …) gehen auf diesem Weg nicht. Die Idee stammt vom
  „Direct Install“ des PKG Managers von itsPLK (GPL-3.0); hier ist sie neu gebaut.
- **QR-Code mit der Adresse der Seite.** Auf der Seite **System** zeigt die Karte **„Am Handy öffnen“** einen QR-Code: Kamera des Handys darauf halten, und die Seite
  geht auf dem Handy auf (im selben Netzwerk). Der Code enthält nur die Adresse, keine Zugangsdaten. Der Encoder stammt aus webhb 0.4.1 von slopmaster33 (GPL-3.0).

### Wichtig

- **Getestet ist nur Firmware 12.00** auf einer PS5 Pro (CFI-7021). Die **Übertragung und Installation vom PC** und der **QR-Code** wurden an der Konsole ausprobiert und liefen
  (Rückmeldung des Users vom 08.10.2026). Noch nicht an der Konsole benutzt: Profile ausführen und als Startprofil, der Import vom PC bei den Profilen, die
  Backport-Erkennung mit `fakelib2`/`backports` und das **Löschen von Spielständen** (aus 1.51.0; nur im Host- und Browsertest geprüft). Probiere das Löschen zuerst
  mit einem unwichtigen Titel.
- **Löschen ist endgültig** – bei Spielen, Sicherungen und im Dateimanager.
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**
- **Was auf Deutsch bleibt:** die Meldungen, die die Konsole selbst auf dem Fernseher einblendet, die Entwicklerdokumente und die Schnittstellenbeschreibung.

### Technisches

- Neue Dateien: `src/pkglive.c` (Übertragung vom PC), `src/qr.c` (QR-Encoder); Endpunkte `POST /api/v1/packages/live/init`, `PUT /api/v1/packages/live/segment`,
  `GET /api/v1/packages/live/state` (mit `since`/`wait` als Wartemodus), `POST /api/v1/packages/live/cancel` und `GET /api/v1/qr`, siehe [API.md](docs/API.md).
- Tests: Installationstest mit simulierter Konsole 644 Prüfungen (80 für diesen Weg, mit ThreadSanitizer sauber), Host-Test mit echtem HTTP-Server, Browser-Suite 53 Szenarien,
  der QR-Code mit einem Lesegerät (OpenCV) für alle sechs Versionen zurückgelesen.
