#!/usr/bin/env bash
# Runs the release procedure of the project (see the memory note "release-vorgehen"):
#
#   tools/release.sh <X.Y.Z> [--dry-run] [--notes docs/RELEASE_NOTES.md] [--yes]
#   (the release page is the English notes, with the German ones, docs/RELEASE_NOTES.de.md, in a folded "Deutsch" section)
#
#   1. checks      clean tree, something new since the last tag, version above the last tag, login, branch
#   2. version     src/ps5tm.h, README*.md (badge + nc line), docs/LIESMICH.txt
#   3. docs        python tools/i18n/docs_build.py app
#   4. commit      "release: vX.Y.Z"
#   5. build       tools/build-windows.sh release, tools/make_starter.sh, FAKE86921.zip + FAKE86922.zip, payload-manager.json
#                  (tools/make_payload_json.py: list for itsPLK's Payload Manager), checks, search for private patterns
#   6. release     push origin public:main and gh release create ...     (one question for both, before the push)
#   7. verify      digests against the local files, "latest" tag
#
# --dry-run changes nothing (read-only checks and a list of what would happen). --yes skips the question.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

REPO="strongt1me/ps5-cooling-system-center-pro"
REMOTE="origin"
BRANCH="public"
REMOTE_BRANCH="main"
IDENT_EMAIL="279990554+strongt1me@users.noreply.github.com"
TRAILER="Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
README_FILES=(README.md README.de.md README.es.md README.fr.md README.it.md README.ru.md README.sv.md)
LIESMICH="docs/LIESMICH.txt"
HEADER="src/ps5tm.h"
TILES=(FAKE86921 FAKE86922)

usage() {
  cat <<'USAGE'
Aufruf: tools/release.sh <Version> [Optionen]

  <Version>            neue Version, z. B. 1.87.0 (muss größer sein als der letzte Tag)
  --dry-run            nichts verändern, nur prüfen und auflisten, was getan würde
  --notes <Datei>      Release-Notizen auf Englisch (Vorgabe: docs/RELEASE_NOTES.md; die erste Zeile wird weggelassen);
                       die deutschen liegen daneben als <Name>.de.md und kommen aufklappbar dazu
  --yes                die eine Rückfrage vor Push und Release überspringen
  -h, --help           diese Hilfe
USAGE
}

VERSION=""
NOTES="docs/RELEASE_NOTES.md"
DRY=0
YES=0
while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run) DRY=1; shift ;;
    --yes)     YES=1; shift ;;
    --notes)   [ $# -ge 2 ] || { echo "--notes braucht eine Datei." >&2; exit 2; }; NOTES="$2"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    -*)        echo "Unbekannte Option: $1" >&2; usage >&2; exit 2 ;;
    *)         if [ -z "$VERSION" ]; then VERSION="${1#v}"; else echo "Zu viele Angaben: $1" >&2; usage >&2; exit 2; fi; shift ;;
  esac
done
[ -n "$VERSION" ] || { usage >&2; exit 2; }
printf '%s' "$VERSION" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$' || { echo "Ungültige Version: $VERSION (erwartet X.Y.Z)" >&2; exit 2; }
NOTES="${NOTES#./}"
NOTES_DE="${NOTES%.md}.de.md"      # the German notes: shown folded under the English ones

TAG="v$VERSION"
RELDIR="PS5 Cooling and System Center v$VERSION"
ELFNAME="PS5_Cooling_System_Center_v$VERSION.elf"

