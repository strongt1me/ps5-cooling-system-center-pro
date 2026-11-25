# PS5 Cooling & System Center – Pro v1.96.1

## Highlights
- **Old page, new app:** When a new app arrives while the web interface is open, the page now says so: "New version … running" with a **Reload page** button. Before, only the version shown was new, while new buttons and fields were missing – one user could not find the file search of 1.96.0 for that reason.
- **Old tile, new app:** The fullscreen app shows the tile's version in yellow next to the app version and once the message "Copy the tile from the release again" when the tile is older than the running app.

## What's new
- **Web interface:** The page remembers the app version of its first load. When the app reports another one, the notice appears at the top of every page. It stays until the page is reloaded; the page does not reload by itself, so nothing typed is lost.
- **Fullscreen app:** The tile reads the version of the app it carries and compares it with the running one. A newer tile still replaces an older app by itself, as before.
- Everything from v1.96.0 (file manager search, game titles, Kraken Mount 0.4.0) is included – see its release notes.

## Notes
- **Tried on the console:** This build runs on the Pro and the Slim (version 1.96.0 in the name, same code). The notice itself appears only when the version number changes, so it was tried in the demo mode (web, 1.95.1 → 1.96.0, also in English) and as PC pictures (fullscreen app); on the console it shows up with this release for the first time.
- All 46 host tests, 396 tests of the fullscreen app and the dictionary check pass. Send the new ELF to port 9021 and copy the tiles FAKE86921 and FAKE86922 from this release again (the fullscreen app must be closed for that). After that, reload an open web page once.

## Downloads
| File | What it is for |
|---|---|
| `PS5_Cooling_System_Center_v1.96.1.elf` | The main program (send it to port 9021) |
| `FAKE86921.zip` | Fullscreen tile: copy the folder to `/data/homebrew/` |
| `FAKE86922.zip` | Browser tile: copy the folder to `/data/homebrew/` |
| `LIESMICH.txt` | Instructions (German) |
| `payload-manager.json` | List for itsPLK's Payload Manager: enter `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` as the source, and it downloads the app from GitHub (with a SHA-256 check) |

## Thanks
To everyone whose work is in the app or makes it possible – the full list is in the app under "Credits" or Profile → Thanks. Especially **quer3q** for Kraken Mount, Dump Forge and AMPR EMU NG, **abcde6543** for PS5-NTFS-BROWSER, **gasb07** for PS4Mount, **drakmor** for ShadowMountPlus, **itsPLK** for the Payload Manager and the payload mirror, and **John Törnblom** and the contributors of the PS5 payload SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.96.0...v1.96.1
