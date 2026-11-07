# Base do mip tail: LUT do prólogo

O fetch da LUT de cor do prólogo descreve DXT1 128×4 com `packed_mips=1`.
Mesmo o nível zero desse recurso está no tail: seu primeiro texel fica em
(0,16), correspondente ao deslocamento tiled de 1024 bytes. O decodificador
lia (0,0), misturando dados de outros níveis. A LUT capturada tinha faixas
pretas, verdes e magenta desconexas; com o deslocamento, forma um gradiente
contínuo. O prólogo PC deixa de apresentar os contornos de cor abruptos.

`PackedBaseOffset` aplica o deslocamento somente quando a menor dimensão
lógica é ≤16 e o fetch ativa packed mips. Texturas grandes e bases não
empacotadas mantêm seu endereço. Os caminhos k8 especializados seguem a mesma
regra; leituras tiled e sua assinatura incluem a página mínima de 4 KiB para
cobrir a seleção de bancos. Não muda precisão de shaders, render targets nem
as otimizações de desenho.

Validação sintética: `port/tests/texture_packed_test.cpp`, 60 casos de
DXT1/RGBA8/k8, orientações, modos linear/tiled e packed/unpacked, com dados
contrastantes no endereço antigo e no correto; guarda adicional de base >16.
Passaram no host com ASan/UBSan e em ARM64/QEMU. Exemplos de comandos:

```
clang++ -std=c++17 -O2 -fsanitize=address,undefined port/tests/texture_packed_test.cpp -o "$OUT/texture_packed_test"
"$OUT/texture_packed_test"
python3 tools/render/check_leve.py
```

Provas visuais e dados comerciais permanecem na área privada `nier-build/x1/provas/i1`.
A comparação adicional da gameplay branca e a validação física seguem com a
missão X1-D/coordenador; o teste do prólogo não certifica esses resultados.
