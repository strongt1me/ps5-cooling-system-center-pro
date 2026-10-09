## Release Notes

Version: v1.54.1
Datum: 2026-10-09

Neu seit 1.54.0: ehrlichere Kühlleistungs-Karte, neue Kacheln und aufgeräumte Knöpfe. Der kleine Lüfter in der Kopfleiste ist wieder entfernt.

### Neu

- **Kühlleistung über die Zeit: Einstellungs-Stempel.** Die Karte hatte nach viel Testen mit geänderten Werten „14,1 °C wärmer, Staub“ gemeldet. Jetzt trägt jede Wochenzeile
  einen Stempel der Einstellungen, die den Lüfter steuern (Modus, Zieltemperatur, Schwellen, Kurve und mehr); verglichen werden nur Wochen unter den **jetzt geltenden** Einstellungen. Ändert man sie,
  beginnt der Vergleich von vorn, und die Karte sagt das offen („Vergleich neu gestartet“, seit wann, wie viele der nötigen 4 Wochen). Die Texte nennen Staub nur noch als eine mögliche Ursache
  („Ein Hinweis, kein Beweis“). Neuer Knopf **„Vergleich neu beginnen“**, zum Beispiel nach einer Reinigung. Alte Wochen bleiben in der Datei, zählen aber nicht mehr mit (Wochen aus Fassungen
  vor 1.54.1 haben keinen Stempel). Schnittstelle: `GET /api/v1/cooling-health` liefert zusätzlich `weeks_needed`, `since_ms`, `older_weeks`; `POST /api/v1/cooling-health` beginnt neu.
- **Payloads:** „Payload Verwaltung“ ist eine Kachel mit Bild, wie das Profil; „Aktualisieren“, „Neues Profil“, „Profile sichern“ und „Profile laden“ stehen in einer Linie darin. Der Profil-Bereich ist schlanker:
  „Bearbeiten“ und „Als Startprofil“ sind runde Symbolknöpfe, das Ergebnis des letzten Laufs steht in einer Zeile.
- **Dateien:** Kachel mit Bild; „Aktualisieren“ links unter dem Titel, „↑ Hoch“ als erster Knopf der Werkzeugzeile. **Spiele:** neuer Knopf „Aktualisieren“.
- **Kühlung:** Die Lüfter-Kachel und die Kachel „Läuft gerade“ haben einen gemeinsamen Sternen-Hintergrund. Die Power-Optionen oben sitzen auf jeder Seite an derselben Stelle.
- **Runde Felder auch an der Konsole:** Auswahlfelder, Knöpfe und Textfelder (schon in 1.53.1).

### Wichtig

- **Firmware:** Getestet auf einer PS5 Pro (CFI-7021) mit **12.00**. **Mehrere Nutzer melden, dass die App auf 13.60 läuft.** Für 13.00 bis 13.40 gibt es keine Meldung (der Startcode kennt sie).
- Die Karte „Kühlleistung über die Zeit“ fängt mit diesem Stand neu an zu zählen; die bisher gesammelten Wochen bleiben in der Datei, werden aber nicht mehr verglichen. Nach vier Wochen unter gleichen Einstellungen gibt es wieder eine Aussage.
- Noch nicht an der Konsole benutzt: Profile ausführen und als Startprofil, der Import vom PC bei den Profilen, die Backport-Erkennung mit `fakelib2`/`backports`, das **Löschen von Spielständen**
  und das Überstehen des Ruhemodus. Probiere das Löschen zuerst mit einem unwichtigen Titel. **Löschen ist endgültig** – bei Spielen, Sicherungen und im Dateimanager.
- Die veröffentlichte ELF ist nicht byte-gleich mit der an der Konsole getesteten (Versionsnummer).
- **`.ffpkg`-Dateien aus Fassungen vor 1.48.0 neu erzeugen.**

### Technisches

- C-Teil: `src/thermalog.c` (Stempel, Neustart, Zeile `#state` in `thermal-health.csv`), `src/api.c`, `src/ps5tm.h`; sonst Oberfläche und Bilder. Tests: Host-Test `thermtest.py` (10 Prüfungen, mit AddressSanitizer/UBSan),
  Browser-Suite 54 Szenarien ohne Befund, Wörterbücher aller fünf Sprachen ohne Fehler.
