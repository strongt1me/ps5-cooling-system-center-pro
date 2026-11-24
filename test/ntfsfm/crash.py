#!/usr/bin/env python3
"""Interrupted index changes on NTFS (24.11.2026, after upstream PR #22 of PS5-NTFS-BROWSER).

The app's NTFS driver keeps small writes in a cache and puts them on the stick by block number, so the order of an index change on
the device is set by the ordering points in ntfs_write.c (ix_order), not by the order of the calls. This test builds test/ntfsfm/crash.c
with the driver, fills a folder (prep), then adds names into one stretch of it (leaf splits, then the parent splits too) and removes
names in random, ascending and descending order (merges, borrowing from a neighbour, the root shrinking). Each step is first run to
the end with a pwrite shim that counts the device writes (N); then, for every K < N, a fresh copy of the image gets the same step cut
off after write K (_exit), and the folder must still hold every name it had: listed and found by lookup in the app's own reader, and
listed by ntfs-3g (ntfsls). A name listed twice or a lost block is allowed (chkdsk tidies those up), a vanished name is not.

Needs clang, cc, ntfs-3g (mkntfs, ntfsls); Linux/WSL, no root.
usage: crash.py [driver dir]   (default src/third_party/ntfsb; another one to compare, e.g. a copy of an older version)
"""
import os
import random
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
NB = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(REPO, "src", "third_party", "ntfsb")
WORK = tempfile.mkdtemp(prefix="ntfscrash-", dir=os.environ.get("TMPDIR"))
TOOL = os.path.join(WORK, "crash")
SHIM = os.path.join(WORK, "shim.so")
LOG = os.path.join(WORK, "writes.log")

SHIM_SRC = r"""
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
static long writes, crash_after = -1;
static int logfd = -1;
static void note(const void *buf, size_t len, long long off) {
  if (crash_after < 0) {
    const char *s = getenv("CRASH_AFTER");
    crash_after = s ? atol(s) : 0;
    if ((s = getenv("CRASH_LOG"))) logfd = open(s, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  }
  writes++;
  if (logfd >= 0) {
    const char *kind = len >= 4 && !memcmp(buf, "INDX", 4) ? "INDX" : len >= 4 && !memcmp(buf, "FILE", 4) ? "FILE" : "DATA";
    char line[96];
    int n = snprintf(line, sizeof line, "%s %lld %zu\n", kind, off, len);
    if (write(logfd, line, (size_t)n) < 0) _exit(98);
  }
  if (crash_after > 0 && writes >= crash_after) _exit(99);
}
#define WRAP(name, offt)                                                                  \
  ssize_t name(int fd, const void *buf, size_t len, offt off) {                           \
    static ssize_t (*real)(int, const void *, size_t, offt);                              \
    if (!real) real = (ssize_t (*)(int, const void *, size_t, offt))dlsym(RTLD_NEXT, #name); \
    ssize_t r = real(fd, buf, len, off);                                                  \
    if (r > 0) note(buf, (size_t)r, (long long)off);                                      \
    return r;                                                                             \
  }
WRAP(pwrite, off_t)
WRAP(pwrite64, off64_t)
"""

PREP = 160          # files in the folder at the start
stats = {"steps": 0, "tested": 0, "points": 0, "bad": 0}
failures = []


def sh(*a, **kw):
    return subprocess.run(a, capture_output=True, text=True, **kw)


def build():
    src = os.path.join(WORK, "shim.c")
    open(src, "w").write(SHIM_SRC)
    r = sh("cc", "-shared", "-fPIC", "-O1", "-o", SHIM, src, "-ldl")
    if r.returncode:
        sys.exit("shim: " + r.stderr)
    files = [os.path.join(NB, f) for f in ("ntfs.c", "ntfs_write.c", "bdev.c", "part.c", "utf.c")]
    inc = os.path.dirname(os.path.dirname(NB))          # .../src, so that "third_party/ntfsb/x.h" is found
    r = sh("clang", "-std=gnu11", "-O1", "-g", "-w", "-DPS5TM_HOST_TEST", "-I" + inc, "-I" + os.path.dirname(NB),
           os.path.join(HERE, "crash.c"), *files, "-lpthread", "-o", TOOL)
    if r.returncode:
        sys.exit("build: " + r.stderr[-3000:])


def name_of(i):
    return "Eintrag_%05d_mit_einem_recht_langen_Namen_fuer_den_Index" % (i * 10)


def run(img, op, arg, crash_after=0):
    env = dict(os.environ, LD_PRELOAD=SHIM, CRASH_AFTER=str(crash_after), CRASH_LOG=LOG)
    r = subprocess.run([TOOL, img, op, arg], capture_output=True, text=True, env=env)
    with open(LOG) as f:
        writes = [ln.split() for ln in f.read().splitlines()]
    return r.returncode, writes, (r.stdout + r.stderr).strip()


