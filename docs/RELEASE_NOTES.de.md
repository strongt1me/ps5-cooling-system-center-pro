# PS5 Cooling & System Center – Pro v1.92.0

## Highlights
- **Ein beschädigtes Startbildschirm-Bild wird vor dem Konvertieren gemeldet:** Die Prüfung vor dem Konvertieren (Browser und Vollbild-App, alle Formate) liest jetzt `icon0.png`, `pic0.png`, `pic1.png` und `pic2.png` aus dem Ordner oder dem Abbild und nennt ein Bild, das kein gültiges PNG ist, in Gelb. Starten und Einreihen bleiben möglich; das Bild wird unverändert übernommen. An der Slim mit sieben echten Spielen erprobt: keine einzige falsche Warnung.
- **Durchsicht der ganzen App, Browser und Vollbild:** Statische Analyse über alle 125 Quelldateien, drei Prüfer und ein lesender Durchgang über jede Route an der Slim. Gefunden und behoben wurden ein Weg, die Bild-Prüfung Minuten bis Stunden zu blockieren, Lücken beim Löschen von PS4Mount-Spielen und Zusatzinhalte, die beim Löschen eines installierten Spiels nicht mit ihrem Namen entfernt wurden.
- **Vollbild-App: großgeschriebene Überschriften werden jetzt übersetzt.** Schritte wie „3. WOHIN?“ und Fenstertitel wie „kopieren“, „verschieben“ und „löschen“ blieben in jeder Sprache deutsch; die Vollbild-App übersetzt jetzt zuerst und schreibt danach groß.

## Neu
### Konvertieren
- **Bild-Prüfung:** Die Warnung lautet „Kein gültiges PNG: sce_sys/pic2.png“, darunter „Das Bild wird unverändert übernommen und kann auf dem Startbildschirm fehlen.“ und, wenn eine `.dds` gleichen Namens daneben liegt, auch diese Datei. Ein fehlendes Bild wird nicht genannt. Geprüft werden der PNG-Anfang und jeder Abschnitt mit Länge, Typ und CRC-32 bis `IEND`; Bilder über 64 MiB werden nicht beurteilt. Gelesen wird durch ein Fenster von 1 MiB, darum ist selbst ein Bild aus 100 000 winzigen Abschnitten in einem Augenblick geprüft (vorher hätte so ein Bild die Prüfung Minuten bis Stunden halten können, und das Laufwerk ließ sich so lange nicht auswerfen). Bei einem Ordner wird ein Bild nur geöffnet, wenn es eine gewöhnliche Datei ist, ohne Verknüpfungen zu folgen und ohne zu warten.

### Spiele
- **Das Löschen eines installierten Spiels nimmt seine Zusatzinhalte mit:** Das Hilfsprogramm, das die Konsole zum Deinstallieren bittet, nennt jetzt jeden Zusatzinhalt des Spiels beim Namen, wenn es ihn entfernen lässt (die Konsole braucht den Namen; der alte Aufruf gab keinen an). Das ist an den Systembibliotheken von 40 Firmware-Fassungen von 1.00 bis 12.70 belegt.
- **PS4Mount-Spiel löschen, bei ausgeschaltetem oder beschäftigtem PS4Mount:** Das Löschen bereitet den Installationsdienst der Konsole selbst vor, bevor es etwas aushängt, lehnt ab, solange ein eigenständiges PS4Mount läuft, und versucht es bis zu 2 Minuten weiter, solange die Konsole noch deinstalliert. Zusatzinhalte mit eigener Titel-ID werden bei ausgeschaltetem PS4Mount mit ausgehängt, außer ein anderes eingehängtes Spiel nutzt denselben. Der Plan sagt genau, was geschieht: „die Updates und Zusatzinhalte, die PS4Mount dafür eingehängt hat“.
- **Kopieren und Verschieben:** Ein gemerkter Plan gilt nur für denselben Ordner; ein anderes Laufwerk unter demselben Namen wird neu vermessen.

### Browser
- **Auswerfen:** Die Seite fragt nur noch eine Sache zur Zeit, und ein verspätetes „läuft noch“ überschreibt „ist ausgeworfen“ nicht mehr.
- **Reiter Disc:** Die Merkmale eines Dumps („entschlüsselt, FSELF“) sind mit „·“ getrennt, jedes wird einzeln übersetzt.

### Vollbild-App
- **Übersetzungen:** Großgeschriebene Überschriften und Fenstertitel (siehe Highlights), die Controller-Zeile im Protokoll, die Kopfzeile eines laufenden Payload-Profils (sie las sich „LÄUFT: AUTOSTART (SCHRITT 1 OF 3)“), „Spielstand“ beim Einspielen einer ZIP und der neue Punkt im Plan sind in allen sechs Sprachen übersetzt. Die allgemeinen Muster „in {0}“ und „{0} in {1}“ sind aus den Wörterbüchern entfernt, weil sie jeden anderen Satz mit einem „in“ halb deutsch ließen; die drei Texte, die sie brauchten, zeigen jetzt Sätze, die die Wörterbücher ganz kennen.

## Hinweise
- **An der Konsole erprobt** (Slim, Firmware 12.00): alle 80 lesenden Routen und 31 Fehl-Anfragen ohne Problem, App-Protokoll und Kernel-Log sauber; die Bild-Prüfung und der Plan für sieben echte Spiele (zwei Ordner, ein exFAT-, drei ffpfsc- und ein ffpkg-Abbild); der Löschplan eines PS4Mount-Spiels; die Controller-Zeile und die Disc-Merkmale auf Englisch.
- **Nicht erprobt:** das Löschen eines installierten Spiels mit Zusatzinhalten (auf der Slim ist keins installiert; die Korrektur stützt sich auf die Firmware-Auswertung) und ein wirklich beschädigtes Bild (das deckt der Host-Test ab).
- 389 Tests der Vollbild-App, 44 Host-Tests und die Simulation bestehen. Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.92.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung (Deutsch) |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` als Quelle eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Dank
An alle, deren Arbeit in der App steckt oder sie möglich macht – die ganze Liste steht in der App unter „Credits“ oder Profil → Dank. Besonders **gasb07** für PS4Mount, **quer3q** für Dump Forge, AMPR EMU NG und Kraken Mount, **drakmor** für ShadowMountPlus, **itsPLK** für den Payload Manager und den Payload-Spiegel sowie **John Törnblom** und den Mitwirkenden am PS5-Payload-SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.91.0...v1.92.0
