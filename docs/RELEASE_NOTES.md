# PS5 Cooling & System Center – Pro v1.75.7

## Highlights
- **`app.log` zeigt jetzt, wie die App geendet hat** – jede Ausführung schließt mit einer Zeile `===== … beendet: <Grund> =====`, und fehlt sie, sagt der nächste Start das

## Neu
- Vor dem Ende schreibt die App alles, was noch nicht in der Datei steht, sofort hinein und setzt dann die Endzeile.
- Gründe: „Konsole wird ausgeschaltet“, „Konsole startet neu“, „Konsole startet in den abgesicherten Modus neu“ (über die Power-Optionen der App), „vom System beendet“ und „abgestürzt (Signal N)“.
- Lehnt die Konsole das Ausschalten oder den Neustart ab, folgt `===== … läuft weiter =====` und das Protokoll geht weiter.
- Vor dem Ruhemodus schreibt die App alles Offene sofort in die Datei.
- Endete die vorige Ausführung ohne Endzeile (Neustart über das Menü der Konsole, Strom weg, neu gesendete ELF), steht vor der neuen Startzeile ein Hinweis darauf.
- Das Handbuch beschreibt die Endzeile in allen sieben Sprachen.

## Hinweise
- An der Konsole geprüft: Neustart über die App hinterlässt „beendet: Konsole startet neu“, eine neu gesendete ELF den Hinweis auf die fehlende Endzeile.
- Die Kacheln FAKE86921 (Vollbild) und FAKE86922 (Browser) aus diesem Release neu kopieren.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.75.7.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.75.6...v1.75.7
