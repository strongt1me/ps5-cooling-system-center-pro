# Vorlage für Release-Notizen

Aufbau wie bei drakmor, itsPLK, EchoStretch und Gezine (seit 19.10.2026): kurz, nach Wichtigkeit, jede Zeile eine
Aussage. Die Langfassung jeder Änderung steht im CHANGELOG.md; die Notizen fassen sie zusammen. Keine Herstellernamen in
eigenen Sätzen, keine Versionen von Testständen, keine Interna (Dateinamen im Quelltext nur, wo der Nutzer sie braucht).

```md
# PS5 Cooling & System Center – Pro v<Version>

## Highlights
- **<Funktion>** – ein Satz, was man damit tun kann
- (2–4 Punkte, das Wichtigste zuerst)

## Neu
### <Bereich>
- eine Zeile je Neuerung

## Verbesserungen
- eine Zeile je Verbesserung (was schneller, ruhiger, bequemer wurde)

## Fehlerbehebungen
- was falsch war, aus Sicht des Nutzers, eine Zeile je Fehler

## Hinweise
- **Dateien / Starter-Kachel / Internet / Firmware:** was man beim Aktualisieren wissen muss

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v<Version>.elf` | die App – nach jedem Neustart der Konsole an Port 9021 senden |
| `PPSA86921.zip` | Starter-Kachel für `/data/homebrew/` (startet die App per Tipp) |
| `LIESMICH.txt` | Kurzanleitung |

## Danke
- **<Name>** – wofür

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v<vorher>...v<Version>
```

Eine kleine Fehlerbehebungs-Version kommt ohne „Highlights“ und „Neu“ aus:

```md
# PS5 Cooling & System Center – Pro v<Version>

## Fehlerbehebungen
- …

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v<vorher>...v<Version>
```
