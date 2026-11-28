# PS5 Cooling & System Center – Pro v1.99.0

## Highlights
- **Kraken Mount plays ShadowMountPlus's games while ShadowMountPlus is not running.** If ShadowMountPlus is not loaded (after a restart, for example), the app gives Kraken Mount the folders ShadowMountPlus searches after a minute: `homebrew` and `etaHEN/games` (internal and on every drive) and the main folder of every drive. Kraken Mount then registers and plays the games there: folders, images (`.exfat`, `.ffpkg`, `.ffpfs`, `.ffpfsc`) and `.pkg` files. When ShadowMountPlus starts, the app gives the folders back within seconds; Kraken Mount releases its mounts and images by itself.
- **A lamp in the KrakenMount tab** (web and fullscreen) shows at a glance which case applies: green and breathing = Kraken Mount has the folders, amber blinking = it takes them over shortly or is giving them back, blue = ShadowMountPlus has them, grey = a folder list changed by hand.
- **"Delete" in the game list now really deletes a game of Kraken Mount.** It used to run as an installed game: the console removed only the tile, the data stayed, and Kraken Mount registered the game again at its next scan. Now Kraken Mount deletes it itself (unmount, data, tile, cache, checksum file).

## What's new
- No restart of Kraken Mount is needed for the switch. Every start of Kraken Mount by the app begins without ShadowMountPlus's folders, so that a ShadowMountPlus from the autostart has time when the console is switched on. A folder list changed by hand on Kraken Mount's own page is left alone.
- ShadowMountPlus counts as running when its process runs or it answered on its port in the last minute. If the switch fails once, the app tries again after 5 s.
- While Kraken Mount has the folders, the game list shows the real path and format of those games (no more dead `/mnt/shadowmnt` paths), so copying and converting work. The app's own tiles never appear in the KrakenMount tab.
- "Original afterwards": deleting goes through Kraken Mount when it plays the original; moving such an original is refused up front. `Kraken-Games` is no target for moving an original any more.
- "Set image aside" is refused while Kraken Mount has the image mounted. The Packages tab names the real reason when Kraken Mount plays a `.pkg` without installing it. The duplicate warning does not count the hand-over between the two as a conflict.
- Delete refusals name the real reason: Kraken Mount switched off, busy, or not listing the game yet. Long paths are no longer cut.
- Translations in six languages, manual in seven languages.
- **Shorter manual and FAQ** in all seven languages (about 40 % shorter, 45 instead of 62 questions), checked against the code; `docs/HANDBUCH.md` reorganised and half as long; READMEs, LIESMICH and the developer documents brought up to date.
- **Credits** (web and fullscreen): ps5-app-dumper (Echo Stretch and slopmaster33), idlesauce (ps5-self-pager), Daniel Nylander (Swedish translation), OffAct, np-fake-signin, Dump Forge, PS4FFPFSC and Transmission added.

## Notes
- **Tried on the console:**
  - On the Pro, after a restart without ShadowMountPlus: switch-over after 60 s, 14 games ready without conflicts, games started through Kraken Mount, the game list with real paths, the hand-back to ShadowMountPlus in 2 s.
  - With a 6 GB test copy: moving refused up front, the original deleted through Kraken Mount after a conversion, "set image aside" refused, the copy deleted from the game list.
  - On the Slim: a delete through Kraken Mount (1.74 GB test copy).
- **Tested on the PC:** 47 host tests with the fan simulation and 396 tests of the fullscreen app.
- A `.pkg` in a drive's main folder is played by Kraken Mount without installing it while ShadowMountPlus is not running; to keep it playable with ShadowMountPlus too, put it into `Kraken-Games`.
- Send the new ELF to port 9021 and copy the tiles FAKE86921 and FAKE86922 from this release again (the fullscreen app must be closed for that). After that, reload an open web page once.

## Downloads
| File | What it is for |
|---|---|
| `PS5_Cooling_System_Center_v1.99.0.elf` | The main program (send it to port 9021) |
| `FAKE86921.zip` | Fullscreen tile: copy the folder to `/data/homebrew/` |
| `FAKE86922.zip` | Browser tile: copy the folder to `/data/homebrew/` |
| `LIESMICH.txt` | Instructions (German) |
| `payload-manager.json` | List for itsPLK's Payload Manager: enter `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` as the source, and it downloads the app from GitHub (with a SHA-256 check) |

## Thanks
To everyone whose work is in the app or makes it possible – the full list is in the app under "Credits" or Profile → Thanks. Especially **quer3q** for Kraken Mount, Dump Forge and AMPR EMU NG, **drakmor** for ShadowMountPlus, **abcde6543** for PS5-NTFS-BROWSER, **gasb07** for PS4Mount, **itsPLK** for the Payload Manager and the payload mirror, and **John Törnblom** and the contributors of the PS5 payload SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.98.0...v1.99.0