STAGE="Vorprüfung"
on_exit() {
  local rc=$?
  rm -f "${NOTES_TMP:-}"
  if [ "$rc" -ne 0 ] && [ "$DRY" = 0 ]; then
    echo "" >&2
    echo "Abgebrochen (Fehlercode $rc) in: $STAGE" >&2
    echo "Bereits Erledigtes bleibt bestehen (Commit, Push, Build); nichts wird zurückgerollt. Zustand mit 'git status' und 'git log' prüfen." >&2
    if [ "$STAGE" = "Nachprüfung" ]; then
      echo "Das Release $TAG ist schon öffentlich. Nachsehen: gh release view $TAG --repo $REPO --json assets" >&2
      echo "Eine abweichende Datei ersetzen: gh release upload $TAG \"<Datei>\" --repo $REPO --clobber" >&2
    fi
  fi
}
trap on_exit EXIT

say()  { echo "$*"; }
dry()  { echo "[Trockenlauf] $*"; }
head_() { echo ""; echo "== $* =="; }

PROBLEMS=()
# a failed check stops a real run; a dry run lists it and goes on
problem() {
  if [ "$DRY" = 1 ]; then
    PROBLEMS+=("$1"); echo "[würde abbrechen] $1"
  else
    echo "FEHLER: $1" >&2; exit 1
  fi
}

confirm() {   # confirm "what comes next"
  if [ "$DRY" = 1 ]; then dry "Rückfrage vor: $1"; return 0; fi
  echo ""
  echo "Nächster Schritt ist nicht umkehrbar: $1"
  if [ "$YES" = 1 ]; then echo "(--yes: ohne Rückfrage)"; return 0; fi
  local a=""
  read -r -p "Fortfahren? (ja/nein) " a || a=""
  if [ "$a" != "ja" ]; then echo "Abgebrochen."; STAGE="vom Benutzer abgebrochen"; exit 1; fi
}

PY=""
for c in python3 python; do
  if command -v "$c" >/dev/null 2>&1 && "$c" -c 'import sys' >/dev/null 2>&1; then PY="$c"; break; fi
done

# ---------- 1. checks ----------
head_ "1. Vorprüfung für $TAG"
[ "$DRY" = 1 ] && say "Trockenlauf: es wird nichts verändert."

for tool in git gh sha256sum; do
  command -v "$tool" >/dev/null 2>&1 || problem "Werkzeug fehlt: $tool"
done
[ -n "$PY" ] || problem "Python fehlt (python3 oder python)"

cur_branch="$(git rev-parse --abbrev-ref HEAD)"
[ "$cur_branch" = "$BRANCH" ] || problem "Aktueller Zweig ist '$cur_branch', erwartet wird '$BRANCH' (Push geht nach $REMOTE $BRANCH:$REMOTE_BRANCH)"

# working tree: clean, except the notes file and CHANGELOG.md, which are part of the release commit
DIRTY_ALLOWED=()
DIRTY_OTHER=()
while IFS= read -r line; do
  [ -n "$line" ] || continue
  st="${line:0:2}"; path="${line:3}"
  if [ "$st" != "??" ] && { [ "$path" = "$NOTES" ] || [ "$path" = "$NOTES_DE" ] || [ "$path" = "CHANGELOG.md" ]; }; then
    DIRTY_ALLOWED+=("$path")
  else
    DIRTY_OTHER+=("$line")
  fi
done < <(git status --porcelain)
if [ "${#DIRTY_OTHER[@]}" -gt 0 ]; then
  say "Der Arbeitsbaum ist nicht sauber:"
  printf '  %s\n' "${DIRTY_OTHER[@]:0:15}"
  [ "${#DIRTY_OTHER[@]}" -gt 15 ] && say "  ... und $(( ${#DIRTY_OTHER[@]} - 15 )) weitere"
  problem "Arbeitsbaum nicht sauber (${#DIRTY_OTHER[@]} Einträge); erst committen oder weglegen"
fi
if [ "${#DIRTY_ALLOWED[@]}" -gt 0 ]; then
  say "Hinweis: geändert und mit in den Release-Commit genommen: ${DIRTY_ALLOWED[*]}"
fi

