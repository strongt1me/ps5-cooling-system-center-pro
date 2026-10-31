// ps5cc - The people thanked on the Credits page, for the fullscreen app's Danke view.
// Copyright (C) 2026 strongt1me
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Generated from the section #page-credits of web/index.html (the web app's Credits page);
// the pictures are the console app's /credits/<file>. Update both together.

#pragma once

namespace hui::ps5cc
{

struct Credit
{
    const char *group;
    const char *picture; // file in /credits, "" for none
    const char *name;
    const char *login; // "@name" on GitHub, "" for none
    const char *text;
};

inline constexpr Credit kCredits[] = {
    {"Ein ganz besonderer Dank", "Gezine.jpg", "Gezine", "@Gezine",
     "Seine Arbeit ist für die PS5-Homebrew-Community grundlegend: Ohne sie liefe auf vielen "
     "Konsolen kein Homebrew – und damit auch diese App nicht. Danke für alles, was du mit der "
     "Community teilst!"},
    {"Plattform und Werkzeuge", "john-tornblom.jpg", "John Törnblom", "@john-tornblom",
     "Das PS5-Payload-SDK, mit dem jede ELF dieser App gebaut wird, der Payload-Lader elfldr auf "
     "Port 9021, dazu klogsrv, ftpsrv und websrv. Danke für das Fundament, auf dem fast alles "
     "Homebrew der PS5 steht – und an alle Mitwirkenden von ps5-payload-dev."},
    {"Plattform und Werkzeuge", "sleirsgoevy.jpg", "sleirsgoevy", "@sleirsgoevy",
     "Der Hauptautor von kstuff. Ohne kstuff gäbe es keinen Zugriff auf den Lüfter-Controller – "
     "und damit keine Lüftersteuerung. Dazu ps4jb und bd-jb für die PS4. Danke!"},
    {"Plattform und Werkzeuge", "EchoStretch.jpg", "Echo Stretch", "@EchoStretch",
     "Pflegt die Weiterentwicklungen kstuff und kstuff-lite, mit über hundert eigenen Beiträgen. "
     "Danke!"},
    {"Code, der in der App steckt", "RenanGBarreto.jpg", "Renan Barreto", "@RenanGBarreto",
     "MkPFS: der Aufbau von exFAT-, PFS- und PFSC-Abbildern. Das Konvertieren auf der Konsole ist "
     "eine Übertragung davon nach C. Danke für dieses Stück Arbeit!"},
    {"Code, der in der App steckt", "rdmrocha.jpg", "rdmrocha", "@rdmrocha",
     "Mitautor von MkPFS – danke, auch an alle weiteren Mitwirkenden von MkPFS."},
    {"Code, der in der App steckt", "SvenGDK.jpg", "SvenGDK", "@SvenGDK",
     "UFS2Tool: die Vorlage für den UFS2-/ffpkg-Schreiber der App. Danke!"},
    {"Code, der in der App steckt", "itsPLK.jpg", "itsPLK", "@itsPLK",
     "ps5-pkg-manager: Vorbild für Pakete finden, aufteilen und installieren. Dazu "
     "ps5-payload-manager als Vorbild der Payload-Verwaltung und ps5-unified-autoloader. Danke für "
     "gleich drei Werkzeuge!"},
    {"Code, der in der App steckt", "phantomptr.jpg", "PhantomPtr", "@phantomptr",
     "ps5upload: der Aufbau der Profilfunktion und das Format des Profilbilds. Danke!"},
    {"Code, der in der App steckt", "ebiggers.jpg", "Eric Biggers", "@ebiggers",
     "libdeflate: die schnelle Kompression, mit der die Abbilder auf der Konsole entstehen. "
     "Danke!"},
    {"Code, der in der App steckt", "DaveGamble.jpg", "Dave Gamble", "@DaveGamble",
     "cJSON: jede Antwort der App an diese Seite. Danke an ihn und alle Mitwirkenden!"},
    {"Zusammenspiel, Wissen und Vorbilder", "drakmor.jpg", "Drakmor", "@drakmor",
     "ShadowMountPlus: Spiele einhängen, verschieben und entpacken – die Spiele-Seite baut darauf "
     "auf. Dazu ps5-hwinfo mit den Stromschienen und Taktwerten, und als einer der größten "
     "Mitwirkenden an kstuff-lite auch am Zugang zur Konsole. Danke für so viel Wissen über die "
     "PS5!"},
    {"Zusammenspiel, Wissen und Vorbilder", "", "gasb07", "@gasb07",
     "PS4Mount: Vorlage und Code für den Reiter „PS4 von USB“, PS4-Spiele (Fake-PKG) vom "
     "USB-Laufwerk ohne Installation. Danke!"},
    {"Zusammenspiel, Wissen und Vorbilder", "", "Knutwurst", "@knutwurst",
     "PatchDL: Die eigene Namensauflösung und der HTTPS-Abruf der App (für den Payload-Store) "
     "stammen daraus. Danke!"},
    {"Zusammenspiel, Wissen und Vorbilder", "", "V-10", "exFAT2AMPR",
     "exFAT2AMPR: zeigte die Liste, die „Automatisch“ beim Packen lose lässt, den Mitschnitt zur "
     "Profil-Erzeugung und die Pflege der Spiel-Plugins mit ihren Prüfsummen. Danke!"},
    {"Zusammenspiel, Wissen und Vorbilder", "", "theghostonline", "@theghostonline",
     "PS5 LZ4 Compression (Husam Osman): zeigte, welche Dateien beim Packen lose bleiben müssen, "
     "warum Spieldateien die Rechte 0777 brauchen und wie „Pakete prüfen“ ein gepacktes Spiel "
     "repariert. Danke!"},
    {"Zusammenspiel, Wissen und Vorbilder", "", "danthespal", "@danthespal",
     "MkPFS.CSharp: schrieb und testete die vier LZ4-Profile, die die App mitbringt (ASTRO BOT, EA "
     "Sports UFC 6, Insomniac-Engine, Unity-Engine). Danke!"},
    {"Zusammenspiel, Wissen und Vorbilder", "kerrdec97.jpg", "kerrdec97", "@kerrdec97",
     "exFAT Image Builder: zeigte, mit welchen Einstellungen ein .ffpkg für ShadowMountPlus gebaut "
     "wird. Danke!"},
    {"Zusammenspiel, Wissen und Vorbilder", "StonedModder.jpg", "StonedModder", "@StonedModder",
     "ps-game-state-lib: die Muster im Kernel-Log, an denen die App das laufende Spiel erkennt. "
     "Danke!"},
    {"Zusammenspiel, Wissen und Vorbilder", "etaHEN.jpg", "etaHEN", "@etaHEN",
     "Quelltext und Dokumentation zu Systemaufrufen, Messwerten und der Bildraten-Abfrage. Danke "
     "an das ganze Projekt!"},
    {"Zusammenspiel, Wissen und Vorbilder", "aydencharles.jpg", "Hexwell", "@aydencharles",
     "onionHEN: Quelltext und Dokumentation, aus denen die App viel gelernt hat. Danke!"},
    {"Zusammenspiel, Wissen und Vorbilder", "BestPig.jpg", "BestPig", "@BestPig",
     "BackPork: das Prinzip der Ersatzbibliotheken, das die Spiele-Seite erkennt und anzeigt. "
     "Danke!"},
    {"Zusammenspiel, Wissen und Vorbilder", "juma-sayeh.jpg", "Juma Sayeh und Osama Abualia",
     "@juma-sayeh",
     "Vorbild für das Zurücklesen und Prüfen von Sicherungen, die Ruhemodus-Sperre und weitere "
     "Ideen beim Kopieren und Konvertieren (alles neu geschrieben). Danke!"},
    {"Zusammenspiel, Wissen und Vorbilder", "soniciso.jpg", "Soniciso", "@soniciso",
     "Elf Arsenal, das All-in-one-Werkzeug für die PS5 und Nachfolger von Sonic Loader: Vorbild "
     "für viele Funktionen dieser App – Spiel beenden, Kernel-Log live, Spielzeit, Spielstände, "
     "Dateimanager, Spiele löschen – und die Form der Aufrufe für die Kachel-Installation. Danke!"},
    {"Zusammenspiel, Wissen und Vorbilder", "", "Linux-Kernel", "",
     "Die Dokumentation des DualSense-Statusbytes für den Akkustand."},
    {"Zusammenspiel, Wissen und Vorbilder", "", "Xbox 360 DashLaunch", "",
     "Der Gedanke der ruhigen, trägen Temperaturregelung."},
    {"Danke an die Community", "owendswang.jpg", "owendswang", "@owendswang",
     "Ein Datei-Manager mit Weboberfläche, eine eigene Lüftersteuerung und viele kleine Werkzeuge "
     "für die PS5."},
    {"Danke an die Community", "LightningMods.jpg", "LightningMods", "@LightningMods",
     "Hauptentwickler von etaHEN, dazu Itemzflow und der PS4-Store."},
    {"Danke an die Community", "TheOfficialFloW.jpg", "Andy Nguyen (TheFloW)", "@TheOfficialFloW",
     "PPPwn und vieles mehr – von der PS Vita bis zur PS5."},
    {"Danke an die Community", "Cryptogenic.jpg", "Specter", "@Cryptogenic",
     "Der IPv6-Kernel-Exploit für die PS5 und ausführliche Write-ups, aus denen die Szene lernt."},
    {"Danke an die Community", "ChendoChap.jpg", "ChendoChap", "@ChendoChap",
     "pOOBs4 und die Code-Ausführung über den WebKit-Browser der PS5."},
    {"Danke an die Community", "idlesauce.jpg", "idlesauce", "@idlesauce",
     "umtx2 und weitere Werkzeuge für PS4 und PS5."},
    {"Danke an die Community", "flatz.jpg", "flatz", "@flatz",
     "Werkzeuge rund um Pakete und das PFS-Dateisystem, dazu der PS5-ELF-Lader für IDA."},
    {"Danke an die Community", "Al-Azif.jpg", "Al Azif", "@Al-Azif",
     "Der Exploit-Host für die PS4 und psfree-lapse."},
    {"Danke an die Community", "zecoxao.jpg", "zecoxao", "@zecoxao",
     "Viel Forschung und Dokumentation rund um die Firmware der PS5."},
    {"Danke an die Community", "ArkSama.jpg", "ArkSama (Team PHU)", "@ArkSama",
     "Der Lapy JB Daemon, ein eigenständiger Jailbreak-Daemon für die PS5, und das "
     "PHU-Trophäensystem."},
    {"Danke an die Community", "nexgen999.jpg", "nexgen999", "@nexgen999",
     "evoX-CoreOS und die PS5 Repository List – sie halten die Releases der Szene an einem Ort "
     "zusammen."},
    {"Danke an die Community", "matem6.jpg", "matem6", "@matem6",
     "p2jb-y2jb und YarP2JB – die Portierungen des p2jb-Exploits auf Y2JB und yarpe."},
    {"Danke an die Community", "Phoenixx1202.jpg", "Phoenixx", "@Phoenixx1202",
     "Der PS5 FPKG Builder, die Spectrum Library und LegacyJB."},
    {"Danke an die Community", "SiSTR0.jpg", "SiSTRo", "@SiSTR0",
     "ps5-versions, ps4ren und weitere Beiträge zur PS4- und PS5-Szene."},
};

} // namespace hui::ps5cc
