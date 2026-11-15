# PS5 Cooling & System Center – Pro v1.86.3

## Highlights
- **Disc: „Starten“ dumpt gleich mit:** Im Reiter Disc startet „Starten“ bei einem Disc-Spiel das Spiel und das Dumpen in einem Zug. Die App muss nicht wieder geöffnet werden, denn das Spiel liegt vor ihr. Die Konsole meldet „Dump gestartet“, „Dump fertig“ oder „Dump gescheitert“.
- **FPKG: der Ordner FPKG-Games ist der richtige Ort:** Beim Konvertieren in das Format FPKG ist „A · Spielbar: Ordner FPKG-Games“ vorgewählt, mit Erklärung. Im Bereich FPKGMount steht bei einer Doppelbelegung mit ShadowMountPlus ein Achtung-Satz, der zu diesem Ordner rät.
- **Prüfung auf neue AMPR-EMU-NG-Fassung meldet sich sofort** (Vollbild-App), auch ohne Internetverbindung mit einer klaren Meldung.

## Neu
### Disc
- „Starten“ legt das Spiel allein in die Dump-Warteschlange und startet sie: Spiel starten, Ladezeit abwarten (Einstellung der Warteschlange, Vorgabe 30 s), dumpen. Nur wenn nichts dagegen spricht (kein anderes Spiel läuft, noch kein Dump, ein Ziel ist gewählt, keine Warteschlange läuft); sonst startet „Starten“ nur das Spiel, und „Dumpen“ folgt von Hand.
- Meldungen auf der Konsole beim Dumpen: „Dump gestartet: <Spiel>“, „Dump fertig: <Spiel>“, „Dump gescheitert: <Grund>“. Neue Aktion `queue_launch` in `POST /api/v1/dump`.

### FPKG und Kraken Mount
- Beim Konvertieren in FPKG ist **A (Ordner FPKG-Games)** von Anfang an gewählt (Browser-App und Vollbild-App), mit dem Hinweis, dass dies der richtige Ort für diese App ist.
- Bei einer Doppelbelegung mit ShadowMountPlus steht im Bereich FPKGMount nur noch: „Achtung! Lege FPKG-Spiele nur in den Ordner FPKG-Games (auf dem internen Speicher /data/FPKG-Games, auf einem USB- oder M.2-Laufwerk der Ordner FPKG-Games im Hauptordner), dort sucht ShadowMountPlus nicht!“ In der Spieleliste (Browser-App) und in der Bibliothek der Vollbild-App erscheint keine Konflikt-Anzeige mehr.
- **Aushängen nach dem Beenden:** Liegt ein Spiel bei Kraken Mount und bei ShadowMountPlus und wird beendet, hängt die App es bei ShadowMountPlus aus. Spiele, die nur bei einem der beiden liegen, und die Kacheln bleiben unberührt.

## Verbesserungen
- **Vollbild-App:** Die Prüfung auf eine neue NG-Fassung zeigt beim Drücken sofort „Prüfe bei GitHub …“, nimmt keinen zweiten Druck an, solange sie läuft, und meldet ohne Internet „Keine Internetverbindung: GitHub ist nicht erreichbar. Bitte die Netzwerkverbindung der PS5 prüfen.“ (auch in der Browser-App).
- **Browser-App:** Die Ergebnis-Meldung über den Spielkarten („Konvertieren fertig“, „Kopie fertig“ …) verschwindet nach 10 Sekunden; das Ergebnis steht weiter im Verlauf.
- Handbuch (Kapitel Spiele und Disc) in sieben Sprachen, neue Texte in sechs Sprachen.

## Hinweise
- Alles andere ist unverändert gegenüber v1.86.2. Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).
- Neu getestet am PC (Host-Test `test/fpkgdup`, 16 Prüfungen; TV-App 353 Tests). An der Konsole noch ungesehen: das Aushängen nach dem Beenden und das automatische Dumpen mit „Starten“.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.86.3.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG, Kraken Mount und Dump Forge sowie **drakmor** für ShadowMountPlus.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.86.2...v1.86.3
