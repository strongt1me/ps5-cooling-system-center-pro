# PS5 Cooling & System Center – Pro v1.94.0

## Highlights
- **NTFS drives are much faster:** Big folders, many small files and the first write after the app starts no longer crawl. On the console with a slow stick: a small upload takes 0.2 s instead of 2–3.5 s, a folder of 101 files (83 MB) copies in 6 s, 121 files delete in 3 s, and the first write after a start of the app takes 0.2 s instead of 5–9 s.
- **AMPR EMU NG 0.5.1 (quer3q):** Faster reading of packed games (about 10 % in pkg mode) and fixes for Marvel's Wolverine (could not save), Hades (hung on exit), DIRT 5 (crashed after 15 s), MARVEL Cosmic Invasion and Scott Pilgrim EX. The app carries 0.5.1 and 0.5.0 now; 0.4.1 is gone.
- **Security update for zstd packs:** zstd 1.6.0 closes CVE-2026-124851. The app only uses the one-shot functions, so it was most likely not affected; packs stay byte-identical, and packs made before still read.

## What's new
### File manager: NTFS
- **Big folders:** A name is looked up in the sorted index instead of walking every entry. On the PC, 20,000 names take 0.03 s instead of 2.45 s; copying or deleting many files in one folder no longer reads the folder over and over.
- **Many small files:** The driver collects its small changes and writes only the changed sectors, in order, when an upload, folder or job ends. On a simulated slow stick, 300 small files are created in 0.8 s instead of 14.2 s and deleted in 3.9 s instead of 10.4 s. A stick pulled in the middle of a job is still marked as not safely removed.
- **First write after the app starts:** The Windows journal of the stick is only cleared when it is not empty already, so the first write no longer rewrites up to 64 MB.

### Games
- **AMPR EMU NG 0.5.1:** See the highlights. The index check ("Does the index fit AMPR EMU NG?") calculates for 0.5.1 – nothing changed in NG's index memory.
- **zstd 1.6.0:** Packing and unpacking use the new library; the frames are byte-identical to those of 1.5.7, so existing packs stay valid.

### Manual, FAQ and screenshots
- **Renewed:** All 98 screenshots (14 pages × 7 languages) show v1.93 – NTFS in the file manager, the grey firmware badge, the full serial number on request. Texts in all seven languages are up to date: start protection, "Device busy" retry, home-network names and error 403, NTFS unplugged in the middle of writing.
- **API notes:** The rows for the index check and the NG download described NG 0.4.0; they now describe what the app answers.

## Fixes
- **Translations:** The location "Internal SSD (/data)" in the file manager and a few lines of the play-time page were half German in the other languages.

## Notes
- **Tried on the console** (Slim, firmware 12.00): the NTFS speeds above with a 60 GB stick; a test game with zstd packs started with the debug build of NG 0.5.1 (1,134 blocks read in 26 s, nothing odd in the kernel log); zstd 1.6.0 verified, unpacked, packed again (compared byte by byte with the originals) and verified again without a bad block.
- **Known:** A very cheap stick can stall for minutes on a write now and then – seen with one stick, also with the old driver. Folders with about 100,000 files have not been tried since the speed-up.
- 391 tests of the fullscreen app, 46 host tests and the simulation pass. Send the new ELF to port 9021 and copy the tiles FAKE86921 and FAKE86922 from this release again (the fullscreen app must be closed for that).

## Downloads
| File | What it is for |
|---|---|
| `PS5_Cooling_System_Center_v1.94.0.elf` | The main program (send it to port 9021) |
| `FAKE86921.zip` | Fullscreen tile: copy the folder to `/data/homebrew/` |
| `FAKE86922.zip` | Browser tile: copy the folder to `/data/homebrew/` |
| `LIESMICH.txt` | Instructions (German) |
| `payload-manager.json` | List for itsPLK's Payload Manager: enter `https://github.com/strongt1me/ps5-cooling-system-center-pro/releases/latest/download/payload-manager.json` as the source, and it downloads the app from GitHub (with a SHA-256 check) |

## Thanks
To everyone whose work is in the app or makes it possible – the full list is in the app under "Credits" or Profile → Thanks. Especially **abcde6543** for PS5-NTFS-BROWSER, **gasb07** for PS4Mount, **quer3q** for Dump Forge, AMPR EMU NG and Kraken Mount, **drakmor** for ShadowMountPlus, **itsPLK** for the Payload Manager and the payload mirror, and **John Törnblom** and the contributors of the PS5 payload SDK.

**Full Changelog**: https://github.com/strongt1me/ps5-cooling-system-center-pro/compare/v1.93.0...v1.94.0
