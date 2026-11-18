#!/usr/bin/env bash
# Sends the app ELF and the two tiles (FAKE86921, FAKE86922) to a PS5.
#
#   tools/deploy.sh <IP|94|52> [--elf <file>] [--tiles <dir>] [--nur-elf | --nur-kacheln] [--dry-run]
#
# Order (the two kinds of transfer are separate steps, as the virus scanner wants them):
#   1. ELF to port 9021 (tools/lib/send_elf.ps1, own process), wait ~12 s, poll http://<IP>:8086/api/v1/system for app_version
#   2. tiles by curl over FTP (port 2121) to /data/homebrew/<tile>/, eboot.bin last
#   3. size of every eboot.bin locally and on the console (Content-Length of the FTP file)
#   4. summary
# Exit codes: 0 ok, 1 errors, 2 wrong call, 3 full-screen app open on the console (FTP 550 on eboot.bin).
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TILES=(FAKE86921 FAKE86922)
LOADER_PORT=9021
FTP_PORT=2121
API_PORT=8086
WAIT_AFTER_ELF=12      # seconds to wait after sending the ELF
WAIT_API_MAX=90        # seconds to wait for the API in total after that

usage() {
  cat <<'USAGE'
Aufruf: tools/deploy.sh <IP> [Optionen]

  <IP>                 IP-Adresse oder Kurzname: 94 -> 192.168.1.94, 52 -> 192.168.1.52
  --elf <Datei>        App-ELF (Vorgabe: PS5_Cooling_Center.elf im Projektordner)
  --tiles <Ordner>     Ordner mit FAKE86921 und FAKE86922 (Vorgabe: starter/out)
  --nur-elf            nur die ELF senden (Schritt 1)
  --nur-kacheln        nur die Kacheln senden (Schritte 2 und 3)
  --dry-run            nur ausgeben, was getan würde
  -h, --help           diese Hilfe
USAGE
}

ELF="$ROOT/PS5_Cooling_Center.elf"
TILES_DIR="$ROOT/starter/out"
DRY=0
DO_ELF=1
DO_TILES=1
IP_ARG=""
ELF_GIVEN=0

while [ $# -gt 0 ]; do
  case "$1" in
    --elf)         [ $# -ge 2 ] || { echo "--elf braucht eine Datei." >&2; exit 2; }; ELF="$2"; ELF_GIVEN=1; shift 2 ;;
    --tiles)       [ $# -ge 2 ] || { echo "--tiles braucht einen Ordner." >&2; exit 2; }; TILES_DIR="$2"; shift 2 ;;
    --nur-elf)     DO_TILES=0; shift ;;
    --nur-kacheln) DO_ELF=0; shift ;;
    --dry-run)     DRY=1; shift ;;
    -h|--help)     usage; exit 0 ;;
    -*)            echo "Unbekannte Option: $1" >&2; usage >&2; exit 2 ;;
    *)             if [ -z "$IP_ARG" ]; then IP_ARG="$1"; else echo "Zu viele Angaben: $1" >&2; usage >&2; exit 2; fi; shift ;;
  esac
done
[ -n "$IP_ARG" ] || { usage >&2; exit 2; }
if [ "$DO_ELF" = 0 ] && [ "$DO_TILES" = 0 ]; then
  echo "--nur-elf und --nur-kacheln zusammen ergeben nichts zu tun." >&2; exit 2
fi

# short name: only digits (1..3) means 192.168.1.<n>
if printf '%s' "$IP_ARG" | grep -Eq '^[0-9]{1,3}$'; then
  IP="192.168.1.$IP_ARG"
else
  IP="$IP_ARG"
fi
if ! printf '%s' "$IP" | grep -Eq '^[A-Za-z0-9.-]+$'; then
  echo "Ungültige Adresse: $IP_ARG" >&2; exit 2
fi

ERRORS=()
err() { ERRORS+=("$1"); echo "FEHLER: $1" >&2; }
say() { echo "$*"; }
dry() { echo "[Trockenlauf] $*"; }

file_size() { wc -c < "$1" | tr -d ' \r'; }
# percent-encode the characters that matter in an FTP URL path
enc_path() { printf '%s' "$1" | sed -e 's/%/%25/g' -e 's/ /%20/g' -e 's/#/%23/g' -e 's/?/%3F/g'; }
api_version() {
  curl -s -m 4 "http://$IP:$API_PORT/api/v1/system" 2>/dev/null \
    | sed -n 's/.*"app_version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1
}

