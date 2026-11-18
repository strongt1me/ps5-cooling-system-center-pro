# PS5 Cooling & System Center – Pro v1.86.6

## Highlights
- **„Original danach“ beim Konvertieren, Kopieren und Entpacken:** Nach dem Vorgang kann man das Original **behalten** (wie bisher), **löschen** oder **an einen Ort verschieben, den ShadowMountPlus nicht durchsucht**. So liegt derselbe Titel nicht mehr zweimal auf der Konsole, und ShadowMountPlus meldet ihn nicht doppelt. In Browser-App und Vollbild-App; Löschen fragt vorher und nennt den Namen.
- **Kraken Mount v0.3.0** ist eingebettet: Spielstände gehen nicht mehr verloren, wenn ein Spiel eine eigene Spielstand-Bibliothek im `fakelib` mitbringt, `.pkg`-Spiele bekommen AMPR EMU NG, ein noch kopiertes Spiel wird übersprungen, Titelmusik und Spielnamen in der Sprache der Konsole.
- **Neu bei GitHub:** Jedes Release trägt eine Liste für den **Payload Manager von itsPLK** (`payload-manager.json`); die App lässt sich so von dort laden, mit SHA-256-Prüfung.

## Neu
### Original danach
- **Die Auswahl** steht in den Fenstern „Konvertieren“, „Kopieren“ und „Entpacken“ (auch bei „Zur Warteschlange“) und in der Vollbild-App: **Behalten** (Vorgabe), **Löschen** oder **Verschieben**.
- **Verschieben:** Datenträger wählen (interne SSD, M.2-Erweiterung, USB; die Auswahl zeigt den freien Platz) und den Ordner; vorgeschlagen wird `<Laufwerk>/PS5-Sicherung/Spiele`, „Ordner wählen“ öffnet eine kleine Auswahl. Die App prüft den Ordner schon beim Eintippen und sperrt den Start, wenn **ShadowMountPlus dort suchen würde** (`homebrew`, `etaHEN/games`, bei USB und M.2 die Wurzel bis zwei Ebenen tief), wenn am Ziel schon etwas gleich heißt, der Platz nicht reicht, der Weg über eine Verknüpfung führt oder es ein Ordner der Konsole ist.
- **Sicher:** Gelöscht oder verschoben wird erst, **nachdem der Vorgang gut zu Ende ging** und sein Ergebnis da ist. Scheitert er oder wird er abgebrochen, bleibt das Original unberührt. Bei „Schnell“ als Prüfung löscht die App nie. **Verschieben** macht zuerst eine geprüfte Kopie (jede Datei zurückgelesen und verglichen, `.sha256` daneben) und löscht das Original erst danach; scheitert ein Schritt, bleibt es, wo es war. Die Spielstände werden nie angefasst.
- **Ablauf:** Die App wartet, bis nichts anderes mehr läuft; die Konvertier-Warteschlange startet den nächsten Eintrag erst, wenn das Original des vorigen erledigt ist. Das Ende kommt als Meldung ohne Ton, im Fenster des Vorgangs zeigt eine Zeile den Stand; der Verlauf führt das Löschen als „Original“.
- Beim Entpacken ersetzt die Auswahl den früheren Schalter „Abbild danach löschen“.
- Neue Schnittstelle (`docs/API.md`): `GET /api/v1/library/orig/plan`, `orig/check`, `orig/status` und die Felder `orig_after` und `orig_dest` an den Starts.

### Kraken Mount
- **Kraken Mount v0.3.0** (quer3q): Spielstände nutzen nicht mehr die Spielstand-Bibliothek aus dem `fakelib` eines Spiels (sie ließ Spielstände verschwinden); `.pkg`-Spiele bekommen immer AMPR EMU NG; dlc_emu und pgo_stub stecken in der Datei; ein Spiel, das noch kopiert wird, wird übersprungen; fehlt die Quelle eines Spiels 5 Minuten, nimmt Kraken Mount es vom Startbildschirm; Titelmusik und Spielnamen in der Sprache der Konsole.
- Eine geladene ältere Fassung nimmt die App nur noch, wenn sie **neuer** ist als die eingebettete.

### Abbilder
- Ein Abbild, das der Direktleser nicht kennt (zum Beispiel ein `.ffpfs`), wird nicht mehr alle 10 Minuten neu geöffnet: Ein Fehlschlag wird wie ein Erfolg nach Größe und Zeit der Datei gemerkt, höchstens 6 Stunden lang; „Prüfung anfordern“ liest alles neu. Das spart Schreibvorgänge und Protokollzeilen (vorher 490 Mal in drei Tagen).

