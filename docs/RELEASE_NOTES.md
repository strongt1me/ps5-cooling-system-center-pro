## Release Notes

Version: v1.62.0
Datum: 2026-10-13

### LZ4-Asset-Packs
- Spiele lassen sich im Spielordner zu LZ4-Asset-Packs packen (ein Paket mit Index und Prüfdatei, Dateien Byte für Byte mit dem Original verglichen, bei jedem Fehler wird zurückgerollt); Pakete prüfen und wieder entpacken sind dabei
- Profil „Automatisch“ (Stufe 1 bis 12) oder eigenes TOML-Profil; „Automatisch“ lässt Programme, Bibliotheken, Spielstände, schon komprimierte Medien, Konfigurations- und Datendateien sowie sehr große Dateien lose
- Die App erkennt, ob ein Spiel libSceAmpr überhaupt lädt, und sperrt das Packen sonst
- Neue Anleitung „LZ4-Asset-Packs“ im Handbuch (6 Sprachen) und klarere Beschriftung der Zeile

### APR-EMU, PlayGo und DLC
- APR-EMU in fünf Fassungen und der PlayGo-Stub 0.6 (mit und ohne Protokoll) sind in der App eingebettet und lassen sich pro Spiel auswählen
- Neue Zeile „Spiel-Plugins“: bringt PlayGo-Stub und die DLC-Module (dlc_emu 0.3) auf den neuesten Stand; alte Dateien bleiben als `.old` erhalten, fehlendes Ausführungsrecht wird gesetzt
- Optimierung per Mitschnitt: die Debug-APR-EMU schreibt beim Spielen mit, aus den gesammelten Sitzungen entsteht ein eigenes Profil für das Spiel
- Prüfen, Entpacken und Reparieren von Abbildern, AMPR-Index, Auftragsverlauf

### Spiele starten
- Aus dem Browser der Konsole schließt sich die Seite sofort beim Tippen auf „Starten“, noch bevor das Spiel lädt
- Dauert ein Start länger (zum Beispiel ein Abbild, das erst eingehängt wird), erscheint nach 1,5 Sekunden die Meldung „Bitte warten. Spiel wird geladen...“
- Absagen und Hinweise erscheinen als Meldung der Konsole
- Die Kachel des laufenden Spiels leuchtet

### Oberfläche
- Die Web UI öffnet beim nächsten Mal auf der Seite, auf der sie geschlossen wurde
- Neue Reihenfolge der Seitenleiste: Kühlung ganz oben, Profil unter System
- Beim Konvertieren steht jetzt das Format des Ergebnisses (zum Beispiel „ffpkg (UFS2-Abbild)“)
- Handbuch, FAQ und Oberfläche in sechs Sprachen aktualisiert; neue Credits für V-10 (exFAT2AMPR), nexgen999, matem6, Phoenixx1202, ArkSama und SiSTR0

### Wichtig
- **Firmware:** Getestet auf einer PS5 Pro (CFI-7021) mit **12.00**. Mehrere Nutzer melden, dass die App auf 13.60 läuft; für 13.00 bis 13.40 gibt es keine Meldung.
- Die LZ4-Pakete sind an der Konsole nur mit Arkanoid ausprobiert (kein Spielstart mit Paketen); der Mitschnitt und die DLC-Module sind dort noch nicht benutzt. Erst ungepackt starten, dann packen.
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**
