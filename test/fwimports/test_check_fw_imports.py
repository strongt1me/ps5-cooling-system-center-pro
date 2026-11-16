"""Host test of tools/check_fw_imports.py (16.11.2026): the NID rule, the names looked up at run time, the reading of a library and of the ELF, the verdicts.
No firmware dump is needed: the libraries are built here in the smallest form the reader accepts."""
import importlib.util
import os
import struct
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
spec = importlib.util.spec_from_file_location("check_fw_imports", os.path.join(ROOT, "tools", "check_fw_imports.py"))
C = importlib.util.module_from_spec(spec)
spec.loader.exec_module(C)


def write_bytes(path, data):
    with open(path, "wb") as f:
        f.write(data)


def fake_sprx(path, exports, imports=()):
    """A library as the reader needs it: one PT_LOAD over the whole file, one PT_DYNAMIC, the symbols named <NID>#A#B."""
    strtab = b"\0"
    syms = [struct.pack("<IBBHQQ", 0, 0, 0, 0, 0, 0)]
    for nid_, defined in [(n, True) for n in exports] + [(n, False) for n in imports]:
        off = len(strtab)
        strtab += ("%s#A#B" % nid_).encode() + b"\0"
        syms.append(struct.pack("<IBBHQQ", off, 0x12, 0, 1 if defined else 0, 0x1000 if defined else 0, 8))
    symtab = b"".join(syms)
    head = 64 + 2 * 56
    str_off = head
    sym_off = (str_off + len(strtab) + 7) & ~7
    dyn_off = (sym_off + len(symtab) + 15) & ~15
    dyn = b"".join(struct.pack("<QQ", t, v) for t, v in [(5, str_off), (10, len(strtab)), (6, sym_off), (11, 24), (0x6100003F, len(symtab)), (0, 0)])
    total = dyn_off + len(dyn)
    ehdr = struct.pack("<16sHHIQQQIHHHHHH", b"\x7fELF\x02\x01\x01\x09" + b"\0" * 8, 3, 62, 1, 0, 64, 0, 0, 64, 56, 2, 64, 0, 0)
    phdr = struct.pack("<IIQQQQQQ", 1, 5, 0, 0, 0, total, total, 0x4000) + struct.pack("<IIQQQQQQ", 2, 4, dyn_off, dyn_off, dyn_off, len(dyn), len(dyn), 8)
    blob = bytearray(total)
    blob[0:len(ehdr)] = ehdr
    blob[64:64 + len(phdr)] = phdr
    blob[str_off:str_off + len(strtab)] = strtab
    blob[sym_off:sym_off + len(symtab)] = symtab
    blob[dyn_off:dyn_off + len(dyn)] = dyn
    write_bytes(path, bytes(blob))


