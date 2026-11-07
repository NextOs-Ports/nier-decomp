# Renderização do NieR: estudo das bases e plano GLES2 (Mali-450)

Data: 07/11/2026. Autor: C1. Escopo: Etapa A (estudo dirigido) da missão do C1.
Este documento cobre só a arquitetura gráfica. Não registra imagem do NieR desenhada nem renderizador pronto.

## 1. Identidade gráfica do executável (medida)

Lida dos cabeçalhos em claro do `default.xex` (SHA-256 `f375d506…653b`), com `tools/render/xex_libs.py`:

| Biblioteca estática | NieR | Rayman Origins | Consequência |
|---|---|---|---|
| D3D9 | **2.0.9328.0** | 2.0.20871.0 | O D3D do NieR é de outra geração do XDK. Os endereços e os bytes das funções do Rayman não servem como assinatura exata. |
| XGRAPHC | 2.0.9328.0 | 2.0.20871.0 | Tiling/formatos de textura iguais na Xenos; utilitários XGraphics com outro código. |
| D3DX9 | 2.0.9328.0 | 2.0.20871.0 | Linkado: o jogo pode compilar/patchar shaders em tempo de execução. Verificar na Etapa B. |
| XAUDIO2/XAPOBA/XMCORE | 2.0.9328.x | 2.0.20871.x | Fora do escopo do C1 (X1). |
| Kernel/XAM | 2.0.9328.0 (mín. 8955) | 2.0.20871.0 | Imports `Vd*` com os mesmos ordinais do kernel. |

O XDK 9328 é de 2009/2010, mesma época do lançamento. Entrada `0x8255CEA0`, base `0x82000000`.

## 2. Como as bases interceptam o gráfico

As duas bases aprovadas interceptam **funções D3D do XDK linkadas estaticamente no executável**, não pacotes PM4.

### Rayman Origins (ReXGlue, `rex/src/native_capture.cpp`, `native_renderer_gles2.cpp`, `render_thread.cpp`)

- Ganchos `REX_HOOK_RAW` em sete funções do D3D: `CreateVertexShader`/`CreatePixelShader` (hash XXH3 do contêiner do shader, chave do cache), `DrawIndexed`, `Draw`, `DrawVerticesUP`, `Resolve` e `Clear`. `Present` (único caminho para `VdSwap`) fecha o quadro.
- Em cada desenho, o renderizador lê o **espelho dos registradores Xenos dentro da estrutura do device** (`device+0x480 … +0x3700`): fetch constants, constantes de VS/PS, blend, viewport, render target e shaders atuais. Copia vértices/índices (big-endian → little-endian) e decodifica texturas (`xenos_texture.h`: tiling 2D, DXT1/3/5, 8888, mips empacotados).
- O D3D original continua rodando por baixo e gera PM4 para o plugin `rexgpu-null` (ring buffer, fences, interrupções e vblank sem desenhar). O estudo do DMC3 mostrou que essa serialização PM4 custa CPU à toa.
- **Thread de render** (`render_thread.cpp`): o tick do jogo grava comandos (`Draw/Clear/Resolve`) com uma cópia do estado do device (`0x3280` bytes por comando) e dos vértices/índices num blob. No `Present` o blob troca de mãos (buffer duplo, espera só se o quadro anterior ainda não foi desenhado). A thread de render reproduz o quadro, faz o `SDL_GL_SwapWindow` e é a **única dona do contexto GL**: `Ensure()` aborta se outra thread tocar o contexto.
- GLES2: shaders do jogo → XenosRecomp (HLSL) → `hlsl_ubo.py` → DXC (SPIR-V) → `prepare_gles2.py` (SPIRV-Cross ESSL 100, samplers fixos). Correções no carregamento: Y invertido, `z*2-w`, varyings não usados cortados, `highp`→`mediump` no fragment quando o provedor não tem highp. Cache de program binary (`GL_OES_get_program_binary`) chaveado pelo texto do shader e pela identidade do driver.
- Texturas: assinatura por amostragem de 32 palavras, reenvio só quando muda; **mipmap em toda textura POT estática maior que 16 px, enviada uma vez**. Essa foi a causa do FPS baixo do Rayman: sem mipmap, a Mali lê texturas minificadas de forma esparsa e a banda de DRAM segura a CPU.
- Render targets: chave por `RB_SURFACE_INFO`/`RB_COLOR_INFO` (`+0x2880/+0x2884`). O alvo mais largo é o back buffer. `Resolve` copia um retângulo para uma textura registrada no endereço de destino. Layout de vértice escolhido pelo **stride** (estruturas da UbiArt), específico do jogo.

