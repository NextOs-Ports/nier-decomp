#!/usr/bin/env python3
"""Trava "renderizador leve" (dono, 07/11): recusa no caminho por quadro do port/render as chamadas que
sincronizam ou copiam a tela na Mali-450. Permitido só em inicialização/diagnóstico marcado com // LEVE_OK: <motivo>.
Uso: tools/render/check_leve.py [arquivos...]  -> código 1 se houver violação.
     tools/render/check_leve.py --diff <base>  -> só as linhas ACRESCENTADAS desde <base> em port/render (gate de commit)."""
import re, sys, pathlib
PROIBIDO = {
    r'\bglGet(Integerv|Floatv|Booleanv|Error|TexParameter\w*|BufferParameter\w*)\s*\(': 'glGet* sincroniza o pipeline da Mali',
    r'\bglCopyTex(Sub)?Image2D\s*\(': 'cópia de tela grava+relê o tile buffer (use quad blit para FBO, como o Rayman)',
    r'\bglReadPixels\s*\(': 'leitura da GPU para a CPU trava o quadro',
    r'\bglFinish\s*\(': 'glFinish espera a GPU inteira',
    r'\bglFlush\s*\(': 'glFlush força envio no meio do quadro',
    r'\bglGenerateMipmap\s*\(': 'mipmap por quadro: gerar só no 1º envio da textura',
    r'\bglCompileShader\s*\(|\bglLinkProgram\s*\(': 'compilar/ligar só no cache de programas (1ª vez)',
}
def linhas():
    if sys.argv[1:2] == ['--diff']:
        import subprocess
        out = subprocess.run(['git', 'diff', '-U0', sys.argv[2], '--', 'port/render'], text=True, capture_output=True, check=True).stdout
        f, n = '?', 0
        for l in out.splitlines():
            if l.startswith('+++ '): f = l[6:] if l.startswith('+++ b/') else l[4:]
            elif l.startswith('@@'): n = int(re.search(r'\+(\d+)', l).group(1))
            elif l.startswith('+'): yield f, n, l[1:]; n += 1
        return
    for f in [pathlib.Path(a) for a in sys.argv[1:]] or sorted(pathlib.Path('port/render').glob('*.cpp')):
        for n, line in enumerate(f.read_text(errors='replace').splitlines(), 1): yield f, n, line
bad = 0
if True:
    for f, n, line in linhas():
        if 'LEVE_OK' in line: continue
        code = re.sub(r'"(\\.|[^"\\])*"', '""', line).split('//')[0]
        for pat, why in PROIBIDO.items():
            if re.search(pat, code):
                print(f'{f}:{n}: {why}\n    {line.strip()[:140]}'); bad += 1
print(f'check_leve: {bad} violação(ões)' if bad else 'check_leve: OK')
sys.exit(1 if bad else 0)
