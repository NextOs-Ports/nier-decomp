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

## Lote X1-6 — filtro e estruturas de áudio

Nove rotinas passaram em 2.000 casos cada, sem falhas de acesso. O filtro de
variável de estado foi reconstruído por suas duas integrações (low/band),
coeficientes frequency/damping e saídas low/band/high/notch. A ordem dos FMAs e
os arredondamentos single intermediários foram conservados. Os dados de teste
usam amostras/estados finitos normalizados, coeficientes variados e 0–32 samples.

Também foram verificados: inicialização dos treze buckets (incluindo campos
que não devem ser zerados), lookup com chaves presentes nas duas posições de
cada bucket, retirada de payload com reciclagem de nó, seleção de modo pendente,
marcação de acesso, máscaras de canais XMA/extended/default e seleção das seis
entradas de conversão PCM. O lookup mantém a pré-condição de chave existente;
não inventa recuperação para a desreferência nula do original fora desse domínio.

210 casos extras passaram: fila vazia/um/dois nós e payload zero, tags de
formato/channels/flags, escolhas suportadas e rejeitadas, e filtro sob quatro
modos FP. A prova é de resultados/ABI/memória; não substitui audição no jogo ou
uma prova formal de estabilidade do filtro para coeficientes arbitrários.

## Lote X1-9 — validação de formatos e seleção de processamento

Quatro validadores (`824B6668`, `824B6738`, `824B6A00`, `824B6B30`) conferem
layouts de PCM/ADPCM/XMA: bits significativos, alinhamento, taxa, coeficientes,
streams, máscaras, blocos e limites de amostras. Preservam a aritmética guest
inclusive somas de 32 bits nos limites. Os coeficientes ADPCM vêm da PE.
Dois seletores (`824BBAF8`, `824BEBF0`) escolhem as funções guest existentes
por representação, canais e flags; não executam esses processadores na prova.

Todos passaram em 2.000 comparações. A prova complementar passou 81.285 casos:
quatro cabeçalhos válidos, substituição de cada um dos primeiros 64 bytes
por todos os 256 valores, 1.000 cabeçalhos aleatórios por layout e combinações
dos seletores. Cada validador teve milhares de aceitações e rejeições iguais
ao original. O número de streams aleatório é limitado a quatro para manter
os buffers válidos. A prova não certifica buffers truncados/malformados.

## Lote X1-10 — matrizes de canais e filtro estéreo

Seis kernels (`824B8498`, `824B9BF8`, `824B85A0`, `824B9D38`, `824B8778`,
`824B9F68`) convertem mono→estéreo, mono→seis canais e estéreo→seis canais,
com entrada float ou PCM16 BE, sobrescrita/acumulação e rampa de coeficientes.
O filtro estéreo `824BFB60` mantém os dois pares de estados e processa o par
final completo quando a contagem de amostras é ímpar, como o original.

As sete funções passaram em 2.000 casos cada, sem falhas. A prova adicional
passou 1.740 casos em quatro modos FP, com/sem rampa, acumulação/sobrescrita,
contagem zero/ímpar, extremos PCM, zeros assinados e subnormais. Mantemos a
ordem de FMA/arredondamento e os temporários f1 observados pelo comparador.
O domínio de amostras/coeficientes desta prova é finito.

Um microbenchmark detectou que estreitar a multiplicação para float pelo
compilador causava lentidão com subnormais. A multiplicação intermediária
agora permanece em double. Mediana de 5×10.000 chamadas, 128 amostras, CPU0
fixa do i7-13700F: reconstrução 1,15–2,27× mais rápida nos casos medidos.
Isso compara o oráculo do verificador em -O1 com a reconstrução em -O3;
não é ganho medido no jogo, nem prova de velocidade no Cortex-A53. Os fontes
C3 são comprovados isoladamente; o CMake atual do port ainda não os inclui.
