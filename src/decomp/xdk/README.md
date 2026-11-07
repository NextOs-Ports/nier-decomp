# Helpers XDK reconstruídos

Lote X1-3, oito rotinas: acesso ao last-error da thread e suas entradas curtas,
conversão de timeout em milissegundos para intervalo NT, strlen escalar, filtro
de código de exceção e troca atômica. Todos passaram em 2.000 comparações por
função, sem divergência e sem falhas de acesso.

Os offsets foram confrontados com o SDK: KPCR contém PRCB em +0x100,
PRCB.dpc_active fica em +0x50 e KTHREAD.last_error em +0x160. O setter não escreve
em contexto DPC e o getter devolve zero nesse contexto. O timeout UINT32_MAX
vira ponteiro nulo (infinito); os demais valores viram intervalos negativos de
100 ns. O filtro lê EXCEPTION_POINTERS/record e compara com STATUS_NO_MEMORY.
Os nomes descritivos desses helpers não são símbolos de PDB recuperados.

A troca de palavra big-endian usa uma operação atômica nativa, conserva o
protocolo global-lock/interrupção do SDK e retorna a palavra anterior com
extensão por zero. Além do comparador, oráculo e reconstrução passaram num
teste de quatro threads/8.000 trocas: cada token aparece uma vez entre retornos
e valor final. O teste não é uma prova formal de todas as interleavings.

Provas/JSONs ficam privados; o coordenador integra os resultados no painel.

## Lote X1-5 — PCM e formatos de áudio

Onze helpers da camada de áudio passaram em 2.000 casos cada, sem falhas.
A posição no caminho XAudio e a semântica dos formatos foram identificadas;
não atribuímos um nome de middleware/PDB não comprovado.

Os sete leitores cobrem PCM unsigned8, signed16 BE, 20 bits em contêiner
24 LE/32 BE, 24 bits em contêiner24 LE/32 BE e signed32 BE. A conversão para
float mantém o arredondamento intermediário e as palavras empacotadas que duas
rotinas deixam em r3. 560 casos suplementares passaram com quatro modos de
arredondamento, extremos/midpoints, bits descartados e ponteiros com word alto.

Duas conversões de cabeçalho preservam a ordem de acessos e os campos que o
original deixa zerados; não foram convertidas em construtores WAVEFORMATEX
completos. Duas rotinas ADPCM preservam o cálculo de samples/bloco, o resultado
de 16 bits, a contagem de blocos/padding e a convenção do oráculo para divisão
por zero. Os parâmetros do teste também incluem cabeçalhos inválidos para
conferir esses retornos, sem afirmar que sejam formatos reproduzíveis.