### DMC3 HD (XenonRecomp + runtime próprio, `t1/runtime/gpu.cpp`, `renderer_gl.cpp`)

- Substitui as funções públicas `D3DDevice_*` inteiras com `GUEST_OVERRIDE`: `SetPixelShader`, `SetVertexShader`, `SetStreamSource`, `SetIndices`, `SetTexture`, `DrawVertices`, `DrawIndexedVertices`, `DrawVerticesUP`, `ClearF`, `Resolve`, `Swap` e `D3D_WaitForGpuProgress`, além do upload de microcódigo (`sub_82480B20`, anulado). **Nenhum PM4 é gerado**: o ring buffer nunca é usado e os ticks pulados não custam D3D.
- Lê o mesmo espelho de registradores do device (`FetchConstants 0x480`, `VertexConstants 0x780`, `PixelConstants 0x1780`, `BoolConstants 0x2780`, `RB_*`/`PA_*` em `0x28DC–0x294C`, `IndexBuffer 0x320C`, `PixelShader 0x330C`, `VertexShader 0x3310`, declaração de vértice em `0x2FD0`).
- Liga os `vfetch` do VS à declaração de vértice atual, como o D3D faz no desenho (lista de vfetch no cabeçalho do shader, seção `+0x1C/+0x28`). Isso é **genérico do XDK** e substitui a escolha por stride do Rayman.
- Lição Mali paga: o VS `64C85B285EB3738C` usa constantes demais (acima de 304 vec4/limite de registradores) e o driver da Mali-450 o recusa. Abortou o jogo aos 229 s. Plano de cópia de constantes (`constant_copy_plan.h`): enviar só as faixas que o shader lê.

### Escolha para o NieR

Seguir o modelo **DMC3** (substituir as funções `D3DDevice_*` públicas e não gerar PM4). A thread de render, o cache de texturas com mipmap, o cache de program binary e o pipeline de shaders vêm do **Rayman**. Motivos:

1. A Mali-450 não tem CPU sobrando. Serializar PM4 para uma GPU nula (Rayman) desperdiça CPU em todo desenho.
2. Com o PM4 fora, a cadência x2/x3/x4 (ticks pulados) fica quase gratuita: o desenho pulado só retorna.
3. A ligação de vfetch pela declaração (DMC3) é genérica. A escolha por stride (Rayman) depende do motor do jogo.

O contrato com o X1 fica assim: o runtime do X1 expõe a janela SDL3, o mapa de memória guest e os pontos de override. O C1 implementa as funções `D3DDevice_*` substituídas e desenha GLES2 com troca de buffer na thread de render.

## 3. O que é genérico do XDK (reaproveitável) e o que é do jogo

| Peça | Genérico (Xenos/XDK) | Ressalva para o 9328 |
|---|---|---|
| Decodificação de textura (`xenos_texture.h`): tiling, DXT1/3/5, 8888, endian 8in16/8in32, fetch constant de 6 palavras | Sim, é formato da GPU | Nenhuma: a Xenos é a mesma |
| Leitura de fetch constants (texturas `+0x480+24t`, vértices `+0x480+8v`, stream `s` → `v = 95 − s`) | Formato Xenos, sim | **O deslocamento `0x480` no device é do XDK 20871/DMC3. Confirmar no 9328** |
| Registradores `RB_*`, `PA_*`, blend, viewport, depth/stencil (valores) | Sim | Os deslocamentos no device podem mudar no 9328 |
| Microcódigo de shader e contêiner do XDK (cabeçalho `0x102A11xx`, constantes literais, lista de vfetch) | Sim | Versão do contêiner pode ser outra. XenosRecomp aceita contêineres de várias versões; testar na Etapa C |
| XenosRecomp → HLSL → SPIR-V → ESSL 100 | Sim | — |
| Thread de render, cache de program binary, mipmaps, troca de FBO mínima | Sim (nosso código) | — |
| Layout de vértice por stride, constantes 16:9 do widescreen, hooks UbiArt | **Não**, específico do Rayman | No NieR, usar a declaração de vértice (modelo DMC3) |
| Tradução PS2→D3D, CLUT, vídeo XMV | **Não**, específico do DMC3 | O NieR usa Sofdec/CRI (`.sfd`), não XMV. Ver `docs/ASSETS.md` |

