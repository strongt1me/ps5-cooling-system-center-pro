# PS5 Cooling & System Center – Pro v1.95.1

## Highlights
- **Kraken Mount is no beta any more:** The FPKGMount tab (browser and fullscreen app) no longer calls Kraken Mount "still a beta", and the manual gives what the Kraken Mount 0.3.2 readme says instead: for large, demanding games, zstd packs run better than a `.pkg`.

## What's new
- Text only, in all seven languages (tab note, dictionaries, manual). Everything else is as in v1.95.0 – see its release notes for the Kraken Mount 0.3.2 update, the safer NTFS drives, the full review of the app and the "Replace" fix.

## Notes
- **Tried on the console:** This build runs on the Pro and the Slim (version 1.95.0 in the name, same code).
- 392 tests of the fullscreen app and the dictionary check pass. Send the new ELF to port 9021 and copy the tiles FAKE86921 and FAKE86922 from this release again (the fullscreen app must be closed for that).

## Downloads
| File | What it is for |
|---|---|
| `PS5_Cooling_System_Center_v1.95.1.elf` | The main program (send it to port 9021) |
| `FAKE86921.zip` | Fullscreen tile: copy the folder to `/data/homebrew/` |
| `FAKE86922.zip` | Browser tile: copy the folder to `/data/homebrew/` |
| `LIESMICH.txt` | Instructions (German) |
| `payload-manager.json` | List for itsPLK's Payload Manager: enter `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` as the source, and it downloads the app from GitHub (with a SHA-256 check) |

## Thanks
To everyone whose work is in the app or makes it possible – the full list is in the app under "Credits" or Profile → Thanks. Especially **quer3q** for Kraken Mount, Dump Forge and AMPR EMU NG, **abcde6543** for PS5-NTFS-BROWSER, **gasb07** for PS4Mount, **drakmor** for ShadowMountPlus, **itsPLK** for the Payload Manager and the payload mirror, and **John Törnblom** and the contributors of the PS5 payload SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.95.0...v1.95.1
