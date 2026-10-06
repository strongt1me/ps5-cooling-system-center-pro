# PS5 Cooling & System Center - Pro — standalone tile installer payload.
#
#   make                          build cooling-center-launcher-installer.elf
#   make TILE_PKG=/path/to.pkg    embed a specific package
#   make clean
#
# With no package present the ELF still builds; it then looks for one on the
# console at run time instead of carrying its own.

ifndef PS5_PAYLOAD_SDK
    $(error PS5_PAYLOAD_SDK is undefined — export it to your SDK directory)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

PS5_HOST ?= ps5
PS5_PORT ?= 9021
PYTHON   ?= python3

# Which firmware the SDK's start-up code must know — see the top Makefile. The
# installer is a payload like the app and fails the same way on a firmware its
# SDK has no case for; FW_NEED= (empty) switches the check off.
FW_NEED ?= 13.00 13.20 13.40 13.60

BIN := cooling-center-launcher-installer.elf

# The package built by pkg/build.ps1, unless one is named explicitly. Sorted,
# so the pick does not depend on the order the directory lists its files in.
# With more than one there is no pick worth the name: gen/tile_pkg.c refuses
# (below) instead of embedding whichever came first. The top-level `release`
# names the package itself, so the release folder and this ELF cannot differ.
PKG_CANDIDATES := $(sort $(wildcard ../pkg/out/*.pkg))
TILE_PKG ?= $(firstword $(PKG_CANDIDATES))

CFLAGS := -Os -Wall -Wextra -Werror -I. -I../src
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident

LDFLAGS := -Wl,--gc-sections

# AppInstUtil is loaded at run time and resolved with dlsym, so it is not on
# this line. IPMI is, because Initialize() waits on that service — the SDK's
# install_app sample links it for the same reason.
LDADD := -lkernel_sys -lSceSystemService -lSceIpmi

all: $(BIN)

gen:
	mkdir -p gen

# Which package is embedded is not something a file name can tell make: with
# gen/tile_pkg.c merely newer than ../pkg/out/*.pkg, a package that had been
# taken away, or another one named, went unnoticed and the ELF kept carrying the
# old one. The stamp holds the choice and a checksum of the file; its recipe runs
# every time (FORCE) and rewrites it only when that changed. Because it runs
# every time, the refusal for an ambiguous pick lives here too, and not in the
# recipe of gen/tile_pkg.c, which would stay silent while that file is current.
gen/tile_pkg.stamp: FORCE | gen
	@[ $(words $(PKG_CANDIDATES)) -le 1 ] || [ "$(origin TILE_PKG)" != "file" ] || { \
	  echo "installer: ../pkg/out enthält mehrere Pakete: $(PKG_CANDIDATES)" >&2; \
	  echo "Eines davon mit TILE_PKG=... nennen, oder die anderen entfernen." >&2; \
	  exit 1; }
	@sum=none; \
	 if [ -n '$(TILE_PKG)' ]; then \
	   sum=$$(cksum < '$(TILE_PKG)' 2>/dev/null) || sum="unlesbar-$$(date +%s)"; \
	 fi; \
	 new="pkg=$(TILE_PKG) sum=$$sum"; \
	 [ "$$(cat $@ 2>/dev/null)" = "$$new" ] || printf '%s\n' "$$new" > $@

gen/tile_pkg.c: $(TILE_PKG) gen/tile_pkg.stamp ../tools/gen_pkg_blob.py | gen
	$(PYTHON) ../tools/gen_pkg_blob.py $@ $(TILE_PKG)

# Which SDK built the ELF: the stamp holds a checksum of the SDK's start-up
# code and changes with it, as in the top Makefile (an SDK put in place later
# has older file dates, and make saw nothing to do).
gen/sdk.stamp: FORCE | gen
	@sum=$$(cksum < '$(PS5_PAYLOAD_SDK)/target/lib/crt1.o' 2>/dev/null) || sum="unlesbar-$$(date +%s)"; \
	 new="crt1=$$sum"; \
	 [ "$$(cat $@ 2>/dev/null)" = "$$new" ] || printf '%s\n' "$$new" > $@

check-sdk-firmware:
	@$(PYTHON) ../tools/check_sdk_firmware.py sdk "$(PS5_PAYLOAD_SDK)" $(FW_NEED)

# The headers are listed too: a change to tile_pkg.h or dynsym.h alone left the
# ELF as it was, and the release copied it as the new installer.
$(BIN): main.c gen/tile_pkg.c ../src/dynsym.c tile_pkg.h ../src/dynsym.h Makefile \
        gen/sdk.stamp | check-sdk-firmware
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ main.c gen/tile_pkg.c ../src/dynsym.c $(LDADD)
	$(PS5_PAYLOAD_SDK)/bin/prospero-strip --strip-all $@
	@$(PYTHON) ../tools/check_sdk_firmware.py elf $@ $(FW_NEED) >/dev/null || { rm -f $@; exit 1; }

deploy: $(BIN)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $(BIN)

clean:
	rm -rf $(BIN) gen

FORCE:

.PHONY: all clean deploy check-sdk-firmware FORCE