# last tag: highest of the local tags and the tags on the remote (local ones may be stale)
REMOTE_TAGS=""
if ! REMOTE_TAGS="$(git ls-remote --tags "$REMOTE" 'v[0-9]*' 2>/dev/null)"; then
  say "Warnung: $REMOTE ist nicht erreichbar, es zählen nur die lokalen Tags."
  REMOTE_TAGS=""
fi
ALL_TAGS="$( { git tag --list 'v[0-9]*'; printf '%s\n' "$REMOTE_TAGS" | awk 'NF {n=$2; sub("refs/tags/","",n); sub(/\^\{\}$/,"",n); print n}'; } \
             | grep -E '^v[0-9]+\.[0-9]+\.[0-9]+$' | sort -uV || true)"
LAST_TAG="$(printf '%s\n' "$ALL_TAGS" | tail -n 1)"
if [ -z "$LAST_TAG" ]; then
  say "Kein früherer Tag gefunden."
else
  LAST_SHA="$(printf '%s\n' "$REMOTE_TAGS" | awk -v t="refs/tags/$LAST_TAG^{}" '$2==t {print $1}')"
  [ -n "$LAST_SHA" ] || LAST_SHA="$(printf '%s\n' "$REMOTE_TAGS" | awk -v t="refs/tags/$LAST_TAG" '$2==t {print $1}')"
  [ -n "$LAST_SHA" ] || LAST_SHA="$(git rev-parse "$LAST_TAG^{commit}" 2>/dev/null || true)"
  say "Letzter Tag: $LAST_TAG (${LAST_SHA:0:7})"

  if [ "$LAST_TAG" = "$TAG" ] || [ "$(printf '%s\n%s\n' "$LAST_TAG" "$TAG" | sort -V | tail -n 1)" != "$TAG" ]; then
    problem "Die Version $TAG ist nicht größer als der letzte Tag $LAST_TAG"
  fi

  if [ -n "$LAST_SHA" ] && git cat-file -e "$LAST_SHA^{commit}" 2>/dev/null; then
    NEWCOUNT="$(git rev-list --count "$LAST_SHA..HEAD")"
    say "Commits seit $LAST_TAG: $NEWCOUNT"
    if [ "$NEWCOUNT" = 0 ] && [ "${#DIRTY_ALLOWED[@]}" = 0 ]; then
      problem "Seit $LAST_TAG gibt es nichts Neues"
    fi
  else
    problem "Der Commit von $LAST_TAG ist lokal nicht vorhanden (git fetch $REMOTE nötig), Neues seit dem Tag nicht prüfbar"
  fi
fi

# the remote branch must be an ancestor of HEAD (the push has to be a fast-forward)
REMOTE_HEAD="$(git ls-remote "$REMOTE" "refs/heads/$REMOTE_BRANCH" 2>/dev/null | awk '{print $1}' | head -n 1 || true)"
if [ -n "$REMOTE_HEAD" ]; then
  if ! git cat-file -e "$REMOTE_HEAD^{commit}" 2>/dev/null || ! git merge-base --is-ancestor "$REMOTE_HEAD" HEAD; then
    problem "$REMOTE/$REMOTE_BRANCH (${REMOTE_HEAD:0:7}) ist kein Vorgänger von HEAD - der Push wäre kein Fast-Forward"
  fi
fi

# notes: exist and name this version in the first line
if [ ! -f "$NOTES" ]; then
  problem "Release-Notizen fehlen: $NOTES"
else
  first="$(head -n 1 "$NOTES" | tr -d '\r')"
  case "$first" in
    *"$TAG"*) say "Notizen: $NOTES (Kopfzeile nennt $TAG)" ;;
    *)        problem "Die erste Zeile von $NOTES nennt $TAG nicht: $first" ;;
  esac
  if [ ! -f "$NOTES_DE" ]; then
    problem "Die deutschen Release-Notizen fehlen: $NOTES_DE (sie kommen aufklappbar auf die Release-Seite)"
  else
    first_de="$(head -n 1 "$NOTES_DE" | tr -d '\r')"
    case "$first_de" in
      *"$TAG"*) say "Notizen: $NOTES_DE (Kopfzeile nennt $TAG)" ;;
      *)        problem "Die erste Zeile von $NOTES_DE nennt $TAG nicht: $first_de" ;;
    esac
  fi
