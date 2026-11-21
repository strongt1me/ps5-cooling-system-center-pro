# PS5 Cooling & System Center – Pro v1.90.0

## Highlights
- **USB-Laufwerke sicher auswerfen:** Die Konsole trennt ein Laufwerk mit Gewalt und fragt niemanden. Die App macht das Laufwerk vorher frei: Alles, was von ihr darauf liest, wartet oder wird abgelehnt, alles wird aufs Laufwerk geschrieben (sync), erst dann wird ShadowMountPlus um das Auswerfen gebeten. Währenddessen steht „usb0 wird ausgeworfen … Bitte noch nicht abziehen, bis „ausgeworfen“ erscheint.“ An der Slim und der Pro erprobt, aus dem Dateimanager und aus der Vollbild-App, ohne Absturz.
- **Dump Forge ist in die App eingebaut:** Das Format FPKG und das Packen beim Konvertieren brauchen keinen Download von GitHub mehr. Ein Knopf holt weiter eine neuere Fassung, und selbst abgelegte Dateien werden geprüft (SHA-256, abgebrochene Downloads werden abgelehnt).
- **Die ganze App durchgesehen (Browser und Vollbild):** zwei Speicherfehler in Dateilesern behoben, die Weboberfläche nimmt nur noch die IP-Adresse der Konsole und lokale Namen an, eine fremde Webseite kann die API nicht mehr auslösen, 168 Meldungen und 97 Vollbild-Texte in sechs Sprachen übersetzt.
- **Seriennummer gekürzt oder ganz:** Ein Knopf auf der Systemseite (in der Vollbild-App die X-Taste) schaltet zwischen `••••8023` und der ganzen Nummer um; die Wahl steht auf der Konsole.

## Neu
### USB-Laufwerke
- **Auswerfen:** Der Dateimanager verlässt zuerst das Laufwerk und wirft über System → Speicher aus; Kopieren, Verschieben, Löschen, Speichern, Hochladen und jede Planung gehen durch ein Laufwerks-Tor und werden abgelehnt, solange ein Laufwerk ausgeworfen wird oder zur Ruhe kommt (30 s). Ist die Konsole oder die App beim Auswerfen abgestürzt, bleibt das Auswerfen aus, bis du es wieder erlaubst (Browser und Vollbild).
- **„Noch nicht abziehen“:** Die Meldung bleibt stehen, bis das Auswerfen fertig ist (an den Testkonsolen 2 bis 7 Sekunden); „ausgeworfen“ erscheint erst, wenn die Konsole das Laufwerk ausgehängt hat. Manche Laufwerke kommen nach einigen Sekunden von selbst wieder, andere erst nach erneutem Einstecken.
- **Nach dem Auswerfen:** kein Gespenst „usb0 · 2 MB frei“ mehr; ein wieder eingestecktes Laufwerk erscheint ohne Seitenwechsel; in den leeren Ordner eines ausgeworfenen Laufwerks (`/mnt/usb0`, er liegt auf der kleinen Systempartition) lässt sich nichts mehr schreiben.

### Spiele und Konvertieren
- **Dump Forge 0.5.6 steckt in der App** (die ELF wächst auf rund 24 MB); die Kacheln bringen keine eigene Kopie mehr mit. Knopf „Neue Fassung von GitHub laden“ im Konvertieren-Fenster.
- **Fortschritt beim Vermessen** für die Packs: Balken, Prozent und Restzeit statt eines stehenden Texts.
- **AMPR EMU NG 0.5.0** (die zwei neuesten Fassungen sind eingebettet: 0.5.0 und 0.4.1) und **Kraken Mount 0.3.1** mit dem **PlayGo-Stub je Spiel** (Schalter in der Spieleliste von FPKGMount, Browser und Vollbild).
- **PS4Mount und Kraken Mount sind ab Werk aus;** wer eines einschaltet, bei dem bleibt es nach einem Neustart an (an der Slim erprobt). PS4Mount 0.8.0 an der Slim mit echten PS4-Paketen erprobt: fünf Spiele von einem USB-Stick angemeldet, eines gestartet und wieder beendet.

### Payloads
- **Zweite Liste unter „Payloads herunterladen“:** `strongt1me/ps5-payload-source` neben dem Spiegel von itsPLK (Browser und Vollbild). Jede Datei wird weiter gegen die SHA-256 aus ihrer eigenen Liste geprüft.

