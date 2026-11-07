# Shaders do NieR para a Mali-450 (Etapa C)

Data: 07/11/2026. Autor: C1. Saída privada: `nier-data/shaders/` (ESSL, microcódigo, `report.tsv`, `final.json`), fora do git.

## Ferramenta

- `tools/shaders/nier_shaders.cpp` + `tools/shaders/build.sh`: compila contra o tradutor Xenos→GLSL do DMC3 (`NextOs-Ports/dmc3-hd-nextos`, commit `c76b074`, `runtime/xenos_shader.cpp` sha256 `d46a61fe7e5a062e…`) com `-DNEXTOS_GLES2` (emite ESSL 100). O código do DMC3 não é copiado para este repositório público.
- `nier_shaders <saída> <virtuais…> --fisico <físicos…>`:
  1. Nos arquivos virtuais (PE, `.EFV`), acha os contêineres `0x102A1100` (pixel) e `0x102A1101` (vértice) e coleta os tamanhos de programa.
  2. Nos arquivos físicos (PE, `.EFP`), acha programas Xenos pelo fluxo de controle: o 1º `exec` começa logo após o bloco de CF, os seguintes são contíguos e o `END` fecha o bloco. O tamanho tem de bater com algum contêiner.
  3. Traduz, deduplica por hash do microcódigo e grava `<HASH>_{vs,ps}.glsl/.ucode`.
- Tipo VS/PS: se o tradutor recusa o programa como pixel por "vfetch no shader de pixel", é VS. Senão, é PS.
- Compilação: `glslangValidator` (ESSL 100, sintaxe e semântica). Não há compilador offline da Mali neste PC (`malioc` ausente), então o limite real de instruções fica para a prova no aparelho.

**Formato no NieR:** a parte **virtual** (`.EFV`, cabeçalho na PE) traz só o cabeçalho do contêiner (tabela de constantes, `vs_3_0/ps_3_0 2.0.9328.0`). O **microcódigo** fica na parte **física** (`.EFP`, região física da PE), deduplicado e referenciado pela relocação do KPK. Por isso os contêineres da PE têm a área física zerada.

## Resultado (medido)

| | Quantidade |
|---|---|
| Contêineres lidos (PE + 121 `.EFV`) | 3.569 (154 tamanhos distintos) |
| Programas físicos achados (PE + 122 `.EFP`) | 5.604 → **573 distintos** |
| Traduzem **e** compilam ESSL 100 | **482** (244 VS + 238 PS) = 84% |
| Traduzem, glslang recusa | 13 VS: helper `nx_sample7/16` não emitido (fetch de vértice por índice fora da faixa do helper) |
| Não traduzem | 78: operação vetorial 18 (32), vfetch classificado como pixel sem par VS (21), fetch opcode 24 (15), exportação 61 (3), op. vetorial 30 (2), escrita de profundidade no PS (2), fetch opcode 3 (1) |

### Contra os limites medidos da Mali-450

Probe GLES2 do aparelho (`device-gles2-probe.txt`): **256 vec4 de uniformes de vértice**, **256 de fragmento**, 12 varyings, 16 atributos, fragmento só `mediump`. A regra comum fala em "VS ≤ 304 vec4". O valor medido no driver é 256, e é ele que vale aqui.

| | VS | PS |
|---|---|---|
| Constantes distintas lidas (máx.) | 16 | 17 |
| Maior índice lido | 255 (literais no topo, `c252–c255`) | 255 |
| Declaração do tradutor | `VC[256]` + `wideScale` + bools → **acima de 256** | `PC[256]` + extras → **acima de 256** |
| Índice relativo (`a0`) | 144 VS | 0 |
| Instruções (estim. = palavras/3) | máx. 88 | máx. 134 |
| Varyings / samplers | ≤ 8 / 0 | ≤ 8 / ≤ 6 |

## Plano para os que estouram ou falham

1. **Uniformes (todos os 394 que declaram 256):** compactar no carregamento. Os literais (`c252–c255`, imutáveis por shader) viram `const vec4`. As constantes lidas são renumeradas num array denso (≤ 17 vec4), e o renderizador envia só essas faixas (plano de cópia do DMC3, `constant_copy_plan.h`).
2. **VS com índice relativo (144, skinning/paletas):** declarar `VC[N]` com `N` = maior faixa endereçável real, nunca 256 + extras. A faixa sai do `a0` e da tabela de constantes do contêiner (nome/registro/tamanho, ex.: matrizes de ossos). Os literais saem do array. Medir no aparelho a primeira cena com personagem.
3. **VS sem formato de vfetch:** a tradução offline de VS não sabe os formatos de vértice, que o D3D preenche no desenho pela declaração (como no DMC3, `BindVertexFetches`). Os VS são traduzidos de novo **em tempo de execução**, com a declaração real, e entram no cache por hash (microcódigo + declaração). O PC faz o aquecimento antes do aparelho. Os PS offline já são finais.
4. **Operações não cobertas** (op. vetorial 18/30, fetch 24/3, export 61, `nx_sampleN`): estender o tradutor numa cópia NieR (`port/render/xenos/`), com teste por shader. Fetch 24 = `getTextureWeights`/LOD; vetorial 18/30 = `MAX4`/`KILL*` a confirmar no desmontador `xenos_disasm.py` do DMC3.
5. **Profundidade no PS (2):** a Mali-450 não tem `gl_FragDepth`. Ver a cena e usar adaptador provado ou pular o efeito, com registro.
6. **Cobertura:** faltam os shaders dos modelos (`.MDV`/`.MDP`, comprimidos com CRILAYLA/LZO) e os com tamanho fora dos contêineres conhecidos. A captura em execução (`CreateVertexShader`/`CreatePixelShader`) fecha a lista real. Precisam de descompressores em `tools/assets/`.
