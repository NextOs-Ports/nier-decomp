# Mapa do Direct3D do NieR (XDK 2.0.9328)

Data: 07/11/2026. Autor: C1 (Etapa B). Entrada: PE decifrada `nier-data/pe/default.pe`, SHA-256 `ff218ae3…b4df` (XEX `f375d506…653b`).
Ferramenta: `tools/render/find_d3d.py` (capstone PPC64 BE; funções = `.pdata` ∪ alvos de `bl`; âncoras de `docs/RENDER.md` §4). Análise estática apenas, sem execução.

**Confiança:** **alta** = âncora de kernel/PM4 mais argumentos e campos do device conferidos no código; **média** = campos do device e forma da função batem com o 20871/DMC3; **inferida** = papel deduzido do uso, a confirmar em execução.

## 1. Bloco do D3D

- O código do D3D fica em `0x82569680–0x82590000`: 573 funções, 221 chamadas de fora. Delimitado pelos chamadores dos thunks `Vd*` (`0x82625994–0x82625B24`) e pela tabela de setters de render state.
- 18.913 funções no executável inteiro (15.087 no `.pdata` + folhas sem `.pdata` alcançadas por `bl`). Os setters folha do D3D (ex.: `0x8256BB90`) não têm `.pdata`.

## 2. Estrutura do device (9328)

O primeiro bloco (até `+0x2A50`) é **idêntico** ao do 20871/DMC3. A partir de algum ponto entre `+0x2A50` e `+0x3144`, os campos estão **−0xC8** em relação ao 20871/DMC3.

| Deslocamento | Conteúdo | Evidência | 20871/DMC3 |
|---|---|---|---|
| `+0x00…+0x28` | palavras de dirty (64 bits) | flush do desenho testa `0,8,0x10,0x18,0x20` e zera | igual |
| `+0x40` | tabela de setters de render state, **0x65 entradas** | `0x8257D190` itera 0x194/4 entradas e chama `0x40(dev+4i)` | igual |
| `+0x1D4` | tabela de setters de sampler | `0x8257D190`, segundo laço | igual |
| `+0x480` | fetch constants: textura `t` em `+0x480+24t`, vértice `v` em `+0x480+8v`, stream `s` → `v = 95−s` | `SetStreamSource` `0x8256C6E0`: `(0xEF−s)*8` | igual |
| `+0x780` / `+0x1780` | constantes float VS / PS (regs `0x4000` / `0x4400`) | flush em `DrawVertices` | igual |
| `+0x2780` | constantes booleanas (`0x4900`) | idem | igual |
| `+0x2880`, `+0x28CC`, `+0x2920`, `+0x2934`, `+0x2964`, `+0x29B8`, `+0x2A50` | blocos de registradores `0x2000`, `0x2100`, `0x2180`, `0x2200`, `0x2280`, `0x2300`, `0x2380` | `0x82571508` chama `0x82578080(dev, máscara, reg, dev+off)` | igual |
| `+0x2938` | `RB_BLENDCONTROL` (blend) | setters `0x8256A3B8…0x8256A748` | igual |
| `+0x2908` | viewport `PA_CL_VPORT_*` | — | igual |
| `+0x3144` | **índice atual (IB)** | `SetIndices` grava, `DrawIndexedVertices` lê | `0x320C` |
| `+0x3148…+0x3154` / `+0x3158` | **render targets 0–3 / depth-stencil** | `Resolve`: `0x3148+4·idx`, idx 4 = `0x3158` | `0x3210` / `0x3220` |
| `+0x315C+4s` | stream source `s` | `SetStreamSource` | — |
| `+0x31A0` | stride/4 do stream 0 | `DrawVerticesUP` grava `stride>>2` | `0x3268` |
| `+0x3244` / `+0x3248` | **pixel shader / vertex shader atuais** | `0x82570568` / `0x82570728` | `0x330C` / `0x3310` |
| `+0x2A94`, `+0x30`, `+0x38` | ring/command buffer: ponteiro de escrita `+0x30`, limite `+0x38` | emissão de PM4 em todo o bloco | — |
| `+0x35EC` | **intervalo de apresentação** (render state 94) | ver §4 | — |

