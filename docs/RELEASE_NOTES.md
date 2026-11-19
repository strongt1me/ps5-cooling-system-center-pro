# PS5 Cooling & System Center – Pro v1.88.1

## Highlights
- **USB-Laufwerk sicher auswerfen – jetzt auch im Dateimanager:** Neben jedem USB-Laufwerk steht **Auswerfen** (Browser), in der Vollbild-App die Quadrat-Taste auf dem Laufwerk unter „Orte“ oder △ → Mehr in einem Ordner des Laufwerks. Ein Laufwerk im Betrieb einfach abzuziehen kann das Dateisystem beschädigen; so geht es sicher, ohne die Konsole herunterzufahren.
- **Mehrere Spielstände eines Spiels einspielen geht schneller:** Die Suche nach einem Spender für die Besitzerangaben läuft nur noch einmal je Auftrag, nicht für jeden Spielstand von vorn.
- **Die Vollbild-Kachel führt sich nie mit den echten Daten vor:** Eine Kachel, die schon mit den Daten der Konsole läuft, nimmt keine Anfrage für den Selbstlauf mehr an.

## Neu
### Dateimanager (Browser und Vollbild-App)
- **Auswerfen im Dateimanager:** Es ist dasselbe Auswerfen wie unter System → Speicher (über ShadowMountPlus ab 1.7beta10): nur wenn kein Auftrag der App läuft und PS4Mount keine Spiele von dem Laufwerk spielt; sonst sagt die App, was noch läuft. Erst abziehen, wenn die Konsole „… ist ausgeworfen und kann abgezogen werden“ meldet.
- **Danach räumt der Dateimanager auf:** Er liest die Orte neu und verlässt einen Ordner des Laufwerks (Browser: zur internen SSD; Vollbild-App: zu den Orten). Die Karte der Vollbild-App zeigt „Wird ausgeworfen …“ und danach das Ergebnis.

### Spielstände übertragen
- **Spendersuche einmal je Auftrag:** Beim Einspielen mehrerer Spielstände eines Titels wurden bis zu 8 Kopien je Spielstand eingehängt, um die Besitzerangaben zu finden; jetzt einmal je Auftrag. Ein Spielstand, der auf der Konsole schon einen eigenen hat, wird weiter einzeln gesucht.

### Vollbild-App
- **Selbstlauf nur mit Demodaten:** Der Selbstlauf der Kachel (für die Bilder in der Anleitung) drückt „Bestätigen“ auf Zeilen, die er in den Demodaten findet. Eine Kachel, die mit den echten Daten gestartet ist, ignoriert die Anfrage jetzt.

### Aufräumen
- Tote Zuweisungen in den Spielstand-Dateien entfernt, eine Absicherung vor einer leeren Liste ergänzt; ohne sichtbare Wirkung.

## Hinweise
- **Nicht an der Konsole erprobt:** das Auswerfen im Dateimanager (geprüft: 35 Host-Tests, 375 Tests der Vollbild-App, die Browser-Seite gegen einen nachgebauten Ersatz der App, die Vollbild-Seite als Bilder am PC). Das Auswerfen selbst ist seit 29.10.2026 unverändert und an der Konsole bestätigt.
- **An der Konsole bestätigt (Slim, Firmware 12.00, 19.11.2026):** die Wiederherstellung eines unterbrochenen Einspielens (beim Start und beim nächsten Einspielen) und die Ablehnung von Apps im Plan des Einspielens.
- Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein). Alles andere ist wie in v1.88.0.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.88.1.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: als Quelle `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG, Kraken Mount und Dump Forge, **drakmor** für ShadowMountPlus sowie **itsPLK** für den Payload Manager und **earthonion** für Garlic SaveMgr (Vorbild für das Format der ZIP-Datei). Und dem Nutzer, der das Auswerfen im Dateimanager vorgeschlagen hat.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.88.0...v1.88.1
