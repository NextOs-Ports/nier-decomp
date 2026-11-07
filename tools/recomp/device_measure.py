#!/usr/bin/env python3
"""Bound one diagnostic process and sample device resources (no system tuning)."""
import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time


def read(path):
    try:
        return Path(path).read_text()
    except (OSError, ProcessLookupError):
        return ''


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--binary', required=True, type=Path)
    ap.add_argument('--game', required=True, type=Path)
    ap.add_argument('--output', required=True, type=Path)
    a = ap.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    for name in ['SDL_AUDIODRIVER','SDL_AUDIO_DRIVER','AUDIODEV','SDL_AUDIO_ALSA_DEFAULT_PLAYBACK_DEVICE']:
        env.pop(name, None)
    env.update(LD_LIBRARY_PATH=str(a.binary.parent), NIER_TIMING_PROBE='1', NIER_DIAG_SWAP_TRACE='1')
    command = [str(a.binary), '--game_data_root='+str(a.game),
        '--user_data_root='+str(a.binary.parent/'user-data'),
        '--cache_root='+str(a.binary.parent/'cache'), '--gpu_plugin=null', '--headless',
        '--mnk_mode=false', '--hid_mappings_file=/storage/.config/SDL-GameControllerDB/gamecontrollerdb.txt',
        '--log_level=trace','--log_noisy', '--log_file='+str(a.output/'runtime.log'),
        '--log_max_file_size_mb=40', '--log_max_files=3']
    receipt = dict(command=command, temperature_before=read('/sys/class/thermal/thermal_zone0/temp').strip(),samples=[])
    start = time.monotonic()
    receipt['started_monotonic'] = start
    receipt['clock_ticks_per_second'] = os.sysconf('SC_CLK_TCK')
    pulse_candidate = None
    pulse_stability = 0
    stop = False
    def requested(signum, frame):
        nonlocal stop
        stop = True
    signal.signal(signal.SIGTERM, requested)
    signal.signal(signal.SIGINT, requested)
    recorder = None
    raw = None
    with (a.output/'console.log').open('w') as log:
        proc = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
        receipt['pid'] = proc.pid
        try:
            while proc.poll() is None and not stop and time.monotonic()-start < 53:
                elapsed = time.monotonic()-start
                status=read('/proc/%d/status'%proc.pid)
                wanted={k:int(v) for k,v in re.findall(r'^(VmRSS|VmHWM|VmSwap|Threads):\s+(\d+)',status,re.M)}
                cpu=[line for line in read('/proc/stat').splitlines() if re.match(r'cpu\d+ ',line)]
                task_stats={p.name:read(p/'stat') for p in Path('/proc/%d/task'%proc.pid).glob('*')}
                receipt['samples'].append(dict(elapsed=elapsed,status=wanted,cpu=cpu,tasks=task_stats))
                if elapsed > 3 and 'pulse' not in receipt:
                    pulse=subprocess.run(['pactl','list','sink-inputs'],capture_output=True,text=True,timeout=3).stdout
                    (a.output/'pulse-sink-inputs.txt').write_text(pulse)
                    for block in re.split(r'(?=Sink Input #)',pulse):
                        if re.search(r'application.process.id = "%d"'%proc.pid,block):
                            match=re.search(r'Sink Input #(\d+)',block)
                            if match:
                                if pulse_candidate == match[1]:
                                    pulse_stability += 1
                                else:
                                    pulse_candidate, pulse_stability = match[1], 1
                                if pulse_stability < 3:
                                    break
                                receipt['pulse']=dict(sink_input=int(match[1]),process_id=proc.pid)
                                raw=(a.output/'audio-monitor.f32').open('wb')
                                recorder=subprocess.Popen(['timeout','-k','1','8','parec',
                                    '--device=@DEFAULT_MONITOR@', '--monitor-stream='+match[1], '--format=float32le','--rate=48000',
                                    '--channels=2','--raw'],stdout=raw,stderr=(a.output/'audio-monitor.err').open('w'))
                                break
                time.sleep(0.5)
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait(timeout=1)
            if recorder is not None:
                if recorder.poll() is None:
                    recorder.terminate()
                try:
                    recorder.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    recorder.kill()
                    recorder.wait()
                raw.close()
            receipt.update(exit_status=proc.returncode,elapsed_seconds=time.monotonic()-start,
                temperature_after=read('/sys/class/thermal/thermal_zone0/temp').strip(),
                process_remaining=Path('/proc/%d'%proc.pid).exists())
            (a.output/'measurement.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({k:v for k,v in receipt.items() if k not in ['samples','command']}))

if __name__ == '__main__':
    main()