## 4. Assinaturas para localizar o D3D do NieR (Etapa B)

Bytes exatos do 20871 não servem para o 9328. A busca usa **âncoras estruturais**, em ordem de confiança:

1. **Imports `Vd*` do xboxkrnl** (ordinais fixos do kernel): `VdSwap`, `VdInitializeRingBuffer`, `VdInitializeEngines`, `VdSetGraphicsInterruptCallback`, `VdGetCurrentDisplayInformation`, `VdQueryVideoMode`, `VdRetrainEDRAM`, `VdEnableRingBufferRPtrWriteBack`, `VdSetSystemCommandBufferGpuIdentifierAddress`, `VdPersistDisplay`, `VdShutdownEngines`. As funções que os chamam delimitam o bloco do D3D (no DMC3, 881 funções em `0x82470000–0x82490000`).
   - Único caminho até `VdSwap` → **Present/Swap**.
   - Chamador de `VdInitializeEngines` + `VdSetGraphicsInterruptCallback` → **inicialização do device**. O ponteiro global do device sai do `stw` do resultado.
2. **Cabeçalhos PM4 tipo 3 montados por `lis/ori` ou `li`**, `0xC000_0000 | (n << 16) | (op << 8)`:
   - `DRAW_INDX` (op `0x22`) e `DRAW_INDX_2` (op `0x36`): funções de desenho. As públicas são chamadas de fora do bloco D3D; as internas (Clear/Resolve) não.
   - `IM_LOAD` (`0x27`) / `IM_LOAD_IMMEDIATE` (`0x2B`): upload de shader antes do desenho.
   - `LOAD_ALU_CONSTANT` (`0x2F`), `SET_CONSTANT` (`0x2D`): constantes.
   - `EVENT_WRITE*` (`0x46/0x58/0x59`), `WAIT_REG_MEM` (`0x3C`), `INTERRUPT` (`0x54`): fences, Resolve e Swap.
3. **Acessos ao device por deslocamento imediato** (`lwz/stw rX, off(rDevice)`), um conjunto de deslocamentos por função:
   - Set*Shader: grava o ponteiro do shader em `+0x330C` (PS) ou `+0x3310` (VS) e lê o contador de fence (`+0x2B08` no 20871).
   - SetIndices: grava `+0x320C`. SetStreamSource: escreve no bloco de fetch `+0x480 + 8·(95−s)` com 4 argumentos (stream, buffer, offset, stride).
   - Set*ShaderConstantF: copia N vec4 para `device + (start+120)·16` (VS) ou o banco de PS.
   - Setters de estado: `OR` de um bit nas palavras de dirty de `+0x10`. A tabela de setters fica em `+0x40` (0x65 entradas) e a de samplers em `+0x1D4`.
   - Se os deslocamentos do 9328 forem outros, achar Present e DrawIndexed pelas âncoras 1 e 2 e ler neles o layout real.
4. **Strings e asserts do D3D** (builds de varejo do XDK costumam manter poucas): nomes de formato, `D3D`, `Xbox 360 D3D`. Usar só como reforço.
5. **Fluxo de chamada**: cada desenho público chama o upload de shader (`IM_LOAD`) e o upload de constantes (`LOAD_ALU_CONSTANT`). Funções com 3 a 5 argumentos e o padrão "upload shader → upload constantes → `DRAW_INDX`" são `DrawIndexedVertices`/`DrawVertices`. A versão com ponteiro de dados e stride é `DrawVerticesUP`.
6. **Criação de recursos**: `CreateVertexShader` copia o microcódigo e acrescenta a área de vfetch (872 bytes no 20871). `CreateTexture`/`CreateVertexBuffer`/`CreateIndexBuffer` chamam o alocador físico (`XPhysicalAlloc`/`MmAllocatePhysicalMemoryEx`) e preenchem fetch constants (formato na palavra 1, dimensões na palavra 2).

Cada endereço do `docs/D3D_MAP.md` do NieR vai registrar a âncora usada e a confiança: **alta** (âncora 1 ou 2 mais o layout conferido), **média** (âncora 3 ou 5) ou **inferida**.

## 5. Funções D3D a localizar (lista da Etapa B)