def check(img, names, tag):
    lst = os.path.join(WORK, "want.txt")
    with open(lst, "w") as f:
        f.write("\n".join(sorted(names)) + "\n")
    r = sh(TOOL, img, "check", lst)
    ok = r.returncode == 0
    why = "" if ok else "app: " + r.stdout.strip().splitlines()[-1] if r.stdout.strip() else "app: rc %d" % r.returncode
    r3 = sh("ntfsls", "-f", "-p", "/c", img)
    if r3.returncode != 0:
        # A cut between a system record and its copy in $MFTMirr (the dirty flag of $Volume is mirrored) makes ntfs-3g refuse the
        # volume as a whole, with any driver. ntfsfix sets the mirror right on a copy (it repairs no index: a lost name stays lost).
        fixed = os.path.join(WORK, "fixed.img")
        shutil.copyfile(img, fixed)
        sh("ntfsfix", fixed)
        r3 = sh("ntfsls", "-f", "-p", "/c", fixed)
        stats["mirror"] = stats.get("mirror", 0) + 1
    got = set(r3.stdout.split("\n")) if r3.returncode == 0 else None
    if got is None:
        ok, why = False, (why + "; " if why else "") + "ntfs-3g: " + (r3.stderr.strip().splitlines() or ["rc %d" % r3.returncode])[-1]
    else:
        lost = sorted(n for n in names if n not in got)
        if lost:
            ok, why = False, (why + "; " if why else "") + "ntfs-3g misses %d, e.g. %s" % (len(lost), lost[0])
    if not ok:
        stats["bad"] += 1
        if len(failures) < 12:
            failures.append("%s: %s" % (tag, why))
    return ok


def step(base, op, arg, names_before):
    """run one step to the end on a copy of base, then crash-test it; returns the new base image"""
    stats["steps"] += 1
    full = os.path.join(WORK, "full.img")
    shutil.copyfile(base, full)
    rc, writes, out = run(full, op, arg)
    if rc != 0:
        failures.append("%s %s failed uncut: %s" % (op, arg, out[-200:]))
        stats["bad"] += 1
        return base
    indx = sum(1 for w in writes if w[0] == "INDX")
    if indx >= 1:                                       # the step touched an index block: every cut point
        stats["tested"] += 1
        work = os.path.join(WORK, "cut.img")
        for k in range(1, len(writes)):
            shutil.copyfile(base, work)
            rc, _, out = run(work, op, arg, k)
            if rc != 99:
                failures.append("%s %s cut at %d: did not stop there (rc %d) %s" % (op, arg, k, rc, out[-120:]))
                stats["bad"] += 1
                continue
            stats["points"] += 1
            keep = set(names_before) - ({arg} if op == "rm" else set())
            check(work, keep, "%s %s cut after write %d of %d" % (op, arg[:24], k, len(writes)))
    nb = os.path.join(WORK, "base_next.img")
    shutil.copyfile(full, nb)
    shutil.move(nb, base)
    return base


def main():
    build()
    base = os.path.join(WORK, "base.img")
    with open(base, "wb") as f:
        f.truncate(32 << 20)
    r = sh("mkntfs", "-F", "-Q", "-q", "-c", "4096", "-L", "CRASH", base)
    if r.returncode:
        sys.exit("mkntfs: " + r.stderr)
    r = sh(TOOL, base, "prep", str(PREP))
    if r.returncode:
        sys.exit("prep: " + r.stdout + r.stderr)
    names = {name_of(i) for i in range(PREP)}
    if not check(base, names, "after prep"):
        sys.exit("prep: names missing")
    # adds into one stretch of the folder: leaf splits, then the parent's
    for j in range(36):
        n = "Eintrag_00800_einschub_%03d_mit_einem_recht_langen_Namen" % j
        base = step(base, "add", n, names)
        names.add(n)
    rnd = random.Random(2411)
    order = sorted(names)
    rnd.shuffle(order)
    for n in order[:60]:                                # random removals
        base = step(base, "rm", n, names)
        names.discard(n)
    rest = sorted(names)
    for n in rest[:25]:                                 # ascending
        base = step(base, "rm", n, names)
        names.discard(n)
    for n in sorted(names, reverse=True)[:25]:          # descending
        base = step(base, "rm", n, names)
        names.discard(n)
    check(base, names, "at the end")
    print("crash: %d steps, %d with index writes cut at %d points, %d bad (ntfs-3g needed ntfsfix for $MFTMirr at %d)"
          % (stats["steps"], stats["tested"], stats["points"], stats["bad"], stats.get("mirror", 0)))
    for f in failures:
        print("  " + f)
    shutil.rmtree(WORK, ignore_errors=True)
    sys.exit(1 if stats["bad"] else 0)


main()
