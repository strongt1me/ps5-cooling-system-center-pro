# PS5 Cooling & System Center – Pro v1.89.0

## Highlights
- **Pack a game into LZ4 or zstd packs while converting it to an image:** In the "Convert" window, exFAT and ffpkg now have a "Packing (AMPR packs)" choice: *Do not pack*, *LZ4 packs* or *zstd packs (AMPR EMU NG only)*. Dump Forge packs the game straight into the image while it writes it – no temporary folder, the original stays untouched. Browser app and fullscreen app both offer it.
- **Firmware 1.00 to 14.22:** The app (the ELF and both tiles) is now built with the PS5 payload SDK **v0.50**, whose start-up code knows every firmware from 1.00 to 14.22 (v0.45 knew 4.03 to 14.10). A firmware the start-up code does not know never reaches `main()`, so this decides on which consoles the app starts at all.
- **Fan speed as a ring (browser app):** The speed in percent is shown as a ring, like in the fullscreen app, instead of a speedometer.

## What's new
### Converting
- **Packing while converting (exFAT and ffpkg of a PS5 game folder):** Choose LZ4 (level 9, 3 or 12; runtime AMPR EMU NG or "APR-EMU original") or zstd (level 5, 1, 9 or 19; zstd is read by AMPR EMU NG only, which Dump Forge puts into the game). The source is read twice (first measured, then written), so it takes longer than without packing; no `.sha256` file is written; "Check afterwards" (thorough or quick) works as before.
- **Not possible (greyed out, with the reason beside it):** PS4 games (they have no AMPR packs), games that already lie in packs (unpack them first under the game's asset packs), a source that cannot be read, Dump Forge missing (button "Load Dump Forge") or older than 0.5.6, and names over 63 bytes without the ending.
- **The job card tells what happens:** "The game is being measured for the packs …", then "NN % · packing and writing", then the check; at the end "24 GB instead of 38 GB (63 %)". The queue and the history name the choice ("to exfat + LZ4").

### Firmware and build
- **PS5 payload SDK v0.50 instead of v0.45:** official release file, SHA-256 checked. Only seven files differ (the start-up code `crt1.o`, two headers, two libraries and two tools). The build now checks 21 firmware versions from 1.00 to 14.22 in the SDK and in the finished program; the system page names the firmware groups over the whole range (5.xx to 8.xx used to read "unknown"). The fullscreen app's SDK is pinned to v0.50 too.

### Games
- **APR-EMU and PlayGo file: choose where it goes:** For an image (`.exfat`, `.ffpkg`) you can put the file into the image or into the backport folder next to it (`backports/<title id>/fakelib`); if the image is full ("No space left on device"), the app puts it into the backport folder by itself. For a compressed `.ffpfsc` and for an existing backport folder the place is fixed.
- **The version window of the fullscreen app names zstd** as well as LZ4 (an LZ4-packed game needs an LZ4 version, a zstd-packed one needs AMPR EMU NG).

### Translations
- **Composed texts are no longer half German:** A pattern such as "{0} min" took "6 payloads · as of 40" as its number and left the rest untranslated; a value that spans a " · " no longer counts. Eleven missing texts added in six languages, three dates use the page's language, and the fullscreen app prints "47.8 GB" with a point in English.
- **Gaps of the convert window closed:** the title, the format names, the UFS2 sentence and two check texts in all six languages; the description of the "Guide" button in the fullscreen app.

## Notes
- **Confirmed on the console (Slim, firmware 12.00, 20.11.2026):** packing while converting with one small game (1.75 GB, a folder): **LZ4 level 3 → 1.21 GB (69.5 %)** in 133 s, **zstd level 5 → 1.03 GB (59.2 %)** in 113 s, both with the thorough check; the game started from each image and ran (confirmed by the user). One measurement with one small game and a USB stick (about 10 to 15 MB/s while writing), so no statement about big games. Not tried: the Pro, the runtime "APR-EMU original", ffpkg as the target.
- **ShadowMountPlus ignores an image whose title ID already exists as a folder** ("Duplicate … ignored"): to start the game from such an image, the original has to leave the search folders first. At the start, ShadowMountPlus logged "sandbox fakelib unavailable (Device busy)" with both images; the game ran all the same.
- **Not tried on the console:** the choice of the backport folder when the image is full (only the lookup was checked), and the firmware range beyond 12.00 – the start-up code knows 1.00 to 14.22, but the app is tested on 12.00 only.
- 36 host tests with the fan simulation and 382 tests of the fullscreen app pass. Send the new ELF to port 9021 and copy the tiles FAKE86921 and FAKE86922 from this release again (the fullscreen app must be closed for that). Everything else is as in v1.88.1.

## Downloads
| File | What it is for |
|---|---|
| `PS5_Cooling_System_Center_v1.89.0.elf` | The main program (send it to port 9021) |
| `FAKE86921.zip` | Fullscreen tile (with Dump Forge): copy the folder to `/data/homebrew/` |
| `FAKE86922.zip` | Browser tile (with Dump Forge): copy the folder to `/data/homebrew/` |
| `LIESMICH.txt` | Instructions (German) |
| `payload-manager.json` | List for itsPLK's Payload Manager: enter `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` as the source, and it downloads the app from GitHub (with a SHA-256 check) |

## Thanks
To everyone whose work is in the app or makes it possible – the full list is in the app under "Credits" or Profile → Thanks. Especially **quer3q** for Dump Forge (it does the packing), AMPR EMU NG and Kraken Mount, **drakmor** for ShadowMountPlus, **itsPLK** for the Payload Manager, and **John Törnblom** and the contributors of the PS5 payload SDK (v0.50 with every firmware from 1.00 to 14.22).

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.88.1...v1.89.0
