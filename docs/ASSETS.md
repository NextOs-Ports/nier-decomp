# Assets do NieR (Xbox 360): catálogo e plano para a Mali-450

Data: 07/11/2026. Autor: C1 (Etapa D). Ferramenta: `tools/assets/cpk_list.py` (lê só tabelas @UTF e os primeiros bytes de cada arquivo).
Os índices completos ficam em `nier-data/assets/layer{1,2}-toc.json`, privados e fora do git. Este documento traz apenas contagens e formatos.

## 1. Contêineres do disco

| Arquivo | Bytes | Formato | Entradas |
|---|---|---|---|
| `media/layer1.cpk` | 3.147.997.136 | CRI CPK v7, `CPKMC2.10.00, DLL2.73.00`, alinhamento 2048, TOC+ETOC+GTOC, tabelas **sem cifra** | 2701 + 2 CPK aninhados (4384) |
| `media/layer2.cpk` | 1.481.824.424 | idem | 3609 + 1 CPK aninhado (117) |
| `media/movie/*.sfd` | 4 arquivos, 1,7 GB | Sofdec: MPEG Program Stream (pack `0x000001BA`, sistema MPEG-1) com vídeo MPEG-1/2 e áudio ADX | `TITLE_NIER_ALL`, `EID_2000_a0142`, `EID_4430_a0090`, `EID_6000_a0130` |

Regra de deslocamento confirmada: o `FileOffset` do TOC é relativo ao menor valor entre `TocOffset` e `ContentOffset`. A magia encontrada no início de cada arquivo (`CRILAYLA`, `@UTF`, `CPK `) bate com o tipo esperado.

## 2. Tipos de arquivo (extraído = após CRILAYLA)

### layer1.cpk (diretórios: 2D, CHARA, OBJ, WEAPON, SCENE, TIMING, EFFECT, AUDIO, STAGE_DATA/INFO, BG, BULLET, SCRIPT, POS, PATH, SKY, CAMERA)

| Ext. | Qtd | Extraído | Magia / formato | Papel provável |
|---|---|---|---|---|
| `.mdp` | 520 | 1,57 GB | `lzo\0` (287), raw | **Parte física** de modelos/mapas: vértices, índices e texturas na memória da GPU. Blocos LZO |
| `.mdv` | 520 | 319 MB | CRILAYLA | **Parte virtual** do modelo (cabeçalhos, materiais, ossos), par do `.mdp` |
| `.cpk` | 2 | 1,0 GB | CPK | `AUDIO/NIER_BGM.CPK` (170 `.aax`) e `AUDIO/NIER_VOICE_ENG.CPK` (4214 `.aax`) |
| `.2dp` / `.2dv` | 196 / 196 | 64 MB / 2,6 MB | raw / `KPKy` | Interface 2D. O `.2dv` lista `*.gpr.vir`, `*.spdpack`, `*.plipack` (recursos de textura + sprites); o `.2dp` traz os bytes físicos das texturas |
| `.efp` / `.efv` | 72 / 72 | 61 MB / 27 MB | `KPKy` | Efeitos (físico/virtual) |
| `.phy` | 45 | 79 MB | raw | Colisão/física |
| `.csb` | 120 | 58 MB | `@UTF` | Bancos de som CRI (CSB/CueSheet) |
| `.pak` | 70 | 12 MB | `KPKy` | Pacotes diversos (câmera etc.) |
| `.sh` | 41 | 9 MB | `SHFd` | Dados de estágio (`STAGE_INFO`) |
| `.tim` | 206 | 2,5 MB | `MTMI` | Tempos de eventos |
| `.emt` | 244 | 1,7 MB | `EVMT` | Eventos/motores de cena |
| `.scr`, `.txt`, `.csv`, `.ctf`, `.asd`, `.vir`, `.bin` | — | ≈ 15 MB | `CPF*`, texto, `ASDi`, `HEAP`, `sall` | Scripts, tabelas e áudio auxiliar |

### layer2.cpk (diretórios: EVENT 3565, TEXT 38, FONT 4, AUDIO 2)

| Ext. | Qtd | Extraído | Formato | Papel provável |
|---|---|---|---|---|
| `.cpk` | 1 | 950 MB | CPK | `AUDIO/NIER_MA_ENG.CPK`: 117 `.aax` (vozes/música de eventos) |
| `.cmp` | 580 | 203 MB | CRILAYLA (572), `KPKy` | Eventos/cenas comprimidos |
| `.efp`/`.efv` | 50/49 | 155 MB / 27 MB | `KPKy` | Efeitos de eventos |
| `.mdp`/`.mdv` | 83/83 | 100 MB / 36 MB | `lzo\0` / CRILAYLA | Modelos dos eventos |
| `.2dp`/`.2dv` | 12/12 | 33 MB | raw / `KPKy` | 2D dos eventos/texto |
| `.emt` | 2035 | 7 MB | `EVMT` | Roteiro de eventos |
| `.cap`, `.tkd`, `.lpd`, `.raw`, `.txd`, `.inw`, `.tnd`, `.crv` | — | ≈ 8 MB | texto/`KPKy`/`CURV` | Legendas, sincronia de lábios, curvas e fonte |

