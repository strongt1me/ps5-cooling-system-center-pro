#!/usr/bin/env python3
import pathlib
import re

text = pathlib.Path("src/ps5tm.h").read_text(encoding="utf-8", errors="ignore")
match = re.search(r'^\s*#define\s+PS5TM_VERSION\s+"([^"]+)"', text, re.M)
print(match.group(1) if match else "")
