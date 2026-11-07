# Cache de vértices e índices — X1-H2

O recorder captura snapshots imutáveis. A chave inclui endereço efetivo do
stream guest, stride, quantidade, formato, endian, escala e capacidades de
fetch. Por ordem X1-H3, o hash cobre 16 elementos inteiros distribuídos pelo
buffer; não há memcmp nem hash completo por draw. Alterações fora das amostras
podem não ser detectadas. UI/DrawUP ignora esse cache e é capturada integralmente.

Só dados iguais observados em pelo menos dois quadros distintos são promovidos
a VBO/IBO STATIC. Repetir o desenho no mesmo quadro não promove. Mudança detectada
reinicia a observação. UI/DrawUP sempre usa o stream dinâmico.

**X1-H4: um upload por quadro em cada VBO/IBO.** Antes do primeiro draw,
`PrepareFrameStreams` fixa as decisões de cache, calcula os offsets, reúne todos
os atributos/índices dinâmicos em dois vetores CPU e chama `glBufferData` com os
dados completos uma vez para cada buffer não vazio. Três pares de objetos GL
são alternados entre quadros; a orfanagem preserva o backing store ainda em voo.
O caminho normal não usa glBufferSubData. A referência opt-in anterior ainda
usa SubData após orfanagem por draw, antes de qualquer uso daquele novo store.

Não restaurar o anel H3 de SubData: F11 no Mali mediu 186–271 ms/quadro nos
menus, apesar de zero criações e resultado rápido no PC. O driver sincroniza
ou copia o buffer inteiro mesmo quando a região atualizada não se sobrepõe.
**PC valida a imagem e os contadores; só o .164 aprova desempenho dos buffers.**
Aceite físico: menus≤4 ms e 3D<21 ms, perseguindo a meta final≤3 ms.

Cada snapshot tem serial único; bytes pendentes não mudam. CPU: LRU de
64 MiB/4096 entradas. STATIC GL: 64 MiB, descarte entre frames após ao menos
três quadros sem uso. Evicção pode exigir reenvio posterior. Staging CPU cresce
até o maior quadro observado; não há mais a pré-alocação H3 de 54 MiB em GL.
O teto de 192 MiB do quadro inclui os snapshots referenciados.
`[nier-perfil] buffers/quadros` informa misses, criações, uploads static/stream
e bytes dinâmicos por quadro. Stream tem no máximo 2 uploads/quadro (1 VBO+1 IBO).
O tempo de `buffers` inclui cálculo/cópia do staging e uploads, além dos binds.

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

`port/tests/vertex_cache_test.cpp` testa invalidação em amostra, promoção entre quadros, layout,
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

## Medição que motivou X1-H3

O coordenador mediu H2 na Mali: buffers 3D 134,6→21,2 ms, mas UI animada
regrediu de 1–4 para 50–77 ms. H2 criava/excluía buffers STATIC para cada versão
dinâmica. O anel e a promoção são a correção dessa regressão; a aceitação física
é UI≤4 ms e 3D≤21 ms, avançando à meta final≤3 ms.
