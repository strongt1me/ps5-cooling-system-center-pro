# PS5 Cooling & System Center – Pro v1.95.0

## Highlights
- **Kraken Mount 0.3.2 (quer3q):** FPKGMount brings the new version: closing a game no longer hangs, apps that start a second ELF on exit work from a `.pkg`, homebrew sees the console's fonts, and ampr-emu-ng 0.5.1 is bundled. Its own update check is switched off in the app's copy – an update from its page would arrive unadapted and wait for ShadowMountPlus; newer versions keep coming through the app's button.
- **NTFS drives are safer:** A stick pulled while the app adds or deletes files in a big folder no longer loses names that were there before. Crafted sticks can no longer make the driver write past its buffers or eat hundreds of MB, and Windows' own files below `$Extend` stay untouched.
- **A full review of the app:** fixes in fan control, jobs, saves, the web pages and the fullscreen app – see below.
- **"Replace" no longer loses the old file:** When a copy that was to replace a file on the console failed or was cancelled, the old file was gone as well.

## What's new
### Fan control
- **Emergency limit:** The limit in force is the stored one or target + 4 °C, whichever is higher; settings no longer raise the stored value for good (after a single 91 °C target the fan stayed capped at the comfort limit up to 95 °C).
- **Single outliers:** A reading more than 12 °C off the previous one is held back for one pass; if the next one confirms it, it counts. A +40 K glitch every 150 s no longer runs the fan up for minutes.
- **Observe mode:** A threshold set by hand stays set (also after a restart); without one the console keeps its own setting – now also at start.
- **Guard and exit:** "Fan at full" excuses an overheat for 60 s at most; ending the app hands the fan back to the console in every case.

### Games and jobs
- **Unpacking LZ4/zstd packs:** If an unpack fails, is cancelled or does not match the packs, the files and folders it put there are taken away again – the game stays packed as before. Tried on the console with a test copy (cancel at a third, LZ4 and zstd: nothing left behind).
- **"As a new folder beside it":** Fails instead of saying "done" when a file is missing; `param.json`/`param.sfo` come last; the original's AMPR index stays exactly as it was.
- **Dump Forge:** The result is checked by the app before a job counts as done and before "Original afterwards" may delete the original.
- **Kraken Mount 0.3.2:** See the highlights. "Rebuild" stays disabled until a requested rebuild is done.

### Saves
- **Backups include the PS5 save database** (`savedata_prospero`), not only the PS4 one – checked against firmware 1.00 to 12.70.
- Leftovers of an interrupted import are cleared at start; setuid/setgid bits from a ZIP are dropped; a restore says when extra files could not be removed.

### File manager: NTFS
- **Interrupted index changes:** The write order of the upstream fix, plus ordering points for the app's write cache. A test cuts every add and remove after each write: 0 of 901 cut points lose a name (11 of 1,094 before).
- **Crafted sticks:** Records are checked like ntfs-3g does; an attribute list repeating itself, an absurd index size or a foreign record in a deleted file's list are refused.
- **Moving:** The target is on the stick before the original is deleted; Windows' system entries are not moved.
- **Names:** Too long names are refused instead of cut; COM0, LPT0 and COM¹–LPT³ get a name Windows can open.

### Web pages
- Translations of composed lines, a quick choice that no longer breaks words in narrow cards, readable buttons and red text in every design, Russian plural forms (116 messages), 82 unused dictionary entries removed.

### Fullscreen app
- Drive telemetry under System → Expert works again; long languages fit (log, cards, dialogs, sidebar 390 px, game tabs, details sheet); twelve missing texts added.

## Fixes
- **"Replace" when copying to the console:** The old file stays when the copy fails or is cancelled (console and NTFS sources).
- **Server:** A NUL in a request head or `\u0000` in a request body is refused at once; downloads must move at least 64 KB in 30 s like every other answer (a trickling reader held a connection, and on NTFS the drive, for ever).
- **Start of the app:** An older copy found over HTTP is only ended when it is really a payload.

## Notes
- **Tried on the console:** Pro (firmware 12.00): unpack rollback with LZ4 and zstd, Kraken Mount 0.3.2 with a game (started, ran, closed; its update check off, no tile of its own). Slim: the NTFS fixes on a test stick (300 files added, all read back, 150 deleted, the rest read back, the new name rules).
- **Kraken Mount:** A running Kraken Mount stays the old version until it is switched off and on again in the FPKGMount tab; the first start of 0.3.2 rebuilds the caches of `.pkg` games once.
- 46 host tests with the fan simulation, 392 tests of the fullscreen app and the dictionary check pass. Send the new ELF to port 9021 and copy the tiles FAKE86921 and FAKE86922 from this release again (the fullscreen app must be closed for that).

## Downloads
| File | What it is for |
|---|---|
| `PS5_Cooling_System_Center_v1.95.0.elf` | The main program (send it to port 9021) |
| `FAKE86921.zip` | Fullscreen tile: copy the folder to `/data/homebrew/` |
| `FAKE86922.zip` | Browser tile: copy the folder to `/data/homebrew/` |
| `LIESMICH.txt` | Instructions (German) |
| `payload-manager.json` | List for itsPLK's Payload Manager: enter `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` as the source, and it downloads the app from GitHub (with a SHA-256 check) |

## Thanks
To everyone whose work is in the app or makes it possible – the full list is in the app under "Credits" or Profile → Thanks. Especially **quer3q** for Kraken Mount, Dump Forge and AMPR EMU NG, **abcde6543** for PS5-NTFS-BROWSER, **gasb07** for PS4Mount, **drakmor** for ShadowMountPlus, **itsPLK** for the Payload Manager and the payload mirror, and **John Törnblom** and the contributors of the PS5 payload SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.94.0...v1.95.0
