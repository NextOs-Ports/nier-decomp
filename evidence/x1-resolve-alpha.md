# Alpha HDR nas cópias de Resolve

Os shaders do jogo usam alpha para transportar a escala HDR (`32 - 31*a`).
O blit compartilhado pelas cópias de Resolve e pela apresentação forçava alpha
igual a 1, destruindo essa informação nos passes `keep` e nas cópias parciais.
Agora ele preserva RGBA no Resolve; somente a apresentação torna alpha opaco.
O sinal de apresentação já existente (`flip`) seleciona esse comportamento,
sem novos programas ou chamadas por quadro. Posição/matrizes dos VS permanecem
na precisão original; o fragment de blit continua mediump.

Todos os alvos de cor desse caminho usam textura `GL_RGBA/GL_UNSIGNED_BYTE`.
Uma consulta na primeira criação registra os bits efetivos RGBA do driver.
No PC GLES2 foram 8/8/8/8. A validação física continua com o coordenador.
A extensão OES_rgb8_rgba8 define formatos de **renderbuffer**; não é motivo
para passar RGBA8_OES como internalformat de TexImage2D nesta rota GLES2.
Especificação: https://registry.khronos.org/OpenGL/extensions/OES/OES_rgb8_rgba8.txt

O teste `nier_resolve_alpha_test` usa o renderer de produção e dados sintéticos,
sem jogo ou tradutor privado. Exercita `ReplayResolve` com keep, alias por troca
de FBO e o shader de apresentação, conferindo exatamente 256 valores de alpha
junto a RGB. Antes do keep, instala máscara alpha desligada e blend que zeraria
a saída: o caminho de cópia precisa restaurar o estado. No renderer anterior,
a prova falha porque todos os valores viram 255; no candidato, RGBA permanece
exato nas cópias/alias e alpha fica 255 apenas na apresentação. GL sem erros.

```
cmake --build "$BUILD" --target nier_resolve_alpha_test
EGL_PLATFORM=surfaceless MESA_GLES_VERSION_OVERRIDE=2.0 "$BUILD/render/nier_resolve_alpha_test"
python3 tools/render/check_leve.py
```

A prova sintética estabelece o contrato de alpha. A causa completa do branco
na gameplay depende da comparação visual Mali-like e da leitura dos passes;
não se declara a gameplay corrigida apenas por esse teste.

## Níveis pequenos

O planejamento e o Resolve também recusavam destinos menores que 8×8.
Agora 1×1, 2×2 e 4×4 são preservados, e a cadeia é atualizada em todos os
quadros: ela pode transportar luminância, não só bloom. O teste acima cobre
esses tamanhos, planejamento direto e ausência de alternância no quadro ímpar.

A rodada PC Mali-like com `NIER_BLOOM_MIN=0` confirmou os destinos pequenos,
mas a constante PC0 ainda muda de aproximadamente 0,7 na cutscene para
(1191,1262,1366) na gameplay; o branco persiste. Isso descarta a hipótese de
que apenas habilitar os Resolve pequenos resolveria a falha. O rastreio da
origem da constante e do uso de LOD segue separado; não se aplica clamp
arbitrário à exposição como correção.
