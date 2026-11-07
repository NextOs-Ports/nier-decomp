#!/usr/bin/env python3
"""Trava "renderizador leve" (dono, 07/11): recusa no caminho por quadro do port/render as chamadas que
sincronizam ou copiam a tela na Mali-450. Permitido só em inicialização/diagnóstico marcado com // LEVE_OK: <motivo>.
Uso: tools/render/check_leve.py [arquivos...]  -> código 1 se houver violação."""
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
files = [pathlib.Path(a) for a in sys.argv[1:]] or sorted(pathlib.Path('port/render').glob('*.cpp'))
bad = 0
for f in files:
    for n, line in enumerate(f.read_text(errors='replace').splitlines(), 1):
        if 'LEVE_OK' in line: continue
        code = re.sub(r'"(\\.|[^"\\])*"', '""', line).split('//')[0]
        for pat, why in PROIBIDO.items():
            if re.search(pat, code):
                print(f'{f}:{n}: {why}\n    {line.strip()[:140]}'); bad += 1
print(f'check_leve: {bad} violação(ões)' if bad else 'check_leve: OK')
sys.exit(1 if bad else 0)
