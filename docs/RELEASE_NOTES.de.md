# PS5 Cooling & System Center – Pro v1.96.0

## Highlights
- **Suche im Dateimanager:** Ein Suchfeld über der Liste filtert den offenen Ordner schon beim Tippen. Mit „Auch Unterordner“ sucht die Konsole im Ordner und in allem darunter – vom Laufwerk aus auf dem ganzen Laufwerk (Konsole, USB, M.2 und NTFS).
- **Spieltitel neben Titel-IDs:** Ordner und Abbilder, die nach einer Titel-ID heißen (`PPSA01325`, `PPSA01325.exfat`, `CUSA05041-app`), zeigen den Titel des Spiels, und die Suche findet sie auch darüber.
- **Kraken Mount 0.4.0 (quer3q):** FPKGMount bringt die neue Fassung mit: neu gestaltete Seite, FPKG-Spiele auf die Konsole installieren, Backports auch für installierte Spiele.

## Neu
### Dateimanager: Suche
- **Teil des Namens**, Groß- und Kleinschreibung egal (auch bei Ä/Ö/Ü). Mit `*` und `?` muss der ganze Name passen (`*.pkg`, `PPSA?????`).
- **Auch Unterordner:** Die Treffer kommen nach und nach, jeweils mit ihrem Ordner. Höchstens 2.000 Treffer und 1 Million geprüfte Einträge; Verknüpfungen werden nicht verfolgt, ein anderes Laufwerk darunter nicht betreten, das ganze System (`/`) nicht durchsucht. Die Suche lässt sich abbrechen; Auswerfen beendet sie vorher.
- **Mit den Treffern arbeiten:** Auswählen, Kopieren, Verschieben, Löschen (fragt wie immer zuerst), Umbenennen, Ansehen und Größe messen gehen wie bei normalen Einträgen. „Ordner öffnen“ springt in den Ordner des Treffers und hebt ihn hervor.
- **Vollbild-App:** △ → „Suchen …“ (Bildschirmtastatur), „Auch Unterordner: an/aus“, „Suche ändern …“ und „Suche beenden“; X öffnet einen Treffer, O beendet die Suche.

### Dateimanager: Spieltitel
- Ein Ordner nennt den Titel aus seiner eigenen `sce_sys/param.json` (PS5) bzw. `param.sfo` (PS4), sonst aus den installierten und angemeldeten Spielen der Konsole. Abbilder werden dafür nicht eingehängt; auf NTFS-Laufwerken zählen nur die Spiele der Konsole.
- Browser: Der Titel steht unter dem Namen. Vollbild-App: hinter dem Namen und als Zeile „Spiel“.

### Kraken Mount 0.4.0
- Beide Ein-Byte-Eingriffe der App greifen weiter: Kraken Mount wartet nicht auf ShadowMountPlus, und seine eigene Update-Abfrage bleibt aus (neue Fassungen kommen weiter über den Knopf der App). Mitgeliefert ist weiter ampr-emu-ng 0.5.1.
- **Installierte Spiele bleiben aus dem Reiter FPKGMount:** Kraken Mount 0.4.0 listet auch die auf der Konsole installierten Spiele. Die App lässt sie aus ihrem Reiter, der Warnung zu doppelten Titeln, der Marke FPKGMount und der Prüfung vor dem Löschen heraus – sie stehen wie immer unter Spiele.
- Ausschalten wird abgelehnt, solange Kraken Mount ein Spiel installiert; die App sagt das jetzt.
- Neu auf der eigenen Seite von Kraken Mount (Port 8097): FPKG-Spiele samt Patches und DLC installieren, Backports (`backports/<Titel-ID>`) auch für installierte Spiele, helles und dunkles Design mit Farbschemata und Sprachwahl, Titelmusik der Kacheln bleibt nach einem Spiel.

## Hinweise
- **An der Konsole erprobt:** Dieser Stand läuft auf der Pro und der Slim (mit der Versionsnummer 1.95.1, gleicher Code). Suche und Spieltitel wurden im Browser an der Pro ausprobiert; Kraken Mount 0.4.0 läuft auf beiden, und beide lassen installierte Spiele aus dem Reiter.
- **Backports neben ShadowMountPlus:** An der Slim haben Kraken Mount 0.4.0 und ShadowMountPlus 1.7beta12 denselben Test-Backport eines installierten Spiels gleichzeitig angewendet: Das Spiel startete und lief, beide gaben ihre Umleitung beim Schließen frei, kein Absturz.
- **Nicht von Hand an einer Konsole ausprobiert:** die Suche in der Vollbild-App mit dem Controller (nur PC-Bilder).
- Alle 46 Host-Tests, 396 Tests der Vollbild-App und die Wörterbuchprüfung bestehen. Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein). Ein laufendes Kraken Mount behält seine alte Fassung, bis es einmal aus- und eingeschaltet wird.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.96.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: den Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: den Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` als Quelle eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für Kraken Mount, Dump Forge und AMPR EMU NG, **abcde6543** für PS5-NTFS-BROWSER, **gasb07** für PS4Mount, **drakmor** für ShadowMountPlus, **itsPLK** für den Payload Manager und den Payload-Spiegel sowie **John Törnblom** und allen Beteiligten am PS5-Payload-SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.95.1...v1.96.0