def fake_payload(path, imports, needed):
    """An ELF as the SDK makes it: sections .dynstr, .dynsym and .dynamic are enough. imports: [(name, 'GLOBAL'|'WEAK')]."""
    dynstr = b"\0"
    off = {}
    for name in [n for n, _b in imports] + needed:
        off[name] = len(dynstr)
        dynstr += name.encode() + b"\0"
    dynsym = struct.pack("<IBBHQQ", 0, 0, 0, 0, 0, 0)
    for name, bind in imports:
        dynsym += struct.pack("<IBBHQQ", off[name], (1 if bind == "GLOBAL" else 2) << 4 | 2, 0, 0, 0, 0)
    dynsym += struct.pack("<IBBHQQ", len(dynstr), 0x12, 0, 1, 0x100, 8)               # a defined symbol: not an import
    dynstr2 = dynstr + b"defined_fn\0"
    dynamic = b"".join(struct.pack("<QQ", 1, off[n]) for n in needed) + struct.pack("<QQ", 0, 0)
    base = 64
    o_str = base
    o_sym = (o_str + len(dynstr2) + 7) & ~7
    o_dyn = (o_sym + len(dynsym) + 7) & ~7
    o_sh = (o_dyn + len(dynamic) + 7) & ~7
    sh = struct.pack("<IIQQQQIIQQ", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    sh += struct.pack("<IIQQQQIIQQ", 0, 3, 2, 0, o_str, len(dynstr2), 0, 0, 1, 0)
    sh += struct.pack("<IIQQQQIIQQ", 0, 11, 2, 0, o_sym, len(dynsym), 1, 1, 8, 24)
    sh += struct.pack("<IIQQQQIIQQ", 0, 6, 3, 0, o_dyn, len(dynamic), 1, 0, 8, 16)
    ehdr = struct.pack("<16sHHIQQQIHHHHHH", b"\x7fELF\x02\x01\x01\x09" + b"\0" * 8, 3, 62, 1, 0, 0, o_sh, 0, 64, 0, 0, 64, 4, 0)
    blob = bytearray(o_sh + len(sh))
    blob[0:64] = ehdr
    blob[o_str:o_str + len(dynstr2)] = dynstr2
    blob[o_sym:o_sym + len(dynsym)] = dynsym
    blob[o_dyn:o_dyn + len(dynamic)] = dynamic
    blob[o_sh:o_sh + len(sh)] = sh
    write_bytes(path, bytes(blob))


class NidRule(unittest.TestCase):
    def test_known_names(self):
        # read from the system libraries of 43 firmwares (1.00 to 12.70); akmC1nXbrLE is also written in src/dualsense.c
        self.assertEqual(C.nid("sceVideoOutOpen"), "Up36PTk687E")
        self.assertEqual(C.nid("sceVideoOutGetResolutionStatus"), "6kPnj51T62Y")
        self.assertEqual(C.nid("sceVideoOutGetMonitorInfo"), "lafoiUl5-5U")
        self.assertEqual(C.nid("sceHidControlGetBatteryState"), "akmC1nXbrLE")
        self.assertEqual(C.nid("sceHidControlInit"), "Y1WkAv+vINM")

    def test_shape(self):
        for name in ("a", "sceKernelUsleep", "x" * 200):
            n = C.nid(name)
            self.assertEqual(len(n), 11)
            self.assertTrue(all(c in C.ID_ALPHABET for c in n))


class RuntimeNames(unittest.TestCase):
    def test_extract(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "a.c"), "w") as f:
                f.write('''
#define VOUT "libSceVideoOut.sprx"
void f(void) {
  p1 = (fn)ps5tm_dynsym(VOUT, "sceVideoOutOpen");
  p2 = (fn)ps5tm_dynsym(VOUT,
                        "sceVideoOutGetMonitorInfo");
  p3 = dlsym(RTLD_DEFAULT, "sceKernelGetAppInfo");
  p4 = ps5tm_dynsym_in(0, "libSceHidControl.sprx", "sceHidControlGetDeviceInfo");
  p5 = dlsym(RTLD_DEFAULT, "not_a_sce_name");
  p6 = kernel_dynlib_dlsym(getpid(), h, "sceAppInstUtilAppInstallTitleDir");
}
''')
            found = C.runtime_names(d)
        self.assertEqual(sorted(found), ["sceAppInstUtilAppInstallTitleDir", "sceHidControlGetDeviceInfo", "sceKernelGetAppInfo",
                                         "sceVideoOutGetMonitorInfo", "sceVideoOutOpen"])