## 3. Funções

| Endereço | Função | Confiança | Evidência |
|---|---|---|---|
| `0x8257AB38` | **D3DDevice_Swap / Present** | alta | único caminho até `VdSwap`. Chamado pelo jogo em `0x825A6D08` |
| `0x8257D7F8` | inicialização dos engines | alta | `VdInitializeEngines` + `VdSetGraphicsInterruptCallback` |
| `0x82574870` | ring buffer | alta | `VdInitializeRingBuffer`, `VdEnableRingBufferRPtrWriteBack`, PM4 `ME_INIT` |
| `0x8257DBE8` | desligamento | alta | `VdShutdownEngines` |
| `0x82571508` | **DrawVertices**(dev, prim, startVertex, vertexCount) | alta | flush de constantes/estado, `DRAW_INDX 0xC0012201` com índice automático (`|0x80`), divide contagens > 0xFFFF |
| `0x825718F8` | **DrawIndexedVertices**(dev, prim, baseVertex, startIndex, indexCount) | alta | mesmo flush, lê o IB `+0x3144`, `DRAW_INDX` |
| `0x82571070` | **DrawVerticesUP** (stride em r6) | média | grava `stride>>2` em `+0x31A0`, `DRAW_INDX`. Ordem exata dos argumentos a confirmar |
| `0x82579240` | upload de shader antes do desenho | alta | `IM_LOAD` ×3, `SET_CONSTANT`. Chamado pelos 3 desenhos |
| `0x82578FB8` | upload de constantes ALU | alta | `LOAD_ALU_CONSTANT` |
| `0x82578418` / `0x82578080` | escrita de constantes / bloco de registradores | alta | chamadas com (`0x4000`, `+0x780`), (`0x2200`, `+0x2934`)… |
| `0x82570568` | **SetPixelShader**(dev, ps) | média | grava `+0x3244`, marca dirty |
| `0x82570728` | **SetVertexShader**(dev, vs) | média | grava `+0x3248`, dirty de vfetch (`oris 8`) |
| `0x8256C6E0` | **SetStreamSource**(dev, stream, vb, offset, stride, dirtyMask) | alta | fórmula `v=95−s`, 6 argumentos como no DMC3 |
| `0x8256C800` | **SetIndices**(dev, ib) | alta | grava `+0x3144`. Chamado com SetStreamSource em `0x8258B850` |
| `0x8256F910` | **SetTexture**(dev, sampler, tex, dirtyMask) | alta | `0x8257D190` chama por sampler com `tex=0` e máscara `1<<(63−(32+i))`. 19 chamadores no jogo |
| `0x8256BBC0`, `0x8256BD68` | setters de sampler (reescrevem a palavra do fetch de textura) | média | `(i+0x30)*0x18`, 15 chamadores cada |
| `0x82585888` | **Resolve**(dev, flags, srcRect, destTex, destPoint, level, slice, clearColor, clearZ=f1, …) | alta | usa `RB_COPY_CONTROL`/`RB_COPY_DEST_INFO`, flags `&7` = alvo, `&0x70` = profundidade, ClearZ em f1 |
| `0x825FFBE0`, `0x825FFCB0` | resolução de 4 alvos + depth do jogo (motor) | média | chamam `Resolve` em laço |
| `0x8257D190` | reaplica todos os render/sampler states (ResetState) | alta | percorre as duas tabelas de setters e preserva `+0x35EC` |
| `0x8256BB90` / `0x8256BB98` | **SetRenderState(94) / GetRenderState(94)** = intervalo de apresentação | alta | ver §4 |
| `0x8257A708`, `0x8257AB30` | monta a espera de vblank do Swap conforme `+0x35EC` | alta | ver §4 |
| `0x8256A320…0x8256BBA0` | setters de render state (tabela de 0x65 em `0x8277ED60`, 12 B por entrada: getter, setter, padrão) | alta | tabela lida da imagem |
| `0x82587940` | operação sobre o RT0/depth atual (`+0x3148`/`+0x3158`) | inferida | candidata a Clear. 2 chamadores (`0x8258E030`, `0x826002C0`) |
| `0x82580290`, `0x8257F660` | caminho com `DRAW_INDX_2` + Resolve, registrado como callback em `0x82580E20` | inferida | candidato a Clear rápido/EDRAM |
| `0x825846F0`, `0x8257FDF8`, `0x825805D8`, `0x82580CF0` | cópia para o front buffer dentro do Swap (`DRAW_INDX_2` + `IM_LOAD_IMMEDIATE`) | média | só alcançados por `0x8257AB38` |