fi

# login and existing release
if ! gh auth status >/dev/null 2>&1; then
  problem "gh ist nicht angemeldet (gh auth login)"
elif gh release view "$TAG" --repo "$REPO" >/dev/null 2>&1; then
  problem "Das Release $TAG gibt es auf GitHub schon"
fi

if [ -d "$RELDIR" ]; then
  say "Hinweis: Der Ordner \"$RELDIR\" gibt es schon; 'make release' bricht ab, wenn Fremdes darin liegt."
fi

# the host tests (the list of .github/workflows/tests.yml): a release with a red test does not go out (audit 18.11.2026, F-314)
head_ "1b. Host-Tests"
if [ "${RELEASE_SKIP_TESTS:-0}" = 1 ]; then
  say "Host-Tests übersprungen (RELEASE_SKIP_TESTS=1)."
elif [ "$DRY" = 1 ]; then
  dry "würde 'bash tools/run-host-tests.sh' ausführen (alle Tests der CI-Liste und die Simulation; unter Windows über WSL)"
else
  bash tools/run-host-tests.sh || problem "Die Host-Tests sind fehlgeschlagen oder nicht ausführbar (siehe oben). Nach dem Beheben noch einmal; nur im Notfall mit RELEASE_SKIP_TESTS=1."
fi