class Reading(unittest.TestCase):
    def test_library_exports(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "libX.sprx")
            fake_sprx(p, ["AAAAAAAAAAA", "BBBBBBBBBBB"], imports=["CCCCCCCCCCC"])
            lib = C._Lib(p)
            try:
                self.assertEqual(lib.export_nids(), {"AAAAAAAAAAA", "BBBBBBBBBBB"})            # the import is not an export
            finally:
                lib.close()

    def test_unreadable_files(self):
        with tempfile.TemporaryDirectory() as d:
            for name, data in (("empty.sprx", b""), ("self.sprx", b"\x54\x14\xf5\xee" + b"\0" * 100), ("short.sprx", b"\x7fELF")):
                p = os.path.join(d, name)
                write_bytes(p, data)
                with self.assertRaises((ValueError, OSError, struct.error)):
                    C._Lib(p).close()

    def test_scan_and_cache(self):
        with tempfile.TemporaryDirectory() as d:
            fw = os.path.join(d, "fw", "12.70")
            lib = os.path.join(fw, "system", "common", "lib")
            app = os.path.join(fw, "system_ex", "app", "NPXS40001", "lib")
            os.makedirs(lib)
            os.makedirs(app)
            fake_sprx(os.path.join(lib, "libA.sprx"), [C.nid("sceFoo")])
            fake_sprx(os.path.join(app, "libApp.sprx"), [C.nid("sceFromApp")])               # the modules of applications are not system libraries
            write_bytes(os.path.join(lib, "broken.sprx"), b"junk")
            cache = os.path.join(d, "cache")
            r1 = C.scan_firmware(fw, cache, "12.70")
            self.assertEqual(r1["libs"], 1)
            self.assertEqual(r1["unreadable"], 1)
            self.assertIn(C.nid("sceFoo"), r1["nids"])
            self.assertNotIn(C.nid("sceFromApp"), r1["nids"])
            self.assertTrue(os.path.exists(os.path.join(cache, "12.70.json")))
            self.assertEqual(C.scan_firmware(fw, cache, "12.70"), r1)                       # from the cache
            fake_sprx(os.path.join(lib, "libB.sprx"), [C.nid("sceBar")])                     # a changed folder is read again
            r2 = C.scan_firmware(fw, cache, "12.70")
            self.assertEqual(r2["libs"], 2)
            self.assertIn(C.nid("sceBar"), r2["nids"])

    def test_folder_names(self):
        with tempfile.TemporaryDirectory() as d:
            fw = os.path.join(d, "2.50")
            for sub in ("lib fw 2.50", os.path.join("PS5 Pro 12.40 Lib", "lib"), os.path.join("system", "common", "lib"), "other", os.path.join("system_ex", "app", "X", "lib")):
                os.makedirs(os.path.join(fw, sub))
                fake_sprx(os.path.join(fw, sub, "libZ.sprx"), [C.nid("sceZ")])
            found = [os.path.relpath(p, fw).replace(os.sep, "/") for p in C._lib_files(fw)]
            self.assertEqual(found, ["PS5 Pro 12.40 Lib/lib/libZ.sprx", "lib fw 2.50/libZ.sprx", "system/common/lib/libZ.sprx"])

    def test_payload(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "x.elf")
            fake_payload(p, [("memcpy", "GLOBAL"), ("weakone", "WEAK"), ("sceKernelBar", "GLOBAL")], ["libkernel_sys.sprx", "libScePad.sprx"])
            imports, needed = C.payload_imports(p)
            self.assertEqual(imports, [("memcpy", "GLOBAL"), ("sceKernelBar", "GLOBAL"), ("weakone", "WEAK")])
            self.assertEqual(needed, ["libkernel_sys.sprx", "libScePad.sprx"])


class Verdicts(unittest.TestCase):
    def test_evaluate(self):
        fws = {
            "1.00": {"nids": {C.nid("memcpy"): ["libc.sprx"], C.nid("sceAppInstUtilAppInstallTitleDir"): ["libAIU.sprx"], C.nid("weakone"): ["libW.sprx"]}, "libs": 3, "unreadable": 0},
            "2.00": {"nids": {C.nid("memcpy"): ["libc.sprx"]}, "libs": 2, "unreadable": 0},
            "3.00": {"nids": {}, "libs": 0, "unreadable": 9},
        }
        imports = [("memcpy", "GLOBAL"), ("weakone", "WEAK"), ("sceKernelBar", "GLOBAL")]
        runtime = {"sceAppInstUtilAppInstallTitleDir": {"tile.c"}, "sceUnknownNew": {"x.c"}, "memcpy": {"y.c"}}
        rep = C.evaluate(imports, ["libc.sprx"], runtime, fws)
        self.assertEqual(rep["firmwares"], ["1.00", "2.00"])
        self.assertEqual(rep["unreadable"], ["3.00"])
        by = {r["name"]: r for r in rep["static"]}
        self.assertEqual(by["memcpy"]["verdict"], "ok")
        self.assertEqual(by["weakone"]["verdict"], "optional")
        self.assertEqual(by["sceKernelBar"]["verdict"], "fehlt")
        rt = {r["name"]: r for r in rep["runtime"]}
        self.assertEqual(rt["sceAppInstUtilAppInstallTitleDir"]["verdict"], "bekannt")
        self.assertEqual(rt["sceUnknownNew"]["verdict"], "fehlt")
        self.assertEqual(rt["memcpy"]["verdict"], "ok")
        self.assertEqual(rep["problems"], 2)                                                  # sceKernelBar and sceUnknownNew

    def test_outside_needed(self):
        fws = {"1.00": {"nids": {C.nid("sceFoo"): ["libOther.sprx"]}, "libs": 1, "unreadable": 0}}
        rep = C.evaluate([("sceFoo", "GLOBAL")], ["libkernel_sys.sprx"], {}, fws)
        self.assertEqual(rep["static"][0]["outside_needed"], ["1.00"])
        self.assertEqual(rep["problems"], 0)

    def test_ranges(self):
        u = ["1.00", "2.00", "3.00", "4.00", "5.00"]
        self.assertEqual(C._ranges(["1.00", "2.00", "4.00"], u), "1.00-2.00, 4.00")
        self.assertEqual(C._ranges(["5.00"], u), "5.00")
        self.assertEqual(C._ranges([], u), "")


if __name__ == "__main__":
    unittest.main()