### GitHub
- `payload-manager.json` als fünfte Datei jedes Releases: Name, Dateiname, Link zur ELF desselben Releases, Version, Kategorie, Beschreibung, Datum und SHA-256 (aus der gebauten Datei berechnet).

## Verbesserungen
- Das eigene Entpacken der App legt `param.json` und `param.sfo` zuletzt an (unter einem Hilfsnamen, am Ende umbenannt): Ein halb entpackter Ordner wird nie für ein Spiel gehalten. Reste einer früheren Sitzung räumt die App beim ersten Auftrag weg.
- Die Prüfung der Abbilder liest höchstens 40 Abbilder je Runde und prüft vor dem Aushängen noch einmal, dass der Titel nicht inzwischen läuft; eine Bremse schützt vor einer Schleife, wenn das kurze Einhängen scheitert.
- „USB auswerfen“ fragt bei Kraken-Spielen den Prozess, ob das Spiel läuft.
- `tools/release.sh` sucht vor dem Veröffentlichen nach Mustern persönlicher Daten und fragt nur noch einmal vor dem Push und dem Release; `tools/deploy.sh` sendet die Kacheln nicht, wenn die ELF nicht lief.
- Handbuch in sieben Sprachen (neuer Abschnitt „Original danach“, Kraken Mount 0.3.0), Übersetzungen der neuen Texte in sechs Sprachen.

## Fehlerbehebungen
- **Prüfrunde 18.11.2026 (REA, Analysator, vier Prüfer mit Gegenprüfung):** 14 Funde behoben, unter anderem eine Spieleliste, die bei sehr vielen Spielen zu früh abschnitt, ein Auswerfen, das ein laufendes Kraken-Spiel nicht erkannte, ein Dump-Forge-„Beenden“, das bei einem stummen Programm hing, eine Auswahlliste im Browser, die bei jedem Zeichnen neu entstand, und zwanzig kleine Fehler des Analysators (Rückgabewerte, Nullzeiger, Grenzen).
- **Sicherheit der neuen Funktion (unabhängiger Review, 10 Funde):** „Schnell“ löscht nie; ein entpackter Ordner muss das Spiel enthalten (`sce_sys/param.json`); eine Kopie, die Verknüpfungen oder Sonderdateien ausgelassen hat, gilt nicht als vollständig; Verschieben kopiert genau den Pfad des Originals; die interne SSD funktioniert als Ziel; Groß- und Kleinschreibung zählt bei den Suchordnern von ShadowMountPlus nicht (exFAT/FAT); Ordner der Konsole sind als Ziel gesperrt; was die Konsole selbst installiert hat, löscht die App nie; ein nicht antwortendes ShadowMountPlus gilt nicht als „kennt den Titel nicht“.

## Hinweise
- **An der Konsole geprüft** (mit zwei echten PS5-Spielen, jedes Mal danach zurückgesetzt): Konvertieren + Verschieben, Kopieren + Löschen, Entpacken über ShadowMountPlus + Abbild verschieben, Abbrechen (das Original bleibt), die Warteschlange mit zwei Einträgen und die Ablehnung falscher Ziele; dazu Kraken Mount v0.3.0 mit Spielstand speichern und laden. **Nicht geprüft:** ein USB- oder M.2-Laufwerk als Ziel, die Bedienung der Vollbild-App mit dem Controller, ein Entpacken, bei dem ShadowMountPlus das Abmelden ablehnt.
- Ein **PFS-Abbild** (`.ffpfs`) lässt sich nicht entpacken: ShadowMountPlus meldet „Permission denied“, und das eigene Entpacken der App kennt das Format nicht. Das Original bleibt dann liegen, auch wenn „Löschen“ gewählt war.
- Beim ersten Start nach dem Wechsel auf Kraken Mount v0.3.0 setzt es den `fakelib` **jedes** Spiels einmal neu zusammen; das dauert und braucht freien Platz. Eine ältere laufende Fassung wird erst nach Aus- und Einschalten ersetzt.
- Ob der Payload Manager der Weiterleitung von `latest/download` bei einer Quellliste folgt, ist nicht belegt.
- Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.86.6.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel (mit Dump Forge): Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: als Quelle `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für AMPR EMU NG, Kraken Mount und Dump Forge, **drakmor** für ShadowMountPlus sowie **itsPLK** für den Payload Manager.
