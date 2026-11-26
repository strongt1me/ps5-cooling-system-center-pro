# PS5 Cooling & System Center – Pro v1.97.0

## Highlights
- **Dump Forge 0.6.0** is built in. It packs LZ4 only with AMPR EMU NG now, so the choice "Runtime in the game" is gone from the convert window (web and fullscreen). The bars "Measuring" and "Writing" take Dump Forge's own numbers instead of an estimate.
- **Saved games:** Reading out or putting in a save could end the whole app when a web page or the fullscreen app closed a connection at the same moment. Fixed.
- **Fan, mode "Observe" without a fixed threshold:** After the fan guard had held full cooling, the fan stayed at full speed until the console switched by itself. The console now gets its own setting back as soon as the guard lets go.

## What's new
- **Kraken Mount 0.4.1** is built in (installed games through ampr-emu-ng – off by default –, close a game, QR code). It now also answers ShadowMountPlus's API on 127.0.0.1:10101, the very address this app uses to talk to ShadowMountPlus. Because the app runs both side by side, its copy of Kraken Mount has that API moved to port 18097; if a copy started on its own answers on 10101, the app recognises it and sends it no jobs meant for ShadowMountPlus.
- **Fan:** A hand-back the fan device refuses is tried again; the guard can no longer be overwritten for a few seconds; on exit the guard writes nothing in between; a fixed threshold follows imported settings at once.
- **File manager search:** Unreadable folders are named at the end; an eject ends the search with its reason; a `*` in a name is matched; a dump next to a folder of the same game is found by its title; long titles are cut at a whole character.
- **File manager:** Merging a folder into an existing one no longer leaves a half-written file under its real name after a cancel.
- **Jobs:** A late "Cancel" no longer runs "Original afterwards"; packing and unpacking in the game folder verify from the drive, not from memory.
- **NTFS:** The copy of the file table is checked before writing; a delete that stops half way and an app that ends while writing keep the drive marked "to be checked".
- **Search in both interfaces:** Cancel shows at once, clicks are no longer lost while results come in, the delete question names the whole path of a hit, and in the fullscreen app Copy/Cut/Delete act on the entry the menu was opened for.
- **Fullscreen tile:** When a newer app is sent from a PC while the tile is open, the tile no longer starts its own older copy in between.
- 23 messages translated into all six languages.

## Notes
- **Tried on the console (Slim, FW 12.00):** packing Asterix & Obelix: Heroes into an exFAT image with LZ4 packs (1.75 GB → 1.20 GB in 67 s, verified) and reading out, putting back and comparing a save (byte-identical) while 38 connections were cut off in the middle of an answer – the app kept running. The fixes made after that test (see the changelog) were tried on the PC only (46 host tests with the fan simulation, 396 tests of the fullscreen app).
- **stb_image** in the fullscreen app stays at 2.30: version 2.34 decodes progressive JPEGs wrongly.
- Send the new ELF to port 9021 and copy the tiles FAKE86921 and FAKE86922 from this release again (the fullscreen app must be closed for that). After that, reload an open web page once.

## Downloads
| File | What it is for |
|---|---|
| `PS5_Cooling_System_Center_v1.97.0.elf` | The main program (send it to port 9021) |
| `FAKE86921.zip` | Fullscreen tile: copy the folder to `/data/homebrew/` |
| `FAKE86922.zip` | Browser tile: copy the folder to `/data/homebrew/` |
| `LIESMICH.txt` | Instructions (German) |
| `payload-manager.json` | List for itsPLK's Payload Manager: enter `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` as the source, and it downloads the app from GitHub (with a SHA-256 check) |

## Thanks
To everyone whose work is in the app or makes it possible – the full list is in the app under "Credits" or Profile → Thanks. Especially **quer3q** for Kraken Mount, Dump Forge and AMPR EMU NG, **abcde6543** for PS5-NTFS-BROWSER, **gasb07** for PS4Mount, **drakmor** for ShadowMountPlus, **itsPLK** for the Payload Manager and the payload mirror, and **John Törnblom** and the contributors of the PS5 payload SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.96.1...v1.97.0
