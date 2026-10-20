<div align="center">

<img src="web/img/app-mark.png" alt="" width="96" height="96">

# PS5 Cooling & System Center - Pro

**Fan control, temperature monitoring and system hub for the jailbroken PlayStation 5,<br>used in the console's own browser or from any device on your home network, in six languages.**

![Version](https://img.shields.io/badge/Version-1.72.0-1f6feb)
![License](https://img.shields.io/badge/License-GPL--3.0--or--later-blue)
![Platform](https://img.shields.io/badge/Platform-PS5%20Payload-003791)
![Languages](https://img.shields.io/badge/Languages-DE%20%C2%B7%20EN%20%C2%B7%20IT%20%C2%B7%20ES%20%C2%B7%20FR%20%C2%B7%20RU-lightgrey)

[Features](#features) · [Quick start](#quick-start) · [Requirements](#requirements) · [Usage](#usage) · [Documentation](#documentation) · [Legal](#legal-notice-and-disclaimer) · [Credits](#credits-and-acknowledgements)

[Deutsch](README.md) · **English** · [Italiano](README.it.md) · [Español](README.es.md) · [Français](README.fr.md) · [Русский](README.ru.md)

<img src="docs/images/kuehlung.jpg" alt="Cooling: temperature, fan, running game, target temperature and mode" width="860">

</div>

A homebrew payload (ELF) that reads the PS5's temperature sensors, drives the fan along a quiet, adjustable comfort curve and comes with a web interface. On top of that: game, file, payload and profile management. Everything runs on the console itself, without a PC and without the internet.

## Features

<table>
  <tr>
    <td width="33%" valign="top">
      <h3>🌡️ Cooling</h3>
      Quiet fan control towards a target temperature (60–91 °C), modes, per-game profiles, a quieter target at night, a fan self-test, a safety limit left to the hardware.
    </td>
    <td width="33%" valign="top">
      <h3>📊 Readings</h3>
      CPU, SoC, GPU (Pro), fan, core load, clocks, power rails, frame rate and controller battery, with history.
    </td>
    <td width="33%" valign="top">
      <h3>🎮 Games</h3>
      Start, close, copy, move, convert to exFAT/ffpkg/ffpfsc, LZ4 asset packs, APR-EMU, PlayGo.
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h3>💾 PS4 from USB</h3>
      Play PS4 fake PKGs straight from a USB drive without installing them (PS4Mount built in), with updates and DLC.
    </td>
    <td valign="top">
      <h3>📦 Packages & saves</h3>
      Find, split and install PKGs; back up and restore save data; play time with temperatures.
    </td>
    <td valign="top">
      <h3>📁 Files & payloads</h3>
      File manager for the console and drives; download payloads (the Payload Manager's list), start and stop them, load them one after another as profiles.
    </td>
  </tr>
  <tr>
    <td valign="top">
      <h3>🖥️ System & log</h3>
      Model, firmware, network, display; event log and live kernel log with recording.
    </td>
    <td valign="top">
      <h3>🕹️ Controller shortcuts</h3>
      L1/R1 switch pages, L2/R2 scroll; pressing Mute twice shows temperature and fan on the TV.
    </td>
    <td valign="top">
      <h3>🚀 Starter tile</h3>
      A folder for <code>/data/homebrew</code>: one tap on its tile starts the app and opens the interface.
    </td>
  </tr>
</table>

### Screens

<table>
  <tr>
    <td width="50%"><img src="docs/images/spiele.jpg" alt="Game library"></td>
    <td width="50%"><img src="docs/images/ps4-von-usb.jpg" alt="PS4 games from USB"></td>
  </tr>
  <tr>
    <td align="center"><sub>Games: library with start, copy, convert</sub></td>
    <td align="center"><sub>PS4 from USB: fake PKGs without installing</sub></td>
  </tr>
  <tr>
    <td><img src="docs/images/spielzeit.jpg" alt="Play time"></td>
    <td><img src="docs/images/dateimanager.jpg" alt="File manager"></td>
  </tr>
  <tr>
    <td align="center"><sub>Play time with daily chart and temperatures</sub></td>
    <td align="center"><sub>File manager for the console and USB drives</sub></td>
  </tr>
  <tr>
    <td><img src="docs/images/payloads.jpg" alt="Payloads"></td>
    <td><img src="docs/images/system.jpg" alt="System"></td>
  </tr>
  <tr>
    <td align="center"><sub>Payloads: profiles, running and saved</sub></td>
    <td align="center"><sub>System: console, network, display</sub></td>
  </tr>
</table>

<details>
<summary><b>More screens</b> (cooling in detail, saves, packages, profile, log, phone)</summary>
<br>
<table>
  <tr>
    <td width="50%"><img src="docs/images/kuehlung-diagramme.jpg" alt="Temperature history, sensors and per-game profiles"></td>
    <td width="50%"><img src="docs/images/spielstaende.jpg" alt="Save data backup"></td>
  </tr>
  <tr>
    <td><img src="docs/images/pakete.jpg" alt="Installing packages"></td>
    <td><img src="docs/images/profil.jpg" alt="Profile with 30 built-in profile pictures"></td>
  </tr>
  <tr>
    <td><img src="docs/images/protokoll.jpg" alt="Log"></td>
    <td align="center"><img src="docs/images/kuehlung-mobil.jpg" alt="Cooling on a phone" width="240"></td>
  </tr>
</table>
<sub>All pictures use sample data and made-up games; the interface is shown in German.</sub>
</details>

<details>
<summary><b>All features in detail</b></summary>

**Cooling**
- Holds a target temperature (default 66 °C, adjustable 60–91 °C) with a moving average, dead band and trend, and changes the fan speed only in small steps.
- Quiet, balanced and cool modes, quick picks and per-game rules.
- Above the safety temperature (default 78 °C) only the hardware decides; when the app is not running, the console regulates on its own.
- Night mode: a quieter target between two clock times; the time is that of the device the page is open on.
- Fan self-test at the press of a button (does the fan answer to the threshold?) and the console's learned minimum fan speed (20 % idle, 34 % in a game), shown in the diagnosis.

**Games**
- Start (from the console's browser the page closes at once), close, copy to other drives, move or unpack with ShadowMountPlus.
- Convert to exFAT, ffpkg and ffpfsc images without a PC; everything is read back and checked after writing, with a `.sha256` file.
- LZ4 asset packs, AMPR index, APR-EMU, PlayGo stub and DLC modules per game; check, unpack and repair images.
- PS4 from USB: PS4 fake PKGs from a USB drive into the library without installing them (PS4Mount by gasb07).
- Play time with temperatures, back up and restore saves, find, split and install packages, history of all jobs.

**Management**
- File manager: view, download and upload, create folders, rename, copy, move, delete (changes only on drives and in `/data`).
- Payloads: view and stop running ones, start your own `.elf` from a folder or a USB stick, profiles as a start sequence; download and update the payloads from itsPLK's Payload Manager list (SHA-256 checked).
- Profile: display name and profile picture (30 built-in pictures or your own file).
- Design: Standard, Light, Dark and Optimal (fastest to draw, meant for the TV); every device remembers its choice.
- System: model, firmware, uptime, storage, network; raw sensors and diagnostics in expert mode.
- Header on every page: rest mode, restart, power off, safe mode (two clicks each).

**Technology**
- Its own HTTP server without a third-party library, a documented [JSON interface](docs/API.md), settings in `/data/PS5-Cooling-Center/config.json`.
- No internet access unless you switch on **game updates** (off by default): then the app asks Sony's download servers and, for older patches, prosperopatches.com or orbispatches.com for the updates of your own games. Likewise “Download payloads” fetches the payload list and the files from GitHub only when you press the button. Otherwise it only talks to programs on the console and to the browsers on your home network.

</details>

## Quick start

1. Download `PS5_Cooling_System_Center_v<version>.elf` from the [Releases](../../releases).
2. Send it to the console (ELF loader, port **9021**), for example:
   ```bash
   nc -q0 <PS5-IP> 9021 < PS5_Cooling_System_Center_v1.72.0.elf
   ```
3. Open **`http://<PS5-IP>:8086`** in a browser (the notification at the top left of the TV shows the address too).

> [!TIP]
> **Start without sending:** unpack `PPSA86921.zip` from the release and copy the folder to `/data/homebrew/`. ShadowMountPlus adds a tile; one tap on it starts the app and opens the interface.

> [!IMPORTANT]
> Only **one** instance may run at a time, otherwise two programs fight over the fan. An old instance can be stopped on the "Payloads" page. More in the [LIESMICH](docs/LIESMICH.txt) (German).

## Requirements

| | |
| --- | --- |
| **Console** | Jailbroken PS5 with an ELF loader on port 9021 ([elfldr](https://github.com/ps5-payload-dev/elfldr) or similar) |
| **Tested** | PS5 Pro (CFI-7021), firmware 12.00; several users report 13.60 working too |
| **Fan** | [kstuff](https://github.com/EchoStretch/kstuff-lite) (without it, sensors and interface work, fan control does not) |
| **Network** | A browser on the same network, or the console's own browser |
| **Optional** | [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) for moving, unpacking and the starter tile |

<details>
<summary>Firmware and SDK in detail</summary>

The ELF is built with PS5 payload SDK **v0.44**, whose start-up code knows firmware **up to 13.60**. A firmware the start-up code does not know never reaches `main()`: the program then does not start at all and writes nothing to the log. There are no reports yet for 13.00 to 13.40 (the start-up code knows them). The GPU temperature exists on the Pro only.
</details>

## Usage

| Page | Contents |
| --- | --- |
| **Cooling** | Status, history, sensors, target temperature, mode, per-game profiles |
| **Games** | Library, play time, saves, packages, PS4 from USB, backups, history |
| **File manager** | Folders of the console and the drives |
| **Payloads** | Running, saved and USB payloads, profiles, download payloads |
| **System** | Console, storage, network, diagnostics |
| **Profile** | Display name and profile picture |
| **Log** | App events and the live kernel log |
| **Credits** | Thanks to the developers; plus the **manual** and **FAQ** in six languages |

**On the controller (console browser):** L1 / R1 previous / next page · L2 / R2 scroll up / down · Mute twice: temperature and fan on the TV.

> [!WARNING]
> The interface has **no login**: every device on your home network can read and change things. Do not set up port forwarding to port 8086. To allow only the console's browser, set `bind_address` to `127.0.0.1`. Details in the [manual](docs/HANDBUCH.md#zugriff-und-sicherheit-im-heimnetz) and the [security policy](SECURITY.md).

## Documentation

| Document | Contents |
| --- | --- |
| [Manual](docs/HANDBUCH.md) | Usage in detail (German; also in the app, in six languages) |
| [API](docs/API.md) | All endpoints and configuration fields |
| [Development](docs/ENTWICKLUNG.md) | Building, sending, troubleshooting, source layout |
| [Changes](CHANGELOG.md) | What changed in which version |
| [Decision log](docs/ERWEITERUNGEN.md) | Why things are built the way they are, what was measured |
| [Third-party parts](THIRD_PARTY_NOTICES.md) | Code taken over and its licences |
| [Contributing](CONTRIBUTING.md) · [Security](SECURITY.md) | Contributions and reporting vulnerabilities |

<details>
<summary><b>Building from source</b></summary>

You need the [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk) **v0.42 or newer** (tested with v0.44); the build checks this and stops with an older SDK. On Windows, Git Bash with LLVM 21 is enough:

```bash
tools/build-windows.sh
```

On Linux or WSL:

```bash
export PS5_PAYLOAD_SDK=$HOME/ps5sdk/sdk
make
```

The result is `PS5_Cooling_Center.elf`; `tools/make_starter.sh` builds the starter tile. Everything else is in [docs/ENTWICKLUNG.md](docs/ENTWICKLUNG.md).
</details>

## Legal notice and disclaimer

> [!CAUTION]
> **Not a Sony product, no piracy, no liability.** This project has no connection to Sony. It contains no games, no firmware, no Sony keys (only a publicly known key for self-built test packages, see THIRD_PARTY_NOTICES.md) and no code to circumvent copy protection, and it downloads nothing from the internet except, when you switch them on, the official updates of your own games from Sony's servers. Modifying a console can cost you your warranty and account; the risk lies with whoever does it. The program is provided **without any warranty**.

<details>
<summary>Full text</summary>

> This section is general information, not legal advice. Anyone using the project is responsible for making sure that their use is permitted in their country and towards their contractual partners.

- **Not a Sony product.** "PlayStation", "PS5" and the related logos are trademarks of Sony Interactive Entertainment Inc. This project has no connection to Sony and is neither supported nor endorsed by Sony. All other names belong to their respective owners.
- **Your console, your responsibility.** The program only runs on a console its owner has modified. Modifying it may breach terms of use and lead to loss of warranty or a ban of the account or console, and in some countries to legal consequences. The risk lies with whoever modifies the console and uses this program.
- **No piracy.** This project is **not** meant to obtain, distribute or use pirated copies, and it does not support that. Copying, moving and converting only work on games already on your own console and are meant for backups of legitimately acquired games. Finding, splitting, installing and mounting packages from USB only work on packages you put on the console or a drive yourself; the app does not obtain any. Distributing copyrighted content is a criminal offence in most countries. There is no help with that here, not in issues either.
- **No Sony files in the repository.** System libraries, firmware and proprietary SDK files are neither shipped nor accepted (see [CONTRIBUTING](CONTRIBUTING.md)). The 30 profile pictures were created by the author.
- **No warranty, no liability.** The program interferes with the console's fan control and system functions. It is provided as is, **without any warranty** (GNU GPL, sections 15 and 16). The author is not liable for damage to consoles, data or accounts. A dusty console cools worse, however the fan is driven.
- **Privacy.** History, log and settings stay on the console in `/data/PS5-Cooling-Center/`. The app sends no data to the internet; only with game updates switched on does it ask Sony's servers and DNS providers about the title IDs of your own games. The only exception is in the browser: the "Credits" page loads the small icon from github.com once to see whether the browser has internet access; only then are the links clickable.
- **Third-party projects** are under their own licences, see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
</details>

## Contributing and reporting bugs

Bugs and wishes are welcome as an [issue](../../issues), with console model, firmware, version and the exported log ("Log" page). Contributions: [CONTRIBUTING.md](CONTRIBUTING.md). Please report security vulnerabilities as described in [SECURITY.md](SECURITY.md).

## Credits and acknowledgements

This project stands on the shoulders of the PS5 homebrew community. **Thank you!** A very special thank you to **[Gezine](https://github.com/Gezine)**: without Gezine's work no homebrew would run on many consoles, and so neither would this app.

<details>
<summary><b>Platform and tools</b></summary>

- **John Törnblom and all contributors to [ps5-payload-dev](https://github.com/ps5-payload-dev)**: the [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk), the payload loader [elfldr](https://github.com/ps5-payload-dev/elfldr), [klogsrv](https://github.com/ps5-payload-dev/klogsrv), [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv) and [websrv](https://github.com/ps5-payload-dev/websrv).
- **The kstuff developers**: [sleirsgoevy](https://github.com/sleirsgoevy), [EchoStretch](https://github.com/EchoStretch) and [drakmor](https://github.com/drakmor) ([kstuff-lite](https://github.com/EchoStretch/kstuff-lite)). Without kstuff there would be no access to the fan controller.
</details>

<details>
<summary><b>Code from other projects (ported or included)</b></summary>

- **[gasb07](https://github.com/gasb07), [PS4Mount](https://github.com/gasb07/ps4mount)** (GPL-3.0): PS4 fake PKGs from a USB drive without installing; the source is built in as the "PS4 from USB" tab.
- **[RenanGBarreto](https://github.com/RenanGBarreto), [rdmrocha](https://github.com/rdmrocha) and the contributors to [MkPFS](https://github.com/PSBrew/MkPFS)** ([PSBrew](https://github.com/PSBrew), GPL-3.0): exFAT, PFS and PFSC images.
- **[SvenGDK](https://github.com/SvenGDK), [UFS2Tool](https://github.com/SvenGDK/UFS2Tool)** (BSD-2-Clause): the model for the UFS2/ffpkg writer.
- **[drakmor](https://github.com/drakmor)**: [ampr_emu](https://github.com/drakmor/ampr_emu) (LZ4 asset packs, APR-EMU), [pgo_stub](https://github.com/drakmor/pgo_stub) and [dlc_emu](https://github.com/drakmor/dlc_emu) (embedded builds, GPL-3.0).
- **[BlackBearReloaded](https://github.com/BlackBearReloaded), ps5-native-app-boilerplate** (GPL-3.0): how the starter tile is built as a native title.
- **[itsPLK](https://github.com/itsPLK), [ps5-pkg-manager](https://github.com/itsPLK/ps5-pkg-manager)** (GPL-3.0): package layout and the install flow (own code after this model).
- **[phantomptr](https://github.com/phantomptr), [ps5upload](https://github.com/phantomptr/ps5upload)** (GPL-3.0): profile function and profile picture format.
- **[slopmaster33](https://github.com/slopmaster33), [webhb](https://github.com/slopmaster33/webhb)** (GPL-3.0): QR encoder.
- **[danthespal](https://github.com/danthespal), [MkPFS.CSharp](https://github.com/danthespal/MkPFS.CSharp)** (GPL-3.0): the four tested LZ4 profiles (ASTRO BOT, EA Sports UFC 6, Insomniac engine, Unity engine), embedded unchanged.
- **[Eric Biggers](https://github.com/ebiggers), [libdeflate](https://github.com/ebiggers/libdeflate)** (MIT) · **[Dave Gamble](https://github.com/DaveGamble), [cJSON](https://github.com/DaveGamble/cJSON)** (MIT) · **[LZ4](https://github.com/lz4/lz4)** (BSD-2-Clause).
</details>

<details>
<summary><b>Working together, knowledge and inspiration</b></summary>

- **[drakmor](https://github.com/drakmor)**: [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) (mounting, moving, unpacking games) and [ps5-hwinfo](https://github.com/drakmor/ps5-hwinfo) (power rails, clocks).
- **V-10, [exFAT2AMPR](https://github.com/exFAT2AMPR/exfat2ampr)**: the list for "Automatic", recording for profile creation, upkeep of the game plugins.
- **theghostonline (Husam Osman), [PS5 LZ4 Compression](https://github.com/theghostonline/PS5-LZ4-Compression)**: files that must stay loose when packing, permissions 0777, repair while checking the packs, choice of the tested profiles.
- **[kerrdec97](https://github.com/kerrdec97), [exFAT Image Builder](https://github.com/kerrdec97/ps5-exfat-builder)**: parameters for `.ffpkg` images.
- **[itsPLK](https://github.com/itsPLK)**: [ps5-unified-autoloader](https://github.com/itsPLK/ps5-unified-autoloader) and [ps5-payload-manager](https://github.com/itsPLK/ps5-payload-manager).
- **[StonedModder](https://github.com/StonedModder), [ps-game-state-lib](https://github.com/StonedModder/ps-game-state-lib)** (MIT): detecting the running game.
- **[etaHEN](https://github.com/etaHEN/etaHEN)** and **[onionHEN](https://github.com/aydencharles/onionHEN)**: system calls, readings, frame rate.
- **[Soniciso](https://git.etawen.dev/soniciso), [Elf Arsenal](https://git.etawen.dev/soniciso/elf-arsenal)**: the model for many features (code rewritten each time).
- **BestPig, [BackPork](https://github.com/BestPig/BackPork)**: the principle of replacement libraries.
- **Juma Sayeh and Osama Abualia, PS5 Game Compressor**: ideas for checking and converting (all rewritten; its source carries no licence).
- **Linux driver `hid-playstation`** (DualSense battery level) · **Xbox 360 DashLaunch** (the idea of quiet regulation).
</details>

<details>
<summary><b>Thanks to the community</b></summary>

Their code is not in this app, but without their shared work the scene would not exist as it does: [owendswang](https://github.com/owendswang), [LightningMods](https://github.com/LightningMods), [Andy Nguyen / TheFloW](https://github.com/TheOfficialFloW), [Specter](https://github.com/Cryptogenic), [ChendoChap](https://github.com/ChendoChap), [idlesauce](https://github.com/idlesauce), [flatz](https://github.com/flatz), [Al Azif](https://github.com/Al-Azif), [zecoxao](https://github.com/zecoxao), [ArkSama](https://github.com/ArkSama), [nexgen999](https://github.com/nexgen999), [matem6](https://github.com/matem6), [Phoenixx1202](https://github.com/Phoenixx1202), [SiSTR0](https://github.com/SiSTR0) – and everyone else who contributes with code, tests, guides or answers. In the app, all of them are listed with a picture on the "Credits" page.
</details>

If you are missing here or described wrongly, please let me know and the list will gladly be extended.

## License

Copyright © 2026 strongt1me · **GNU General Public License v3.0 or later**, see [LICENSE](LICENSE). Up to 1.45.1 the project was MIT; from 1.46.0 it is GPL-3.0-or-later because parts are taken from GPL projects (details in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

<sub>This README is also available in [Deutsch](README.md), [Italiano](README.it.md), [Español](README.es.md), [Français](README.fr.md) and [Русский](README.ru.md). The manual and FAQ come with every release in all six languages.</sub>
