#!/usr/bin/env python3
"""Rodada C1 no aparelho autorizado (.164) com o renderizador GLES2. Baseada na receita B1 do X1.

Travas locks/aparelho + /tmp/nextos-device-test.lock; dados por NFS só leitura exportado só para o .164;
ES parado e mascarado (nunca religado); jogo cortado em 55 s + 5 s no próprio comando; limpeza no finally.
Uso: run_device_c1.py BUILD_DIR OUT_DIR "ROTEIRO_DE_ENTRADA" (roteiro pode ser vazio)
"""
import fcntl
import subprocess
import sys
from pathlib import Path

host = '192.168.31.164'
server = '192.168.31.239'
game = '/mnt/ARQUIVOS/HOME-FELIPE/ports_compile/nier-data/game'
remote = '/storage/roms/ports/nier-dev-c1'
mnt = remote + '/game'
build = Path(sys.argv[1])
out = Path(sys.argv[2])
script = sys.argv[3] if len(sys.argv) > 3 else ''
capture = not (len(sys.argv) > 4 and sys.argv[4] == 'semcaptura')  # medição limpa de tempo
out.mkdir(parents=True, exist_ok=True)
locks = []
for name in ['/mnt/ARQUIVOS/HOME-FELIPE/ports_compile/nier-team/locks/aparelho', '/tmp/nextos-device-test.lock']:
    f = open(name, 'a')
    fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
    locks.append(f)


def run(args, check=True, timeout=120):
    p = subprocess.run(args, text=True, capture_output=True, timeout=timeout)
    print(p.stdout, p.stderr, flush=True)
    if check and p.returncode:
        raise RuntimeError('falhou %d %s' % (p.returncode, args[0]))
    return p


def ssh(command, check=True, timeout=120):
    return run(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', 'root@' + host, command], check, timeout)


mounted = exported = False
try:
    ssh('test -z "$(pgrep -x nier)"')  # nada do NieR rodando (de ninguém)
    run(['sudo', '-n', 'exportfs', '-i', '-o', 'ro,sync,no_subtree_check,insecure,all_squash,anonuid=1000,anongid=1000', host + ':' + game])
    exported = True
    ssh('mkdir -p %s %s/run; rm -rf %s/run/capturas; mkdir -p %s/run/capturas %s/run/shaders; '
        'mount -i -t nfs4 -o ro,vers=4.1,proto=tcp,addr=%s,clientaddr=%s %s:%s %s' % (mnt, remote, remote, remote, remote, server, host, server, game, mnt))
    mounted = True
    ssh('test -r %s/default.xex && test -r %s/media/layer1.cpk' % (mnt, mnt))
    for name in ['nier', 'librexruntime.so', 'librexgpu-null.so']:
        run(['smbclient', '-N', '//' + host + '/roms', '-c', 'put "%s" "ports/nier-dev-c1/%s"' % (build / name, name)], timeout=180)
    ssh('chmod +x %s/nier; sha256sum %s/nier %s/librexruntime.so %s/librexgpu-null.so' % (remote, remote, remote, remote))
    env = ('NIER_NATIVE_RENDER=1 %sNIER_GLES2_SHADERS=%s/run/shaders '
           'NIER_INPUT_SCRIPT="%s" LD_LIBRARY_PATH=%s' % ('NIER_GLES2_CAPTURE=%s/run/capturas ' % remote if capture else '', remote, script, remote))
    sampler = ('( for i in $(seq 1 40); do sleep 2; p=$(pgrep -x nier) || break; '
               'echo "$(date +%%s) $(grep -E \'VmRSS|VmHWM\' /proc/$p/status | tr -s \' \' | tr \'\\n\' \' \')"; done ) > ' + remote + '/run/rss.txt 2>&1 & ')
    cmd = ('systemctl stop emustation; systemctl mask emustation; test -z "$(pgrep -x emulationstatio)"; ' + sampler +
           'cd %s/run && %s flock -n /tmp/nextos-device-test.lock timeout -s TERM -k 5 55 %s/nier '
           '--game_data_root=%s --user_data_root=%s/run/user --cache_root=%s/run/cache --gpu_plugin=null --headless '
           '--log_file=%s/run/runtime.log --log_level=info > %s/run/console.log 2>&1; echo status=$?'
           % (remote, env, remote, mnt, remote, remote, remote, remote))
    ssh(cmd, check=False, timeout=75)
finally:
    if mounted:
        ssh('test -z "$(pgrep -x nier)" && umount %s' % mnt, check=False, timeout=20)
    if exported:
        run(['sudo', '-n', 'exportfs', '-u', host + ':' + game], check=False)
    ssh('pgrep -x nier; awk \'$2=="%s" {print}\' /proc/mounts; systemctl is-active emustation; systemctl is-enabled emustation' % mnt, check=False)
    run(['smbclient', '-N', '//' + host + '/roms', '-c', 'lcd "%s"; cd ports/nier-dev-c1/run; prompt OFF; recurse ON; mget console.log runtime.log rss.txt capturas shaders' % out], check=False, timeout=300)
