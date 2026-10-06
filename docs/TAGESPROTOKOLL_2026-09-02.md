# Tagesprotokoll 2026-09-02

## Zusammenfassung

Heute wurde der Release- und Qualitätsworkflow für das Projekt durchgängig abgeschlossen und verifiziert.

## Umgesetzte Punkte

- Avatar-Library-Workflow in Frontend und API vervollständigt (Speichern, Laden, Löschen von Avatar-Paketen).
- Makefile-Härtung für Build-Metadaten und robustere Toolchain-Nutzung.
- Neue Helper-Skripte:
  - tools/build_date.py
  - tools/read_version.py
- Neues Ein-Kommando-Skript für Vorabprüfung und Release-Sync:
  - tools/pre-release-check.ps1
- .gitignore erweitert um lokale/nicht zu versionierende Artefakte:
  - PS5_PAYLOAD_SDK/
  - build.out.txt
  - build.err.txt
- README aktualisiert (Pre-Release-Check, Erfolgsmarker, Verifikationshinweise).
- Release-Notes-Vorlagen erstellt und konkrete Notes-Datei vorbereitet.

## Release-Status

- Version im Code auf 1.32.2 angehoben (PS5TM_VERSION).
- v1.32.2 erfolgreich in WSL gebaut.
- Assets lokal verifiziert:
  - PS5_Cooling_System_Center_v1.32.2.elf
  - cooling-center-launcher-installer_v1.32.2.elf
  - IV9999-PSCC69690_00-PS5COOLINGCNTR01-A0100-V0100.pkg
  - LIESMICH.txt
- Assets zur GitHub-Release v1.32.2 hochgeladen und verifiziert.
- Release v1.32.2 veröffentlicht (nicht Draft).

## Bewährter Ablauf für nächste Releases

1. Version in src/ps5tm.h anheben.
2. Pre-Release-Check mit Upload laufen lassen:
   - powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\pre-release-check.ps1 -CreateOrUpdateRelease
3. Release-Notizen aus docs/RELEASE_NOTES.md setzen (falls nötig).
4. Draft final veröffentlichen.
