---
name: vollbild-app
description: Neue Funktionen der PS5-App in der Vollbild-App (Kachel FAKE86921, tvapp/ auf ps5-homebrew-ui) bauen – und vorher fragen, ob die Funktion in die Browser-Oberfläche (web/), in die Vollbild-App oder in beide gehört. Zu benutzen bei jeder neuen Funktion, Seite, Anzeige oder Einstellung („bau …“, „füge … hinzu“, „ich hätte gern …“), bei „Vollbild“, „Kachel“, „tvapp“, „Controller-Bedienung“ und bei Fehlern in der Vollbild-App.
---

# Vollbild-App bauen

Antworten auf Deutsch, Commit-Nachrichten englisch. Arbeitsordner `PS5_Temperature_Manager_src`.

## Grundsatz: beide Oberflächen können dasselbe

Vorgabe des Users vom 27.10.2026: **Alles, was die Browser-App kann, soll auch die Vollbild-App können.** Jede neue Funktion daher in beide Oberflächen bauen:

- **Browser-Oberfläche** (`web/app.js`, `web/index.html`, Sprachdateien `web/lang/*.json`): am Handy/PC und über die Browser-Kachel FAKE86922.
- **Vollbild-App** (`tvapp/`): auf dem Fernseher mit Controller, Kachel FAKE86921.

Nicht mehr „Browser oder Vollbild?“ fragen. Nur nachfragen, wenn etwas mit dem Controller wirklich nicht geht (z. B. Dateien vom PC hochladen) – dann eine Vollbild-Variante vorschlagen (Hinweis/QR-Code auf die Browser-Seite, Bildschirmtastatur des Kits für kurze Eingaben).

**Nichts doppelt bestätigen** (Vorgabe des Users, gilt für die ganze App, Browser und Vollbild): Löschen, Power-Optionen, Payload beenden, Auftrag abbrechen, Deaktivieren/Aktivieren, Leeren … passieren sofort beim Wählen – kein Schalter, kein zweiter Druck, kein „Wirklich?“/„Trotzdem?“. Ein `confirm`-Kennwort holt die Oberfläche still aus dem Plan; einen Dialog gibt es nur bei einer Ablehnung. Warnungen (z. B. Ruckler bei ffpfsc) bleiben sichtbar, ohne Zusatzschritt.

## Aufbau

- `tvapp/` ist eine erhaltene Kopie von **ps5-homebrew-ui** (BlackBearReloaded, GPL-3.0-or-later) mit **ps5-opengl 1.0.2** (SHA-256 im Kit fest eingetragen, `tvapp/ORIGIN.md`). Die Regeln des Kits stehen in `tvapp/AGENTS.md` – lesen und befolgen (Bausteine aus `src/ui/components/` statt Eigenbau, `update()` ändert, `draw()` ist `const`, 1920×1080, Sicherheitsrand, Töne, `tour()` als Bildquelle).
- Unsere Oberfläche: Design `tvapp/src/concepts/ps5cc.cpp` (Reiter Kühlung, Spiele, Aufträge), Seiten `tvapp/src/ps5cc/*_page.cpp` (Einstellungen, Payloads, Protokoll; Vertrag in `pages.hpp`), Daten `tvapp/src/ps5cc/data.hpp`.
- **Daten kommen immer von der App über HTTP** (`tvapp/src/platform/ps5/http_source.cpp`, 127.0.0.1:8086, eigener Faden mit 1 MiB Stack, nie im Bild-Takt). Erst in `docs/API.md` die Route nachsehen; fehlt sie, zuerst in der App (`src/api.c`) bauen. Abfragen nur, solange die Seite offen ist (`watch(...)`).
- Am PC liefert `tvapp/src/ps5cc/mock_source.cpp` erfundene, realistische Daten – für jede neue Anzeige mitpflegen.
- Bilder (Cover, Entwickler-Bilder): über den Cover-Dienst `tvapp/src/ps5cc/covers.*` (Schlüssel, URL, höchstens 2 Texturen je Bild, PNG/JPEG über stb_image ohne stdio).
- Spielstart aus der Vollbild-App immer mit `after_exit` (die Kachel ist selbst ein Titel; die App startet das Spiel erst nach dem Ende der Kachel).
- Texte: deutsch, Umlaute über den Zeichensatz „european“ (`make fonts`), nie „Sony“, „Power-Optionen“ nie „Strom-Knöpfe“.

## Ablauf

1. Fragen (oben). Bei Gestaltung erst Vorschau zeigen, erst nach „Ja“ auf die Konsole.
2. Ändern im Repo (`tvapp/`). Der WSL-Baum `~/ps5cc-tvapp` (Ubuntu-26.04) ist nur zum Bauen; `tools/tvapp-build.sh` gleicht ihn ab.
3. Vorschau: im WSL-Baum `USE_CCACHE=0 tools/host-snapshots.sh build/snapshots ps5cc`, für neue Zustände `tour()`-Schritte mit Bildnamen ergänzen, **jedes Bild ansehen** (Überlauf, abgeschnittene Texte, Umlaute), dem User die wichtigsten mit SendUserFile zeigen.
4. Tests: `USE_CCACHE=0 make test-unit` im WSL-Baum, alle grün; neue Hilfsfunktionen bekommen Tests (ohne OpenGL).
5. Bauen: zuerst die App-ELF `bash tools/build-windows.sh`, dann `bash tools/make_starter.sh` – baut beide Kacheln (`starter/out/FAKE86921` Vollbild, `starter/out/FAKE86922` Browser).
6. Auf die Konsole nur nach „Ja“: Kachel-Ordner per FTP nach `/data/homebrew/FAKE86921/` (alle Dateien, Größe der `eboot.bin` vergleichen). **Die Vollbild-App muss geschlossen sein**, sonst lehnt FTP die `eboot.bin` ab (550). Neue App-Routen brauchen auch die neue ELF (Port 9021, getrennter Aufruf).
7. CHANGELOG („Unveröffentlicht“, Abschnitt der Vollbild-App), bei neuen Routen `docs/API.md`; committen nur nach „commit das“.

## Fallen, die schon Zeit gekostet haben

- `tvapp/sce_sys/param.json` nicht anfassen: Medien-Bereich = Kategorie 65536 + Badge 2 ohne `gameIntent`, **Seitentabellen höchstens 134217728 (128 MiB)** – mit 256 MiB startet die Kachel nicht (CE-100022-5, Kernel-Log „cpuPageTableSize … max 134217728“).
- Listen/Details nie bei jeder Status-Aktualisierung neu befüllen (`set_items` startet die Einblend-Animation neu → Flackern): nur, wenn sich der Inhalt ändert (Schlüssel vergleichen).
- Werte in Detail-Listen einzeilig (`max_lines = 1`), sonst Pendeln zwischen mit/ohne Scrollbalken.
- Die eigene Kachel und die Browser-Kachel nie in der Spieleliste zeigen, nie als „Im Spiel“.
- Ein Kernel-Log-Fehler beim Start steht im Kernel-Log der App (`GET /api/v1/klog`) – zuerst dort nachsehen, nicht raten.
