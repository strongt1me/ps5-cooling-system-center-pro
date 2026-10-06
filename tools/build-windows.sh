#!/usr/bin/env bash
# Builds the app on Windows without WSL: Git Bash, GNU Make and LLVM 21.
#
#   tools/build-windows.sh            PS5_Cooling_Center.elf
#   tools/build-windows.sh release    plus the release folder
#   tools/build-windows.sh clean
#
# One-time setup (both from winget):
#   winget install --id ezwinports.make --exact
#   winget install --id LLVM.LLVM --version 21.1.8 --exact
#
# Not LLVM 18, although older SDK notes name it: its ELF carries OS/ABI
# "System V" and a different dynamic layout. LLVM 21 reproduced the v1.43.0
# release byte for byte, build ID aside (24.09.2026).
#
# WSL stays the reference route (tools/pre-release-check.ps1). This one is for
# machines without a WSL distribution.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LLVM_BIN="${LLVM_BIN:-/c/Program Files/LLVM/bin}"
SDK_DIR="${PS5_PAYLOAD_SDK_DIR:-${ROOT}/PS5_PAYLOAD_SDK}"

if [[ ! -x "${LLVM_BIN}/clang.exe" ]]; then
  echo "LLVM not found at ${LLVM_BIN} — winget install --id LLVM.LLVM --version 21.1.8 --exact" >&2
  exit 1
fi

# The version is part of the result, not a detail of the setup: another major
# builds an ELF with another layout (LLVM 18's carries OS/ABI "System V"), and
# a plain `winget install LLVM.LLVM` or an upgrade quietly brings whatever is
# newest. PS5TM_LLVM_MAJOR names the major to expect when moving on on purpose;
# 0 switches the check off.
LLVM_WANT="${PS5TM_LLVM_MAJOR:-21}"
LLVM_HAVE="$("${LLVM_BIN}/clang.exe" --version | sed -n 's/.*clang version \([0-9]*\).*/\1/p' | head -n 1)"
if [[ "${LLVM_WANT}" != "0" && "${LLVM_HAVE}" != "${LLVM_WANT}" ]]; then
  echo "LLVM ${LLVM_HAVE:-?} found at ${LLVM_BIN}, LLVM ${LLVM_WANT} expected." >&2
  echo "Install the one the releases were built with:" >&2
  echo "  winget install --id LLVM.LLVM --version 21.1.8 --exact" >&2
  echo "or, to build with this one anyway (the ELF will differ from a reference build):" >&2
  echo "  PS5TM_LLVM_MAJOR=${LLVM_HAVE:-0} tools/build-windows.sh ..." >&2
  exit 1
fi
if [[ ! -f "${SDK_DIR}/toolchain/prospero.mk" ]]; then
  echo "SDK not found at ${SDK_DIR}" >&2
  exit 1
fi

# winget adds make to PATH, but only for shells started after the install.
MAKE_BIN="$(command -v make || true)"
if [[ -z "${MAKE_BIN}" ]]; then
  for m in "${LOCALAPPDATA:-}"/Microsoft/WinGet/Packages/ezwinports.make_*/bin/make.exe; do
    [[ -x "$m" ]] && MAKE_BIN="$m" && break
  done
fi
if [[ -z "${MAKE_BIN}" ]]; then
  echo "make not found — winget install --id ezwinports.make --exact" >&2
  exit 1
fi

# Short DOS paths. make splits the compiler path at spaces, and
# "PS5 Temperatur Studio 0815" has two of them.
short() { cygpath -m "$(cygpath -d "$1")"; }
SDK_SHORT="$(short "${SDK_DIR}")"
ROOT_SHORT="$(short "${ROOT}")"
case "${SDK_SHORT}${ROOT_SHORT}" in
  *" "*) echo "There is no path without spaces for the SDK or the project:" >&2
         echo "  SDK:     ${SDK_SHORT}" >&2
         echo "  project: ${ROOT_SHORT}" >&2
         echo "The SDK's scripts cannot cope with a space (make would stop with Error 127)," >&2
         echo "and Windows has no 8.3 short name for it - are 8.3 names disabled on this drive?" >&2
         echo "Move the SDK to a folder without spaces, e.g. C:/ps5sdk, and set PS5_PAYLOAD_SDK_DIR to it." >&2
         exit 1 ;;
esac

export LLVM_BIN
export LLVM_CONFIG="${ROOT}/tools/win/llvm-config"
export PS5_PAYLOAD_SDK="${SDK_SHORT}"
# tools/win carries the rev(1) the SDK's strip wrapper needs.
export PATH="${ROOT}/tools/win:${PATH}"

cd "${ROOT}"
exec "${MAKE_BIN}" PS5_PAYLOAD_SDK="${SDK_SHORT}" \
     CC="${ROOT_SHORT}/tools/win/prospero-clang" PYTHON=python "$@"
