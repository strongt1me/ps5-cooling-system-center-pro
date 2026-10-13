#!/usr/bin/env bash
# Builds the starter eboot.bin as a native PS5 title (WSL/Linux). Same steps as the USB formatter's app/build_title.sh and
# ps5-native-app-boilerplate/tools/build.sh:
#   1. starter.c with clang-18 and the SDK's target flags
#   2. link with the SDK's prospero-lld to an intermediate PIE, imports against the stubs in target/lib
#   3. ps5-native-tool: turn it into a PS5 title (process parameters, SDK ids) and sign it
# Usage (from anywhere): PS5_PAYLOAD_SDK=<sdk> starter/build_title.sh
# ps5-native-tool comes from ps5-native-app-boilerplate (build/host); change the path with PS5_NATIVE_TOOL.
set -euo pipefail
: "${PS5_PAYLOAD_SDK:?set PS5_PAYLOAD_SDK}"
TOOL=${PS5_NATIVE_TOOL:-$HOME/lmms-ps5/boilerplate/build/host/ps5-native-tool}
CLANG=${PS5_CLANG:-$(command -v clang-18)}
[[ -x $TOOL ]] || { echo "ps5-native-tool missing: $TOOL" >&2; exit 2; }
[[ -x $CLANG ]] || { echo "clang-18 missing" >&2; exit 2; }

# values of ps5-native-app-boilerplate, checked there on FW 6.02 and 12.70
MODULE_SDK=0x02000009
COMPANION_SDK=0x08050001
FSELF_MAGIC=0x1D3D154F

cd "$(dirname "$0")"
B=build
mkdir -p "$B"
flags=(-target x86_64-sie-ps5 -fvisibility-nodllstorageclass=default -isysroot "$PS5_PAYLOAD_SDK"
       -isystem "$PS5_PAYLOAD_SDK/target/include" -fno-stack-protector -fno-plt -femulated-tls -fPIC
       -ffreestanding -fno-builtin -std=c11 -O2 -Wall -Wextra -Werror -I.
       -funwind-tables -fasynchronous-unwind-tables)   # ps5-native-tool wants .eh_frame
# payload.elf (the app) is embedded with .incbin: tools/make_starter.sh puts it here
[[ -f payload.elf ]] || { echo "starter/payload.elf missing (tools/make_starter.sh copies it)" >&2; exit 2; }
"$CLANG" "${flags[@]}" -I. -c title/starter.c -o "$B/starter.o"

"$PS5_PAYLOAD_SDK/bin/prospero-lld" -T title/title.ld --eh-frame-hdr \
    --version-script title/symbols.map -e _start -o "$B/pie.elf" "$B/starter.o" \
    --as-needed "$PS5_PAYLOAD_SDK"/target/lib/*.so

# Only these imports are meant to be there (libkernel, libSceSystemService) - anything else would be NULL at run time
allowed="close connect recv sceKernelDebugOutText sceKernelSendNotificationRequest sceKernelUsleep sceSystemServiceLoadExec send socket"
imports=$("$PS5_PAYLOAD_SDK/bin/llvm-nm" -D --undefined-only "$B/pie.elf" | awk '{ sub(/@.*/, "", $2); print $2 }' | sort -u)
for sym in $imports; do
    [[ " $allowed " == *" $sym "* ]] || { echo "unexpected import: $sym" >&2; exit 1; }
done
echo "imports: $(echo $imports)"

"$TOOL" link --in "$B/pie.elf" --out "$B/eboot.elf" --stub-dir "$PS5_PAYLOAD_SDK/target/lib" \
    --module-sdk "$MODULE_SDK" --companion-sdk "$COMPANION_SDK" --file-name eboot.elf
"$TOOL" self --sign --in "$B/eboot.elf" --out eboot.bin --magic "$FSELF_MAGIC"
"$TOOL" self --inspect --file eboot.bin
