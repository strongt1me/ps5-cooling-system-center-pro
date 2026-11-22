# PS5 Cooling & System Center – Pro v1.90.0

## Highlights
- **Ejecting USB drives safely:** The console cuts a drive off by force and asks nobody. The app now frees the drive first: everything of the app that reads from it waits or is refused, everything is written to the drive (sync), and only then is ShadowMountPlus asked to eject. While it runs the app says "Ejecting usb0 … Do not unplug it until “is ejected” appears." Tried on the Slim and the Pro, from the file manager and from the fullscreen app, without a crash.
- **Dump Forge is built into the app:** The FPKG format and packing while converting no longer need a download from GitHub. A button still fetches a newer version, and files you place yourself are checked (SHA-256, cut-off downloads are refused).
- **The whole app reviewed (browser and fullscreen):** two memory errors in file readers fixed, the web interface accepts only the console's IP address and local names, a foreign web page can no longer trigger the API, 168 messages and 97 fullscreen texts translated in six languages.
- **Serial number shortened or whole:** a button on the system page (X button in the fullscreen app) switches between `••••8023` and the whole number; the choice is stored on the console.

## What's new
### USB drives
- **Eject:** The file manager leaves the drive first and ejects through System → Storage; copying, moving, deleting, saving, uploading and every planning step pass a drive gate and are refused while a drive is being ejected or settles (30 s). If the console or the app crashed during an eject, ejecting stays off until you allow it again (browser and fullscreen).
- **"Do not unplug yet":** The message stays up until the eject has finished (it took 2 to 7 seconds on the test consoles); "is ejected" only appears after the console has unmounted the drive. Some drives come back by themselves after a few seconds, others only after plugging them in again.
- **After the eject:** no more "usb0 · 2 MB free" ghost; a drive plugged in again appears without switching pages; the empty folder of an ejected drive (`/mnt/usb0`, it lies on the small system partition) can no longer be written to.

### Games and converting
- **Dump Forge 0.5.6 inside the app** (the ELF grows to about 24 MB); the tiles no longer carry their own copy. Button "Download newer version from GitHub" in the convert window.
- **Progress while measuring** for packs: bar, percent and time left instead of a still text.
- **AMPR EMU NG 0.5.0** (the two newest versions are embedded: 0.5.0 and 0.4.1) and **Kraken Mount 0.3.1** with the **PlayGo stub per game** (switch in the game list of FPKGMount, browser and fullscreen).
- **PS4Mount and Kraken Mount are off by default;** whoever switches one on keeps it on after a restart (tried on the Slim). PS4Mount 0.8.0 tried with real PS4 packages on the Slim: five games registered from a USB stick, one started and closed again; then a game with its update (1.02) from the same stick: the update was found and used, the game started as 1.02.

### Payloads
- **Second list in "Download payloads":** `strongt1me/ps5-payload-source` next to itsPLK's mirror (browser and fullscreen). Every file is still checked against the SHA-256 in its own list.

### System
- **Serial number switch** (see above); the whole number leaves the app only when chosen.
- **Controller battery:** the fullscreen app shows the state and the age of the value like the browser ("80 % · on battery · as of 9 h ago"); the hint "switch the controller off and on for a current value" is translated now.
- **Browser:** the version tile is orange.

### Security and stability
- **Two memory errors fixed** (reproduced with AddressSanitizer): a crafted `ampr_zstd.index` and a crafted `playgo-chunk.dat` in a PS4 package could make the app read past its buffers (the second one crashed it). Further checks for PFS block tables and PS4 package sizes, two 32 KiB buffers moved off the job stacks.
- **Addresses:** only an IP address, `localhost` or a private home-network name (`.local`, `.lan`, `.home`, `.home.arpa`, `.internal`, `.localdomain`, `.intranet`). Names without a dot and `.fritz.box` are refused (DNS rebinding).
- **Foreign web pages** (`Sec-Fetch-Site` cross-site/same-site) cannot call the API; downloads open regular files only; `nosniff`; uploads only with POST; a few connections are kept free for the console itself.
- **SQLite 3.54.1** (was 3.46.0), used to register an imported save.

### Manual, FAQ, translations
- Manual and FAQ checked against the code and corrected in seven languages (FAQ now 60 questions); new screenshots in all seven languages.
- Translations: 168 server messages and 97 fullscreen texts in six languages; upper-case headings in the fullscreen app show umlauts and Cyrillic correctly; "(game rule)" under the temperature ring is translated.

## Notes
- **Addresses changed:** If you opened the web interface through `ps5.fritz.box` or a name without a dot, use the console's IP address now.
- **PS4Mount and Kraken Mount after the update:** Whoever never switched them shows both off – switch them on once if you use them. Kraken Mount has to be switched on again even if it ran before (until now only "off" was stored).
- **Tried on the console** (Slim, firmware 12.00): eject from the file manager (also refused correctly while something still read from the stick), the "do not unplug yet" message, the fullscreen eject, the controller battery line, importing a save with SQLite 3.54.1 (the entry in `savedata.db` was written), a PS4 game with its update through PS4Mount (1.02 found and active, started as 1.02 and closed again), 80 read routes and 31 malformed requests without a crash, every browser page in English. The Pro (firmware 12.00) was tried with the eject only, on an earlier build.
- **Not proven:** whether this ends the eject crash one user had on firmware 4.03 (no feedback yet); PS4Mount add-ons were not tried on the console (no PS4 add-on packages at hand).
- 46 host test runs and 387 tests of the fullscreen app pass. Send the new ELF to port 9021 and copy the tiles FAKE86921 and FAKE86922 from this release again (the fullscreen app must be closed for that).

## Downloads
| File | What it is for |
|---|---|
| `PS5_Cooling_System_Center_v1.90.0.elf` | The main program (send it to port 9021) |
| `FAKE86921.zip` | Fullscreen tile: copy the folder to `/data/homebrew/` |
| `FAKE86922.zip` | Browser tile: copy the folder to `/data/homebrew/` |
| `LIESMICH.txt` | Instructions (German) |
| `payload-manager.json` | List for itsPLK's Payload Manager: enter `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` as the source, and it downloads the app from GitHub (with a SHA-256 check) |

## Thanks
To everyone whose work is in the app or makes it possible – the full list is in the app under "Credits" or Profile → Thanks. Especially **quer3q** for Dump Forge (now inside the app), AMPR EMU NG and Kraken Mount, **drakmor** for ShadowMountPlus (it ejects the drives), **gasb07** for PS4Mount, **itsPLK** for the Payload Manager and the payload mirror, the **SQLite** authors, and **John Törnblom** and the contributors of the PS5 payload SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.89.0...v1.90.0
