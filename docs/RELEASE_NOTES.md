# PS5 Cooling & System Center – Pro v1.96.0

## Highlights
- **Search in the file manager:** A search field above the list filters the open folder as you type. With "Include subfolders" the console searches the folder and everything below it – from a drive, the whole drive (console, USB, M.2 and NTFS).
- **Game titles next to title IDs:** Folders and images named after a title ID (`PPSA01325`, `PPSA01325.exfat`, `CUSA05041-app`) show the game's title, and the search finds them by it.
- **Kraken Mount 0.4.0 (quer3q):** FPKGMount brings the new version with its redesigned page, installing FPKG games to the console and backports for installed games.

## What's new
### File manager: search
- **Part of the name**, upper and lower case the same (also Ä/Ö/Ü). With `*` and `?` the whole name must match (`*.pkg`, `PPSA?????`).
- **Include subfolders:** Hits arrive bit by bit, each with the folder it is in. At most 2,000 hits and 1 million entries checked; links are not followed, another drive below is not entered, and the whole system (`/`) is not searched. The search can be cancelled; an eject ends it first.
- **Working with hits:** Select, copy, move, delete (asks first, as always), rename, view and measure work as with normal entries. "Open folder" jumps to the hit's folder and highlights it.
- **Fullscreen app:** △ → "Search …" (on-screen keyboard), "Include subfolders: on/off", "Change search …" and "End search"; X opens a hit, O ends the search.

### File manager: game titles
- A folder takes the title from its own `sce_sys/param.json` (PS5) or `param.sfo` (PS4), otherwise from the games installed and registered on the console. Images are not mounted for it; on NTFS drives only the console's games count.
- Browser: the title stands below the name. Fullscreen app: after the name and as the row "Game".

### Kraken Mount 0.4.0
- Both of the app's one-byte changes still apply: Kraken Mount does not wait for ShadowMountPlus, and its own update check stays off (newer versions keep coming through the app's button). ampr-emu-ng 0.5.1 is still bundled.
- **Installed games stay out of the FPKGMount tab:** Kraken Mount 0.4.0 also lists the games installed on the console. The app leaves them out of its tab, the duplicate warning, the FPKGMount badge and the delete check – they are under Games as always.
- Switching Kraken Mount off is refused while it installs a game; the app now says so.
- New on Kraken Mount's own page (port 8097): install FPKG games with patches and DLC, backports (`backports/<title ID>`) for installed games too, light and dark design with colour schemes and a language choice, tile music that stays after a game.

## Notes
- **Tried on the console:** This build runs on the Pro and the Slim (version 1.95.1 in the name, same code). The search and the game titles were tried in the browser on the Pro; Kraken Mount 0.4.0 runs on both, and both leave installed games out of the tab.
- **Backports next to ShadowMountPlus:** On the Slim, Kraken Mount 0.4.0 and ShadowMountPlus 1.7beta12 applied the same test backport of an installed game at the same time: the game started and ran, both released their redirect at exit, no crash.
- **Not tried by hand on a console:** the search in the fullscreen app with the controller (PC pictures only).
- All 46 host tests, 396 tests of the fullscreen app and the dictionary check pass. Send the new ELF to port 9021 and copy the tiles FAKE86921 and FAKE86922 from this release again (the fullscreen app must be closed for that). A Kraken Mount that is running keeps its old version until it is switched off and on once.

## Downloads
| File | What it is for |
|---|---|
| `PS5_Cooling_System_Center_v1.96.0.elf` | The main program (send it to port 9021) |
| `FAKE86921.zip` | Fullscreen tile: copy the folder to `/data/homebrew/` |
| `FAKE86922.zip` | Browser tile: copy the folder to `/data/homebrew/` |
| `LIESMICH.txt` | Instructions (German) |
| `payload-manager.json` | List for itsPLK's Payload Manager: enter `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` as the source, and it downloads the app from GitHub (with a SHA-256 check) |

## Thanks
To everyone whose work is in the app or makes it possible – the full list is in the app under "Credits" or Profile → Thanks. Especially **quer3q** for Kraken Mount, Dump Forge and AMPR EMU NG, **abcde6543** for PS5-NTFS-BROWSER, **gasb07** for PS4Mount, **drakmor** for ShadowMountPlus, **itsPLK** for the Payload Manager and the payload mirror, and **John Törnblom** and the contributors of the PS5 payload SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.95.1...v1.96.0