### Áudio (escopo do X1, só catalogado aqui)

`.aax` é áudio CRI: tabela `@UTF` que envolve segmentos **ADX** (`0x8000`). Mais `.csb` (CueSheet CRI). É o mesmo ecossistema CRI do DMC3, onde o decodificador ADX já existe.

## 3. Motor e convenção de pares

O jogo usa o motor da Cavia, com pares **`*V` / `*P`**: o `V` (virtual) é a memória da CPU, com cabeçalhos e contêiner `KPKy` de N subarquivos nomeados; o `P` (physical) é a memória contígua da GPU, carregada direto em memória física (texturas tiled e vertex/index buffers). Consequências:

- As texturas **já estão no formato da Xenos** (tiling 2D, DXT/8888, big-endian) dentro dos `*P`. O decodificador `xenos_texture.h` do Rayman serve como está, porque o formato é da GPU e não do XDK.
- A conversão pode ser feita **offline, no PC**, em toda a base: o `*V` descreve cada textura (fetch constant ou cabeçalho XPR-like `gpr`), e o `*P` traz os bytes. A conferência byte a byte dos cabeçalhos `gpr`/modelos fica para a Etapa C/E, junto com os fetch constants que o jogo envia ao D3D.
- Compressão: `CRILAYLA` (CRI, de trás para frente) nos `*V`/`.cmp`, e `lzo\0` em blocos nos `*P`. Os dois descompressores vão para `tools/assets/` quando a extração for necessária.

## 4. Plano de texturas para a Mali-450

A Mali-450 (GLES2) **não tem DXT/S3TC**. Formatos disponíveis: ETC1 (RGB, 4 bpp), RGB565, RGBA4444, RGBA5551, RGBA8888 e L/LA8.

| Origem Xenos | Destino Mali | Critério |
|---|---|---|
| DXT1 sem alfa | **ETC1** (4 bpp, mesma taxa) | Padrão para cenário e personagens. Converter de RGBA decodificado com codificador de qualidade alta (etcpak/etc2comp em modo ETC1) |
| DXT1 com alfa de 1 bit | ETC1 cor + **ETC1 alfa em segunda textura** (dual-ETC1, receita da casa `ETC1-DUAL-CAMADA.md`) ou RGBA5551 | Alfa binário: 5551 quando a textura for pequena |
| DXT3/DXT5 (alfa gradual) | dual-ETC1 (cor + alfa) ou RGBA4444 | Interface e efeitos com degradê usam 4444 ou 8888 se a faixa ficar visível. Decidir por diferença medida (PSNR/SSIM por textura, registrada) |
| 8888 | RGBA8888, ou 565/4444 quando não houver perda visível | Texturas dinâmicas e render targets ficam em 8888/565 |
| Normal maps (DXN/CTX1, se houver) | ETC1 de 2 canais ou RG565 reconstruindo Z no shader | Verificar na Etapa C se o jogo usa |

- **Mipmaps**: reaproveitar os mips originais quando o fetch constant indicar mips empacotados. Gerar mips no PC para toda textura estática POT que não os tenha. NPOT fica sem mip, com `CLAMP_TO_EDGE`, ou é reamostrada para POT quando for estática. Motivo: no Rayman a ausência de mipmap era a causa do FPS baixo.
- **Cache por hash**: chave = SHA-256 dos bytes Xenos da textura + parâmetros (formato, dimensões, mips) + versão do conversor. Saída em `nier-data/textures/<hash>.ktx` (privado). O renderizador procura pelo hash e, se não achar, decodifica em tempo de execução.
- **Qualidade**: nada de reduzir resolução de textura de cenário/personagem. Diferenças documentadas por textura quando o formato mudar.
- **Memória**: os `*P` de mapa chegam a 38 MB cada (`A_CENTER_LIBRARY_01.MDP`). Com ETC1 (mesma taxa do DXT1), o tamanho em VRAM se mantém, e a Mali-450 compartilha a RAM de 1–2 GB.

## 5. Vídeo (SFD)

Sofdec = MPEG Program Stream com vídeo MPEG-1 (ou MPEG-2, a confirmar no 1º cabeçalho de sequência) e áudio ADX intercalado. São 4 vídeos, 1,7 GB. Na Mali-450, decodificar com FFmpeg (mpeg1video/mpeg2video + ADX), já presente no NextOS, e enviar o quadro como textura YUV→RGB no shader, sem passar pelo D3D. Confirmar se o jogo chama o decodificador Sofdec linkado (ponto de override) quando o X1 mapear o código.

## 6. Pendências

- Descompressores CRILAYLA e LZO em `tools/assets/` para a conversão offline de texturas (Etapa C/E).
- Ler o formato `gpr` do `.2dv`/`*V` e confirmar o cabeçalho de textura contra o fetch constant capturado no D3D.
- Confirmar o codec de vídeo no cabeçalho de sequência dos `.sfd`.
