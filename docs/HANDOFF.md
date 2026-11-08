# Estado para continuação — 07/11/2026 22:15 (fim da rodada da tarde/noite)

## Onde estamos
- **Imagem até a gameplay: FECHADA, PC ≈ Mali-450 (.164).** Prólogo com cor (sala nevada, prateleiras, destroços), Nier inteiro e texturizado, HUD, tutorial "Controls", tarjas de cutscene fiéis (preto 30% do próprio jogo). Prova: `nier-build/c1/provas/i3-pc-vs-164-gameplay.png`, `marco1-164-*.png`.
- **FPS na gameplay do .164: 3-7 quadros/s** (lógica a 100% do tempo real; o jogo usa Δt real). Frameskip existe (`NIER_FRAMESKIP=auto|2|3|4`), padrão desligado: no NieR mais updates não aumentam o desenhado.
- **Decomp:** 3216 funções verificadas e ligadas no jogo (≈3,16%), incluindo as quentes da gameplay (partículas 82144588, 82154D88, 821569E8, cache/estado de shader, Draw indexado/não indexado pela ponte de ganchos). D1-C6 (82578568, BeginVertices 82571070) em prova no fim da rodada.

## Correções de imagem desta rodada (causas reais)
1. Quadrado num quarto da tela: Resolve ignorava o nível de mip (r8) → nível próprio (7cb5f63).
2. Corpo do personagem sumindo: tradutor aplicava o bit de negação só à constante nas ops escalares (clip(alpha-0.5) virava kill total) — corrigido no tradutor privado compartilhado (sha 12d5d8a3).
3. Gameplay branca: o jogo lê pela CPU o mip 8 (2x2) da cadeia de luminância (825A3668, ponteiro FEC21000, bytes A,R,G,B, pixels 0/1/4/5) para a autoexposição; no port a memória ficava zero → PC0 ~1200. Correção: cópia de volta do Resolve pequeno para a memória do guest **no físico certo** (faixa virtual 0xE0000000+ tem +0x1000, PhysicalOf). C1 42850fc. Origem achada pelo D1 (messages/D1-exposicao-origem.json) e GDB do X1 (X1-GDB-exposure.json).
4. Reduções liam sempre 512²: LOD constante por desenho (X1 81902e5).
5. Gamma Xenos das texturas (X1 36efd81); alpha preservado nos Resolve (9fe02aa); Resolve 1/2/4 px (3d17107); endereços de textura/VB com PhysicalOf (X1 215d237).
6. Textura half (fmt 29, até 325): HDR8 = um RGBA8 com sqrt + escala por textura (X1, fechando no fim da rodada).

## FPS: a régua é o BANCO DE REPLAY (não abrir o jogo para medir)
- `NIER_FRAME_DUMP=<dir> NIER_FRAME_DUMP_AT=<s>` grava um quadro; `nier_replay_bench frame.bin` reproduz em laço por modo (8 s/modo). Quadro da gameplay: `nier-build/c1/k1-dump/dump/frame.bin` (84 MB; no .164 vai em /tmp, o cartão tem ~77 MB livres).
- **.164 (BENCH1/BENCH2 em nier-team/messages):** base 176-183 ms | sem-cenário 23 | sem-personagens 74 | PS chapado 70 | tesoura 74 | texturas 1x1 142 | personagens: ~102 ms, dos quais ~75 PIXEL e **~56 só do discard** (alpha-test), ~27 vértice+skin+envio. O agrupamento do cenário (C1-J1) deu só 5-10 ms; LOD por distância não ajuda.
- **Próximo:** (1) C1: discard dos personagens (variante sem discard onde alpha=1; blend no cabelo/roupa como preset Mali); (2) X1: PS leves dos personagens e do cenário; (3) captura ~30 ms/update; (4) D1 segue a lista quente (messages/D1-quentes-gameplay.json).

## Infra
- Equipe por D-Bus no Konsole `org.kde.konsole-1687`: coord aba 3, X1 Codex aba 4, D1 Codex aba 5, C1 Claude aba 6 (`nier-team/bin/mandar.sh`, `avisar-coord.sh`, REGRAS-COMUNS.md).
- Aparelho: `nier-ab/run_ab.py` (NFS do jogo; descobre o IP do PC sozinho — o PC trocou .239→.218 no Wi-Fi). Corte duro 280 s do processo. ES mascarado.
- Regra do dono: imagem até a gameplay antes de FPS; aparelho só em marco visível ou no banco; decomp sem parar.

---

# Estado para continuação — 07/11/2026, fim da rodada de 6 horas

## Resumo

Em uma noite, o NieR de Xbox 360 saiu do arquivo compactado para **gameplay no PC com o nosso renderizador GLES2**, **execução no Mali-450 com imagem correta nos menus e lógica a 100% do tempo real**, e uma **decompilação verificada por equivalência** que começou do zero. Os números do painel estão em `docs/generated/STATUS.md`.

## Equipe e método

Três terminais coordenados por D-Bus no Konsole (regras, missões e entregas fora do Git, em `/mnt/ARQUIVOS/HOME-FELIPE/ports_compile/nier-team/`).

| Papel | Branch | Frente |
| --- | --- | --- |
| Coordenação (Claude) | `main` | integração, identidade, timing, inventário, comparador, decomp por templates, painel |
| X1 (Codex) | `work/x1` | PE decifrada, codegen ReXGlue, inventário, runtime ARM64, áudio, controles, saves, decomp de bibliotecas |
| C1 (Claude) | `work/c1` | renderizador GLES2 Mali-450, shaders, assets CRI, testes no .164 |

