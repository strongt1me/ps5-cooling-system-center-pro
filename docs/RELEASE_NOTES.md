# PS5 Cooling & System Center – Pro v1.98.0

## Highlights
- **Kraken Mount 0.4.2** is built in. It now also mounts game images (`.exfat`, `.ffpkg`, `.ffpfs`, `.ffpfsc`, read-only), so game folders, images and debug packages count the same. The tab is called **"KrakenMount"** now (it was "FPKGMount").
- **Folder "Kraken-Games" instead of "FPKG-Games":** the old name sounded as if only FPKG belonged there. The app renames the folder (internal storage and every plugged-in drive) by itself the next time it starts Kraken Mount – only by renaming, nothing is copied or overwritten.
- **Convert and copy with three targets** (web and fullscreen): **A · Playable with ShadowMountPlus**, **B · Playable with Kraken Mount** and **C · Backup**.

## What's new
- **Eject:** While Kraken Mount has an image of a drive mounted, the app refuses to eject that drive and names the image – the console would otherwise pull the drive away from under it. Kraken Mount's own "Eject" frees it.
- **KrakenMount tab:** An image game is shown as "Image (exfat)" and so on; "Delete game" and "Source" name the image file.
- **FPKG:** Target B is chosen from the start (ShadowMountPlus does not mount a `.pkg`). PS4 games cannot go to B: Kraken Mount plays PS5 games. No debug FPKG is offered for the image of a PS4 game any more.
- **Copy to B:** If Kraken Mount does not search the `Kraken-Games` folder of the chosen drive (its folder list was changed on its own page), the copy window says so, as the check before a conversion already did.
- **Renaming the folder only when it is safe:** Kraken Mount really stopped, no game and no job running, and nothing mounted from an old `FPKG-Games` folder any more. Otherwise the app waits for the next start and writes that once to the log.
- **Fullscreen app:** The row "Target" offers only the kinds that fit the game and format; "C · Backup" could be unreachable before.
- **Duplicate images** (ShadowMountPlus) no longer counts what lies in Kraken Mount's folders; an image set aside there earlier stays listed and can be taken back.
- **ShadowMountPlus:** If Kraken Mount answers on ShadowMountPlus's port, the app only sends it requests again once ShadowMountPlus itself answers there.
- New messages translated into all six languages; manual and FAQ in seven languages updated.

## Notes
- **Tried on the console (Slim):** the folder move FPKG-Games → Kraken-Games, image games mounted by Kraken Mount (internal storage and USB), the refused eject and Kraken Mount's own eject, and copying and converting a game (Asterix & Obelix: Heroes, 1.74 GB) to target B – the copy once more with this release's state. On the Pro the folder move was confirmed. Tested on the PC: 47 host tests with the fan simulation, 396 tests of the fullscreen app.
- **If Kraken Mount is running while you update the app,** it keeps its old folder list until it is started again. The KrakenMount tab says so: switch Kraken Mount off and on once, then the app renames the folder.
- Send the new ELF to port 9021 and copy the tiles FAKE86921 and FAKE86922 from this release again (the fullscreen app must be closed for that). After that, reload an open web page once.

## Downloads
| File | What it is for |
|---|---|
| `PS5_Cooling_System_Center_v1.98.0.elf` | The main program (send it to port 9021) |
| `FAKE86921.zip` | Fullscreen tile: copy the folder to `/data/homebrew/` |
| `FAKE86922.zip` | Browser tile: copy the folder to `/data/homebrew/` |
| `LIESMICH.txt` | Instructions (German) |
| `payload-manager.json` | List for itsPLK's Payload Manager: enter `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` as the source, and it downloads the app from GitHub (with a SHA-256 check) |

## Thanks
To everyone whose work is in the app or makes it possible – the full list is in the app under "Credits" or Profile → Thanks. Especially **quer3q** for Kraken Mount, Dump Forge and AMPR EMU NG, **abcde6543** for PS5-NTFS-BROWSER, **gasb07** for PS4Mount, **drakmor** for ShadowMountPlus, **itsPLK** for the Payload Manager and the payload mirror, and **John Törnblom** and the contributors of the PS5 payload SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.97.0...v1.98.0
