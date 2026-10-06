#!/usr/bin/env python3
from datetime import datetime, timezone

print(datetime.now(timezone.utc).strftime("%Y%m%d"))
