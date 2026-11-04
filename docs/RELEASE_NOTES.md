# PS5 Cooling & System Center – Pro v1.76.2

## Highlights
- **Wichtige Fehlerbehebung:** „Alles zurücksetzen“ bei der Fake-Anmeldung löschte den Namen des Offline-Kontos mit und brachte so Programme wie den Y2JB durcheinander. Das ist behoben.

## Fehlerbehebungen

### „Alles zurücksetzen“ löschte den Namen des Offline-Kontos (schwerwiegend)
Vier der Registrierungsschlüssel, die die Fake-Anmeldung beim Zurücksetzen leerte, sind auf der Konsole dieselben wie Name, Konto-ID, Kennzeichen und Art des **Offline-Kontos** – beide Funktionen teilen sich denselben Platz 1. „Alles zurücksetzen“ nahm sie deshalb mit, auch wenn das Offline-Konto weiter gebraucht wurde. Nach einem Neustart hatte die Konsole dann keinen Namen mehr für das Konto, und Programme, die eine vollständige Kontokennung erwarten (etwa der Y2JB), liefen nicht mehr richtig.

Der Knopf heißt jetzt **„Fake-Anmeldung zurücksetzen“** (Browser und Vollbild-App) und lässt Name, Konto-ID, Kennzeichen und Art des Offline-Kontos unangetastet; gelöscht werden nur `auth.dat`, `config.dat` und die Registrierungsschlüssel, die ausschließlich die Fake-Anmeldung betreffen.

**Wer schon „Alles zurücksetzen“ gedrückt hat:** Unter „Profil“ → „Offline-Konto aktivieren“ den Namen erneut eingeben – das setzt die Kontokennung wieder.

## Hinweise
- Dieses Release ersetzt v1.76.1, das den Fehler noch enthielt. Bitte auf v1.76.2 aktualisieren.
- Die Kacheln FAKE86921 (Vollbild) und FAKE86922 (Browser) aus diesem Release neu kopieren.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.76.2.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.76.1...v1.76.2
