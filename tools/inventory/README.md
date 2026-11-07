# Inventário privado para integração

`export_functions.py --pe memory.pe --info xex-info.json --generated directory
--hints nier_hints.toml --output functions.json` cruza o hash da PE, `.pdata`,
mapa de funções gerado e endereços das instruções/labels emitidos pelo ReXGlue.
O resultado usa o esquema do tracker, sem copiar código ou bytes comerciais.

A saída mantém `inventory_complete=false`: não declara denominador global
antes da revisão dos intervalos, áreas descobertas e entradas compartilhadas.
`size` atribui bytes exclusivos por entrada, cortando no próximo início para
não contar duas vezes helpers com múltiplas entradas. O tamanho anterior ao
corte permanece em `original_extent_before_partition`; toda divisão aparece
em `analysis.boundary_issues`. Extensões inferidas são identificadas pela origem.

Reconhece imports XDK, vídeo/D3D e XAudio pelos nomes/ordinais do loader e
helpers CRT por assinatura nomeada pelo SDK. Outras bibliotecas, inclusive CRI,
continuam sem classificação quando não há prova suficiente. Recompilação não
é decompilação: nenhuma entrada é promovida a `decompiled` ou `verified`.

Validação realizada sobre o inventário real com `tools/progress.py:calculate`:
faixas executáveis, ausência de sobreposição, tamanhos positivos, identidade
SHA e estágios. O arquivo vai à caixa de mensagens do coordenador; este script
não altera `progress/` nem regenera o painel compartilhado.

## Perfil quente

`export_hot_profile.py` lê `perf script -F comm,pid,tid,time,period,event,ip,sym,dso`
obtido de `perf record -e cpu-clock:u -F 199 --call-graph dwarf,16384`.
Recebe `--stacks`, `--inventory`, `--pid` e `--output`. O período da amostra é
usado como peso em nanossegundos de CPU. A primeira frame determina tempo
próprio; funções guest distintas na pilha recebem tempo inclusivo, sem dupla
contagem de recursão/aliases dentro da mesma amostra. O denominador global é
toda CPU user amostrada do PID, incluindo runtime; cada thread tem seu próprio
denominador. Tempo inclusivo não deve ser somado entre funções.

O ranking contém no máximo 300 funções realmente observadas por lista,
ordenadas por tempo próprio e depois inclusivo. Funções não amostradas não
são preenchidas com tempos inventados. O perfil de boot null no PC serve
para priorização, não para declarar custo do gameplay ou da Mali.

`scan_this_thunks.py` varre todas as seções executáveis da PE por
`addi r3,r3,simm16; b alvo` e cruza ponteiros alinhados de todas as seções de
dados com o registro gerado. Na revisão X1-2c foram encontrados 328 padrões,
135 referenciados em dados e 12 entradas ausentes. Quatro limites antigos
agrupavam thunks adjacentes; após separá-los em unidades de oito bytes, os
135 estão registrados. O scanner fixa a identidade da PE e não publica bytes.
