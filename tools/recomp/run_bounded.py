#!/usr/bin/env python3
"""Run an externally bounded probe and record child resource consumption."""
import json
from pathlib import Path
import resource
import subprocess
import sys
import time
start = time.monotonic()
result = subprocess.call(sys.argv[2:])
usage = resource.getrusage(resource.RUSAGE_CHILDREN)
Path(sys.argv[1]).write_text(json.dumps(dict(elapsed_seconds=time.monotonic()-start,
    exit_status=result, max_rss_kib=usage.ru_maxrss, user_seconds=usage.ru_utime,
    system_seconds=usage.ru_stime), indent=2)+'\n')
sys.exit(result if result >= 0 else 128-result)