# The process id of the app that answers now (/api/v1/status, "pid"): a new ELF that really started has another one (audit 17.11.2026: the version alone
# says nothing when the one that runs is the version that was sent - an old app that goes on running looked like a success).
api_pid() {
  curl -s -m 4 "http://$IP:$API_PORT/api/v1/status" 2>/dev/null \
    | sed -n 's/.*"pid"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' | head -n 1
}

# The PowerShell host for the socket send (Windows); nc as the fallback elsewhere.
send_elf_cmd() {
  local ps=""
  if command -v pwsh >/dev/null 2>&1; then ps=pwsh
  elif command -v powershell.exe >/dev/null 2>&1; then ps=powershell.exe
  elif command -v powershell >/dev/null 2>&1; then ps=powershell; fi
  if [ -n "$ps" ]; then
    local winscript winelf
    if command -v cygpath >/dev/null 2>&1; then
      winscript="$(cygpath -w "$ROOT/tools/lib/send_elf.ps1")"; winelf="$(cygpath -w "$ELF")"
    else
      winscript="$ROOT/tools/lib/send_elf.ps1"; winelf="$ELF"
    fi
    "$ps" -NoProfile -ExecutionPolicy Bypass -File "$winscript" -Elf "$winelf" -Ip "$IP" -Port "$LOADER_PORT"
  elif command -v nc >/dev/null 2>&1; then
    nc -q0 "$IP" "$LOADER_PORT" < "$ELF" && echo "gesendet (nc): $(file_size "$ELF") Bytes an $IP:$LOADER_PORT"
  else
    echo "Weder PowerShell noch nc gefunden." >&2; return 1
  fi
}

# Version expected from the ELF name (..._vX.Y.Z.elf) or, for the default ELF, from src/ps5tm.h.
EXPECT_VER=""
case "$(basename "$ELF")" in
  *_v[0-9]*.[0-9]*.[0-9]*.elf) EXPECT_VER="$(basename "$ELF" | sed -n 's/.*_v\([0-9][0-9.]*[0-9]\)\.elf$/\1/p')" ;;
