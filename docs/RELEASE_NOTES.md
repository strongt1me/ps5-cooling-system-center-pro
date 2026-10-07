## Release Notes

Version: v1.51.0
Datum: 2026-10-07

Neu seit 1.50.0: Payload-Profile mit Startprofil, ein eigener Abbild-Leser statt Einhängen, Erkennung von fakelib2 und Backport-Ordnern und eine übersichtlichere Spielstände-Seite.

### Neu

- **Payload-Profile.** Auf der Seite „Payloads“ legst du **Profile** an: benannte Abfolgen aus Payloads des Konsolenordners mit Pausen dazwischen
  (zum Beispiel kstuff, 3 Sekunden Pause, ShadowMountPlus). „Ausführen“ startet sie Schritt für Schritt, „Anhalten“ beendet nach dem laufenden Schritt.
  Ein Profil lässt sich als **Startprofil** festlegen: die App führt es dann nach jedem eigenen Start von selbst aus. Läuft ein Ablauf nicht zu Ende
  (ein Payload startet die App neu), führt die App das Startprofil beim nächsten Start **nicht** noch einmal aus (Schutz vor Endlosschleifen).
  **„Vom PC importieren“** lädt `.elf`-Dateien vom Rechner in den Ordner der Konsole, auch direkt beim Bearbeiten eines Profils; „Profile sichern“
  und „Profile laden“ tauschen Profile als Datei aus. Die Idee stammt vom ps5-payload-manager von itsPLK (GPL-3.0); hier ist sie neu gebaut.
- **Abbild-Leser statt Einhängen.** „Anpassungen unbekannt“ bei Abbild-Spielen (`.exfat`, `.ffpkg`, `.ffpfsc`) verschwindet, ohne dass etwas eingehängt wird:
  die App liest `eboot.bin` und `fakelib` direkt aus der Abbilddatei. Das Ergebnis bleibt gespeichert; beim nächsten Start wird nur gelesen, was sich geändert hat.
  Nur ein Abbild, das der Leser nicht kennt, wird (falls ShadowMountPlus es zulässt) kurz eingehängt.
- **fakelib2 und Backport-Ordner** werden erkannt wie bei ShadowMountPlus: `fakelib2` ersetzt `fakelib`, und ein Ordner
  `<Scanpfad>/backports/<TITEL-ID>/` hat Vorrang vor den Bibliotheken im Spiel. Die Spieldetails nennen, woher die Bibliotheken kommen.
- **Spielstände:** das Bild des Benutzers links neben dem Namen, eine größere Benutzerkachel und der **Spielname neben der Titel-ID**
  (auch bei nicht mehr installierten Spielen, soweit die Konsole den Namen noch kennt).
  Die Liste ist je Benutzer in **PS5-Spiele, PS5-Apps, PS4-Spiele und PS4-Apps** getrennt (Apps wie YouTube sind gekennzeichnet).
  **Löschen:** jeder Titel hat einen Knopf „Löschen“ (zwei Klicks); die App sichert den jetzigen Stand vorher und lässt ihn zurückspielen.

### Verbessert

- Die Warnschwelle geht mit dem Ziel mit; Ziel bis 91 °C (siehe 1.50.0).
- Daten der App liegen unter `/data/PS5-Cooling-Center/covers_and_more` (Cover, Metadaten, Abbild-Ergebnisse, gemerkte Spielnamen).
- Handbuch und FAQ beschreiben Profile und Backport-Quellen in allen sechs Sprachen.

### Wichtig

- **Getestet ist nur Firmware 12.00** auf einer PS5 Pro (CFI-7021). An der Konsole liefen: der Abbild-Leser (10 Abbilder, nichts eingehängt),
  der Start der App mit den Profilen und die neue Spielstände-Liste (PS5/PS4 getrennt, YouTube als App erkannt). **Noch nicht an der Konsole
  benutzt:** Profile ausführen und als Startprofil, der Import vom PC, die Backport-Erkennung mit `fakelib2`/`backports` und das **Löschen von
  Spielständen** (nur im Host- und Browsertest geprüft). Probiere das Löschen zuerst mit einem unwichtigen Titel.
- **Löschen ist endgültig** – bei Spielen, Sicherungen und im Dateimanager.
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**
- **Was auf Deutsch bleibt:** die Meldungen, die die Konsole selbst auf dem Fernseher einblendet, die Entwicklerdokumente und die Schnittstellenbeschreibung.

### Technisches

- Neue Dateien: `src/payprofiles.c` (Profile), `src/imgread.c` (Leser für exFAT, UFS2 und PFSC), Endpunkte `/api/v1/payload-profiles*`,
  `POST /api/v1/library/probe`, `GET /api/v1/saves/avatar`, siehe [API.md](API.md).
- Hosttests: Profile 18 Prüfungen (mit Sanitizern), Backport-Quellen 4, Browser 18.