# ---------- 2. version ----------
STAGE="Version setzen"
head_ "2. Version setzen"
OLD_VERSION="$(sed -n 's/^[[:space:]]*#define[[:space:]]\{1,\}PS5TM_VERSION[[:space:]]\{1,\}"\([^"]*\)".*/\1/p' "$HEADER" | head -n 1 | tr -d '\r')"
[ -n "$OLD_VERSION" ] || problem "PS5TM_VERSION in $HEADER nicht lesbar"
say "Bisherige Version in $HEADER: ${OLD_VERSION:-?}  ->  neu: $VERSION"

# replace the version in the files; mode "check" only counts. Keeps the bytes (line endings) of everything else.
set_versions() {
  "$PY" - "$1" "$OLD_VERSION" "$VERSION" "$HEADER" "$LIESMICH" "${README_FILES[@]}" <<'PYEOF'
import re, sys
mode, old, new, header, liesmich = sys.argv[1:6]
readmes = sys.argv[6:]
apply = (mode == "apply")
nb = new.encode()
bad = 0

def work(path, pats, required):
    global bad
    try:
        data = open(path, "rb").read()
    except OSError as e:
        print("  %s: nicht lesbar (%s)" % (path, e)); bad += 1; return
    out, total = data, 0
    for pat, rep in pats:
        out, n = pat.subn(rep, out)
        total += n
    ok = total >= required
    print("  %s: %d Ersetzung(en)%s" % (path, total, "" if ok else "  <- erwartet mindestens %d" % required))
    if not ok:
        bad += 1
    elif apply and out != data:
        open(path, "wb").write(out)

work(header, [(re.compile(rb'(#define\s+PS5TM_VERSION\s+")[^"]+(")'), rb'\g<1>' + nb + rb'\g<2>')], 1)
for r in readmes:
    work(r, [(re.compile(rb'Version-\d+\.\d+\.\d+'), b'Version-' + nb),
             (re.compile(rb'(PS5_Cooling_System_Center_v)\d+\.\d+\.\d+(\.elf)'), rb'\g<1>' + nb + rb'\g<2>')], 1)
work(liesmich, [(re.compile(rb'(?<![\d.])' + re.escape(old.encode()) + rb'(?!\d)'), nb)], 1)
sys.exit(1 if bad else 0)
PYEOF
}

if [ -n "$PY" ] && [ -n "$OLD_VERSION" ]; then
  if [ "$DRY" = 1 ]; then
    dry "würde die Version in $HEADER, ${README_FILES[*]} und $LIESMICH setzen (Zählung nur lesend):"
    set_versions check || problem "Die Versionsnummer lässt sich in mindestens einer Datei nicht ersetzen (siehe oben)"
  else
    set_versions apply || { echo "FEHLER: Versionsnummer nicht in allen Dateien ersetzbar (siehe oben)" >&2; exit 1; }
  fi
fi

# ---------- 3. docs ----------
STAGE="Handbuch/FAQ bauen"
head_ "3. Handbuch und FAQ für die App bauen"
if [ "$DRY" = 1 ]; then
  dry "würde '$PY tools/i18n/docs_build.py app' ausführen (schreibt web/handbuch*.html und web/faq*.html)"
else
  "$PY" tools/i18n/docs_build.py app
fi

# ---------- 4. commit + push ----------
STAGE="Commit"
head_ "4. Commit (der Push folgt erst in Schritt 6, wenn gebaut und geprüft ist)"
if [ "$DRY" = 1 ]; then
  dry "würde alle geänderten Dateien zum Commit vormerken (git add -u, dazu neue web/handbuch*.html und web/faq*.html)"
  dry "würde committen: git -c user.email=$IDENT_EMAIL commit -m \"release: $TAG\" -m \"$TRAILER\""
  SHA="<voller SHA des neuen Commits>"
else
  git add -u
  mapfile -t new_docs < <(git ls-files --others --exclude-standard -- 'web/handbuch*.html' 'web/faq*.html')
  if [ "${#new_docs[@]}" -gt 0 ]; then
    git add -- "${new_docs[@]}"
  fi
  leftovers="$(git status --porcelain | grep -v '^[MADR]' || true)"
  if [ -n "$leftovers" ]; then
    echo "FEHLER: Nach dem Vormerken bleibt Unversioniertes oder Ungestagetes übrig:" >&2
    printf '%s\n' "$leftovers" >&2
    exit 1
  fi
  if git diff --cached --quiet; then
    say "Keine Änderungen zu committen (die Version stand schon überall richtig); der Commit entfällt."
  else
    git diff --cached --stat | tail -n 15
    git -c user.email="$IDENT_EMAIL" -c user.name="$(git config user.name || echo strongt1me)" \
        commit -q -m "release: $TAG" -m "$TRAILER"
    say "Commit: $(git log -1 --format='%h %s')"
  fi
  SHA="$(git rev-parse HEAD)"
fi

# ---------- 5. build ----------
STAGE="Bau"
head_ "5. Bauen und Dateien zusammenstellen"
if [ "$DRY" = 1 ]; then
  dry "würde prüfen, dass der Arbeitsbaum sauber ist (nichts Unversioniertes im Bau)"
  dry "würde ausführen: bash tools/build-windows.sh release   (legt \"$RELDIR\" an)"
  dry "würde ausführen: bash tools/make_starter.sh \"$RELDIR/$ELFNAME\"   (baut starter/out/FAKE86921 und FAKE86922)"
  dry "würde in \"$RELDIR\" anlegen: FAKE86921.zip, FAKE86922.zip (Python zipfile, ZIP_STORED, Einträge <Kachel>/<Pfad>, eboot.bin zuerst)"
  dry "würde prüfen: Release-ELF steckt byte-gleich in beiden eboot.bin; LIESMICH.txt byte-gleich mit $LIESMICH"
  dry "würde mit tools/make_payload_json.py \"$RELDIR/payload-manager.json\" erzeugen (Liste für den Payload Manager von itsPLK: Link auf die ELF dieses Releases, SHA-256 der gebauten Datei)"
  dry "würde ELF, beide Zips, LIESMICH.txt und payload-manager.json auf private Muster prüfen (Build-Pfad, Benutzerordner, WSL, Heimnetz-IP, eigene Namen)"
else
  if [ -n "$(git status --porcelain)" ]; then
    echo "FEHLER: Der Arbeitsbaum ist nach dem Commit nicht sauber - so würde etwas Ungeprüftes in die ELF gebaut:" >&2
    git status --short >&2
    exit 1
  fi
  bash tools/build-windows.sh release
  [ -f "$RELDIR/$ELFNAME" ] || { echo "FEHLER: $RELDIR/$ELFNAME wurde nicht gebaut" >&2; exit 1; }
  bash tools/make_starter.sh "$ROOT/$RELDIR/$ELFNAME"

  STAGE="Zip-Dateien und Prüfungen"
  "$PY" - "$ROOT" "$RELDIR" "$ELFNAME" "${TILES[@]}" <<'PYEOF'
import os, sys, zipfile
root, reldir, elfname = sys.argv[1:4]
tiles = sys.argv[4:]
elf = open(os.path.join(root, reldir, elfname), "rb").read()
bad = 0
for t in tiles:
    src = os.path.join(root, "starter", "out", t)
    files = []
    for d, _, names in os.walk(src):
        for n in names:
            p = os.path.join(d, n)
            files.append((os.path.relpath(p, src).replace(os.sep, "/"), p))
    files.sort(key=lambda x: (x[0] != "eboot.bin", x[0]))      # eboot.bin first
    if not files or files[0][0] != "eboot.bin":
        print("FEHLER: %s hat keine eboot.bin" % t); bad += 1; continue
    out = os.path.join(root, reldir, t + ".zip")
    if os.path.exists(out):
        os.remove(out)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED) as z:
        for rel, p in files:
            z.write(p, t + "/" + rel)
    if elf in open(os.path.join(src, "eboot.bin"), "rb").read():
        print("ok: %s.zip (%d Dateien, %d Bytes); die Release-ELF steckt byte-gleich in der eboot.bin" % (t, len(files), os.path.getsize(out)))
    else:
        print("FEHLER: die Release-ELF steckt NICHT byte-gleich in %s/eboot.bin" % t); bad += 1
sys.exit(1 if bad else 0)
PYEOF
  if cmp -s "$LIESMICH" "$RELDIR/LIESMICH.txt"; then
    say "ok: LIESMICH.txt ist byte-gleich mit $LIESMICH"
  else
    echo "FEHLER: $RELDIR/LIESMICH.txt weicht von $LIESMICH ab" >&2; exit 1
  fi
  # list for itsPLK's Payload Manager: link to this release's ELF and the SHA-256 of the file just built; it is uploaded as a fifth file,
  # so https://github.com/$REPO/releases/latest/download/payload-manager.json always serves the newest release
  "$PY" tools/make_payload_json.py --elf "$RELDIR/$ELFNAME" --version "$VERSION" --repo "$REPO" --out "$RELDIR/payload-manager.json"

  STAGE="Suche nach privaten Mustern"
  # Own names only from the environment, never from the source: the Windows user, the WSL user, the local part of a non-noreply address
  # (audit F-266, 18.11.2026: the tile of 1.75.0 to 1.86.4 carried the build user's folder and the home address; fixed at the source, this stops a relapse).
  PRIV_WORDS=()
  if [ -n "${USERNAME:-}" ]; then PRIV_WORDS+=("$USERNAME"); fi
  wsl_user="$(MSYS_NO_PATHCONV=1 wsl -d "${PS5TM_WSL_DISTRO:-Ubuntu-26.04}" -- id -un 2>/dev/null | tr -d '\r' || true)"
  if [ -n "$wsl_user" ]; then PRIV_WORDS+=("$wsl_user"); fi
  priv_mail="$(git config user.email 2>/dev/null || true)"
  case "$priv_mail" in ""|*@users.noreply.github.com) ;; *) PRIV_WORDS+=("${priv_mail%@*}") ;; esac
  # What lies zlib-packed inside the ELF (the helper programs: tools/gen_blob.py) the scan cannot see there, so the built helper files are looked at too; the web
  # texts (gzip) are the public sources, the third-party programs are not scanned on purpose (they may carry public paths of their authors).
  SCAN_EXTRA=()
  for helper in gen/pkginst_helper.elf gen/bgpause_helper.elf; do
    if [ -f "$helper" ]; then SCAN_EXTRA+=("$helper"); fi
  done
  "$PY" - "$RELDIR/$ELFNAME" "$RELDIR/${TILES[0]}.zip" "$RELDIR/${TILES[1]}.zip" "$RELDIR/LIESMICH.txt" "$RELDIR/payload-manager.json" "${SCAN_EXTRA[@]+"${SCAN_EXTRA[@]}"}" \
        -- "${PRIV_WORDS[@]+"${PRIV_WORDS[@]}"}" <<'PYEOF'