## Estado por frente

### Identidade e timing
- `default.xex` SHA-256 `f375d506…`, Title ID `535107E8`, XDK D3D 9328. Prova: `evidence/identity-20261107.md`.
- Xbox 360 original apresenta 30 quadros distintos/s; destravado roda a 60. O jogo tem vsync adaptativo (`0x825A6F48`) e o passo de tempo da lógica vem do relógio real (`mftb`). No .164 a lógica mediu **100,1%** do tempo real.

### Recompilação e runtime (X1)
- Codegen ReXGlue sem avisos; inventário completo de 22.441 entradas e 5.372.176 bytes, revisado byte a byte em 4 rodadas (`evidence/inventory-review-20261107.md`): auxiliares do CRT, 251 funções só de vtable, 135 thunks de ajuste de `this`, 18 stubs com endereço montado no código.
- Boot no PC e ARM64 no .164 (`evidence/runtime-arm64-164-20261107.md`). Thread de áudio: 47 s → 0,4 s de CPU por minuto. Música e efeitos provados no PC. Controles, SELECT+START e saves persistentes provados. Teclado do sistema devolve "Nier" quando o jogo não passa nome.

### Renderizador GLES2 (C1)
- Código próprio MIT em `port/render`; tradutor de shaders Xenos é dependência PRIVADA fixada (port do DMC3 de outro autor, sem licença) e nunca entra neste repositório.
- PC: logos, vídeo, título, menus, nome, NOW LOADING com modelo 3D, prólogo 3D (cidade sob neve) e **gameplay com HUD e tutorial**.
- PC: o jogo é desenhado até o prólogo 3D completo (cidade, "Summer 2049", sala, livro) e a primeira tela de gameplay com HUD.
- .164 (Mali-450), 5 rodadas com corte mecânico, todas encerradas limpas: todos os shaders compilam e linkam, 0 desenhos pulados, sem erro de GL. Logos 10–20 ms/quadro, menus 23–37 ms, vídeo 28–45 ms. **Lógica 100,0–100,1% do tempo real**. RAM 386–511 MB, até 76 °C.
- A cena 3D no aparelho ainda não foi alcançada. A primeira rodada parou por roteiro contado em quadros, a segunda pelo perfil não conectado com dois controles (corrigido: todos os controles no perfil 0, DLL ARM `f828329c…`). A última parou em "No system data has been saved… Create a new file? Yes", que volta à mesma tela: a criação do save não conclui no ARM. A primeira hipótese (save dentro do NFS só leitura) **não se confirmou**: a receita já gravava fora dele. O lançador agora fixa `/storage/roms/ports/nier-dev/user-data` e exige escrita real antes de abrir o jogo; no QEMU, `XamContentCreateEx`, criar, gravar, ler e reabrir em outro processo funcionam. **Resolvido em 07/11 de manhã:** era o roteiro de prova (`left` levava a "No"); com `tools/render/roteiro-3d.txt` o .164 criou o save e chegou à cena 3D do prólogo (~1,3 fps no 1º bloco 3D). Ver `evidence/save-e-3d-arm64-164-20261107.md`.
- Pendências visíveis: texturas de cenário brancas (formatos), shaders 3D restantes (CUBE/op.18, fetch 24, textura no VS).

### Decompilação verificada
- Comparador `tools/verify`: a reconstrução roda contra a tradução do original no mesmo estado aleatório (registradores da ABI, memória tocada, chamadas virtuais, NaN/infinitos, limite de tempo e tratamento de falhas). Determinístico; teste de mutação feito.
- Reconstruções à mão em `src/decomp/` (CRT, Lua 5.1, D3D, listas, pools, UI, estados) e **templates** cujas instâncias são localizadas pela estrutura das instruções e provadas uma a uma (`tools/decomp/gen_*_instances.py`).
- Só conta como verificada a função que passou sem nenhuma divergência e com ao menos 500 execuções válidas.

## Próximos passos

1. **Save no ARM**: aplicar a receita do X1 (`messages/X1-C6-C1-recipe.patch` na pasta da equipe), registrar o status real de cada chamada XContent durante a tela "Create a new file" no .164 e corrigir o que falhar; então chegar à cena 3D no .164 com o mesmo roteiro por relógio, medindo FPS e velocidade no prólogo e na gameplay. Depois corrigir texturas brancas e shaders 3D restantes (CUBE/op.18, fetch 24, textura no VS).
2. **Integrar `src/decomp` ao build do jogo** (`port/CMakeLists.txt`): hoje as reconstruções estão provadas, mas o ELF ainda roda as traduções. Fazer isso por grupos, com A/B no aparelho.
3. **Decomp**: seguir os grupos de templates restantes (`tools/decomp` lista ~380 grupos de mesma estrutura, ~280 KB) e os quentes do perfil de gameplay quando houver gameplay no aparelho.
4. **Espaço no .164**: `/storage/roms` tem ~1,4 GB livres e o jogo tem 6 GB; testes usaram NFS só leitura. Para entrega, reduzir dados (texturas convertidas) e decidir o aparelho/espaço.
5. **Funções fora da contagem**: inicializadores estáticos e cadeias de inicialização (templates `static_init`/`chain3_global`) e destrutores cujo corpo falha com objeto aleatório precisam de entradas estruturadas antes de contar.

## Limites

Nenhuma entrega/ZIP foi feita. Nada foi apagado do aparelho; todo teste usou trava, corte mecânico ≤ 300 s, ES parado e mascarado, e terminou limpo. Dados do jogo, código gerado e o tradutor privado ficam fora do Git.
