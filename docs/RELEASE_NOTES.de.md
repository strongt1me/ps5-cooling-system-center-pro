# PS5 Cooling & System Center – Pro v1.97.0

## Highlights
- **Dump Forge 0.6.0** ist eingebaut. Es packt LZ4 nur noch mit AMPR EMU NG; die Wahl „Laufzeit im Spiel“ im Konvertieren-Fenster gibt es deshalb nicht mehr (Web und Vollbild). Die Balken „Vermessen“ und „Schreiben“ nehmen die eigenen Zahlen von Dump Forge statt einer Schätzung.
- **Spielstände:** Beim Auslesen oder Einspielen konnte die ganze App enden, wenn eine Webseite oder die Vollbild-App im selben Moment eine Verbindung schloss. Behoben.
- **Lüfter, Betriebsart „Beobachten“ ohne feste Schwelle:** Hatte der Lüfterwächter volle Kühlung gehalten, blieb der Lüfter danach auf Höchstdrehzahl, bis die Konsole selbst umschaltete. Jetzt bekommt die Konsole ihre eigene Einstellung zurück, sobald der Wächter loslässt.

## Neu
- **Kraken Mount 0.4.1** ist eingebaut (installierte Spiele über ampr-emu-ng – ab Werk aus –, Spiel schließen, QR-Code). Es beantwortet jetzt auch die Schnittstelle von ShadowMountPlus auf 127.0.0.1:10101 – genau die Adresse, über die diese App mit ShadowMountPlus spricht. Weil die App beide nebeneinander betreibt, ist diese Schnittstelle in ihrer Kraken-Fassung auf Port 18097 verlegt; antwortet eine eigenständig gestartete Fassung auf 10101, erkennt die App das und schickt ihr keine Aufträge für ShadowMountPlus.
- **Lüfter:** Eine Rückgabe, die der Lüfterbaustein ablehnt, wird wiederholt; der Wächter kann nicht mehr für einige Sekunden überschrieben werden; beim Beenden schreibt er nichts mehr dazwischen; eine feste Schwelle folgt importierten Einstellungen sofort.
- **Suche im Dateimanager:** Nicht lesbare Ordner werden am Ende genannt; ein Auswerfen beendet die Suche mit Grund; ein `*` im Namen wird gefunden; ein Dump neben einem Ordner desselben Spiels wird über seinen Titel gefunden; lange Titel werden an ganzen Zeichen gekürzt.
- **Dateimanager:** Beim Zusammenführen in einen vorhandenen Ordner bleibt nach einem Abbruch keine halb geschriebene Datei unter ihrem echten Namen mehr liegen.
- **Aufträge:** Ein spätes „Abbrechen“ lässt „Original danach“ nicht mehr laufen; Packen und Entpacken im Spielordner prüfen vom Laufwerk statt aus dem Speicher.
- **NTFS:** Die Kopie der Dateitabelle wird vor dem Schreiben geprüft; ein Löschen, das mittendrin scheitert, und eine App, die beim Schreiben endet, lassen das Laufwerk als „zu prüfen“ markiert.
- **Suche in beiden Oberflächen:** „Abbrechen“ wirkt sofort sichtbar, Klicks gehen nicht mehr verloren, während Treffer kommen, die Löschfrage nennt den ganzen Pfad eines Treffers, und in der Vollbild-App wirken Kopieren/Ausschneiden/Löschen auf den Eintrag, für den das Menü geöffnet wurde.
- **Vollbild-Kachel:** Kommt eine neuere App vom PC, während die Kachel offen ist, startet die Kachel nicht mehr zwischendurch ihre eigene, ältere Kopie.
- 23 Meldungen in alle sechs Sprachen übersetzt.

## Hinweise
- **An der Konsole erprobt (Slim, FW 12.00):** Asterix & Obelix: Heroes als exFAT-Abbild mit LZ4-Packs (1,75 GB → 1,20 GB in 67 s, geprüft) sowie einen Spielstand auslesen, wieder einspielen und vergleichen (byte-gleich), während 38 Verbindungen mitten in der Antwort abgebrochen wurden – die App lief durch. Die Korrekturen nach diesem Test (siehe Changelog) sind nur am PC geprüft (46 Host-Tests mit Lüfter-Simulation, 396 Tests der Vollbild-App).
- **stb_image** in der Vollbild-App bleibt auf 2.30: Die Fassung 2.34 entschlüsselt progressive JPEGs falsch.
- Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein). Danach eine offene Weboberfläche einmal neu laden.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.97.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: den Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: den Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` als Quelle eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Besonders **quer3q** für Kraken Mount, Dump Forge und AMPR EMU NG, **abcde6543** für PS5-NTFS-BROWSER, **gasb07** für PS4Mount, **drakmor** für ShadowMountPlus, **itsPLK** für den Payload Manager und den Payload-Spiegel sowie **John Törnblom** und allen Beteiligten am PS5-Payload-SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.96.1...v1.97.0
