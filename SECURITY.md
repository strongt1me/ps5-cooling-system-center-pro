# Sicherheitsrichtlinie

## Unterstützte Version

Sicherheitskorrekturen gibt es für die jeweils **neueste Veröffentlichung**. Ältere
Versionen werden nicht gepflegt; bitte zuerst aktualisieren.

## Eine Sicherheitslücke melden

Bitte melde Sicherheitslücken **nicht öffentlich** (kein Issue, keine Diskussion).

- Bevorzugt über **Security → „Report a vulnerability“** in diesem Repository
  (private Meldung), sofern GitHub die Funktion hier anbietet.
- Sonst über eine Nachricht an den Maintainer auf GitHub: [@strongt1me](https://github.com/strongt1me).

Bitte nenne in der Meldung:

- die betroffene Version (Seite „System“ oder Dateiname der ELF),
- Konsolenmodell und Firmware,
- was du getan hast, was passiert ist und was du erwartet hättest,
- wenn möglich eine kleine, nachvollziehbare Anleitung (Anfrage an die
  Schnittstelle, Browser-Verhalten).

Der Maintainer antwortet nach Möglichkeit innerhalb weniger Tage, bestätigt den
Eingang und meldet sich, sobald die Ursache geklärt ist. Es gibt keine
Kopfgelder. Wer eine Lücke verantwortungsvoll meldet, wird auf Wunsch in den
Versionshinweisen genannt.

## Was als Sicherheitslücke zählt

Zum Beispiel:

- eine fremde Webseite oder ein fremdes Gerät kann die Konsole über die
  Schnittstelle steuern, obwohl die Schutzmaßnahmen des Servers (Prüfung von
  `Host` und `Origin`, Content-Security-Policy) das verhindern sollten,
- Dateipfade, die aus Anfragen entstehen, führen aus dem vorgesehenen Bereich
  hinaus (Pfadumgehung),
- eine Anfrage bringt das Programm zum Absturz oder die Oberfläche dauerhaft
  zum Stillstand,
- eingeschleuste Zeichen werden in der Oberfläche als Code ausgeführt (XSS).

## Was kein Fehler dieses Projekts ist

- **Die Oberfläche hat keine Anmeldung.** Das ist Absicht: Sie ist für das
  eigene Heimnetz gedacht, und jedes Gerät in diesem Netz darf lesen **und**
  ändern. Die Konsole gehört nicht ins offene Internet: keine Portweiterleitung
  auf Port 8086. Wer das Programm nur auf der Konsole selbst ansprechen lassen
  will, setzt `bind_address` auf `127.0.0.1`.
- **Kraken Mount (Port 8097) und Dump Forge (Port 8095) sind Programme anderer
  Autoren.** Kraken Mount startet die App seit dem 17.11.2026 von selbst, rund
  fünf Sekunden nach ihrem eigenen Start, solange es nicht im Reiter FPKGMount
  ausgeschaltet wurde (die Markierung `kraken/aus.txt` im Datenordner); Dump
  Forge startet sie nur für die Dauer einer Konvertierung. Beide lauschen im
  ganzen Heimnetz und prüfen weder `Host` noch `Origin`; die Einstellung
  `bind_address` und der Browser-Schutz dieser App gelten für sie nicht. Wer
  das nicht will, schaltet Kraken Mount im Reiter FPKGMount aus (der Schalter
  merkt sich das über Neustarts). Gemeldet wird so etwas bei den Autoren dieser
  Programme. Die Folge für Kraken Mount 0.3.0 (Prüfung vom 18.11.2026, aus dem
  Quelltext des Programms gelesen, nicht nachgestellt): Eine fremde Webseite im
  Browser eines Geräts im Heimnetz kann dort ohne Anmeldung die Einstellungen
  ändern (Suchordner, Fehlersuch-Modus) und ein Spiel löschen, dessen genauen
  Pfad sie kennt; Dateien unter `/data` liest sie nur bei eingeschaltetem
  Fehlersuch-Modus und über einen Umweg (DNS-Rebinding). Der Reiter FPKGMount
  zeigt abweichende Suchordner an; die Prüfung von `Host` und `Origin` kann nur
  der Autor von Kraken Mount einbauen.
- Schwachstellen in der Konsole selbst, im Jailbreak, in kstuff, im Payload-Lader
  oder in anderen Homebrew-Programmen. Bitte melde sie dort.
- Das Umgehen von Schutzmaßnahmen des Herstellers. Dieses Projekt enthält dafür keinen
  Code und leistet dazu keine Hilfe.

## Rechtliches

Wie in der [README](README.md#rechtliches-und-haftungsausschluss) beschrieben:
Das Programm wird ohne jede Gewährleistung bereitgestellt.