import re, sys
args = sys.argv[1:]; i = args.index("--")
files = args[:i]
# the public handle and the CI user are not private; very short names would hit ordinary words
words = [w for w in args[i + 1:] if len(w) >= 4 and w.lower() not in ("strongt1me", "runner")]
pats = [rb'/home/(?!runner/)[A-Za-z0-9_.-]+/',                                   # Linux home, but not the CI runner of the ps5-opengl SDK
        rb'[A-Za-z]:[/\x5c]+Users[/\x5c]+(?!Public[/\x5c])[A-Za-z0-9_. -]{1,64}',  # Windows user folder, both slash kinds
        rb'/(?:mnt/)?[a-z]/Users/',                                               # the same folder as MSYS or WSL sees it
        rb'wsl\.localhost|wsl\$',                                                 # WSL share paths
        rb'(?<![\d.])192\.168\.(?!1\.20(?!\d))\d{1,3}\.\d{1,3}(?!\d)']            # home network, except the preview address 192.168.1.20
pats += [rb'(?<![A-Za-z0-9_])' + re.escape(w.encode()) + rb'(?![A-Za-z0-9_])' for w in words]   # own names only as a whole word
bad = 0
for f in files:
    data = open(f, "rb").read()          # the zips are ZIP_STORED: eboot.bin and every file name lie in them as plain bytes
    for k, p in enumerate(pats, 1):
        n = len(re.findall(p, data, re.I))
        if n:
            print("FEHLER: %s: %d Treffer zu privatem Muster Nr. %d (Inhalt wird nicht ausgegeben)" % (f, n, k)); bad += 1
