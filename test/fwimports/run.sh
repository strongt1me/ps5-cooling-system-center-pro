#!/bin/sh
# Host test of tools/check_fw_imports.py (the check of the app's system functions against the firmware dumps) - needs python3 only:
#   sh test/fwimports/run.sh
set -e
cd "$(dirname "$0")/../.."
python3 -I test/fwimports/test_check_fw_imports.py