**Pendentes (Etapa E / captura do X1):** `ClearF` público, `SetRenderTarget`/`SetDepthStencilSurface` (gravam `+0x3148…+0x3158`), `SetVertexDeclaration`, `CreateVertexShader`/`CreatePixelShader`, `CreateTexture`/buffers, `Set*ShaderConstantF` (o NieR pode gravar `+0x780` direto), `BeginTiling`/`EndTiling`. O jogo chega a `DrawIndexedVertices` só por `0x8258B850` ← `0x825FA150`. Uma captura curta no X1 deve confirmar que não há outros caminhos (ex.: ponteiro de função).

## 4. `0x8256BB90`: a trava 30/60 (patch 60 FPS da Xenia Canary)

```
0x8256BB90  stw r4, 0x35EC(r3)   ; SetRenderState(dev, 94, valor)
0x8256BB94  blr
0x8256BB98  lwz r3, 0x35EC(r3)   ; GetRenderState(dev, 94)
```

- É a entrada 94 da tabela de setters (`0x8277F1CC` aponta para ela; padrão **0**). Pela convenção do XDK 360 (`D3DRS_* = índice·4`, `D3DRS_ZENABLE = 40` na entrada 10), equivale ao render state 376. Pelo uso, é o **intervalo de apresentação** (`D3DRS_PRESENTINTERVAL`, nome inferido).
- Consumidor: `0x8257A708`, chamado pelo **Swap** (`0x8257AB38` → `0x8257A8D0`/`0x8257A950`) e por `0x8257AB30`. Ele converte o valor (0 ou 1 → 1 vblank; 2 → 2; 4 → 3) e monta os pacotes `WAIT_REG_MEM` (`0xC0043C00`) que fazem a GPU esperar esse número de vblanks antes da troca.
- Quem chama o setter: o **jogo**, em `0x825A6F48`, a cada quadro:
  - `t = max(tempo("Draw"), tempo("Main"))` (perfis nomeados com as strings `"Draw"`/`"Main"`, via objeto global em `0x84841AF8`).
  - `limite = N · 0,0166834` (1/59,94 s, constante em `0x820A1D18`; `N` vem do objeto em `0x827CE830`).
  - `t ≤ limite` → **1 (60 FPS)**; senão **2 (30 FPS)**; e se `t > 0,0333667` (`0x820A1D14`, 2/59,94 s) → **4 (15 FPS)**.
  - Em seguida chama `0x8257AB30` (espera de vblank com o novo intervalo).
- Ou seja, o NieR tem **vsync adaptativo**: tenta 60 e cai para 30/15 quando o quadro estoura. No Xenia, a leitura de resolve deixa o tempo de "Draw" alto, o jogo escolhe 2 e fica preso em 30. O patch de NOP mantém o padrão 0 (1 vblank).
- **Para o port:** a apresentação é nossa (thread de render GLES2), então o render state 94 vira só uma informação e a troca nunca espera vblanks da GPU guest. O ponto de decisão do jogo (`0x825A6F48`) é o lugar certo para registrar, no ritmo medido, quantas atualizações por quadro desenhado o motor espera. **A velocidade lógica do jogo pode depender do intervalo escolhido** (Δt por vblank). Isso precisa da medição externa do coordenador antes de fixar a cadência x2/x3.

## 5. Uso no port (Etapa E)

Substituir (modelo DMC3, sem PM4): `0x82571508`, `0x825718F8`, `0x82571070`, `0x82585888`, `0x8257AB38`, os setters de shader/stream/índice/textura e o upload de shader `0x82579240` (anulado). O estado é lido do device com os deslocamentos da §2. O render state 94 é só registrado.
