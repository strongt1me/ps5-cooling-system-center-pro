# PS5 Cooling & System Center – Pro v1.70.0

## Highlights
- **Payloads herunterladen** – die Liste des Payload Managers von itsPLK direkt in der App: 28 Payloads (ShadowMountPlus, etaHEN, kstuff-lite, ftpsrv, elfldr, ps5upload, PKG-Manager …) herunterladen, aktualisieren, starten und löschen, jede Datei mit SHA-256 geprüft
- **Spiel-Updates** – neuer Reiter „Updates“: offizielle Updates für PS5- und PS4-Spiele laden und installieren, passend zur Firmware, mit Warteschlange, Fortsetzen und Prüfsummen (nach PatchDL von Knutwurst)
- **Flüssiger und stabiler** – Durchsicht des ganzen Projekts mit über 30 behobenen Fehlern; die Oberfläche zeichnet nur noch, was sichtbar ist, und lädt ihre Dateien aus dem Browser-Speicher
- **Nur noch eine Datei zum Senden** – Kachel-Installer und Kachel-Paket entfallen, die App legt ihre Kachel selbst an

## Neu
### Payloads herunterladen
- Neuer Bereich auf der Payloads-Seite mit Suche und Kategorien (Werkzeuge, Netzwerk & Server, System & Jailbreak, Lader)
- Quelle ist dieselbe Liste wie im Payload Manager 0.6.3 (`ps5-payloads-mirror`, jeden Tag aktualisiert)
- Jede Datei wird vor dem Speichern mit der SHA-256 der Liste verglichen; eine abweichende wird verworfen
- Updates werden erkannt („alt → neu“, „Aktualisieren“); geladene Payloads stehen auch unter „Gespeicherte Payloads“ und in den Profilen bereit
- Ins Internet geht die App erst nach „Liste laden“; danach höchstens alle drei Stunden beim Öffnen der Seite
- `.bin`-Payloads (etaHEN) lassen sich jetzt wie `.elf` starten

### Spiel-Updates
- Prüft jedes offiziell signierte Spiel (Disc, PlayStation Store); Spiele über ShadowMountPlus, Fake-PKGs und Homebrew bleiben unangetastet
- Bietet die neueste Version an, die auf der Firmware läuft; ältere Patches über einen SC-Link von prosperopatches.com / orbispatches.com (am Handy per QR-Code)
- Warteschlange, 1–16 Verbindungen, Anhalten und Fortsetzen (auch nach einem Neustart), Prüfsumme für jedes Stück
- Löscht das Paket, sobald das Spiel die neue Version meldet; ein vorhandenes Paket wird geprüft und weiterverwendet
- Eigene DNS-Anfragen (Cloudflare, Google, Quad9, DNS über HTTPS) – die DNS-Einstellungen der Konsole und eine Sperre gegen System-Updates bleiben, wie sie sind
- Aus ab Werk

### Spiele und LZ4 (nach exFAT2AMPR 1.104 von V-10)
- **Doppelte Abbilder:** Liegt ein Spiel mehrfach auf den Laufwerken, zeigt eine Karte alle Quellen; „Beiseitelegen“ nimmt eines aus dem Spiel
- **Dauer vorab:** Das Packfenster schätzt, wie lange Packen und Prüfen dauern, und lernt die Geschwindigkeit der Konsole
- **Startprüfung:** Warnung, wenn ein gepacktes Spiel ohne fakelib/APR-EMU starten würde
- Verknüpfungen im Spielordner werden vor dem Packen gemeldet statt still übergangen
- `*.rmdtoc` (Remedy, z. B. Alan Wake 2) bleibt immer lose

### Pakete und PS4 von USB
- Rückportier-Patches (`LIH`) werden erkannt und als Update gezeigt; neue Marke „SDK 5.xx“ (nach ps5-pkg-manager 1.5.1)
- PS4Mount 0.6.0: Schalter „Neue Spiele schneller anmelden“ (aus ab Werk)

