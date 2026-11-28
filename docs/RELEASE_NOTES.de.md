# PS5 Cooling & System Center – Pro v1.99.0

## Highlights
- **Kraken Mount spielt die Spiele von ShadowMountPlus, solange ShadowMountPlus nicht läuft.** Ist ShadowMountPlus nicht geladen (zum Beispiel nach einem Neustart), gibt die App Kraken Mount nach einer Minute die Ordner, die ShadowMountPlus durchsucht: `homebrew` und `etaHEN/games` (intern und auf jedem Laufwerk) sowie den Hauptordner jedes Laufwerks. Kraken Mount meldet die Spiele dort an und spielt sie: Ordner, Abbilder (`.exfat`, `.ffpkg`, `.ffpfs`, `.ffpfsc`) und `.pkg`-Dateien. Startet ShadowMountPlus, gibt die App die Ordner nach wenigen Sekunden zurück; Kraken Mount gibt seine Einhängungen und Abbilder selbst frei.
- **Ein Lämpchen im Reiter KrakenMount** (Browser und Vollbild) zeigt auf einen Blick, was gilt: grün und pulsierend = Kraken Mount hat die Ordner, gelb blinkend = es übernimmt sie gleich oder gibt sie gerade zurück, blau = ShadowMountPlus hat sie, grau = von Hand geänderte Ordnerliste.
- **„Löschen“ in der Spieleliste löscht ein Spiel von Kraken Mount jetzt wirklich.** Es lief als installiertes Spiel: Die Konsole entfernte nur die Kachel, die Daten blieben, und Kraken Mount meldete das Spiel beim nächsten Suchlauf wieder an. Jetzt löscht Kraken Mount es selbst (aushängen, Daten, Kachel, Zwischenspeicher, Prüfsummen-Datei).

## Neu
- Für das Umschalten muss Kraken Mount nicht neu starten. Jeder Start von Kraken Mount durch die App beginnt ohne die Ordner von ShadowMountPlus, damit ein ShadowMountPlus aus dem Autostart beim Einschalten der Konsole Zeit hat. Eine auf der Seite von Kraken Mount von Hand geänderte Ordnerliste bleibt unberührt.
- ShadowMountPlus gilt als laufend, wenn sein Prozess läuft oder es in der letzten Minute auf seinem Port geantwortet hat. Klappt das Umstellen einmal nicht, versucht die App es nach 5 s wieder.
- Solange Kraken Mount die Ordner hat, zeigt die Spieleliste bei diesen Spielen den echten Pfad und das Format (keine toten `/mnt/shadowmnt`-Pfade mehr); Kopieren und Konvertieren gehen. Die eigenen Kacheln der App stehen nie im Reiter KrakenMount.
- „Original danach“: Löschen läuft über Kraken Mount, wenn es das Original spielt; Verschieben eines solchen Originals wird gleich abgelehnt. `Kraken-Games` ist kein Ziel mehr für das Verschieben eines Originals.
- „Abbild beiseitelegen“ wird abgelehnt, solange Kraken Mount das Abbild eingehängt hat. Der Reiter Pakete nennt den echten Grund, wenn Kraken Mount eine `.pkg` ohne Installation spielt. Die Doppelt-Warnung zählt die Übergabe zwischen beiden nicht als Konflikt.
- Ablehnungen beim Löschen nennen den richtigen Grund: Kraken Mount aus, beschäftigt oder das Spiel noch nicht in seiner Liste. Lange Pfade werden nicht mehr gekürzt.
- Übersetzungen in sechs Sprachen, Handbuch in sieben Sprachen.
- **Handbuch und FAQ kürzer** in allen sieben Sprachen (rund 40 % kürzer, 45 statt 62 Fragen), gegen den Code geprüft; `docs/HANDBUCH.md` neu gegliedert und halb so lang; READMEs, LIESMICH und Entwickler-Dokumente aktuell.
- **Credits** (Browser und Vollbild): ps5-app-dumper (Echo Stretch und slopmaster33), idlesauce (ps5-self-pager), Daniel Nylander (schwedische Übersetzung), OffAct, np-fake-signin, Dump Forge, PS4FFPFSC und Transmission ergänzt.

## Hinweise
- **An der Konsole erprobt:**
  - An der Pro nach einem Neustart ohne ShadowMountPlus: Umschalten nach 60 s, 14 Spiele bereit ohne Konflikte, Spiele über Kraken Mount gestartet, Spieleliste mit echten Pfaden, Rückgabe an ShadowMountPlus in 2 s.
  - Mit einer 6-GB-Testkopie: Verschieben vor dem Start abgelehnt, Original nach dem Konvertieren über Kraken Mount gelöscht, „Abbild beiseitelegen“ abgelehnt, Kopie über die Spieleliste gelöscht.
  - An der Slim: Löschen über Kraken Mount (Testkopie 1,74 GB).
- **Am PC geprüft:** 47 Host-Tests mit Lüfter-Simulation, 396 Tests der Vollbild-App.
- Eine `.pkg` im Hauptordner eines Laufwerks spielt Kraken Mount ohne Installation, solange ShadowMountPlus nicht läuft; soll sie auch mit ShadowMountPlus spielbar bleiben, gehört sie nach `Kraken-Games`.
- Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein). Danach eine offene Weboberfläche einmal neu laden.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.99.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: den Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: den Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` als Quelle eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für Kraken Mount, Dump Forge und AMPR EMU NG, **drakmor** für ShadowMountPlus, **abcde6543** für PS5-NTFS-BROWSER, **gasb07** für PS4Mount, **itsPLK** für den Payload Manager und den Payload-Spiegel sowie **John Törnblom** und allen Beteiligten am PS5-Payload-SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.98.0...v1.99.0
