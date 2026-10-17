## Release Notes

Version: v1.69.0
Datum: 2026-10-17

### Lüftersteuerung
- **Nachtmodus:** Zwischen zwei Uhrzeiten gilt ein leiseres Nachtziel (neue Karte auf der Kühlungsseite, unter „Betriebsart“). Die Uhrzeit ist die des Geräts, auf dem die Seite offen ist; eine eigene Spielregel geht vor. An der Konsole geprüft, auch im Spiel: Der Lüfter fiel auf das Minimum der Konsole, der heißeste Sensor lag etwa 3 °C höher
- **Lüfter-Selbsttest:** Der Knopf „Lüfter jetzt testen“ in der Lüfterdiagnose (Expertenansicht) prüft, ob der Lüfter auf die Schwelle antwortet, auch ohne dass die Konsole heiß wird (höchstens 30 Sekunden, nur bei kühler Konsole, jederzeit abbrechbar). An der Konsole: Drehzahl von 20 % auf 54 % in 6 Sekunden
- **Mindestdrehzahl der Konsole** wird gelernt (20 % im Leerlauf, 34 % im Spiel) und in der Lüfterdiagnose angezeigt; die Soll-Ist-Abweichung rechnet damit
- **Vorhalt bei hohem Ziel** (ab 85 °C): Bei deutlich steigender Temperatur liest die Regelung das Band etwas voraus; der Status zeigt ihn als `control.lead_c10`
- **Sauberes Beenden:** Bei SIGTERM und SIGINT gibt die App die Lüftersteuerung an die Konsole zurück, bevor sie endet (an der Konsole geprüft: danach hält sie 91 °C)
- Die wirkungslose **„Lüfterkurve (Experte)“** ist aus den Einstellungen entfernt; sie wurde gespeichert, aber nie von der Regelung benutzt (Export und Import ändern sich nicht)
- Die Lüfterdiagnose rät bei einer kühlen Konsole nicht mehr, das Ziel zu senken, nur weil die Schwelle in ihrer Ruhelage („oberer Anschlag“) steht; der Rat erscheint erst, wenn die Temperatur sich dem Ziel nähert
- Fällt nur **ein** Temperatursensor aus, gilt sein letzter Wert bis zu 10 Sekunden weiter, statt den Durchschnitt um den Abstand der beiden Sensoren zu senken; längeres Ausbleiben des Prozessor-Sensors steht im Protokoll
- **Fehler behoben:** Die Lüfterschleife rechnete alle Intervalle auf der Wanduhr; ein Zeitsprung (Zeitabgleich kurz nach dem Start) löste alle Takte auf einmal und eine falsche „Pause erkannt“-Meldung aus. Jetzt laufen die Regel-Intervalle auf der monotonen Uhr, nur die Erkennung langer Pausen (Ruhemodus) auf der Wanduhr
- **Fehler behoben:** Verließ man die Automatik während der Notfallregelung, blieben „Sicherheit“, rote Lightbar und das Warn-Kennzeichen der Spielzeit stehen
- **Fehler behoben:** Der Warn-Countdown richtete sich nach den rohen Warnwerten statt nach der Grenze, die die Warnung selbst anwendet (bei Ziel 91 stand dauerhaft „noch 0 s“)

### Oberfläche und Dokumentation
- Handbuch in allen sechs Sprachen um Nachtmodus, Lüfter-Selbsttest und Mindestdrehzahl ergänzt
- Rund 120 bisher deutsche Texte sind jetzt auch auf Englisch, Spanisch, Französisch, Italienisch und Russisch da
- Credits-Seite: Dank und Verweise für die Emulator-Bausteine (ampr_emu, pgo_stub, dlc_emu) sowie für LZ4 Compression, webhb und BlackBearReloaded
- Die Behauptung, Handbuch und FAQ lägen dem Release als PDF bei, ist aus den Anleitungen entfernt (das Release enthält keine PDFs)
- Die READMEs nennen Nachtmodus und Selbsttest in der Funktionsliste
- API: `control.night_active`, `fan.floor_pct`/`floor_idle_pct`/`floor_game_pct`, `control.lead_c10`/`lead_max_c10`, `/api/v1/fan/selftest` und die Felder `night_*` der Konfiguration (siehe `docs/API.md`)

### Stabilität und Fehler
- Der Zeitstempel im Status (Diagramm, „Zeitpunkt“) und die Spielzeit-Aufzeichnung der Temperaturen bleiben Wanduhrzeit
- Das Startprofil läuft pro Start der App einmal, nicht nach jedem erneuten Binden des Ports (Ruhemodus)
- Der Temperaturverlauf wird bei noch nicht gesetzter Uhr nicht mehr verworfen; der Auftragsverlauf überlebt einen Nullblock nach Stromausfall
- Spiele löschen und kopieren: eigene Stackgröße der Arbeitsfäden, die Verzeichnisrekursion braucht pro Ebene nur noch wenige Byte
- Bei Speichermangel schloss die LZ4-Paketprüfung zufällige Dateideskriptoren; die Übersichtsliste der Titelbilder (Zwischenspeicher) verdrängt bei voller Tabelle den ältesten Eintrag, statt neue Titel nie zu speichern
- Der Mitschnitt startet bei einem Doppelklick nicht mehr zwei Aufträge; eine nicht lesbare Konfigurationsdatei wird nicht mehr durch einen alten Stand ersetzt
- Oberfläche: korrekte Anzeige im Modus „Beobachten“ ohne gesetzte Schwelle, Seitenwechsel nach dem Sprachwechsel lädt wieder seine Daten, Bestätigungsklicks mit 800 ms Sperre gegen Doppelklick, Abbrechen-Knöpfe bleiben nach einem Fehler bedienbar, Paketliste bleibt bei einer fehlgeschlagenen Nebenabfrage stehen, ungespeicherte Haken der Zusatzabfragen springen nicht zurück

### Starter-Kachel
- `PPSA86921.zip` enthält die App von v1.69.0; den Ordner in `/data/homebrew/` ersetzen

### Wichtig
- **Firmware:** Getestet auf einer PS5 Pro (CFI-7021) mit **12.00**. Mehrere Nutzer melden, dass die App auf 13.60 läuft; für 13.00 bis 13.40 gibt es keine Meldung.
- **Vorhalt:** An der Konsole rechnet er richtig (in einem Testbau mit abgesenkter Schwelle gegen eine unabhängige Nachrechnung geprüft: keine Abweichung), hat im echten Spiel aber bisher nie den Lüfter bewegt, weil die Anstiege klein sind (höchstens 0,17 °C pro Sekunde) und die Konsole im Spiel ohnehin nie unter 34 % dreht. Seine Wirkung bei sehr steilem Anstieg ist nur in der Simulation gezeigt (Spitze 96 statt 98 °C bei Ziel 91). Mit einem Ziel unter 85 °C merkst du nichts davon.
- **Sauberes Beenden** gilt nur für SIGTERM und SIGINT. Sendest du die App erneut, beendet die neue die alte per SIGKILL; dann bleibt die zuletzt gesetzte Schwelle, bis die Konsole sie beim nächsten Zustandswechsel zurücksetzt.
- **Nachtmodus:** Ob er spürbar leiser ist, hängt vom Spiel ab. Im Spiel dreht die Konsole nie unter ihrem eigenen Minimum (34 % gemessen); bei einem Spiel, das schon bei normalem Ziel kaum darüber liegt, bleibt der Unterschied klein.
- Alles Weitere steht bei v1.68.0 und v1.68.1.
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**
