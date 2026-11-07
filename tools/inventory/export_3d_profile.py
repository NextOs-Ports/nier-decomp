#!/usr/bin/env python3
"""Export the delayed 3D perf run; reuse the verified sample parser."""
import argparse
import hashlib
import json
from pathlib import Path
from collections import Counter
from export_hot_profile import export, HEADER

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('run', type=Path)
ap.add_argument('binary', type=Path)
ap.add_argument('inventory', type=Path)
ap.add_argument('output', type=Path)
a = ap.parse_args()
counts = Counter()
for line in (a.run / 'stacks.txt').open():
    match = HEADER.match(line)
    if match and match[1].strip() not in ('sudo', 'perf', 'timeout', 'sh'):
        counts[int(match[2])] += 1
pid = counts.most_common(1)[0][0]
r = export(a.run / 'stacks.txt', json.loads(a.inventory.read_text()), pid)
r['source'] = 'perf cpu-clock:u 199 Hz DWARF 16384; delayed 140 s; host GLES2; empty save; 195 s timeout + 5 s kill'
r['host_binary_sha256'] = hashlib.sha256(a.binary.read_bytes()).hexdigest()
r['limitations'] = ['Host x86, not Mali performance or original gameplay speed.',
                     'Sampling gives CPU estimates; inclusive totals overlap.',
                     'Scene must be confirmed by accompanying captures.']
r['top30'] = r.pop('top300')[:30]
for thread in r['threads']:
    thread['top30'] = thread.pop('top300')[:30]
a.output.write_text(json.dumps(r, indent=2) + '\n')
print(json.dumps({'samples':r['process_samples'],'cpu_seconds':r['sampled_cpu_seconds'], 'top':r['top30'][:10]},indent=2))
