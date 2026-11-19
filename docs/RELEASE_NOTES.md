# PS5 Cooling & System Center – Pro v1.88.1

## Highlights
- **Eject a USB drive safely – now also in the file manager:** Every USB drive has an **Eject** button next to it (browser app); in the fullscreen app press the Square button on the drive under "Locations", or △ → More inside a folder of the drive. Pulling a drive out while the console is running can damage its file system; this way it is safe, without shutting the console down.
- **Importing several saved data sets of one game is faster:** The search for a donor of the owner details now runs once per job instead of starting over for every saved data set.
- **The fullscreen tile never plays its tour with real data:** A tile that already runs with the console's data no longer accepts a request for the self-running tour.

## What's new
### File manager (browser and fullscreen app)
- **Eject in the file manager:** It is the same eject as under System → Storage (through ShadowMountPlus 1.7beta10 or newer): only while no job of the app is running and PS4Mount plays no game from the drive; otherwise the app says what is still running. Unplug the drive only when the console says "… is ejected and can be unplugged".
- **Afterwards the file manager tidies up:** It reads the locations again and leaves a folder of the drive (browser: to the internal SSD; fullscreen app: to the locations). The card in the fullscreen app shows "Ejecting …" and then the result.

### Transferring saved data
- **Donor search once per job:** When several saved data sets of one title were imported, up to 8 copies were mounted for each set to find the owner details; now it happens once per job. A set that already has one of its own on the console is still searched on its own.

### Fullscreen app
- **Self-running tour with demo data only:** The tile's self-running tour (it produces the pictures for the guide) presses "Confirm" on rows it finds in the demo data. A tile that was started with the real data now ignores the request.

### Housekeeping
- Dead assignments removed in the saved-data files and a guard added before an empty list; no visible effect.

## Notes
- **Confirmed on the console (19.11.2026):** ejecting in the file manager, in the browser app and in the fullscreen app, tried by the user on the console. Also 35 host tests and 375 tests of the fullscreen app. The eject itself has been unchanged since 29.10.2026 and is confirmed on the console.
- **Confirmed on the console (Slim, firmware 12.00, 19.11.2026):** the recovery of an interrupted import (at start-up and at the next import) and the refusal of apps in the import plan.
- Send the new ELF to port 9021 and copy the tiles FAKE86921 and FAKE86922 from this release again (the fullscreen app must be closed for that). Everything else is as in v1.88.0.

## Downloads
| File | What it is for |
|---|---|
| `PS5_Cooling_System_Center_v1.88.1.elf` | The main program (send it to port 9021) |
| `FAKE86921.zip` | Fullscreen tile (with Dump Forge): copy the folder to `/data/homebrew/` |
| `FAKE86922.zip` | Browser tile (with Dump Forge): copy the folder to `/data/homebrew/` |
| `LIESMICH.txt` | Instructions (German) |
| `payload-manager.json` | List for itsPLK's Payload Manager: enter `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` as the source, and it downloads the app from GitHub (with a SHA-256 check) |

## Thanks
To everyone whose work is in the app or makes it possible – the full list is in the app under "Credits" or Profile → Thanks. Especially **quer3q** for AMPR EMU NG, Kraken Mount and Dump Forge, **drakmor** for ShadowMountPlus, **itsPLK** for the Payload Manager and **earthonion** for Garlic SaveMgr (the model for the format of the ZIP file). And to the user who suggested ejecting in the file manager.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.88.0...v1.88.1
