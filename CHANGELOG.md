# Changelog

## v1.62.0

### LZ4-Asset-Packs
- Spiele lassen sich im Spielordner zu LZ4-Asset-Packs packen (ein Paket mit Index und Prüfdatei, Dateien Byte für Byte mit dem Original verglichen, bei jedem Fehler wird zurückgerollt); Pakete prüfen und wieder entpacken sind dabei
- Profil „Automatisch“ (Stufe 1 bis 12) oder eigenes TOML-Profil; „Automatisch“ lässt Programme, Bibliotheken, Spielstände, schon komprimierte Medien, Konfigurations- und Datendateien sowie sehr große Dateien lose
- Die App erkennt, ob ein Spiel libSceAmpr überhaupt lädt, und sperrt das Packen sonst
- Neue Anleitung „LZ4-Asset-Packs“ im Handbuch (6 Sprachen) und klarere Beschriftung der Zeile

### APR-EMU, PlayGo und DLC
- APR-EMU in fünf Fassungen und der PlayGo-Stub 0.6 (mit und ohne Protokoll) sind in der App eingebettet und lassen sich pro Spiel auswählen
- Neue Zeile „Spiel-Plugins“: bringt PlayGo-Stub und die DLC-Module (dlc_emu 0.3) auf den neuesten Stand; alte Dateien bleiben als `.old` erhalten, fehlendes Ausführungsrecht wird gesetzt
- Optimierung per Mitschnitt: die Debug-APR-EMU schreibt beim Spielen mit, aus den gesammelten Sitzungen entsteht ein eigenes Profil für das Spiel
- Prüfen, Entpacken und Reparieren von Abbildern, AMPR-Index, Auftragsverlauf

### Spiele starten
- Aus dem Browser der Konsole schließt sich die Seite sofort beim Tippen auf „Starten“, noch bevor das Spiel lädt
- Dauert ein Start länger (zum Beispiel ein Abbild, das erst eingehängt wird), erscheint nach 1,5 Sekunden die Meldung „Bitte warten. Spiel wird geladen...“
- Absagen und Hinweise erscheinen als Meldung der Konsole
- Die Kachel des laufenden Spiels leuchtet

### Oberfläche
- Die Web UI öffnet beim nächsten Mal auf der Seite, auf der sie geschlossen wurde
- Neue Reihenfolge der Seitenleiste: Kühlung ganz oben, Profil unter System
- Beim Konvertieren steht jetzt das Format des Ergebnisses (zum Beispiel „ffpkg (UFS2-Abbild)“)
- Handbuch, FAQ und Oberfläche in sechs Sprachen aktualisiert; neue Credits für V-10 (exFAT2AMPR), nexgen999, matem6, Phoenixx1202, ArkSama und SiSTR0

---

## v1.61.0

### Kühlung
- Die Effekte auf den drei Kacheln sind wieder entfernt: kein farbiges Leuchten des Luftstroms, kein farbiger Leuchtrand und kein Lichtschein um den Tacho
- Die Hintergrundbilder bleiben und zeigen wieder ihre eigenen Farben; Tacho, Nadel und Zahl färben sich weiterhin nach der Drehzahl

### Payloads
- Größere Überschriften („Profile“, „Laufende Payloads“, „Gespeicherte Payloads“) mit größeren Symbolen und Zählern
- Größere Ordnerzeile und größere Hinweistexte

---

## v1.60.0

### Kühlung
- Die große Kachel mit der Temperatur hat jetzt auch einen leuchtenden Luftstrom und einen Leuchtrand in der Farbe der Lüfterdrehzahl, wie die Lüfter-Kachel; das Bild füllt die ganze Kachel
- Die Kachel „Läuft gerade“ leuchtet ebenso: Luftstrom und Rand in der Farbe der Lüfterdrehzahl, hellere Beschriftung
- Die bisherigen orangen und roten Bildvarianten der großen Kachel für „warm“ und „heiß“ sind entfernt; den Zustand zeigen Ring, Zeichen und Text

