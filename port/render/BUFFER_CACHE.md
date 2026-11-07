# Cache de vértices e índices — X1-H2

O recorder captura snapshots imutáveis. A chave inclui endereço efetivo do
stream guest, stride, quantidade, formato, endian, escala e capacidades de
fetch. Um hash de 16 amostras rejeita mudanças óbvias; **todo hit compara todos
os bytes referenciados**. Alteração fora das amostras também invalida o cache.
Padding não usado pelo atributo não afeta sua identidade.

Cada snapshot tem um serial único. A thread GL mantém VBO/IBO por serial e só
chama `glBufferData` quando esse serial ainda não está residente. Frames mantêm
referências aos snapshots até terminar o desenho; sobrescrever um endereço do
guest não altera um quadro pendente. CPU: LRU de 64 MiB/4096 entradas. GPU:
64 MiB, com descarte somente entre frames. Evicção pode exigir reenvio posterior.
O teto de 192 MiB do quadro inclui os snapshots referenciados conservadoramente.

Formatos compactos: byte/short inteiros, unsigned normalizados, float1/2/3/4 e
half2/4 se `GL_OES_vertex_half_float` estiver anunciado. Packed 10/11 bits,
escala de expoente e signed-normalized conservam a conversão float4. GLES2
especifica uma normalização signed que não é universalmente idêntica ao
`max(c/32767,-1)` usado pelo decoder; não assumir equivalência no Mali.
Fontes: https://registry.khronos.org/OpenGL/specs/es/2.0/es_full_spec_2.0.pdf
(§2.1.2) e https://registry.khronos.org/OpenGL/extensions/OES/OES_vertex_half_float.txt.

`NIER_BUFFER_REFERENCE=1` conserva o caminho anterior de conversão e upload
por desenho para A/B. `NIER_BUFFER_AUDIT=<diretório existente>` é diagnóstico
opt-in: depois de 70 s e ≥50 draws não-UP, repete um único Frame com buffers
float4 e compactos. Compara todos os alvos de cor e resolve. Duas referências
antes e uma depois precisam concordar, incluindo a imagem original com
geometria visível antes de qualquer replay; dependência temporal produz
INCONCLUSIVE, nunca PASS. Grava PPMs privados e resultado no console. A janela
da auditoria não deve entrar na medição de desempenho. Não altera shader,
textura, resolve, resolução, geometria, HUD ou cadência lógica.

## Validação

`port/tests/vertex_cache_test.cpp` testa invalidação fora da amostra, layout,
limite LRU, vida útil e IBO. Compara 480 imagens (491520 vertex fetches) contra o
decoder anterior, em contexto GLES2 real; recusa ES3. Exercita formatos,
quatro endians, sinal, normalização e escala, inclusive fallback. Compilar com
Clang C++23 e ligar EGL/GLESv2; repetir com ASan/UBSan para verificar acessos.
No PC com NVIDIA, selecionar explicitamente o vendor Mesa para o teste:

```sh
clang++ -std=c++23 -O2 port/tests/vertex_cache_test.cpp -lEGL -lGLESv2 -o "$BUILD/vertex_cache_test"
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json \
EGL_PLATFORM=surfaceless MESA_GLES_VERSION_OVERRIDE=2.0 "$BUILD/vertex_cache_test"
python3 tools/render/check_leve.py --diff origin/main
```

A execução real usa `tools/render/roteiro-3d.txt`, save vazio, GLES2 e corte ≤300s.
Os recibos X1 em `messages/` distinguem CPU/GL no PC de medições físicas Mali.
