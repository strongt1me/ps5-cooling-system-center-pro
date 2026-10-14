## Release Notes

Version: v1.66.0
Datum: 2026-10-14

### PS4-Spiele von USB (PS4Mount)
- Neuer Reiter **„PS4 von USB“** auf der Spiele-Seite: PS4-Spiele (Fake-PKG) direkt vom USB-Laufwerk spielen, ohne sie zu installieren. Das Spiel kommt mit Symbol auf den Startbildschirm, die PKG-Dateien bleiben auf dem Laufwerk
- Beruht auf PS4Mount 0.4.4 von gasb07 (GPL-3.0), dessen Quelltext in die App übernommen ist: Updates und Zusatzinhalte (DLC) werden mit eingebunden, viele Spiele in Gruppen hinzugefügt, Löschen auf der Konsole entfernt nur den Eintrag in der Bibliothek
- Liste mit Symbol, Version, Update, Zusatzinhalten und Zustand; „Jetzt suchen“, „Aus Bibliothek entfernen“ und „Hinzufügen“; Einstellungen (Ordner, Gruppen, DLC) und Protokoll
- In der Voreinstellung ausgeschaltet; läuft gleichzeitig das eigenständige `ps4mount.elf`, lässt es sich nicht einschalten
- Neue Schnittstelle `/api/v1/ps4mount`

### Bedienung mit dem Controller
- **L1 / R1** wechseln im Browser der Konsole zur vorherigen / nächsten Seite der Seitenleiste
- **L2 / R2** rollen die Seite nach oben / unten

### Meldungen
- Die Meldung beim Start der App und die Anzeige von Temperatur und Lüfter (Mute-Taste zweimal drücken) erscheinen oben links, ohne Ton

### Starter-Kachel
- `PPSA86921.zip` enthält die App von v1.66.0; den Ordner in `/data/homebrew/` ersetzen

### Wichtig
- **Firmware:** Getestet auf einer PS5 Pro (CFI-7021) mit **12.00**. Mehrere Nutzer melden, dass die App auf 13.60 läuft; für 13.00 bis 13.40 gibt es keine Meldung.
- PS4Mount ist an der Konsole nur ausgeschaltet geprüft; mit einem echten PS4-Spiel vom USB-Laufwerk ist der Einbau noch nicht erprobt (das Original prüfte gasb07 auf 13.20).
- Die LZ4-Pakete sind an der Konsole nur mit Arkanoid ausprobiert (kein Spielstart mit Paketen); der Mitschnitt und die DLC-Module sind dort noch nicht benutzt.
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**
