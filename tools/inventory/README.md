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