### System
- **Seriennummer-Schalter** (siehe oben); die ganze Nummer verlässt die App nur, wenn sie gewählt ist.
- **Controller-Akku:** Die Vollbild-App zeigt wie der Browser Zustand und Alter des Werts („80 % · im Akkubetrieb · Stand vor 9 Std.“); der Hinweis „Controller kurz aus- und einschalten für einen aktuellen Wert“ ist jetzt übersetzt.
- **Browser:** Die Versions-Kachel ist orange.

### Sicherheit und Stabilität
- **Zwei Speicherfehler behoben** (mit AddressSanitizer nachgestellt): Ein manipulierter `ampr_zstd.index` und eine manipulierte `playgo-chunk.dat` in einem PS4-Paket konnten die App außerhalb ihrer Puffer lesen lassen (beim zweiten stürzte sie ab). Dazu Prüfungen der PFS-Blocktabelle und der PS4-Paketgrößen, zwei 32-KiB-Puffer vom Stack der Aufträge genommen.
- **Adressen:** nur noch IP-Adresse, `localhost` oder ein privater Heimnetz-Name (`.local`, `.lan`, `.home`, `.home.arpa`, `.internal`, `.localdomain`, `.intranet`). Namen ohne Punkt und `.fritz.box` werden abgelehnt (DNS-Rebinding).
- **Fremde Webseiten** (`Sec-Fetch-Site` cross-site/same-site) können die API nicht aufrufen; Downloads öffnen nur gewöhnliche Dateien; `nosniff`; Hochladen nur per POST; ein paar Verbindungen bleiben für die Konsole selbst frei.
- **SQLite 3.54.1** (bisher 3.46.0), gebraucht zum Eintragen eines eingespielten Spielstands.

### Handbuch, FAQ, Übersetzungen
- Handbuch und FAQ gegen den Code geprüft und in sieben Sprachen berichtigt (FAQ jetzt 60 Fragen); neue Bildschirmfotos in allen sieben Sprachen.
- Übersetzungen: 168 Server-Meldungen und 97 Vollbild-Texte in sechs Sprachen; großgeschriebene Überschriften der Vollbild-App zeigen Umlaute und Kyrillisch richtig; „(Spielregel)“ unter dem Temperaturring ist übersetzt.

## Hinweise
- **Adressen geändert:** Wer die Weboberfläche über `ps5.fritz.box` oder einen Namen ohne Punkt aufgerufen hat, nimmt jetzt die IP-Adresse der Konsole.
- **PS4Mount und Kraken Mount nach dem Update:** Wer sie nie geschaltet hat, sieht beide aus – bei Bedarf einmal einschalten. Kraken Mount muss auch dann einmal neu eingeschaltet werden, wenn es vorher lief (bisher wurde nur „aus“ gespeichert).
- **An der Konsole erprobt** (Slim, Firmware 12.00): Auswerfen aus dem Dateimanager (auch richtig abgelehnt, solange noch vom Stick gelesen wurde), die Meldung „noch nicht abziehen“, das Auswerfen aus der Vollbild-App, die Akku-Zeile, 80 lesende Abfragen und 31 fehlerhafte Anfragen ohne Absturz, alle Browser-Seiten auf Englisch. Die Pro (Firmware 12.00) nur beim Auswerfen, mit einem früheren Stand.
- **Nicht belegt:** ob damit der Absturz beim Auswerfen weg ist, den ein Nutzer mit Firmware 4.03 hatte (noch keine Rückmeldung); das Schreiben in `savedata.db` mit dem neuen SQLite ist nur am PC geprüft; Updates und Zusatzinhalte von PS4Mount sind an der Konsole nicht erprobt.
- 46 Host-Testläufe und 387 Tests der Vollbild-App bestehen. Die neue ELF an Port 9021 senden und die Kacheln FAKE86921 und FAKE86922 aus diesem Release neu kopieren (die Vollbild-App muss dafür geschlossen sein).

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.90.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung (Deutsch) |
| `payload-manager.json` | Liste für den Payload Manager von itsPLK: `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` als Quelle eintragen, dann lädt er die App von GitHub (mit SHA-256-Prüfung) |

## Dank
An alle, deren Arbeit in der App steckt oder sie möglich macht – die ganze Liste steht in der App unter „Credits“ oder Profil → Dank. Besonders **quer3q** für Dump Forge (jetzt in der App), AMPR EMU NG und Kraken Mount, **drakmor** für ShadowMountPlus (es wirft die Laufwerke aus), **gasb07** für PS4Mount, **itsPLK** für den Payload Manager und den Payload-Spiegel, den Autoren von **SQLite** sowie **John Törnblom** und den Mitwirkenden am PS5-Payload-SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.89.0...v1.90.0