`Present/Swap`, inicialização do device, ring buffer, `D3D_WaitForGpuProgress`, `DrawVertices`, `DrawIndexedVertices`, `DrawVerticesUP`, `DrawIndexedVerticesUP`, `BeginVertices`/`EndVertices`, `ClearF`, `Resolve`, `SetRenderTarget`, `SetDepthStencilSurface`, `SetVertexShader`, `SetPixelShader`, `SetVertexDeclaration`, `SetStreamSource`, `SetIndices`, `SetTexture`, `SetSamplerState`, `SetRenderState`, `Set{Vertex,Pixel}ShaderConstantF` (e B/I), `SetViewport`, `SetScissorRect`, `CreateVertexShader`, `CreatePixelShader`, `CreateTexture`/`CreateCubeTexture`/`CreateVolumeTexture`, `CreateVertexBuffer`, `CreateIndexBuffer`, `CreateRenderTarget`, `CreateDepthStencilSurface`, `Lock`/`Unlock`, `Release`, `BeginScene`/`EndScene`, `SetPredication`/`BeginTiling`/`EndTiling` (o tiling da EDRAM tem predicação; o 9328 já o tem), `XGTexture*` (layout/tiling).

## 6. Plano para a Mali-450 (decisões já tomadas)

- Contexto, desenho e troca de buffer na **mesma thread de render**, com o blob de comandos do Rayman.
- Resolução interna ≥ 640 de largura (alvo 640x360 ou 854x480, escalado para 720p sem mexer no modo de vídeo do aparelho).
- Mipmap em toda textura estática POT; reaproveitar os mips originais quando o fetch constant indicar mips empacotados.
- Evitar troca de FBO no meio do quadro (cerca de 30 ms na Mali). Agrupar passes por alvo e pular `Resolve` cujo destino ninguém amostra.
- Shaders convertidos no PC com cache por hash. Cada VS medido contra 304 vec4 e o limite de instruções. Os que estourarem recebem plano de cópia de constantes (DMC3) ou versão reduzida documentada.
- Sem PM4: ticks pulados retornam antes de qualquer trabalho de D3D.
- Tiling da EDRAM (predicação/`BeginTiling`): desenhar uma vez para o alvo inteiro e ignorar os ladrilhos repetidos (cada ladrilho repetiria os mesmos desenhos).

## 7. Esqueleto do renderizador (Etapa E)

`port/render/` é código próprio (MIT). O renderizador do Rayman é GPL-3 e **não foi copiado**: a arquitetura é a mesma, a implementação é nova. As funções de endereço tiled seguem a Xenia (BSD-3, com atribuição em `xenos_texture.h`).

| Arquivo | Conteúdo |
|---|---|
| `nier_d3d.h` | Endereços e campos do device do 9328 (de `docs/D3D_MAP.md`) |
| `renderer.h/.cpp` | Fila de quadro (estado do device + índices + vértices do stream 0 copiados por comando), buffer duplo, **thread de render dona do contexto** (contexto, desenho e troca de buffer na mesma thread), alvo interno ≥ 640 (padrão 854x480), apresentação letterbox 16:9, cache de texturas com assinatura e **mipmap em POT estática**, estados de blend/depth/cull/viewport lidos dos registradores, Resolve para textura, captura PPM opcional (`NIER_GLES2_CAPTURE`) |
| `xenos_texture.h` | Fetch constant, tiling 2D, troca de bytes, DXT1/3/5, 8888, 565, 4444, 8 → RGBA8 |
| `d3d_hooks.cpp` | `REX_HOOK_RAW` em DrawVertices `0x82571508`, DrawIndexedVertices `0x825718F8`, DrawVerticesUP `0x82571070`, Resolve `0x82585888`, Swap `0x8257AB38` e SetRenderState(94) `0x8256BB90`. Por padrão grava e ainda chama o D3D original. `bypassGuestPM4` liga o modelo DMC3 |
| `selftest.cpp` | Autoteste sem jogo: device sintético, Resolve com limpeza, Present |
| `CMakeLists.txt` | Lib OBJECT (necessária para os símbolos fracos `sub_…` serem substituídos) + autoteste |

**Provas (07/11, PC):**
- Host x86-64: `nier_render_selftest` (sha256 `317be184…`) rodou 240 quadros com SDL offscreen + EGL (`timeout 20`, sem jogo). Thread de render com GLES 3.2 NVIDIA, interno 854x480, captura 1280x720 com pixel central (255,0,0) = cor do Resolve com limpeza, GL sem erro.
- aarch64 NextOS (toolchain do firmware, `-mcpu=cortex-a53 -O3`): `nier_render_selftest` sha256 `8e7c7cbc…` compila e linka. Não executado no aparelho (fica com o coordenador).
- `d3d_hooks.cpp` passa a checagem de sintaxe contra o SDK ReXGlue 0.10.0. Todos os seis símbolos existem no código gerado do X1 como `DEFINE_REX_FUNC` (alias fraco de `__imp__`).