esac
if [ -z "$EXPECT_VER" ] && [ "$ELF_GIVEN" = 0 ]; then
  EXPECT_VER="$(sed -n 's/^[[:space:]]*#define[[:space:]]\{1,\}PS5TM_VERSION[[:space:]]\{1,\}"\([^"]*\)".*/\1/p' "$ROOT/src/ps5tm.h" 2>/dev/null | head -n 1 | tr -d '\r')"
fi

say "Ziel: $IP (Lader $LOADER_PORT, FTP $FTP_PORT, Web-Oberfläche $API_PORT)"
[ "$DRY" = 1 ] && say "Trockenlauf: es wird nichts gesendet."

# ---------- checks of the inputs ----------
if [ "$DO_ELF" = 1 ] && [ ! -f "$ELF" ]; then
  if [ "$DRY" = 1 ]; then dry "ELF nicht gefunden (würde abbrechen): $ELF"; else echo "ELF nicht gefunden: $ELF" >&2; exit 1; fi
fi
if [ "$DO_TILES" = 1 ]; then
  for t in "${TILES[@]}"; do
    if [ ! -f "$TILES_DIR/$t/eboot.bin" ]; then
      if [ "$DRY" = 1 ]; then dry "Kachel unvollständig (würde abbrechen): $TILES_DIR/$t/eboot.bin fehlt"
      else echo "Kachel unvollständig: $TILES_DIR/$t/eboot.bin fehlt" >&2; exit 1; fi
    fi
  done
fi

RUNNING_VER=""
declare -a SUMMARY_TILES=()
declare -a SUMMARY_LINES=()
FULLSCREEN_OPEN=0

# ---------- step 1: ELF ----------
if [ "$DO_ELF" = 1 ]; then
  say ""
  say "== Schritt 1: ELF an Port $LOADER_PORT senden =="
  if [ "$DRY" = 1 ]; then
    size="?"; [ -f "$ELF" ] && size="$(file_size "$ELF")"
    dry "würde $ELF ($size Bytes) an $IP:$LOADER_PORT senden (tools/lib/send_elf.ps1)"
    dry "würde $WAIT_AFTER_ELF s warten und dann bis zu $WAIT_API_MAX s auf http://$IP:$API_PORT/api/v1/system warten"
    dry "würde app_version lesen${EXPECT_VER:+ und mit $EXPECT_VER vergleichen}"
  else
    OLD_PID="$(api_pid)"                      # the app that runs before the send ("" when none answers)
    say "Sende $ELF ($(file_size "$ELF") Bytes) ..."
    if ! send_elf_cmd; then
      err "ELF konnte nicht an $IP:$LOADER_PORT gesendet werden"
    else
      say "Warte $WAIT_AFTER_ELF s, bis die App neu gestartet ist ..."
      sleep "$WAIT_AFTER_ELF"
      waited=0
      while [ "$waited" -le "$WAIT_API_MAX" ]; do
        RUNNING_VER="$(api_version)"
        [ -n "$RUNNING_VER" ] && break
        sleep 3; waited=$((waited + 3))
      done
      if [ -z "$RUNNING_VER" ]; then
        err "http://$IP:$API_PORT/api/v1/system antwortet nicht (oder enthält keine app_version) - App nicht gestartet?"
      else
        say "App läuft, Version $RUNNING_VER"
        if [ -n "$EXPECT_VER" ] && [ "$RUNNING_VER" != "$EXPECT_VER" ]; then
          err "Es läuft Version $RUNNING_VER, gesendet wurde $EXPECT_VER (die alte App läuft noch?)"
        fi
        NEW_PID="$(api_pid)"
        if [ -n "$OLD_PID" ] && [ -n "$NEW_PID" ] && [ "$OLD_PID" = "$NEW_PID" ]; then
          err "Die App läuft noch mit derselben Prozessnummer ($NEW_PID) wie vor dem Senden: Die neue ELF ist nicht angekommen oder nicht gestartet."
        fi
      fi
    fi
  fi
elif [ "$DRY" = 0 ]; then
  RUNNING_VER="$(api_version)"
fi

# ---------- step 2 + 3: tiles ----------
if [ "$DO_TILES" = 1 ]; then
  say ""
  say "== Schritt 2: Kacheln per FTP nach /data/homebrew/ senden (eboot.bin zuletzt) =="
  for t in "${TILES[@]}"; do
    tdir="$TILES_DIR/$t"
    # file list: everything but eboot.bin sorted, then eboot.bin
    files=()
    if [ -d "$tdir" ]; then
      mapfile -t files < <(cd "$tdir" && { find . -type f ! -name eboot.bin | sort; [ -f ./eboot.bin ] && echo ./eboot.bin; })
    fi
    if [ "$DRY" = 1 ]; then
      dry "Kachel $t: würde ${#files[@]} Dateien aus $tdir nach ftp://$IP:$FTP_PORT/data/homebrew/$t/ senden (eboot.bin zuletzt)"
      [ -f "$tdir/eboot.bin" ] && dry "Kachel $t: eboot.bin lokal $(file_size "$tdir/eboot.bin") Bytes; würde danach die Größe an der Konsole per curl -I prüfen"
      continue
    fi
    say "Kachel $t: ${#files[@]} Dateien ..."
    n=0; failed=0
    for f in "${files[@]}"; do
      f="${f#./}"
      url="ftp://$IP:$FTP_PORT/data/homebrew/$t/$(enc_path "$f")"
      if [ "$f" = "eboot.bin" ] && [ "$failed" -gt 0 ]; then
        # Other files of this tile did not arrive: a new eboot.bin beside old or cut ones would give a tile that mixes two versions - the old one stays whole.
        err "Kachel $t ist uneinheitlich: $failed Datei(en) fehlgeschlagen, die eboot.bin wurde nicht gesendet (die alte bleibt). Noch einmal senden."
        failed=$((failed + 1))
        continue
      fi
      if [ "$f" = "eboot.bin" ]; then
        # eboot.bin goes up under a temporary name and is renamed when it is whole: a net that fails in the middle leaves the old file, not a cut one.
        # The old file is never removed by the script: the rename replaces it (FreeBSD), and when that fails the old file stays, with the new one beside it as eboot.bin.part.
        out="$(cd "$tdir" && curl -s -S -m 300 --ftp-create-dirs -T "$f" "$url.part" 2>&1)"; rc=$?
        if [ "$rc" -ne 0 ]; then
          curl -s -m 20 -Q "*DELE /data/homebrew/$t/eboot.bin.part" "ftp://$IP:$FTP_PORT/" -o /dev/null 2>&1 || true      # the upload broke off: no half file is left
        else
          out="$(curl -s -S -m 60 -Q "RNFR /data/homebrew/$t/eboot.bin.part" -Q "RNTO /data/homebrew/$t/eboot.bin" "ftp://$IP:$FTP_PORT/" -o /dev/null 2>&1)"; rc=$?
          if [ "$rc" -ne 0 ] && ! printf '%s' "$out" | grep -q '550'; then
            # the rename may have worked and only the listing after it failed: the size on the console decides
            rs="$(curl -s -I -m 30 "ftp://$IP:$FTP_PORT/data/homebrew/$t/eboot.bin" 2>/dev/null | tr -d '\r' | sed -n 's/^[Cc]ontent-[Ll]ength:[[:space:]]*\([0-9]*\).*/\1/p' | head -n 1)"
            [ "$rs" = "$(file_size "$tdir/eboot.bin")" ] && rc=0
          fi
        fi
      else
        out="$(cd "$tdir" && curl -s -S -m 300 --ftp-create-dirs -T "$f" "$url" 2>&1)"; rc=$?
      fi
      n=$((n + 1))
      if [ "$rc" -ne 0 ]; then
        failed=$((failed + 1))
        err "Kachel $t, Datei $f: $(printf '%s' "$out" | head -n 1)"
        if [ "$f" = "eboot.bin" ] && printf '%s' "$out" | grep -q '550'; then
          FULLSCREEN_OPEN=1
          err "Kachel $t ist uneinheitlich: die übrigen Dateien sind neu, die eboot.bin ist die alte geblieben (die Vollbild-App ist offen). Nach dem Schließen noch einmal senden."
        fi
      fi
    done
    say "Kachel $t: $n Dateien gesendet, $failed fehlgeschlagen"
    SUMMARY_TILES+=("$t")
    if [ "$FULLSCREEN_OPEN" = 1 ]; then
      echo "" >&2
      echo "FTP 550 bei eboot.bin: Die Vollbild-App ist an der Konsole offen." >&2
      echo "Vollbild-App an der Konsole schließen und noch einmal starten" >&2
      break
    fi
  done

  say ""
  say "== Schritt 3: Größe der eboot.bin lokal und an der Konsole =="
  if [ "$DRY" = 1 ]; then
    dry "würde je Kachel 'curl -I ftp://$IP:$FTP_PORT/data/homebrew/<Kachel>/eboot.bin' ausführen und Content-Length mit der lokalen Größe vergleichen"
  else
    for t in "${SUMMARY_TILES[@]+"${SUMMARY_TILES[@]}"}"; do
      local_size="$(file_size "$TILES_DIR/$t/eboot.bin")"
      remote_size="$(curl -s -I -m 30 "ftp://$IP:$FTP_PORT/data/homebrew/$t/eboot.bin" 2>/dev/null | tr -d '\r' | sed -n 's/^[Cc]ontent-[Ll]ength:[[:space:]]*\([0-9]*\).*/\1/p' | head -n 1)"
      if [ -z "$remote_size" ]; then
        res="Konsole: nicht lesbar"
        [ "$FULLSCREEN_OPEN" = 1 ] || err "Kachel $t: Größe der eboot.bin an der Konsole nicht lesbar"
      elif [ "$remote_size" = "$local_size" ]; then
        res="Konsole: $remote_size Bytes - gleich"
      else
        res="Konsole: $remote_size Bytes - ABWEICHUNG"
        err "Kachel $t: eboot.bin lokal $local_size Bytes, an der Konsole $remote_size Bytes"
      fi
      say "  $t: lokal $local_size Bytes, $res"
      SUMMARY_LINES+=("$t: eboot.bin lokal $local_size Bytes, $res")
    done
  fi
fi

# ---------- summary ----------
say ""
say "== Zusammenfassung =="
if [ "$DRY" = 1 ]; then
  say "Trockenlauf beendet - nichts wurde gesendet."
  exit 0
fi
say "Laufende Version: ${RUNNING_VER:-unbekannt}"
for l in "${SUMMARY_LINES[@]+"${SUMMARY_LINES[@]}"}"; do say "  $l"; done
if [ "${#ERRORS[@]}" -gt 0 ]; then
  say "Fehler (${#ERRORS[@]}):"
  for e in "${ERRORS[@]}"; do say "  - $e"; done
  [ "$FULLSCREEN_OPEN" = 1 ] && exit 3
  exit 1
fi
say "Keine Fehler."
exit 0
