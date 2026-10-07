# PS5 Cooling & System Center - Pro — advanced cooling & thermal management for PlayStation 5.
#
#   make                       build PS5_Temperature_Manager.elf
#   make deploy PS5_HOST=<ip>  build and send it to elfldr
#   make clean
#
# Requires the ps5-payload-sdk; point PS5_PAYLOAD_SDK at your checkout:
#   export PS5_PAYLOAD_SDK=$HOME/ps5sdk/sdk

PS5_HOST ?= ps5
PS5_PORT ?= 9021

empty :=
space := $(empty) $(empty)
PS5_PAYLOAD_SDK_ESC := $(subst $(space),\ ,$(PS5_PAYLOAD_SDK))

ifndef PS5_PAYLOAD_SDK
    $(error PS5_PAYLOAD_SDK is undefined — export it to your SDK directory)
endif
include $(PS5_PAYLOAD_SDK_ESC)/toolchain/prospero.mk

PYTHON ?= python3

# The SDK decides which firmware the app can start on. Its start-up code
# (crt1.o) holds the kernel offsets of every firmware it knows, and before
# main() it refuses one it does not: with v0.41, which stops at 13.40, an app on
# a console with 13.60 never gets going and writes no line anywhere. The case
# for 13.60 came with v0.42; this project builds with v0.43. The build checks
# the SDK before it starts (tools/check_sdk_firmware.py) and the ELF after it
# is linked, and stops when a firmware named here has no case. FW_NEED= (empty)
# switches the check off, for building with an older SDK on purpose; naming
# fewer firmwares keeps it for those.
FW_NEED ?= 13.00 13.20 13.40 13.60

BUILD_DATE_CMD := $(PYTHON) tools/build_date.py
VERSION_CMD := $(PYTHON) tools/read_version.py

BUILD_DATE ?= $(shell $(BUILD_DATE_CMD))
VERSION := $(shell $(VERSION_CMD))

BIN := PS5_Cooling_Center.elf

SRCS := src/main.c
SRCS += src/http.c
SRCS += src/api.c
SRCS += src/config.c
SRCS += src/platform.c
SRCS += src/fan.c
SRCS += src/tile.c
SRCS += src/log.c
SRCS += src/regstats.c
SRCS += src/chanlog.c
SRCS += src/thermalog.c
SRCS += src/clocks.c
SRCS += src/sysinfo.c
SRCS += src/notify.c
SRCS += src/gamestate.c
SRCS += src/history.c
SRCS += src/procmgr.c
SRCS += src/payloads.c
SRCS += src/payprofiles.c
SRCS += src/netdisp.c
SRCS += src/power.c
SRCS += src/dualsense.c
SRCS += src/probe.c
SRCS += src/dynsym.c
SRCS += src/profile.c
SRCS += src/sony_api_lock.c
SRCS += src/micbutton.c
SRCS += src/telemetry.c
SRCS += src/sqlite_ro.c
SRCS += src/library.c
SRCS += src/gamecopy.c
SRCS += src/gamemove.c
SRCS += src/gamedelete.c
SRCS += src/filemgr.c
SRCS += src/assets.c
SRCS += src/imgread.c
SRCS += src/smp.c
SRCS += src/gameconvert.c
SRCS += src/conv_exfat.c
SRCS += src/conv_pfs.c
SRCS += src/conv_ufs2.c
SRCS += src/checkfile.c
SRCS += src/iopolicy.c
SRCS += src/powerguard.c
SRCS += src/klog.c
SRCS += src/playtime.c
SRCS += src/savebackup.c
SRCS += src/klogfiles.c
SRCS += src/libcache.c
SRCS += src/pkgparse.c
SRCS += src/pkgscan.c
SRCS += src/pkgsplit.c
SRCS += src/pkgstream.c
SRCS += src/pkginstall.c

