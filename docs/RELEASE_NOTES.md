# PS5 Cooling & System Center – Pro v1.75.4

## Highlights
- **Uhrzeiten stimmen** – die Vollbild-App zeigt Protokoll, Kernel-Log, Dateien, Spielzeit und Sicherungen in der Ortszeit der Konsole statt in UTC
- **Klarer Hinweis zur Kernlast** – statt „keine Angabe“ steht da, welche Zusatzabfrage sie braucht

## Fehlerbehebungen
- Vollbild-App: Uhrzeiten lagen eine Stunde zurück (UTC statt Ortszeit). Die App kannte nur die Zeitzone, die ein Browser beim Speichern des Nachtmodus mitgeteilt hatte. Jetzt rechnet sie die Ortszeit aus den Einstellungen der Konsole selbst aus (Zeitzone und Sommerzeit).
- Vollbild-App: System → Experte zeigte die Zeitzone als „UTC+60:00“; jetzt „UTC+1:00“.
- Browser: Die Systemseite zeigte den Zeitzonen-Versatz als „+60:00 (3600 min)“; jetzt „+01:00 (60 min)“.

## Verbesserungen
- Die Auslastung je Kern wird nur mit der Zusatzabfrage „Risikotelemetrie“ gemessen. Ist sie aus, sagen Vollbild-App und Browser das jetzt in allen Sprachen.
- Neu in `GET /api/v1/system`: `local_offset_min` (Ortszeit der Konsole minus UTC, Sommerzeit eingerechnet), `summer_time` und `utc_offset_raw`.

## Hinweise
- Ob die Sommerzeit richtig dazukommt, ließ sich im November nicht prüfen. Die App schreibt die Rohwerte beim Start einmal ins Protokoll („Zeitzone der Konsole …“).
- Die Kacheln FAKE86921 (Vollbild) und FAKE86922 (Browser) aus diesem Release neu kopieren.

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.75.4.elf` | Das Hauptprogramm (an Port 9021 senden) |
| `FAKE86921.zip` | Vollbild-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `FAKE86922.zip` | Browser-Kachel: Ordner nach `/data/homebrew/` kopieren |
| `LIESMICH.txt` | Anleitung |

## Danke
An alle, deren Arbeit in der App steckt oder sie möglich macht – die vollständige Liste steht in der App unter „Credits“ bzw. Profil → Danke.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.75.3...v1.75.4
