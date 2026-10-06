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
- Schwachstellen in der Konsole selbst, im Jailbreak, in kstuff, im Payload-Lader
  oder in anderen Homebrew-Programmen. Bitte melde sie dort.
- Das Umgehen von Schutzmaßnahmen von Sony. Dieses Projekt enthält dafür keinen
  Code und leistet dazu keine Hilfe.

## Rechtliches

Wie in der [README](README.md#rechtliches-und-haftungsausschluss) beschrieben:
Das Programm wird ohne jede Gewährleistung bereitgestellt.
