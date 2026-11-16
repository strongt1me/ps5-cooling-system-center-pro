#!/usr/bin/env bash
# Runs the release procedure of the project (see the memory note "release-vorgehen"):
#
#   tools/release.sh <X.Y.Z> [--dry-run] [--notes docs/RELEASE_NOTES.md] [--yes]
#
#   1. checks      clean tree, something new since the last tag, version above the last tag, login, branch
#   2. version     src/ps5tm.h, README*.md (badge + nc line), docs/LIESMICH.txt
#   3. docs        python tools/i18n/docs_build.py app
#   4. commit      "release: vX.Y.Z" + push origin public:main            (asks first)
#   5. build       tools/build-windows.sh release, tools/make_starter.sh, FAKE86921.zip + FAKE86922.zip, checks
#   6. release     gh release create ...                                  (asks first)
#   7. verify      digests against the local files, "latest" tag
#
# --dry-run changes nothing (read-only checks and a list of what would happen). --yes skips the two questions.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

REPO="strongt1me/ps5-cooling-system-center-pro"
REMOTE="origin"
BRANCH="public"
REMOTE_BRANCH="main"
IDENT_EMAIL="279990554+strongt1me@users.noreply.github.com"
TRAILER="Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
README_FILES=(README.md README.en.md README.es.md README.fr.md README.it.md README.ru.md README.sv.md)
LIESMICH="docs/LIESMICH.txt"
HEADER="src/ps5tm.h"
TILES=(FAKE86921 FAKE86922)

usage() {
  cat <<'USAGE'
Aufruf: tools/release.sh <Version> [Optionen]

  <Version>            neue Version, z. B. 1.87.0 (muss größer sein als der letzte Tag)
  --dry-run            nichts verändern, nur prüfen und auflisten, was getan würde
  --notes <Datei>      Release-Notizen (Vorgabe: docs/RELEASE_NOTES.md; die erste Zeile wird weggelassen)
  --yes                die Rückfragen vor Push und Release überspringen
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

TAG="v$VERSION"
RELDIR="PS5 Cooling and System Center v$VERSION"
ELFNAME="PS5_Cooling_System_Center_v$VERSION.elf"

STAGE="Vorprüfung"
on_exit() {
  local rc=$?
  if [ "$rc" -ne 0 ] && [ "$DRY" = 0 ]; then
    echo "" >&2
    echo "Abgebrochen (Fehlercode $rc) in: $STAGE" >&2
    echo "Bereits Erledigtes bleibt bestehen (Commit, Push, Build); nichts wird zurückgerollt. Zustand mit 'git status' und 'git log' prüfen." >&2
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
  if [ "$st" != "??" ] && { [ "$path" = "$NOTES" ] || [ "$path" = "CHANGELOG.md" ]; }; then
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
STAGE="Commit und Push"
head_ "4. Commit und Push"
if [ "$DRY" = 1 ]; then
  dry "würde alle geänderten Dateien zum Commit vormerken (git add -u, dazu neue web/handbuch*.html und web/faq*.html)"
  dry "würde committen: git -c user.email=$IDENT_EMAIL commit -m \"release: $TAG\" -m \"$TRAILER\""
  confirm "git push $REMOTE $BRANCH:$REMOTE_BRANCH"
  dry "würde pushen: git push $REMOTE $BRANCH:$REMOTE_BRANCH"
  SHA="<voller SHA des neuen Commits>"
else
  git add -u
  new_docs="$(git ls-files --others --exclude-standard -- 'web/handbuch*.html' 'web/faq*.html')"
  if [ -n "$new_docs" ]; then
    # shellcheck disable=SC2086
    git add -- $new_docs
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
  confirm "git push $REMOTE $BRANCH:$REMOTE_BRANCH (Commit ${SHA:0:7})"
  git push "$REMOTE" "$BRANCH:$REMOTE_BRANCH"
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
fi

FILES=("$RELDIR/$ELFNAME" "$RELDIR/${TILES[0]}.zip" "$RELDIR/${TILES[1]}.zip" "$RELDIR/LIESMICH.txt")

# ---------- 6. release ----------
STAGE="GitHub-Release anlegen"
head_ "6. GitHub-Release anlegen"
if [ "$DRY" = 1 ]; then
  dry "würde Seitentext aus $NOTES ohne die erste Zeile in eine Temp-Datei schreiben"
  confirm "gh release create $TAG (4 Dateien) in $REPO"
  dry "würde ausführen: gh release create $TAG <4 Dateien aus \"$RELDIR\"> --repo $REPO --target $SHA --title \"PS5 Cooling & System Center - Pro $TAG\" --notes-file <Notizen ohne erste Zeile> --latest"
else
  for f in "${FILES[@]}"; do [ -f "$f" ] || { echo "FEHLER: Datei fehlt: $f" >&2; exit 1; }; done
  NOTES_TMP="$(mktemp)"
  tail -n +2 "$NOTES" > "$NOTES_TMP"
  confirm "gh release create $TAG mit ${#FILES[@]} Dateien in $REPO (Ziel-Commit ${SHA:0:7})"
  gh release create "$TAG" "${FILES[@]}" --repo "$REPO" --target "$SHA" \
     --title "PS5 Cooling & System Center - Pro $TAG" --notes-file "$NOTES_TMP" --latest
  rm -f "$NOTES_TMP"
fi

# ---------- 7. verify ----------
STAGE="Nachprüfung"
head_ "7. Nachprüfung"
if [ "$DRY" = 1 ]; then
  dry "würde 'gh release view $TAG --json assets' lesen und die digests (sha256:...) mit sha256sum der vier lokalen Dateien vergleichen"
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
