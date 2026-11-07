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
renderer: não viram overrides de decomp, mesmo quando verificados. Assim a captura e o
estado do render são preservados sem símbolos fortes duplicados. A4 tem 3.215 funções
aprovadas e 3.214 overrides ativos; `sub_8256BB90` continua no hook do render.

Bisseção se o jogo quebrar: `-DNIER_DECOMP_OVERRIDES_LIST=<arquivo com sub_X por linha>` ou
`-DNIER_DECOMP_EXCLUDE=<arquivo>`; `-DNIER_DECOMP_RUNS=a.json;b.json` soma rodadas ainda
não integradas em `progress/functions.json`.

Prova no PC: `prove_overrides_host.sh BUILD_REF BUILD_CAND GAME OUT [s]` roda referência e
candidato com `tools/render/roteiro-3d.txt`, captura PCM (interposer do X1) e quadros GLES2,
e resume em `OUT/resumo.json`.

## Limites conhecidos

- Funções cujo caminho principal depende do que uma chamada indireta ESCREVE na memória
  (ex.: `sub_824FF5C0`, que recebe a lista de buffers por callback) não são cobertas pelo
  comparador: o destino falso não escreve. Ficam para hook no runtime ou extensão do comparador.
- Chamadas ao kernel (eventos, esperas: `sub_824B5F80`) e à inicialização real do CRT
  (`static_init`) faltam/abortam no comparador: ambas as execuções falham e a função não conta.
- Sinal/carga de NaN: só com `nan=payload` na especificação (DSP), nunca por padrão.