**Integração pedida ao X1 (dono do `port/CMakeLists.txt` e de `nier_app.h`):** `add_subdirectory(render)`, `target_sources(nier PRIVATE $<TARGET_OBJECTS:nier_render>)`, `target_link_libraries(nier PRIVATE nier_render_deps)` e, em `OnPostSetup`, com `NIER_NATIVE_RENDER` definido, `nier::render::Init(config, {runtime()->virtual_membase(), runtime()->memory()->physical_membase()})`. SELECT+START continua no ciclo de vida do X1 (já provado).

**Pendências do desenho 3D, em ordem:**
1. Tradutor de shaders em execução. O VS depende da declaração de vértice, que no 9328 ainda não foi localizada (`SetVertexDeclaration`). O tradutor do DMC3 é privado: decidir entre dependência privada fixada (como em `tools/shaders/build.sh`) ou tradutor próprio MIT.
2. Layout do objeto shader do 9328 (ponteiro do microcódigo; no DMC3: `+0x18` PS / `+0x20` VS, cabeçalho em `+0x28`/`+0x368`), a confirmar por captura.
3. Fetch constant da textura destino do Resolve (`+0x18` no 20871), bit de limpeza do Resolve (`0x100`, a confirmar) e argumentos exatos do DrawVerticesUP.
4. Compactação de constantes e literais como `const` (docs/SHADERS.md).
5. Δt do jogo igual ao tempo real do quadro desenhado (regra do coordenador). O render state 94 é só registrado e a apresentação não espera vblanks do guest.

## 8. Etapa F — primeira imagem 3D (07/11)

**Estado medido.**
- **PC:** o jogo inteiro passa pelo GLES2. Logos, vídeo SFD, título, menus (texto correto), dificuldade, nome "Nier", NOW LOADING com modelo 3D animado, abertura do prólogo em 3D (cidade em ruínas com neve) e o tutorial de gameplay com HUD. Provas em `nier-build/c1/provas/` (privado): `f1-pc-criware.png`, `f1-pc-menu-dificuldade.png`, `f1-pc-3d-now-loading.png`, `f1-pc-3d-cidade-prologo.png`.
- **Mali-450 (.164):** shaders compilam e linkam (0 falhas nas telas cobertas), uniformes compactados dentro de 256 vec4. Sem captura: logos 10–20 ms/quadro, menus 20–35 ms, vídeo SFD 28–45 ms. Velocidade lógica 100,1% (Δt do jogo = tempo real). RSS 506–511 MB, 63→70 °C em 295 s. A cena 3D **ainda não foi medida na Mali**: o roteiro por quadro não chega ao prólogo no tempo autorizado.

**Correções do caminho (todas no código público, MIT).**

| Problema | Causa | Correção |
|---|---|---|
| Sprites com textura do vizinho | `0x82571070` é **BeginVertices**: o jogo escreve os vértices depois do retorno | Estado do device e shaders congelados no Begin; vértices copiados na próxima chamada D3D |
| Texto trocado | Swizzle da fetch constant ignorado | Canais na ordem da memória e swizzle da palavra 3, como a Xenia |
| Travamento no 1º 3D | SIGSEGV meu (faixa de vértices em `uint32`), tratado pelo runtime em laço | Faixa em 64 bits com sinal e tamanho real do elemento |
| Cena 3D invisível | Textura destino do Resolve lida em `+0x18` (20871); no 9328 é `+0x1C`. Endereço `0xE0000000+` = físico + `0x1000` | Offset `+0x1C` e chave física com `+0x1000` |
| Resolve de profundidade copiado como cor | Flag de profundidade é `4` | `flags & 4` ignora |
| 23 ms/quadro de Resolve na Mali | 4 cópias de 1280x720 por quadro, sem uso | Cópia só se o destino é amostrado no quadro, já foi amostrado, ou nas 3 primeiras vezes |
| Vídeo SFD lento na Mali | Planos k8 (x,x,x,1) lineares decodificados texel a texel em RGBA | `GL_LUMINANCE` com cópia de linha; hash amostrado nos planos reenviados |

