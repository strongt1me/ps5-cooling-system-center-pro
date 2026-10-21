# PS5 Cooling & System Center – Pro v1.73.1

## Fehlerbehebungen
- **Weboberfläche nach einem Netzwerkwechsel.** Wird das WLAN aus- und wieder eingeschaltet oder das Kabel gezogen, beendete die Konsole mitunter den Zugang der Seite, obwohl die App weiterlief – die Seite lud erst nach erneutem Senden der ELF wieder. Die App prüft ihren Zugang jetzt alle zehn Sekunden und öffnet ihn bei Bedarf von selbst neu (nach PatchDL 0.3.4)

## Verbesserungen
- Auf dem Stand von ShadowMountPlus 1.7beta7 (dessen Änderung betrifft nur das Aushängen eines mitten im Betrieb abgezogenen USB-Laufwerks); an der App war dafür nichts zu ändern
- Handbuch und FAQ in allen sechs Sprachen: Hinweis zur Seite nach einem Netzwerkwechsel
- Die täglich prüfbare Liste der Fremdbausteine (`tools/upstream.json`) ist auf dem neuesten Stand

## Hinweise
- **Starter-Kachel:** wie bei jeder Fassung enthält `PPSA86921.zip` die aktuelle App; den Ordner `PPSA86921` neu nach `/data/homebrew/` kopieren

## Downloads
| Datei | Wofür |
|---|---|
| `PS5_Cooling_System_Center_v1.73.1.elf` | die App – nach jedem Neustart der Konsole an Port 9021 senden |
| `PPSA86921.zip` | Starter-Kachel für `/data/homebrew/` (startet die App per Tipp) |
| `LIESMICH.txt` | Kurzanleitung |

**SHA-256**
```
c0b440f6032cbf3785685bd01e826dddc5dc7e98d37dd4a4b3a027ca81ef122f  LIESMICH.txt
dd8116e215f186b2b96354abad86c15ac1744575463a3d11510f0ae8efdabf87  PPSA86921.zip
1378dd6a0622b9186e21214016dc70e5b349a3d44cfffe8f065d9e0c6b38c52f  PS5_Cooling_System_Center_v1.73.1.elf
```

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.73.0...v1.73.1
