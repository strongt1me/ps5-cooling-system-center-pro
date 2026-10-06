# Release Notes Templates

Diese Vorlagen sind für private Releases dieses Projekts gedacht.

## 1) Kurzfassung (Endnutzer)

```md
## Release Notes

### Neu
- [Kurz die wichtigsten neuen Funktionen/Verbesserungen]

### Verbesserungen
- [Stabilität/Performance/Usability in 1-3 Punkten]

### Assets
- PS5_Cooling_System_Center_v<Version>.elf
- cooling-center-launcher-installer_v<Version>.elf
- IV9999-PSCC69690_00-PS5COOLINGCNTR01-A0100-V0100.pkg
- LIESMICH.txt

### Installation
1. [Haupt-ELF oder Installer-ELF auf die PS5 laden]
2. [Web-UI aufrufen: http://<PS5-IP>:8086]
3. [Optionales kurzes Troubleshooting]
```

## 2) Langfassung (Entwickler)

```md
## Release Notes

Diese Version fokussiert [Schwerpunkt: z.B. Stabilität/Automation/Feature-Ausbau].

### Neu
- [Feature A]
- [Feature B]

### Build & Tooling
- [Build-System / Makefile / Skripte]
- [Neue Tools oder Automatisierung]

### Fixes
- [Bugfix 1]
- [Bugfix 2]

### Qualität & Verifikation
- [Tests/Checks die durchlaufen wurden]
- [Pre-Release-Check, Asset-Verifikation, Release-Upload]

### Assets
- PS5_Cooling_System_Center_v<Version>.elf
- cooling-center-launcher-installer_v<Version>.elf
- IV9999-PSCC69690_00-PS5COOLINGCNTR01-A0100-V0100.pkg
- LIESMICH.txt

### Kompatibilität / Hinweise
- [FW-/Jailbreak-/kstuff-Hinweise]
- [Bekannte Einschränkungen]
```

## Schneller Einsatz mit gh CLI

Release Notes aus Datei setzen:

```powershell
gh release edit v<Version> --repo strongt1me/ps5-cooling-system-center-pro --notes-file .\docs\RELEASE_NOTES.md
```

Tipp: Lege vorab eine konkrete Datei (z.B. docs/RELEASE_NOTES.md) an, kopiere eine der Vorlagen hinein und ersetze alle Platzhalter.