### Lesbarkeit
- Alle Texte der Oberfläche sind heller, vor allem graue Beschriftungen und Hinweise, und haben einen leichten Schatten
- Die Einträge der Seitenleiste haben eine dunkle, runde Fläche und einen Schatten und sind auch über dem Lüfter-Bild gut lesbar

---

## v1.59.0

### Lüfter-Kachel
- Neues Hintergrundbild mit einem Luftstrom, der in der Farbe der gemessenen Drehzahl leuchtet
- Die Farben gehen jetzt gleitend von Blau (bis 25 %) über Grün (55 %) und Gelb (75 %, neu) zu Rot (ab 95 %); Nadel, Zahl und Rand der Kachel folgen denselben Farben
- Der Tacho ist sauber entlang seiner Form freigestellt und hat einen weichen Lichtschein in derselben Farbe, damit er zum Bild gehört
- „Lüfter“, „%“ und „gemessene Drehzahl“ sind heller und auf dem Hintergrund besser lesbar
- Die Zahl zählt jetzt mit der Nadel mit, statt sofort auf den neuen Wert zu springen

### Kachel „Läuft gerade“
- Neues Hintergrundbild
- Text oben links, Controller unten rechts direkt auf dem Bild, beide mit gleichem Abstand zum Rand

### Dateimanager
- Der Knopf in der Seitenleiste heißt jetzt „Dateimanager“ statt „Dateien“
- Größerer Titel und größere Pfadzeile
- „Aktualisieren“ steht in der Werkzeugzeile neben „Hochladen“

### Fehlerbehebungen
- Nadel und Zahl blieben auf einem alten Wert stehen, wenn die Lüfter-Kachel während einer Bewegung den Bildschirm verließ

---

## v1.58.0

- „Gespeicherte Avatare“ samt Funktion entfernt (Pakete speichern, laden und löschen sowie die Schnittstellen `/api/v1/profile/avatar/library*`); schon gespeicherte Pakete in `/data/PS5-Cooling-Center/Avatars` bleiben auf der Konsole
- Größere Karte „Anzeigename“ auf der Profilseite
- Tacho und Text stehen als Paar in der Mitte der Lüfter-Kachel

## v1.57.0

- Eigene runde Dateiauswahl („Datei auswählen“, „Keine Datei ausgewählt.“) in der Sprache der App statt des englischen, eckigen Feldes des Browsers
- Runde Kästchen und runder Regler im Browser der Konsole
- Auswahl „Animation“ der Lüfter-Kachel entfernt; der Tacho läuft immer voll
- Text der Lüfter-Kachel zentriert und größer; mehr Abstand zwischen Controller und Text bei „Läuft gerade“

### Fehlerbehebungen
- Die Schnellwahl (Kühl / Ausgewogen / Leise) gilt sofort, auch wenn der Regler vorher angefasst wurde
- Laden und Löschen gespeicherter Avatare nehmen das in der Liste gewählte Paket, nicht einen im Feld stehen gebliebenen Namen

## v1.56.0

- Tacho statt drehendem Lüfter (Bild des Users); die Nadel fährt weich auf jeden neuen Wert, dazwischen läuft nichts – behebt das Ruckeln beim Zoomen und Scrollen im Browser der Konsole
- Installierte Spiele (`app.pkg`) heißen jetzt „Installiert“ statt „Abbild“; „Pfad öffnen“, „Verschieben“ und „Kopieren“ sind dort ausgeschaltet

## v1.55.0

- Warteschlange für Pakete: mehrere Pakete vormerken und nacheinander installieren, mit Pause und Abbrechen; jedes Paket wird erst an seiner Reihe geprüft
- Spiele-Seite: neuer Kopf mit Bild, „Aktualisieren“ neben der Suche, „Infos & Metadaten“ als Fenster über der Seite
- Neuer Knopf „Pfad öffnen“ auf jeder Spielkarte öffnet den Speicherort im Dateimanager
- Die Banner füllen die ganze Breite der Kacheln

---

Ältere Versionen: siehe [GitHub-Releases](https://github.com/strongt1me/ps5-cooling-system-center-pro/releases).
