#!/usr/bin/env python3
"""Ranking por thread/família usando o simbolizador do X1 e seu gate de Build ID."""
import argparse
from collections import Counter, defaultdict
import contextlib
import hashlib
import io
import json
from pathlib import Path
import runpy
import struct
import sys

ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('profile', type=Path)
ap.add_argument('--elf', type=Path, action='append', required=True)
ap.add_argument('--inventory', type=Path, required=True)
ap.add_argument('--pe', type=Path, required=True)
ap.add_argument('--sampled-list', type=Path, required=True)
ap.add_argument('--active-list', type=Path, required=True)
ap.add_argument('--symbolized-output', type=Path, required=True)
ap.add_argument('--output', type=Path, required=True)
ap.add_argument('--scene', default='gameplay')
a = ap.parse_args()
root = Path(__file__).resolve().parents[2]
symbolizer = root / 'tools/inventory/export_cpu_samples.py'
sys.argv = [str(symbolizer), str(a.profile), '--inventory', str(a.inventory),
            '--output', str(a.symbolized_output)]
for elf in a.elf:
    sys.argv += ['--elf', str(elf)]
with contextlib.redirect_stdout(io.StringIO()):
    state = runpy.run_path(str(symbolizer), run_name='__main__')
result = json.loads(a.symbolized_output.read_text())
profile = json.loads(a.profile.read_text())
inventory = {f['address']: f for f in json.loads(a.inventory.read_text())['functions']}
sampled = set(a.sampled_list.read_text().split())
active = set(a.active_list.read_text().split())
hz = profile['hz']

# Candidatas a template: preservar operações/registradores, mascarar apenas
# imediatos de formas conhecidas. Igualdade de forma não comprova equivalência.
pe = a.pe.read_bytes()
families = defaultdict(list)
family_for = {}
for address, function in inventory.items():
    words = struct.unpack_from(f'>{function["size"] // 4}I', pe, address - 0x82000000)
    shape = []
    for word in words:
        opcode = word >> 26
        if opcode == 18 and word & 1:
            shape.append(word & 0xFC000003)
        elif opcode in (7, 8, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29) or 32 <= opcode <= 55:
            shape.append(word & 0xFFFF0000)
        elif opcode in (58, 62):
            shape.append(word & 0xFFFF0003)
        else:
            shape.append(word)
    key = hashlib.sha256(struct.pack(f'>{len(shape)}I', *shape)).hexdigest()[:20]
    families[key].append(address)
    family_for[address] = key

def rows(counts, denominator, limit=100):
    output = []
    for address, count in counts.most_common(limit):
        function = inventory.get(address, {})
        name = f'sub_{address:08X}'
        output.append(dict(address=address, name=name, own_samples=count,
            sample_seconds=count / hz,
            percent_scope_samples=100 * count / denominator if denominator else 0,
            original_bytes=function.get('size'), family=family_for.get(address),
            family_members=len(families.get(family_for.get(address), [])),
            native_in_captured_build=name in sampled, active_in_latest_d1=name in active))
    return output

def role(tid, name):
    runtime = ' '.join(symbol for _, symbol in state['thread_host'][tid])
    guest = state['thread_guest'][tid]
    if any(word in name for word in ('Audio', 'Pulse', 'XMA')):
        return 'audio', 'nome da thread'
    if 'RecordDraw' in runtime or 'RecordBeginVertices' in runtime:
        return 'captura', 'símbolo de captura de comandos'
    if 'RenderThread()' in runtime or 'UploadVertexTextures' in runtime:
        return 'render_gl', 'símbolos do replay GL'
    if 'TimerThreadMain' in runtime:
        return 'timers', 'símbolo do timer'
    if name.startswith('GPU '):
        return 'gpu_runtime', 'nome da thread'
    if any(guest.get(address, 0) for address in (0x824F5E40, 0x824F5EF0)):
        return 'audio', 'rotinas DSP'
    game_samples = sum(count for address, count in guest.items() if 0x82100000 <= address < 0x82480000)
    if game_samples >= 100 and game_samples > sum(guest.values()) / 2:
        return 'jogo', 'maioria de amostras guest no código do jogo'
    if name.startswith('Main XThread'):
        return 'jogo_main', 'nome da thread'
    if name.startswith('XThread'):
        if 'libMali' in runtime and not guest:
            return 'driver_gl', 'Mali sem funções guest amostradas'
        return 'workers', 'XThread sem papel mais específico comprovado'
    return 'frontend_outros', 'papel não determinado'

threads = []
groups = defaultdict(Counter)
group_cpu = Counter()
group_samples = Counter()
for metadata in sorted(profile['threads'], key=lambda t: -t['cpu_seconds_observed']):
    tid = metadata['tid']
    category, reason = role(tid, metadata['name'])
    total = state['threads'][tid]
    guests = state['thread_guest'][tid]
    groups[category].update(guests)
    group_cpu[category] += metadata['cpu_seconds_observed']
    group_samples[category] += total
    threads.append(dict(**metadata, role=category, role_evidence=reason,
        samples=total, guest_samples=sum(guests.values()),
        top_guest=rows(guests, total),
        top_runtime=[dict(module=m, symbol=s, own_samples=n, sample_seconds=n / hz)
                     for (m, s), n in state['thread_host'][tid].most_common(15)]))
target = Counter()
for category in ('jogo', 'jogo_main', 'captura', 'workers'):
    target.update(groups[category])
family_samples = Counter()
for address, count in target.items():
    if address in family_for:
        family_samples[family_for[address]] += count
result.update(schema_version=3, scene=a.scene,
    profile_sha256=hashlib.sha256(a.profile.read_bytes()).hexdigest(),
    build_ids=profile['build_ids'], delay_seconds=profile['delay_seconds'],
    sampled_overrides=len(sampled), latest_d1_overrides=len(active),
    unassigned_cpu_seconds=profile['process_cpu_seconds'] - sum(group_cpu.values()),
    decomp_priority_scope='jogo + jogo_main + captura + workers; soma das amostras dessas threads',
    threads=threads,
    groups=[dict(role=category, cpu_seconds_observed=group_cpu[category],
                 samples=group_samples[category], top_guest=rows(counts, group_samples[category]))
            for category, counts in sorted(groups.items(), key=lambda x: -group_cpu[x[0]])],
    decomp_priority=rows(target, sum(group_samples[c] for c in ('jogo', 'jogo_main', 'captura', 'workers')), 300),
    family_priority=[dict(shape=key, own_samples=count, sample_seconds=count / hz,
                          members=[dict(name=f'sub_{address:08X}', original_bytes=inventory[address]['size'],
                                        samples=target[address], active_in_latest_d1=f'sub_{address:08X}' in active)
                                   for address in families[key]])
                     for key, count in family_samples.most_common(100)])
result['limitations'] += [
    'Papéis das threads inferidos dos símbolos/nomes; não são labels fornecidos pelo jogo.',
    'sample_seconds=count/hz é estimativa amostrada; não redistribui CPU não observada.',
    'Famílias por forma são candidatas; cada implementação ainda exige revisão e nierverify.',
    f'O perfil usa {len(sampled)} overrides; funções já ativas no D1 posterior não representam trabalho novo.',
    'Custo de captura/GL nativo aparece separado e não conta como decomp guest.']
a.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
print(json.dumps(dict(output=str(a.output), coverage_percent=result['sample_coverage_percent'],
    groups=[dict(role=g['role'], cpu_seconds=g['cpu_seconds_observed']) for g in result['groups']],
    priority=result['decomp_priority'][:12]), ensure_ascii=False, indent=2))