if not bad:
    print("ok: keine privaten Muster in %d Dateien" % len(files))
sys.exit(1 if bad else 0)
PYEOF
fi

FILES=("$RELDIR/$ELFNAME" "$RELDIR/${TILES[0]}.zip" "$RELDIR/${TILES[1]}.zip" "$RELDIR/LIESMICH.txt" "$RELDIR/payload-manager.json")

# ---------- 6. release ----------
STAGE="GitHub-Release anlegen"
head_ "6. Push und GitHub-Release anlegen"
if [ "$DRY" = 1 ]; then
  confirm "git push $REMOTE $BRANCH:$REMOTE_BRANCH und gleich danach gh release create $TAG (5 Dateien) in $REPO"
  dry "würde pushen: git push $REMOTE $BRANCH:$REMOTE_BRANCH (erst jetzt: Bau und Zip-Dateien sind geprüft; scheitert vorher etwas, steht nichts auf $REMOTE_BRANCH)"
  dry "würde Seitentext aus $NOTES ohne die erste Zeile in eine Temp-Datei schreiben, danach $NOTES_DE ohne die erste Zeile in einem aufklappbaren Abschnitt \"Deutsch\""
  dry "würde ausführen: gh release create $TAG <5 Dateien aus \"$RELDIR\"> --repo $REPO --target $SHA --title \"PS5 Cooling & System Center - Pro $TAG\" --notes-file <Notizen ohne erste Zeile> --latest"
