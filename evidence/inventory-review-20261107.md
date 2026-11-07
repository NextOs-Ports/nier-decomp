# Revisão dos limites do inventário — 07/11/2026

Ferramenta: `tools/inventory/review_boundaries.py` sobre a imagem PE `ff218ae3…` e o inventário de 22.165 entradas.

## Sobreposições (257)

| Grupo | Quantidade | Veredito |
| --- | ---: | --- |
| Cadeia de auxiliares save/restore do compilador, `0x825D17A0`–`0x825D27BC` | 156 + 72 | Entradas encadeadas por desenho (`__savegprlr`/`__restgprlr`/VMX). O corte em pedaços de 4–8 bytes conta cada byte uma vez. Biblioteca CRT. |
| Caudas compartilhadas com `bl` direto para a entrada seguinte | 29 | Entradas legítimas; corte mantido. |

Todas as 257 instruções foram conferidas por padrão de instrução (std/ld/stw/lwz em r1/r12, li, stvx/lvx, mtlr, blr) ou por referência direta.

## Lacunas do `.text` fora do inventário

| Tipo | Bytes | Destino |
| --- | ---: | --- |
| Enchimento (zeros, nop, trap) | 43.896 | Fora do denominador |
| Tabela de thunks de imports em `0x82625184` | 3.196 | Fora do denominador (já contada como 180 imports) |
| Tabelas de endereços embutidas | 696 | Dados, fora do denominador |
| Código não traduzido | 7.076 | 251 funções, 105 delas referenciadas por ponteiro em dados (vtables, thunks de chamada virtual, funções vazias) |

## Consequência

O inventário só pode ser declarado completo depois que essas 251 funções entrarem no codegen e no inventário. Isso também evita falha no runtime em chamadas virtuais para código não traduzido. A lista foi entregue à frente de recompilação.

## Segunda rodada (após incluir as 251 funções)

Inventário reexportado: 22.411 entradas, 5.372.176 bytes, zero avisos no codegen. Na correção, 5 entradas viraram casos internos de `switch` (`bdzf` em `0x82269DA8` e `0x8235F388`).

Mapa byte a byte das faixas de código:

| Faixa | Coberta pelo inventário | Fora do inventário | Enchimento | Outros |
| --- | ---: | ---: | ---: | ---: |
| `.text` (5.397.700 B) | 5.352.788 | 44.912 | 44.352 | 560 |
| `PSFD00` (19.464 B) | 19.388 | 76 | 76 | 0 |

Nenhum byte é contado duas vezes. Os 560 bytes restantes são tabelas de endereços embutidas e os campos da tabela de thunks de imports, todos dados. Não há código fora do inventário.

**Inventário declarado completo** para o `default.xex` SHA-256 `f375d506…`. O denominador da porcentagem passa a ser 5.372.176 bytes.
