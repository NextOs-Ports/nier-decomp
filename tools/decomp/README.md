# tools/decomp — reconstrução em lote e ligação no jogo

## Geradores de instâncias (prova individual pelo `tools/verify`)

| Gerador | Saída | O que cobre |
| --- | --- | --- |
| `gen_pool_instances.py` | `src/decomp/game/pool_instances.cpp` | template de pool (`pools_template.h`) |
| `gen_lifecycle_instances.py` | `src/decomp/game/lifecycle_instances.cpp` | construtores/destrutores/encaminhadores (`lifecycle_template.h`) |
| `gen_pattern_instances.py` | `src/decomp/game/pattern_instances.cpp` | padrões do jogo e do XDK (`pattern_template.h`): alocação+init, FIR de 16 taps, fábrica de nós, bitsets, listas, sorteios de efeito/script etc. |
| `gen_micro_instances.py` | `src/decomp/game/micro_instances.cpp` | funções ≤ 24 bytes de código reto (acessores, constantes, `b alvo`, `bctr`), instrução a instrução em tipos nativos |

Todos recebem `<default.pe privado>`, localizam as instâncias pela **estrutura das instruções**
(opcode e registradores, sem os imediatos) comparada a uma referência, extraem os imediatos e
emitem `NIER_DECOMP(sub_X) { template(ctx, base, ...); }` + `NIER_VERIFY_SPEC`. Alvos de
código vão como símbolo (`sub_Y`, chamada direta como o `bl`), nunca como endereço.

Para achar famílias novas: agrupar o inventário por (tamanho, forma) sem as já reconstruídas
e olhar os grupos com mais bytes; `show.py sub_X` mostra o PowerPC e quem chama.

## Gate de cada instância

`nierverify` (ver `tools/verify/README.md`): 0 divergências em 2.000 tentativas, ≥ 500 válidas
e tempo da reconstrução ≤ tempo da tradução (5 % + 3 ns de tolerância do relógio).
Instância reprovada não entra no jogo; o gerador pode mantê-la no arquivo (fica inerte).

## Ligação no jogo (D1-B)

`port/CMakeLists.txt`, opção `NIER_DECOMP_OVERRIDES` (ON):

1. `gen_overrides_header.py functions.json nier_decomp_overrides.h [--run rodada.json] [--only lista] [--exclude lista]`
   gera `#define NIER_OVERRIDE_sub_X sub_X` para cada função verificada (e um `.txt` com a lista).
2. `include/nier/decomp.h`: `NIER_DECOMP(sub_X)` só define o símbolo forte `sub_X` quando esse
   `#define` existe; o código recompilado é alias fraco de `__imp__sub_X`, então o nativo vence
   no link e na tabela `PPCFuncMappings` (que usa `sub_X`).
3. Pós-build: `check_overrides.py <elf> <lista.txt> --nm llvm-nm --json ...` exige `sub_X`
   forte em endereço diferente de `__imp__sub_X` para cada nome; falha = build vermelho.

A última rodada fornecida revoga qualquer reprovação, inclusive menos de 500 execuções
válidas. Os símbolos `REX_HOOK_RAW` de `port/render/d3d_hooks.cpp` são reservados para o
renderer: o nome forte `sub_X` continua no hook, e a reconstrução verificada recebe o
nome `nier_native_sub_X`. O hook chama esse backend quando presente; com overrides
desligados, chama a tradução original. Assim captura/estado e bypass são preservados.
O gerador grava `.backends.txt`; `check_overrides.py` também exige esses backends fortes
no ELF. A4 tinha 3.214 overrides diretos e o hook `sub_8256BB90`; a ponte também liga
a reconstrução dessa função sem duplicar o símbolo do hook.

Bisseção se o jogo quebrar: `-DNIER_DECOMP_OVERRIDES_LIST=<arquivo com sub_X por linha>` ou
`-DNIER_DECOMP_EXCLUDE=<arquivo>`; `-DNIER_DECOMP_RUNS=a.json;b.json` soma rodadas ainda
não integradas em `progress/functions.json`.

Prova no PC: `prove_overrides_host.sh BUILD_REF BUILD_CAND GAME OUT [s]` roda referência e
candidato com `tools/render/roteiro-3d.txt`, captura PCM (interposer do X1) e quadros GLES2,
e resume em `OUT/resumo.json`.

## Caminho D3D nativo (D1-C2)

- `texture_state.cpp`: `8256F910` combina descritores de textura, limites de LOD,
  flags de estado e a fila de referências. O comparador cobre textura presente/nula
  e fila pendente; o alocador de fila continua sendo uma chamada guest existente.
- `constant_runs.cpp`: `82578080` e `82578418` emitem intervalos contíguos de um
  bitset de 64 bits. Copiam palavras/blocos já big-endian, sem conversão FP ou
  registradores vetoriais do guest. A versão de blocos preserva alinhamento,
  padding e leitura dos quatro vetores antes da escrita.
- Provas adicionais: máscaras cheias, quatro alinhamentos da fila e reserva do
  emissor de blocos; gate estrito com `NIER_TIME_SLACK=1.0 NIER_TIME_ABS=0`.
  Resultados não medem ganho no Cortex-A53; a medição física pertence à coordenação.

## Limites conhecidos

- Funções cujo caminho principal depende do que uma chamada indireta ESCREVE na memória
  (ex.: `sub_824FF5C0`, que recebe a lista de buffers por callback) não são cobertas pelo
  comparador: o destino falso não escreve. Ficam para hook no runtime ou extensão do comparador.
- Chamadas ao kernel (eventos, esperas: `sub_824B5F80`) e à inicialização real do CRT
  (`static_init`) faltam/abortam no comparador: ambas as execuções falham e a função não conta.
- Sinal/carga de NaN: só com `nan=payload` na especificação (DSP), nunca por padrão.

## Partículas e cache (D1-C3)

`particle_update.cpp` reconstrói `82144588` com vetores locais do host
(`particle_math.h`), integração, curvas, eventos, colisão e chamada virtual final.
O comparador força o ramo de física em todas as 2.000 entradas; r3/r4 são os dois
argumentos inteiros do callback, além de dt em f1. A seleção de payload quando
ambos os operandos são NaN é preservada sem habilitar tolerância de NaN.
A fixture principal desliga eventos e colisão externos; esses caminhos mantêm
as chamadas guest e exigem a prova no jogo, não são certificados isoladamente
pela fixture principal.

`shader_cache.cpp` reconstrói `82579100`, preservando a reserva atômica/global
lock e as sentinelas de versão. O caminho de atualização de descritores continua
chamando a rotina guest existente.