**Tradutor (dependência privada).** Pino original `dmc3-hd-nextos@c76b074` (`xenos_shader.cpp` `d46a61fe…`). Patch NieR em `nier-data/deps/dmc3-xenos-c76b074-nier/` (`4af99b31…`, diff `nier-xenos-patch-c76b074.diff` `2b6e18e7…`; versão anterior `d707e60e…`/`11379d39…`):
- op. vetorial 18 (CUBE) sem projeção de face;
- fetch 3D/cúbico devolve cor neutra;
- `setTextureLod`/gradientes sem efeito;
- textura no VS devolve 0 (a Mali-450 não tem busca de textura no vértice);
- PS que exporta profundidade (oDepth) compila com a profundidade interpolada (GLES2 não tem `gl_FragDepth`); antes reprovava e o desenho era pulado.

Cada desvio é visível: reflexos e efeitos cúbicos ficam neutros. Build: `-DNIER_XENOS_TRANSLATOR_DIR=…-nier -DNIER_XENOS_TRANSLATOR_PATCHED=ON`.

**Como rodar (PC).**

```sh
NIER_RENDER_PBUFFER=1 SDL_VIDEO_DRIVER=offscreen \
NIER_INPUT_SCRIPT="300:start,800:start,1300:start,1900:start,2500:left:4,2515:a:4,…" \
tools/render/run_host.sh <build> <game> <saída> 290
```

O roteiro aceita `Ns:botão` (segundos desde o 1º quadro). Diagnósticos:
- `NIER_RENDER_TRACE_SCENE=N`: rastro do quadro N depois da 1ª cena 3D;
- `NIER_FLAT_3D`: desenhos 3D em vermelho;
- `NIER_GL_PROFILE`: tempos por fase com `glFinish`;
- `NIER_DUMP_TEXTURES`, `NIER_K8_CHECK`.

**Atenção:** o build do CMake recopia o `librexruntime.so` do SDK por cima do diretório do binário. A DLL do X1 precisa ser copiada de novo depois de cada build.

**Atualização 06:55.**
- **PC (`1c618bc`):** o prólogo 3D inteiro sai sem travar: cidade, "Summer 2049", corredores, sala e livro. Prova: `nier-build/c1/provas/f1-pc-prologo-3d-sequencia.png`. Causa do travamento intermitente na entrada do 3D: ponteiro pendurado para o estado copiado no blob (realocado pelos `Append`) → SIGSEGV repetido pelo tratador do runtime. As fetch constants agora são copiadas antes.
- Composição: a tela resolvida alimenta desenhos não-UP e também UP quando a origem recebeu 3D no quadro. Menus de UI pura voltaram limpos. A primeira tela de gameplay depois do prólogo ainda sai estourada em branco (pendência 1b).
- **Mali (.164), duas rodadas autorizadas de 295 s:**
  - F3: roteiro por quadro, ficou na dificuldade/nome.
  - F4: roteiro por relógio, parou no diálogo "You have not selected a gamer profile… Continue without signing in?", que só existe no aparelho (sem perfil conectado no runtime ARM).
  - Medido nos menus: 23–37 ms/quadro, lógica 100,09% (Δt real), RSS 387–511 MB, até 76 °C, 0 shaders falhando. A cena 3D na Mali segue sem medição.

- **F5 (07:15, DLL C5 do X1):** o perfil foi resolvido. A rodada parou em "No system data has been saved… Create a new file? Yes": a criação do save no ARM não conclui e a tela volta. Menus 25–37 ms/quadro, lógica 100,07%, RSS 386 MB, 63→76 °C.

**Pendências.**
0. (X1) Criação do save de sistema no aparelho; depois, mesmo roteiro (`roteiro-f4.txt`) para medir o 3D na Mali.
1b. Primeira tela de gameplay estourada em branco (composição/pós-processamento).
1. Faixas brancas nos menus desde que a composição passou a usar a tela resolvida (fundo do menu = tela anterior). O ClearF do 9328 ainda não foi localizado; a limpeza por quadro é heurística.
2. Texturas de profundidade amostradas (formato 22) saem brancas: Resolve de profundidade ainda não é implementado.
3. 2 PS escrevem profundidade (sem `gl_FragDepth` na Mali).
4. Texturas de cenário claras demais: conferir formatos e ETC1.
5. Medir a cena 3D na Mali com roteiro por tempo (`Ns:`, `nier-build/c1/roteiro-f4.txt`), depois que o X1 deixar o perfil conectado no aparelho como no PC.
