# PS5 Cooling & System Center – Pro v1.93.0

## Highlights
- **NTFS drives in the file manager:** A stick or hard drive with NTFS (the Windows file system), which the console does not mount itself, appears as `ntfs0` and can be read and written: view, download, upload, new folder, copy and move in both directions, delete, eject safely. In the browser and in the fullscreen app, with the driver of PS5-NTFS-BROWSER (abcde6543).
- **Safe with broken or crafted sticks:** The NTFS driver is hardened against damaged media (fuzzing with more than 22 million runs, images that hang or crash the original driver). A stick that Windows has not fully let go of, or that was unplugged in the middle of writing, stays read-only, with the reason and what to do.
- **Firmware badge follows the game's SDK:** If the `param.json` asks for a newer firmware but the `eboot.bin` is built with a fitting SDK, the badge stays grey and says why the game starts anyway. FPKGMount games (Kraken Mount) are now checked for a backport too.

## What's new
### File manager: NTFS
- **Read and write:** Every NTFS partition (MBR or GPT) becomes a location of its own (`ntfs0`, `ntfs1` …). Renaming, editing and replacing are not possible – the driver never changes an existing file; for names that are taken there are "Keep both" and "Skip". Names Windows does not allow are adjusted with `_`. Games cannot be started from NTFS.
- **Read-only, with the reason:** unplugged without "Safely remove", Fast Startup, chkdsk unfinished, a hibernated Windows system disk, unplugged from the PS5 in the middle of writing – the note says what to do on the Windows PC.
- **Eject safely:** button on the NTFS location (fullscreen: Square button). Writes out what is pending, empties the stick's write cache and lets it go ("ntfs0 can be unplugged now.").
- **Care when plugging in:** The app opens whole disks only, a freshly plugged one only after 10 s, never during an eject, and tries a disk that does not open only at growing intervals.
- **Uploads in whole blocks:** 2.8 MB/s on a slow stick instead of 17 KB/s and an abort before.

### Games
- **Firmware badge and backport for FPKGMount games:** See the highlights. The fullscreen app shows the row "Built with SDK" in the details.
- **PS4Mount:** PS4 games on the M.2 expansion are no longer registered (the console aborts their start); they are listed under "Found, not set up" with the reason. Right after registering, a game starts at once (before: "not in the game list").
- **Start on "Device busy":** If ShadowMountPlus briefly refuses a start, the app tries twice more, 3 s apart.

### Full reviews
- Two reviews of the whole app with several reviewers, counter-checks and stress tests. Fixed among others: ejecting a USB drive while PS4Mount searches; images with folders that point to themselves; exFAT folders of other tools ("NoFatChain") that counted as damaged; a double game start; the payload store replaces a foreign file only after asking; zstd-packed games are named correctly in the AMPR window.
- **Fullscreen app:** A save-game ZIP on a drive can be deleted (asks first); before installing a package it asks for the plan; conflict question, note cards and more than 30 translations fixed.

## Notes
- **Tried on the console** (Slim, firmware 12.00): NTFS with two sticks (250 GB and 60 GB) – upload, read back with checksum, copies in every direction, move, delete, eject, plugging in again after 10 s; a stick unplugged from a Windows PC and one unplugged from the PS5 in the middle of writing appear read-only with the fitting reason. All 80 read-only routes and 31 malformed requests without a problem, kernel log without a crash.
- **Known:** Very large folders on NTFS (about 100,000 files) are slow; on a slow stick every new small file takes one to three seconds, and the first write after each start of the app empties the stick's Windows journal (a few seconds).
- 391 tests of the fullscreen app, 46 host tests and the simulation pass. Send the new ELF to port 9021 and copy the tiles FAKE86921 and FAKE86922 from this release again (the fullscreen app must be closed for that).

## Downloads
| File | What it is for |
|---|---|
| `PS5_Cooling_System_Center_v1.93.0.elf` | The main program (send it to port 9021) |
| `FAKE86921.zip` | Fullscreen tile: copy the folder to `/data/homebrew/` |
| `FAKE86922.zip` | Browser tile: copy the folder to `/data/homebrew/` |
| `LIESMICH.txt` | Instructions (German) |
| `payload-manager.json` | List for itsPLK's Payload Manager: enter `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` as the source, and it downloads the app from GitHub (with a SHA-256 check) |

## Thanks
To everyone whose work is in the app or makes it possible – the full list is in the app under "Credits" or Profile → Thanks. Especially **abcde6543** for PS5-NTFS-BROWSER, **gasb07** for PS4Mount, **quer3q** for Dump Forge, AMPR EMU NG and Kraken Mount, **drakmor** for ShadowMountPlus, **itsPLK** for the Payload Manager and the payload mirror, and **John Törnblom** and the contributors of the PS5 payload SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.92.0...v1.93.0
