# PS5 Cooling & System Center – Pro v1.91.0

## Highlights
- **Deleting PS4Mount games works:** The game delete (browser and fullscreen) now recognises a PS4 game that PS4Mount mounts from a USB drive and hands it to PS4Mount's "Remove": PS4Mount unmounts it first, then the console uninstalls it. The PKG files stay on the drive, and "Restore tile" on the PS4Mount page brings the game back. Until now the delete refused such games, or the console refused with error 0x80AF5002. Tried on the Slim.
- **Dump Forge 0.5.7 inside the app** (was 0.5.6): A game's own AMPR index whose table starts at an 8-byte boundary (for example Crisis Core) is no longer called damaged; it is kept when converting. The job log says why an index is damaged, and `.ffpfsc` images whose file ends at the last data byte can be read. Tried on the Slim: a debug FPKG of a 1.74 GB game, built and fully verified in 94 seconds.

## What's new
### Games
- **Delete a PS4Mount game:** The delete window names the kind "Game from PS4Mount" and says that the PKG files stay on the USB drive and that PS4Mount does not add the game again by itself. It works even when PS4Mount is switched off or the PKG was already deleted. If PS4Mount is busy, the delete waits up to 2 minutes.
- **PS4Mount's "Remove"** says "The PKG stays on the USB drive" only when it really is still there.

### Converting
- **Dump Forge 0.5.7:** The interface the app uses, the stages, the compression levels, the name limit (63 bytes) and the bundled AMPR EMU NG (0.4.1) are unchanged. Packing while converting still needs Dump Forge 0.5.6 or newer.
- **Guide in the convert window:** In the fullscreen app, way 1 ("The app loads a newer version itself") now points to the window's own "Load newer version" row, right above "Guide", instead of sending you to the browser. In both surfaces a German sentence had subject and object swapped ("Dump Forge starts the app …"); it now says "The app starts Dump Forge …" (the translations were already right).

## Notes
- **Tried on the console** (Slim, firmware 12.00): deleting a PS4Mount game (Tetris Ultimate, PKG on a USB stick) through the game card and adding it back with "Restore tile"; Dump Forge 0.5.7 started from the app, built and verified a debug FPKG and was closed again. The fullscreen guide was checked in the PC preview (German, English, Russian), not on the console.
- **Not tried:** deleting a PS4Mount game that has add-ons (no PS4 add-on packages at hand).
- 387 tests of the fullscreen app, 43 host tests and the simulation pass. Send the new ELF to port 9021 and copy the tiles FAKE86921 and FAKE86922 from this release again (the fullscreen app must be closed for that).

## Downloads
| File | What it is for |
|---|---|
| `PS5_Cooling_System_Center_v1.91.0.elf` | The main program (send it to port 9021) |
| `FAKE86921.zip` | Fullscreen tile: copy the folder to `/data/homebrew/` |
| `FAKE86922.zip` | Browser tile: copy the folder to `/data/homebrew/` |
| `LIESMICH.txt` | Instructions (German) |
| `payload-manager.json` | List for itsPLK's Payload Manager: enter `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` as the source, and it downloads the app from GitHub (with a SHA-256 check) |

## Thanks
To everyone whose work is in the app or makes it possible – the full list is in the app under "Credits" or Profile → Thanks. Especially **quer3q** for Dump Forge 0.5.7, AMPR EMU NG and Kraken Mount, **gasb07** for PS4Mount (its "Remove" now also serves the game delete), **drakmor** for ShadowMountPlus, **itsPLK** for the Payload Manager and the payload mirror, and **John Törnblom** and the contributors of the PS5 payload SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.90.0...v1.91.0
