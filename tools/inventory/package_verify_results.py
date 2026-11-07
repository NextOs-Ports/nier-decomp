#!/usr/bin/env python3
"""Package successful comparator runs for the coordinator, without editing progress."""
import argparse
import hashlib
import json
from pathlib import Path


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--inventory',type=Path,required=True)
    ap.add_argument('--worktree',type=Path,required=True)
    ap.add_argument('--integration-root',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('runs',nargs='+',type=Path)
    a=ap.parse_args();root=a.worktree.resolve()
    inventory=json.loads(a.inventory.read_text())
    functions={f['address']:f for f in inventory['functions']}
    results=[];seen=set();trials=None;table=None
    for path in a.runs:
        run=json.loads(path.read_text())
        if run.get('tool')!='nierverify' or run['failed']!=0:raise ValueError(f'Unsuccessful run: {path}')
        if trials is not None and trials!=run['trials']:raise ValueError('Mixed trial counts')
        trials=run['trials'];table=run['functions_in_table']
        for result in run['results']:
            address=int(result['address'],16)
            if address in seen or address not in functions:raise ValueError('Duplicate/unknown address')
            if not result['pass'] or result['equal']<500:raise ValueError('Insufficient successful comparisons')
            source=Path(result['file']).resolve();relative=source.relative_to(root)
            if relative.parts[:2]!=('src','decomp'):raise ValueError('Not a reconstructed source')
            item=dict(result);item.update(file=str(a.integration_root/relative),
                source_relative=relative.as_posix(),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                size=functions[address]['size'])
            results.append(item);seen.add(address)
    if not results:raise ValueError('Empty result set')
    document=dict(tool='nierverify',functions_in_table=table,trials=trials,
        executable_sha256=inventory['baseline']['executable_sha256'],
        results=sorted(results,key=lambda r:int(r['address'],16)),failed=0,
        verified_functions=len(results),verified_bytes=sum(r['size'] for r in results))
    a.output.write_text(json.dumps(document,indent=2)+'\n')
    print(f"{len(results)} functions, {document['verified_bytes']} bytes, {trials} trials each")

if __name__=='__main__':main()
