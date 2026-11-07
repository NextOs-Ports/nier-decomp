#!/usr/bin/env python3
"""Summarize 120-frame profiler windows with >=150 draws at the sampled frame.
Excludes the window containing the opt-in repeated-frame buffer audit.
These are scene samples, not a paired deterministic FPS benchmark.
"""
import argparse,json,re,statistics
from pathlib import Path
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('console',type=Path)
ap.add_argument('--output',type=Path)
ap.add_argument('--min-draws',type=int,default=150)
ap.add_argument('--max-draws',type=int,default=1000000)
ap.add_argument('--min-frame',type=int,default=0)
ap.add_argument('--max-frame',type=int,default=1000000000)
a=ap.parse_args()
rows=[]; row=None; audit=[]
for line in a.console.read_text().splitlines():
    m=re.search(r'\[nier-gl\] (\d+) quadros, ([\d.]+) ms, (\d+) draws.*texturas ([\d.]+) ms/quadro \(hash ([\d.]+)',line)
    if m: row=dict(frame=int(m[1]),draws=int(m[3]),texture=float(m[4]),texture_hash=float(m[5]))
    if '[nier-perfil]' in line and row:
        nums=[float(x) for x in re.findall(r'\d+\.\d+',line)]
        row.update(zip(['target','state_texture','buffers','draw','resolve','resolves','copies_requested','copies_done','copies_skipped'],nums))
        row['gpu']=nums[-1]  # new Resolve counters may appear before glFinish
        row['state_without_texture']=row['state_texture']-row['texture']
        row['draw_gpu']=row['draw']+row['gpu']
        rows.append(row);row=None
    m=re.search(r'buffer-audit.*frame (\d+)',line)
    if m:audit.append(int(m[1]))
selected=[r for r in rows if a.min_draws<=r['draws']<=a.max_draws and a.min_frame<=r['frame']<=a.max_frame and not any(frame<r['frame']<=frame+120 for frame in audit)]
if not selected:raise SystemExit('No 3D profile windows')
keys=['target','state_texture','state_without_texture','texture','texture_hash','buffers','draw','resolve','gpu','draw_gpu']
r=dict(source=str(a.console),criteria=f'draws {a.min_draws}..{a.max_draws}, frames {a.min_frame}..{a.max_frame}; exclude audit window; 120-frame averages',
       median_ms={k:statistics.median(row[k] for row in selected) for k in keys},windows=selected)
text=json.dumps(r,indent=2)+'\n'
if a.output:a.output.write_text(text)
else:print(text,end='')
