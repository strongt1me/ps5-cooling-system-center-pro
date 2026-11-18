#!/bin/bash
# Runs the host tests of the project (the list is the one of .github/workflows/tests.yml, so that the two never differ) and the fan simulation.
# On Windows (Git Bash) the tests run inside WSL (they need gcc and the sanitizers): HOST_TESTS_WSL_DISTRO names the distribution (default Ubuntu-26.04).
#   bash tools/run-host-tests.sh          exit 0: all passed, 1: a test failed, 2: the list was not found, 3: no way to run them here
# Used by tools/release.sh before anything is committed or pushed (audit 18.11.2026, F-314): a release with a red test does not go out.
set -u
cd "$(dirname "$0")/.." || exit 2

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    command -v wsl.exe >/dev/null 2>&1 || { echo "Host-Tests: kein WSL gefunden (wsl.exe), nicht ausgeführt." >&2; exit 3; }
    win="$(pwd -W)"
    wslp="$(printf '%s' "$win" | sed -E 's#^([A-Za-z]):#/mnt/\L\1#')"
    MSYS_NO_PATHCONV=1 wsl.exe -d "${HOST_TESTS_WSL_DISTRO:-Ubuntu-26.04}" -- sh -c "cd '$wslp' && bash tools/run-host-tests.sh"
    exit $?
    ;;
esac

command -v gcc >/dev/null 2>&1 || { echo "Host-Tests: gcc fehlt, nicht ausgeführt." >&2; exit 3; }
LIST="$(sed -n 's/^[[:space:]]*for t in \(.*\); do[[:space:]]*$/\1/p' .github/workflows/tests.yml | head -n 1)"
[ -n "$LIST" ] || { echo "Host-Tests: die Testliste in .github/workflows/tests.yml wurde nicht gefunden." >&2; exit 2; }

LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT
failed=""
n=0
for t in $LIST; do
  n=$((n + 1))
  if sh "test/$t/run.sh" >"$LOG" 2>&1; then
    echo "ok: $t ($(grep -E '[0-9]+ (checks|Prüfungen)' "$LOG" | tail -n 1 | tr '\n' ' '))"
  else
    echo "FEHLER: test/$t" >&2
    tail -n 25 "$LOG" >&2
    failed="$failed $t"
  fi
done
if sh test/sim/run.sh >"$LOG" 2>&1; then echo "ok: sim"; else echo "FEHLER: test/sim" >&2; tail -n 25 "$LOG" >&2; failed="$failed sim"; fi
if [ -n "$failed" ]; then echo "Fehlgeschlagen:$failed" >&2; exit 1; fi
echo "Alle $n Host-Tests und die Simulation bestanden."