## Verbesserungen
- Die Statusabfrage zeichnet jede Sekunde nur noch die sichtbare Seite und nur, was sich geändert hat; die Systemabfrage fragt Controller, Kanäle und Laufwerke nur auf der Systemseite
- Oberfläche, Skripte und Wörterbücher werden im Browser zwischengespeichert (ETag); vorher lud jeder Aufruf alles neu, das Wörterbuch allein 0,4–0,5 MB
- Spiel-Updates laden schneller: Schreiben in Blöcken von 1 MiB, 2 MiB Empfangspuffer je Verbindung (wie PatchDL 0.3.1)
- Fenster (Kopieren, Löschen, Dateimanager) mit dem Controller: Fokus im Fenster, Kreis-Taste schließt, L1/R1 blättert nicht mehr dahinter um
- Paketsuche fragt währenddessen nur die Liste ab; Löschen großer Spiele gibt laufend Rechenzeit ab; die Sicherungsliste misst Größen nicht mehr bei jeder Anfrage
- Starter-Kachel `PPSA86921`: meldet HDR und kein VRR (kein Bildwechsel mehr beim Start)
- Gebaut mit dem PS5-Payload-SDK v0.44

## Fehlerbehebungen
- Schlummernder Stapelüberlauf beim Lesen der Netzwerkdaten
- Zwei Abbild-, LZ4- oder AMPR-Aufträge konnten im selben Moment starten
- Fragmentierte exFAT-Abbilder wurden beim Prüfen und Entpacken extrem langsam gelesen
- Langsame Antworten von ShadowMountPlus blockierten andere Seiten bis zu 5,5 s
- Aufträge konnten nach Verschieben, Entpacken oder Löschen mit der alten Spieleliste geplant werden
- Das Ergebnis einer Installation konnte von einer gleichzeitig startenden überschrieben werden; die Warteschlange hielt dann an
- Der Lüfter-Thread konnte nach dem Ruhemodus eine laufende Kachel-Installation stören
- Fehlermeldungen bei zu großen Anfragen kamen als Verbindungsabbruch an; langsame Clients konnten alle Verbindungsplätze belegen
- Import der Einstellungen übernahm „Covers & Metadaten speichern“ erst später; Schritte von Payload-Profilen scheiterten, wenn gleichzeitig etwas kopiert wurde
- Der Löschen-Dialog konnte den Plan eines vorher geöffneten Spiels zeigen; der Reiter „Updates“ blieb nach einem Seitenwechsel stehen; die Verbindungsanzeige konnte nach einem Ausfall rot bleiben
- Ein Installationshelfer, der schon beim Start hing, blieb bei jedem neuen Versuch liegen
- Fehlende Speicherprüfung in PS4Mount; cJSON-Fix aus upstream (b2890c8)

## Hinweise
- **Dateien:** Es gibt keinen `cooling-center-launcher-installer` und kein Kachel-`.pkg` mehr. Die App legt ihre Kachel beim ersten Start selbst an (Systemseite → „Kachel installieren“, falls sie gelöscht wurde).
- **Starter-Kachel:** `PPSA86921.zip` enthält die App von v1.70.0 – den Ordner in `/data/homebrew/` ersetzen.
- **Internet:** nur für „Spiel-Updates“ (wenn eingeschaltet) und „Payloads herunterladen“ (nach „Liste laden“), sonst nie.
- **Firmware:** getestet auf einer PS5 Pro (CFI-7021) mit 12.00; Nutzer melden 13.60.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.70.0.elf` | die App – nach jedem Neustart der Konsole an Port 9021 senden |
| `PPSA86921.zip` | Starter-Kachel für `/data/homebrew/` (startet die App per Tipp) |
| `LIESMICH.txt` | Kurzanleitung |

## Danke
- **itsPLK** – Payload Manager und `ps5-payloads-mirror`, ps5-pkg-manager
- **Knutwurst** – PatchDL
- **V-10** – exFAT2AMPR
- **gasb07** – PS4Mount
- **drakmor** – ShadowMountPlus, ampr_emu, pgo_stub, dlc_emu
- **danthespal** – MkPFS.CSharp

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.69.1...v1.70.0
