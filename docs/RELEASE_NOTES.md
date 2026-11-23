# PS5 Cooling & System Center – Pro v1.92.0

## Highlights
- **A damaged home-screen picture is reported before you convert:** The check before converting (browser and fullscreen app, every format) now reads `icon0.png`, `pic0.png`, `pic1.png` and `pic2.png` from the folder or the image and names a picture that is not a valid PNG, in yellow. Starting and queueing stay possible; the picture is copied unchanged. Tried on the Slim with seven real games: not a single false warning.
- **Review of the whole app, browser and fullscreen:** Static analysis over all 125 source files, three reviewers, and a read-only pass over every route on the Slim. It found and fixed a way to block the picture check for minutes to hours, gaps when deleting PS4Mount games, and add-ons that were not removed by name when an installed game was deleted.
- **Fullscreen app: headings in capitals are translated now.** Steps such as "3. WHERE TO?" and window titles such as "copy", "move" and "delete" stayed German in every language; the fullscreen app now translates first and capitalises afterwards.

## What's new
### Converting
- **Picture check:** The warning reads "Not a valid PNG: sce_sys/pic2.png", followed by "The picture is copied unchanged and may be missing on the home screen." and, if a `.dds` of the same name lies next to it, that file too. A missing picture is not reported. The check looks at the PNG header, every chunk with its length, type and CRC-32 up to `IEND`; pictures above 64 MiB are not judged. It reads through a 1 MiB window, so even a picture made of 100,000 tiny chunks is checked in a moment (before, such a picture could have held the check for minutes to hours, and the drive could not be ejected meanwhile). In a folder, a picture is opened only if it is an ordinary file, without following links and without waiting.

### Games
- **Deleting an installed game takes its add-ons along:** The helper that asks the console to uninstall now names each add-on of the game when it asks for it to be removed (the console needs the add-on's name; the old call gave none). This is proven from the console libraries of 40 firmware versions, 1.00 to 12.70.
- **Deleting a PS4Mount game, switched off or busy:** The delete prepares the console's installer service itself before it unmounts anything, refuses while a stand-alone PS4Mount runs, and keeps trying for up to 2 minutes while the console is still uninstalling. Add-ons with their own title ID are unmounted too when PS4Mount is switched off, unless another mounted game uses the same one. The plan says exactly what happens: "the updates and add-ons PS4Mount mounted for it".
- **Copy and move:** A remembered plan is used only for the very same folder; another drive under the same name is measured again.

### Browser
- **Eject:** The page asks one thing at a time, and a late "still running" no longer overwrites "has been ejected".
- **Disc tab:** The features of a dump ("decrypted, FSELF") are separated by "·" and each one is translated.

### Fullscreen app
- **Translations:** Capitalised headings and window titles (see Highlights), the controller line in the log, the header of a running payload profile (it had read "RUNNING: AUTOSTART (STEP 1 OF 3)"), "Saved data" when importing a ZIP and the new plan item are translated in all six languages. The general patterns "in {0}" and "{0} in {1}" are gone from the dictionaries, because they left every other sentence with an "in" half German; the three texts that used them now show sentences the dictionaries know as a whole.

## Notes
- **Tried on the console** (Slim, firmware 12.00): all 80 read routes and 31 bad requests without a problem, app log and kernel log clean; the picture check and the plan for seven real games (two folders, an exFAT, three ffpfsc and one ffpkg image); the delete plan of a PS4Mount game; the controller line and the disc features in English.
- **Not tried:** deleting an installed game that has add-ons (the Slim has none installed; the fix rests on the firmware read-out), and a really damaged picture (the host test covers it).
- 389 tests of the fullscreen app, 44 host tests and the simulation pass. Send the new ELF to port 9021 and copy the tiles FAKE86921 and FAKE86922 from this release again (the fullscreen app must be closed for that).

## Downloads
| File | What it is for |
|---|---|
| `PS5_Cooling_System_Center_v1.92.0.elf` | The main program (send it to port 9021) |
| `FAKE86921.zip` | Fullscreen tile: copy the folder to `/data/homebrew/` |
| `FAKE86922.zip` | Browser tile: copy the folder to `/data/homebrew/` |
| `LIESMICH.txt` | Instructions (German) |
| `payload-manager.json` | List for itsPLK's Payload Manager: enter `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` as the source, and it downloads the app from GitHub (with a SHA-256 check) |

## Thanks
To everyone whose work is in the app or makes it possible – the full list is in the app under "Credits" or Profile → Thanks. Especially **gasb07** for PS4Mount, **quer3q** for Dump Forge, AMPR EMU NG and Kraken Mount, **drakmor** for ShadowMountPlus, **itsPLK** for the Payload Manager and the payload mirror, and **John Törnblom** and the contributors of the PS5 payload SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.91.0...v1.92.0
