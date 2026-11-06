# PS5 Cooling & System Center – Pro v1.77.0

## Highlights
- **zstd-Pakete:** Spiele lassen sich jetzt auch in **zstd-Asset-Packs** packen (für AMPR EMU NG) – **LZ4 oder zstd zur Wahl**
- **AMPR EMU NG 0.3.2** ist fest eingebaut; beim Einbauen **fragt die App**, ob die automatische Fakelib-Aktualisierung in ShadowMountPlus abgeschaltet werden soll
- **Löschen fragt zuerst** und nennt, was weggeht (Browser und Vollbild-App)
- Die Nutzlast ist mit **9,87 MB** wieder klein genug (zstd inklusive)

## Neu

### zstd-Pakete (AMPR EMU NG)
- Im Fenster eines Spiels stehen **Mit LZ4 packen …** und **Mit zstd packen …** nebeneinander (Browser und Vollbild-App). zstd packt das **ganze Spiel außer den Systemdateien**, Stufe 1 bis 22 (angeboten: 1, 5, 9, 19; Vorgabe 5), kein Profil nötig.
- Ablauf wie bei LZ4: Spielordner prüfen, packen, **jede Datei Byte für Byte mit dem Original vergleichen**, AMPR EMU NG einlegen, erst dann die Originale entfernen; dazu **Pakete prüfen**, **zstd entpacken** und „Als neuen Ordner …-zstd daneben“. Ein Spiel liegt in LZ4- **oder** zstd-Paketen; in einem zstd-Spiel sind alle APR-EMU-Versionen außer NG gesperrt.
- Das Format kommt von quer3q (ampr-emu-ng, ps5-dump-forge). Geprüft ist es gegen die Originale und gegen einen unabhängigen Leser des Formats; **an der Konsole ist das Packen und Spielen mit zstd noch nicht erprobt** – bitte zuerst an einem Spiel versuchen, dessen Original du behältst.

### AMPR EMU NG 0.3.2
- NG ist mit der Version **0.3.2** fest eingebaut (normal und Debug; 0.3.1 und die 0.3.6.6-Fassungen von drakmor liegen nicht mehr bei, „Datei hinzufügen“ legt sie bei Bedarf wieder ab). Neu in 0.3.2: Resonance: A Plague Tale Legacy läuft als vollständig gepacktes Spiel.
- Ist in ShadowMountPlus die **automatische Fakelib-Aktualisierung** an, ersetzt es die NG-Datei beim Spielstart wieder. Deshalb fragt die App beim Einbauen einer NG-Version: **Ja** schaltet sie ab (geändert wird nur die Zeile `update_emulators` in `/data/shadowmount/config.ini`, die Datei wird vorher einmal gesichert), **Abbrechen** lässt alles, wie es ist.

### Löschen fragt zuerst
- Jeder „Löschen“-Knopf öffnet jetzt erst ein Fenster, das **nennt, was gelöscht wird** (Datei- oder Ordnernamen, bei Spielen den Plan der Konsole). Erst **Löschen** im Fenster löscht; **Abbrechen** hat den Fokus.
- Gilt für Dateimanager, Spiele, Sicherungen, Spielstände, Payload-Dateien und -Profile, Disc-Abbilder, Dumps, Hintergrundbilder und mehr – in Browser und Vollbild-App, in allen sechs Sprachen.

### Weiteres
- Browser-Oberfläche: das Symbol links neben dem Titel hat abgerundete Ecken.
- PS4 von USB: PS4Mount 0.7.3 übernommen (bis zu 768 Spiele je Laufwerk, 64 Suchordner).
- Handbuch, FAQ und README in sieben Sprachen aktualisiert (README.sv.md neu); die FAQ erklärt „LZ4 oder zstd?“.
- Die Nutzlast bleibt klein: zwei bisher unkomprimiert eingebettete Programme sind gepackt, Bilder verlustfrei neu gepackt, ungenutzte Bilder und Übersetzungsreste entfernt.

## Hinweise
- Die Kacheln FAKE86921 (Vollbild) und FAKE86922 (Browser) aus diesem Release neu kopieren; die Vollbild-App braucht die neuen Sprachdateien und Texte. Die Vollbild-App muss dafür geschlossen sein.
- Für Entwickler: `POST /api/v1/library/lz4` und `GET /api/v1/library/lz4/plan` nehmen `format` (`lz4`/`zstd`); neu `GET`/`POST /api/v1/ampr-emu/smp-update`.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.77.0.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke. Für diese Version besonders: **quer3q** (AMPR EMU NG, das zstd-Format, ps5-dump-forge), **drakmor** (ampr_emu, ShadowMountPlus) und **gasb07** (PS4Mount).

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.76.9...v1.77.0
