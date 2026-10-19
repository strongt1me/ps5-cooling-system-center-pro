#!/usr/bin/env python3
"""Writes src/upd_ca.h: BearSSL trust anchors for the game updates (19.10.2026).

Two sets, made with BearSSL's own tool (brssl ta) from the PEM certificates PatchDL 0.3.0 ships (GPL-3.0-or-later,
Knutwurst): the Sony set (SCEI DNAS Root, src/patchdl_ca.h) for Sony's download servers, and the public set
(src/patchdl_doh_ca.h: SSL.com ECC, GTS R1, GTS R4, GlobalSign Root CA, DigiCert G2/G3) for the DNS-over-HTTPS resolvers and the
patch history sites prosperopatches.com / orbispatches.com (both chain to GTS Root R4 / GlobalSign, checked 19.10.2026).
Since the same day a third set, UPD_WEB_*, for the payload store (paystore.c): the catalog on itsplk.github.io and the
files on github.com / release-assets.githubusercontent.com chain to ISRG Root X1/X2 (Let's Encrypt) and USERTrust ECC/RSA
(Sectigo), checked 19.10.2026; the PEMs come from the system's CA bundle (Debian ca-certificates, /etc/ssl/certs).
Run in WSL: python3 tools/mk_upd_ta.py <brssl> <patchdl src dir> <out upd_ca.h> [web root PEM files ...]"""
import os, re, subprocess, sys, tempfile, hashlib, base64

brssl, src, out = sys.argv[1], sys.argv[2], sys.argv[3]
web_files = sys.argv[4:]


def pems(path):
    t = open(path, encoding="utf-8").read()
    lits = re.findall(r'"((?:[^"\\]|\\.)*)"', t)
    joined = "".join(bytes(l, "utf-8").decode("unicode_escape") for l in lits)
    return re.findall(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", joined, re.S)


def ta_code(pem_list, prefix):
    with tempfile.TemporaryDirectory() as d:
        files = []
        for i, p in enumerate(pem_list):
            f = os.path.join(d, "c%d.pem" % i)
            open(f, "w").write(p + "\n")
            files.append(f)
        r = subprocess.run([brssl, "ta"] + files, capture_output=True, text=True)
        if r.returncode:
            sys.exit(r.stderr)
        c = r.stdout
    # rename the generated symbols so two sets can live in one file
    c = re.sub(r"\bTA(\d+)_", prefix + r"\1_", c)
    c = c.replace("TAs[", prefix + "TAs[").replace("TAs_NUM", prefix + "TAs_NUM")
    c = c.replace("static const br_x509_trust_anchor " + prefix + "TAs", "static const br_x509_trust_anchor " + prefix + "TAs")
    return c


sony = pems(os.path.join(src, "patchdl_ca.h"))
pub = pems(os.path.join(src, "patchdl_doh_ca.h"))
assert len(sony) == 1 and len(pub) >= 5, (len(sony), len(pub))
names = []
for p in sony + pub:
    der = base64.b64decode("".join(l for l in p.splitlines() if not l.startswith("-----")))
    names.append(hashlib.sha256(der).hexdigest()[:16])
hdr = ["/* Trust anchors of the game updates (19.10.2026), made by tools/mk_upd_ta.py with BearSSL's brssl ta.",
       " *",
       " * UPD_SONY_*: \"SCEI DNAS Root 05\", the root of Sony's PS5/PS4 download CDN (sgst/gst/gs2/gs-sec .playstation.net), in no",
       " * public bundle; the only anchor for Sony's hosts. UPD_PUB_*: SSL.com Root ECC, GTS Root R1, GTS Root R4, GlobalSign Root CA,",
       " * DigiCert Global Root G2 and G3, for the DNS-over-HTTPS resolvers (cloudflare-dns.com, dns.google) and the patch history",
       " * sites (prosperopatches.com, orbispatches.com). The certificates are those PatchDL 0.3.0 by Knutwurst carries",
       " * (src/patchdl_ca.h, src/patchdl_doh_ca.h, GPL-3.0-or-later): public, non-secret. SHA-256 (first 16 hex) of the DER:",
       " * " + ", ".join(names) + ". */",
       "#ifndef PS5TM_UPD_CA_H", "#define PS5TM_UPD_CA_H", "", "#include <bearssl.h>", ""]
body = ta_code(sony, "UPD_SONY_") + "\n" + ta_code(pub, "UPD_PUB_")
if web_files:
    web = [re.search(r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", open(f).read(), re.S).group(0) for f in web_files]
    body += "\n/* UPD_WEB_*: " + ", ".join(os.path.basename(f) for f in web_files) + " (payload store: GitHub Pages and releases) */\n"
    body += ta_code(web, "UPD_WEB_")
open(out, "w", newline="\n").write("\n".join(hdr) + body + "\n#endif\n")
print("ok", len(sony), len(pub))