# ⚠ Die Kopfdateien gehören in die Abhängigkeiten.
#
# Ohne sie hielt make das Programm für aktuell, sobald nur ein Header geändert
# wurde — und kopierte beim Release-Ziel stillschweigend die *alte* Binärdatei
# in den neuen Ordner. Am 02.08.2026 genau so passiert: Zwischen 1.32.0 und
# 1.32.1 änderte sich allein die Versionsnummer in ps5tm.h, also keine einzige
# .c-Datei. Der Ordner hieß v1.32.1, das ELF darin meldete 1.32.0, und die
# Ursache wurde zuerst auf der Konsole gesucht.
#
# Ein Wildcard reicht: Bei zwei Dutzend Dateien kostet ein unnötiger
# Neuübersetzung Sekunden, eine ausgelieferte falsche Binärdatei kostet mehr.
#
# Eingebundene Dateien, die keine .h sind, gehören dazu: conv_exfat.c bindet
# src/conv_exfat_upcase.inc ein, und eine Änderung allein an dieser Tabelle
# endete in "Nothing to be done" — mit der alten Tabelle im ELF und im Release.
# Dasselbe gilt für die Kopfdateien der mitgelieferten Bibliotheken; für ihre
# eigenen Objekte stehen sie bei den Regeln weiter unten.
DEFLATE_HDRS := $(wildcard src/third_party/libdeflate/*.h \
                src/third_party/libdeflate/lib/*.h \
                src/third_party/libdeflate/lib/x86/*.h)
HDRS := $(wildcard src/*.h) $(wildcard src/*.inc) \
        $(wildcard src/third_party/*.h) $(DEFLATE_HDRS)

GEN_SRCS := gen/assets.c
GEN_SRCS += gen/tile_pkg.c
GEN_SRCS += gen/tile_files.c
GEN_SRCS += gen/pkginst_blob.c

# The home-screen tile as a folder: param.json, icon and pictures from
# tile/sce_sys, about 1.7 MB. The app writes them to /user/app/<id>/sce_sys and
# has the system register the folder (src/tile.c) — the way the other launcher
# tiles on a console are made, and the reason one ELF is enough. Everything in
# that folder lies in the app's memory for as long as it runs, so
# tools/gen_tile_files.py refuses more than 4 MB; see the package note below.
TILE_FILES := $(wildcard tile/sce_sys/*)

# The tile package is NOT embedded in the app by default.
#
# It was, briefly, so that a single payload could do everything. That cost
# 10.5 MB of resident image, and on the console the remaining heap was too
# small to serve a single HTTP request: the port opened, every connection was
# accepted and immediately dropped. The fan kept working, which made it look
# like a networking fault rather than an allocation one.
#
# The tile only ever needs installing once, and cooling-center-launcher-
# installer.elf carries the package for exactly that. The app still installs a
# tile when it finds a package on disk — the installer leaves one there.
#
#   make                      the app, ~3 MB (web UI, converters, libraries)
#   make TILE_PKG=pkg/out/x.pkg   embeds a package anyway
TILE_PKG ?=

# The install helper (src/helper/pkginst_helper.c): a program of its own, which the app hands to the payload loader for
# one installation and which makes the call into the system's install library in its own process (src/pkginstall.c has
# the reasons). It is built as an ELF here and put into the app as a byte array (tools/gen_blob.py), about 80 KB.
# -lSceIpmi is what the SDK's own install_app sample links beside AppInstUtil: without that service
# sceAppInstUtilInitialize() blocks for good.
PKGI_HELPER_ELF := gen/pkginst_helper.elf
PKGI_HELPER_LIBS := -lpthread -lSceNetCtl -lSceUserService -lSceSystemService -lSceIpmi -lSceAppInstUtil -lSceNet

# cJSON is built separately: upstream trips several of our warning flags.
CJSON_OBJ := gen/cJSON.o

# libdeflate 1.26 (MIT) for compressing and checking .ffpfsc images
# (conv_pfs.c). Built separately like cJSON, and at -O2: it is the one place
# where the app's speed is the user's waiting time. It replaced zlib on
# 29.09.2026 — same streams, two to three times the speed. crc32.c is the one
# file of this copy that is not upstream's: the entry point libdeflate_crc32()
# was left out when the copy was cut down, and was written again on the pattern
# of adler32.c (03.10.2026), over upstream's own tables and PCLMULQDQ routines.
DEFLATE_SRCS := $(addprefix src/third_party/libdeflate/lib/,adler32.c crc32.c \
                deflate_compress.c deflate_decompress.c zlib_compress.c \
                zlib_decompress.c utils.c x86/cpu_features.c)
DEFLATE_OBJS := $(patsubst src/third_party/libdeflate/lib/%.c,gen/libdeflate/%.o,$(DEFLATE_SRCS))

# SHA-256 is the other hot loop: it hashes every byte of a backup when that is
# read back (checkfile.c). At -Os the compiler leaves its rounds rolled up.
FAST_SRCS := src/sha256.c
FAST_OBJS := $(patsubst src/%.c,gen/fast/%.o,$(FAST_SRCS))

# web/img and web/avatars too: replacing only a picture left gen/assets.c "up to
# date", and the ELF quietly shipped the old one (25.09.2026, the new app mark).
# The avatars are the 30 built-in profile pictures (tools/build_avatars.py);
# they are the largest part of the web UI, about 1.4 MB.
# web/lang holds the dictionaries of the interface languages (07.10.2026); a changed
# translation alone has to rebuild the ELF as well.
WEB_FILES := $(wildcard web/*) $(wildcard web/img/*) $(wildcard web/avatars/*) $(wildcard web/lang/*)

CFLAGS := -Os -Wall -Wextra -Werror -Isrc -Isrc/third_party
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident
CFLAGS += -DPS5TM_BUILD=$(BUILD_DATE)

LDFLAGS := -Wl,--gc-sections

# -lSceIpmi is what the SDK's own install_app sample links alongside
# AppInstUtil, and IPMI is exactly the service whose absence makes
# sceAppInstUtilInitialize() block forever. It was missing here.
LDADD := -lkernel_sys -lSceSystemService -lSceUserService
LDADD += -lSceIpmi -lSceAppInstUtil
# Without this libSceNetCtl is never loaded and every network field stays
# empty — dlsym cannot search a module that is not there. ps5debug-NG links
# the same module and runs on FW 12.00, so it is safe in NEEDED.
LDADD += -lSceNetCtl
# Same reasoning for the controller. dualsense.c looked its four functions up
# with dlsym by name, which cannot work — Sony's modules export by NID — and
# libScePad was not linked at all, so the pointers stayed null and the battery
# reported "no controller" for the whole life of the feature. Confirmed on
# 01.08.2026 while a game was being played with that controller.
LDADD += -lScePad
# Drive traffic counters and the console name, read from the registry. offact
# links the same module and runs on FW 12.00. Read-only use — see regstats.c.
LDADD += -lSceRegMgr
# The shell's own helper, which closes the console's browser after a start
# from it (library.c). The SDK has no stub for it, so one is built below; the
# module itself sits in /system_ex/common_ex/lib, which the SDK's loader
# searches. Loaded at start on FW 12.00 by a test payload, 29.09.2026.
SHELLUI_STUB := gen/stubs/libSceShellUIUtil.so
LDADD += -Lgen/stubs -lSceShellUIUtil

all: $(BIN)

gen:
	mkdir -p gen

# --no-pie: the compiler driver asks for a position-independent executable,
# which a shared object cannot be. -nodefaultlibs keeps libc and friends out
# of the stub's own NEEDED list.
# Das Makefile selbst gehört zu den Voraussetzungen jeder Datei, die seine
# Flags und Bibliotheken in sich trägt: Eine Änderung an CFLAGS oder LDADD löste
# sonst keine Neuübersetzung aus ("Nothing to be done"), das ELF blieb das alte.
$(SHELLUI_STUB): src/stubs/libSceShellUIUtil.c Makefile gen/sdk.stamp | gen
	mkdir -p gen/stubs
	$(CC) -shared -fPIC -nodefaultlibs -Wl,--no-pie \
	    -Wl,-soname,libSceShellUIUtil.sprx -o $@ $<

gen/assets.c: $(WEB_FILES) tools/gen_assets.py | gen
	$(PYTHON) tools/gen_assets.py $@ web

# Ein Dateiname sagt nicht, WELCHES Paket gemeint ist und was darin steht, also
# genügt "gen/tile_pkg.c ist neuer als TILE_PKG" nicht: Nach einem einzigen
# `make TILE_PKG=x.pkg` fand ein schlichtes `make` die Datei aktuell und ließ
# das Paket still eingebettet — genau das 10-MB-Abbild, vor dem der Kommentar
# oben warnt. Die Marke hält fest, welches Paket gewählt ist und seine
# Prüfsumme. Ihr Rezept läuft jedes Mal (FORCE) und schreibt sie nur dann neu,
# wenn sich das geändert hat; gen/tile_pkg.c wird also genau dann neu erzeugt,
# wenn sich die Wahl oder der Inhalt ändert.
gen/tile_pkg.stamp: FORCE | gen
	@sum=none; \
	 if [ -n '$(TILE_PKG)' ]; then \
	   sum=$$(cksum < '$(TILE_PKG)' 2>/dev/null) || sum="unlesbar-$$(date +%s)"; \
	 fi; \
	 new="pkg=$(TILE_PKG) sum=$$sum"; \
	 [ "$$(cat $@ 2>/dev/null)" = "$$new" ] || printf '%s\n' "$$new" > $@

$(PKGI_HELPER_ELF): src/helper/pkginst_helper.c src/pkginst_ipc.h Makefile gen/sdk.stamp | gen
	$(CC) -Os -Wall -Wextra -Werror -Isrc -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident -DPKGI_BUILD=$(BUILD_DATE) $(LDFLAGS) -o $@ src/helper/pkginst_helper.c $(PKGI_HELPER_LIBS)
	"$(PS5_PAYLOAD_SDK)/bin/prospero-strip" --strip-all $@
	@$(PYTHON) tools/check_sdk_firmware.py elf $@ $(FW_NEED) >/dev/null || { rm -f $@; exit 1; }

# The mark stands in the image once, followed by the dots the app writes its token over: the build checks it.
gen/pkginst_blob.c: $(PKGI_HELPER_ELF) tools/gen_blob.py | gen
	$(PYTHON) tools/gen_blob.py $@ $(PKGI_HELPER_ELF) ps5tm_pkginst_helper PS5CC-PKGI-TOKEN:

gen/tile_pkg.c: $(TILE_PKG) gen/tile_pkg.stamp tools/gen_pkg_blob.py | gen
	$(PYTHON) tools/gen_pkg_blob.py $@ $(TILE_PKG)

# src/tile.c is a prerequisite because the script checks that param.json names
# the title id that file looks for.
gen/tile_files.c: $(TILE_FILES) tools/gen_tile_files.py src/tile.c | gen
	$(PYTHON) tools/gen_tile_files.py $@ tile src/tile.c

$(CJSON_OBJ): src/third_party/cJSON.c src/third_party/cJSON_real.c \
              $(wildcard src/third_party/*.h) Makefile gen/sdk.stamp | gen
	$(CC) -Os -Isrc -Isrc/third_party -ffunction-sections -fdata-sections \
	    -c -o $@ src/third_party/cJSON.c

gen/libdeflate/%.o: src/third_party/libdeflate/lib/%.c $(DEFLATE_HDRS) Makefile gen/sdk.stamp | gen
	mkdir -p $(dir $@)
	$(CC) -O2 -ffunction-sections -fdata-sections -c -o $@ $<

gen/fast/%.o: src/%.c $(HDRS) Makefile gen/sdk.stamp | gen
	mkdir -p $(dir $@)
	$(CC) -O2 -Wall -Wextra -Werror -Isrc -Isrc/third_party \
	    -ffunction-sections -fdata-sections \
	    -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident -c -o $@ $<

# Which SDK built the objects is not something file times can tell make: an SDK
# put in place later has files with older dates (a release's own), and make saw
# nothing to do. The stamp holds a checksum of the SDK's start-up code; its
# recipe runs every time (FORCE) and rewrites it only when that changed, so a
# change of SDK rebuilds everything that depends on it.
gen/sdk.stamp: FORCE | gen
	@sum=$$(cksum < '$(PS5_PAYLOAD_SDK)/target/lib/crt1.o' 2>/dev/null) || sum="unlesbar-$$(date +%s)"; \
	 new="crt1=$$sum"; \
	 [ "$$(cat $@ 2>/dev/null)" = "$$new" ] || printf '%s\n' "$$new" > $@

check-sdk-firmware:
	@$(PYTHON) tools/check_sdk_firmware.py sdk "$(PS5_PAYLOAD_SDK)" $(FW_NEED)

$(BIN): Makefile $(SRCS) $(HDRS) $(GEN_SRCS) $(CJSON_OBJ) $(DEFLATE_OBJS) $(FAST_OBJS) \
        $(SHELLUI_STUB) gen/sdk.stamp | check-sdk-firmware
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(GEN_SRCS) $(CJSON_OBJ) $(DEFLATE_OBJS) $(FAST_OBJS) $(LDADD)
	"$(PS5_PAYLOAD_SDK)/bin/prospero-strip" --strip-all $@
	@$(PYTHON) tools/check_sdk_firmware.py elf $@ $(FW_NEED) >/dev/null || { rm -f $@; exit 1; }

deploy: $(BIN)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $(BIN)

# Everything a person actually installs, collected in one place. The version is
# read from the header so the folder name cannot drift away from the build.
# "and", not "&", and deliberately so.
#
# The app calls itself "PS5 Cooling & System Center - Pro" everywhere a person reads
# it. This is a path, and paths get pasted into shells. On Windows "&" is a
# command separator in cmd.exe and the call operator in PowerShell, so an
# unquoted paste would break in a way that looks like a missing file. The
# recipe below quotes every use, but the person copying the path is not bound
# by the recipe.
RELEASE := PS5 Cooling and System Center v$(VERSION)
RELEASE_MAIN := PS5_Cooling_System_Center_v$(VERSION).elf
RELEASE_INST := cooling-center-launcher-installer_v$(VERSION).elf

# Das Paket, das mitgeliefert wird, und das, das der Installer einbettet, sind
# EIN und dieselbe Datei: die einzige in pkg/out. Früher nahm der Installer die
# erste, die der Verzeichniseintrag lieferte, und das Release kopierte alle —
# bei zwei Paketen stand im Ordner nicht, was im Installer steckte. Jetzt
# bricht das Release bei mehreren ab, und der Installer bekommt das Paket
# ausdrücklich genannt (ohne Paket: leer, er sucht dann zur Laufzeit).
TILE_PKGS_OUT := $(sort $(wildcard pkg/out/*.pkg))
RELEASE_PKG   := $(firstword $(TILE_PKGS_OUT))

# Dieses Ziel löscht nichts, was es nicht selbst angelegt hat. Früher räumte
# `rm -rf` den Ordner der Version leer, auch wenn dort von Hand Dazugelegtes
# lag — und die Release-Ordner sind nicht in git, es wäre unwiederbringlich
# fort gewesen. Jetzt werden nur die Dateien dieses Laufs überschrieben; liegt
# sonst etwas im Ordner (auch ein Paket mit anderem Namen, das sonst
# mitausgeliefert würde), bricht das Ziel mit einer Liste ab. Thumbs.db und
# desktop.ini legt Windows von selbst an und stören nicht.
release: $(BIN)
	@[ -n "$(VERSION)" ] || { \
	  echo "release: PS5TM_VERSION ließ sich nicht lesen (PYTHON=$(PYTHON)) — der Ordnername wäre falsch." >&2; \
	  exit 1; }
	@[ $(words $(TILE_PKGS_OUT)) -le 1 ] || { \
	  echo "release: pkg/out enthält mehrere Pakete: $(TILE_PKGS_OUT)" >&2; \
	  echo "Genau eines darf dort liegen; sonst steht nicht fest, welches der Installer einbettet." >&2; \
	  exit 1; }
	@if [ -d "$(RELEASE)" ]; then \
	  foreign=$$(ls -A "$(RELEASE)" | grep -v -x -F \
	    -e "$(RELEASE_MAIN)" -e "$(RELEASE_INST)" -e LIESMICH.txt \
	    -e Thumbs.db -e desktop.ini $(if $(RELEASE_PKG),-e "$(notdir $(RELEASE_PKG))")); \
	  if [ -n "$$foreign" ]; then \
	    echo "release: In \"$(RELEASE)\" liegt, was dieses Ziel nicht selbst anlegt — es löscht nichts:" >&2; \
	    printf '  %s\n' "$$foreign" >&2; \
	    echo "Verschieben oder löschen, dann erneut." >&2; \
	    exit 1; \
	  fi; \
	fi
	$(MAKE) -C installer TILE_PKG=$(if $(RELEASE_PKG),../$(RELEASE_PKG))
	mkdir -p "$(RELEASE)"
	cp -f $(BIN) "$(RELEASE)/$(RELEASE_MAIN)"
	cp -f installer/cooling-center-launcher-installer.elf "$(RELEASE)/$(RELEASE_INST)"
	@# The package always ships, whether or not it is embedded in the app.
	@[ -z "$(RELEASE_PKG)" ] || cp -f "$(RELEASE_PKG)" "$(RELEASE)/"
	@if [ -f docs/LIESMICH.txt ]; then cp -f docs/LIESMICH.txt "$(RELEASE)/"; \
	 else echo "release: docs/LIESMICH.txt fehlt — der Ordner bekommt keine LIESMICH." >&2; fi
	@echo ""
	@ls -la "$(RELEASE)"

# Auch der Installer: Sein ELF und sein eingebettetes Paket bleiben sonst über
# ein `make clean && make release` hinweg liegen und gelangen unbesehen in den
# neuen Ordner.
clean:
	rm -rf $(BIN) gen
	$(MAKE) -C installer clean

FORCE:

.PHONY: all clean deploy release check-sdk-firmware FORCE