else
  for f in "${FILES[@]}"; do [ -f "$f" ] || { echo "FEHLER: Datei fehlt: $f" >&2; exit 1; }; done
  STAGE="Push"
  NOTES_TMP="$(mktemp)"
  tail -n +2 "$NOTES" | tr -d '\r' > "$NOTES_TMP"
  {
    printf '\n---\n\n<details>\n<summary><b>Deutsch</b></summary>\n\n'
    tail -n +2 "$NOTES_DE" | tr -d '\r'
    printf '\n</details>\n'
  } >> "$NOTES_TMP"
  # one question for both steps that cannot be taken back (audit F-267: a "no" after the push used to leave main pointing at a release that did not exist)
  confirm "git push $REMOTE $BRANCH:$REMOTE_BRANCH (Commit ${SHA:0:7}) und gleich danach gh release create $TAG mit ${#FILES[@]} Dateien in $REPO"
  git push "$REMOTE" "$BRANCH:$REMOTE_BRANCH"
  STAGE="GitHub-Release anlegen"
  gh release create "$TAG" "${FILES[@]}" --repo "$REPO" --target "$SHA" \
     --title "PS5 Cooling & System Center - Pro $TAG" --notes-file "$NOTES_TMP" --latest
  rm -f "$NOTES_TMP"
fi

# ---------- 7. verify ----------
STAGE="Nachprüfung"
head_ "7. Nachprüfung"
if [ "$DRY" = 1 ]; then
  dry "würde 'gh release view $TAG --json assets' lesen und die digests (sha256:...) mit sha256sum der fünf lokalen Dateien vergleichen"
  dry "würde 'gh api repos/$REPO/releases/latest --jq .tag_name' prüfen: muss $TAG sein"
  echo ""
  if [ "${#PROBLEMS[@]}" -gt 0 ]; then
    say "Trockenlauf beendet: ${#PROBLEMS[@]} Vorprüfung(en) würden ein echtes Release abbrechen:"
    for p in "${PROBLEMS[@]}"; do say "  - $p"; done
  else
    say "Trockenlauf beendet: alle Vorprüfungen bestanden."
  fi
  exit 0
fi

ASSETS=""
for i in 1 2 3 4 5; do
  ASSETS="$(gh release view "$TAG" --repo "$REPO" --json assets --jq '.assets[] | "\(.name)\t\(.digest)"' | tr -d '\r')"
  if [ "$(printf '%s\n' "$ASSETS" | grep -c 'sha256:')" -ge "${#FILES[@]}" ]; then break; fi
  sleep 3
done
FAIL=0
for f in "${FILES[@]}"; do
  name="$(basename "$f")"
  local_sum="$(sha256sum "$f" | awk '{print $1}')"
  remote="$(printf '%s\n' "$ASSETS" | awk -F'\t' -v n="$name" '$1==n {print $2}')"
  if [ "$remote" = "sha256:$local_sum" ]; then
    say "ok: $name  sha256:${local_sum:0:16}..."
  else
    echo "FEHLER: $name - lokal sha256:$local_sum, GitHub '${remote:-fehlt}'" >&2; FAIL=1
  fi
done
LATEST="$(gh api "repos/$REPO/releases/latest" --jq .tag_name | tr -d '\r')"
if [ "$LATEST" = "$TAG" ]; then
  say "ok: $TAG ist das neueste Release (latest)"
else
  echo "FEHLER: latest ist '$LATEST', erwartet $TAG" >&2; FAIL=1
fi
[ "$FAIL" = 0 ] || exit 1
say ""
say "Fertig: $TAG ist veröffentlicht: https://github.com/$REPO/releases/tag/$TAG"
STAGE="fertig"
