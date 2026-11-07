# Reconstruções do CRT

As rotinas abaixo foram identificadas pela semântica dos argumentos, acessos e
retorno da PE fixada do NieR. Os nomes descrevem contratos CRT reconhecidos;
não são símbolos recuperados de um PDB. O código é uma reconstrução dos
algoritmos, não uma cópia da saída automática do tradutor/Ghidra.

| Endereço | Contrato reconstruído | Bytes originais | Domínio da prova |
| --- | --- | ---: | --- |
| `825D3010` | memcpy | 1160 | RAM comum não sobreposta, 0–2048 bytes, alinhamentos variados |
| `825D3498` | variante escalar de memcpy | 168 | mesmo domínio |
| `825D3D90` | memset | 160 | RAM comum, inteiro convertido ao byte baixo, 0–2048 bytes |
| `825D3620` | comprimento UTF-16 (wcslen) | 36 | strings terminadas em zero, unidades BE de 16 bits |
| `825D3648` | concatenação UTF-16 (wcscat) | 52 | regiões separadas, conteúdo aleatório terminado em zero |
| `825D3680` | cópia UTF-16 (wcscpy) | 28 | mesmo domínio, incluindo terminador |

Lote X1-1: 2.000 comparações por função, todas iguais e sem falhas de acesso.
Compara retorno/registradores ABI, dados, TLS, pilha observável e páginas
escritas da imagem. Não certifica MMIO, chamadas com overlap proibido pelo
contrato memcpy, buffers inválidos ou strings sem terminador. Na cópia e no
preenchimento, o r3 completo original é preservado como ponteiro de retorno.
O wchar do Xbox tem 16 bits; não usamos o `wchar_t` de 32 bits do host Linux.

O comparador aproveitou 96 objetos de oráculo com comandos de compilação
idênticos (Clang 23.1.4, mesmo gerado/SDK/flags); só o executável de verificação
e as reconstruções foram recompilados. O recibo do cache é privado. O
coordenador integra os JSONs em `progress/`; este trabalho não edita o painel.

## Lote X1-2 — comparação e busca de strings

Mais 19 rotinas passaram com 2.000 comparações cada e nenhuma falha de acesso:
strlen/strcpy/strcat/strncat; strcmp/strncmp; strrchr/strstr; strcspn/strpbrk;
três comparadores ASCII sem locale; memchr/memcmp; wcschr/wcsstr/wcscmp/wcsncmp.
Os nomes são descrições semânticas, sem PDB. Preservamos diferenças observáveis:
`strcmp`/`wcscmp` retornam sinal normalizado, `strncmp`/`wcsncmp` devolvem a
subtração, e `memcmp` retorna +/-1 para grupos de oito bytes diferentes e a
subtração exata na cauda. Um comparador ASCII preserva o atalho de RHS terminado
antes da conversão para minúsculas. As buscas devolvem o ponteiro guest completo.

A nova área de teste do coordenador tem 512 KiB. Os ponteiros de origem do
lote 1 foram movidos para dentro dela (0x40050000–0x40060000), sem mudar os
algoritmos. As seis funções passaram novamente em 2.000 casos sem falhas.
Os testes do lote 2 usam somente RAM válida, strings terminadas, regiões
separadas para cópia/concatenação e limites explícitos de comparação.

## Lote X1-4 — helpers numéricos

Cinco helpers (336 bytes) passaram em 2.000 casos cada, incluindo os especiais
NaN/infinito adicionados ao comparador da main. A prova adicional executou
504 casos com quatro modos de arredondamento, zeros assinados, subnormais,
infinidades, NaNs com payload e limites inteiros.

- `825DAAF0`: uint64 para binary64 por construção de expoente/mantissa,
  descartando bits baixos; preserva também o padrão construído em r3.
- `825D91D8` / `825D9218`: contratos de ceil/floor, preservando a assinatura
  peculiar do zero sob arredondamento para baixo.
- `825E4B80`: arredondamento por adição/subtração ordenada de 2^52, respeitando
  o modo ativo; não foi trocado por `round` com semântica diferente.
- `825D93E0`: classifica valores int64 exatos como ímpares/pares, preservando
  o spill de argumento em SP+16. Nãointeiros/fora de faixa retornam zero.

O teste extra inicialmente encontrou oito diferenças de zero assinado; elas
foram corrigidas e a rodada final passou. A prova compara resultados e memória
ABI, não certifica todos os flags de exceção floating-point do hardware Xbox.

## Lote X1-7 — campos numéricos e cópia limitada

Seis rotinas passaram em 2.000 casos cada no comparador da main, sem falhas
ou divergências. `825D38F8` implementa strncpy com padding; `825DA3C8`
substitui bits selecionados da palavra global CRT e devolve o valor anterior.
As quatro rotinas em `numeric_bits.cpp` preservam o layout guest:

- `825E4B40`: copia o sinal de f2 para f1, incluindo payloads NaN e spills.
- `825DE390`: substitui o campo do expoente; argumentos fora da faixa têm
  o mesmo truncamento de 16 bits do original, inclusive efeito sobre o sinal.
- `825DE458`: separa mantissa/expoente, normaliza subnormais e preserva o
  spill modificado. NaN/infinito seguem a transformação dos bits original,
  não a semântica de `frexp` da biblioteca do host.
- `825E79C0`: binary64 BE para extended80 BE de dez bytes, sem depender
  do layout de `long double` do compilador.

A prova complementar `nier_numeric_bits_probe` passou 6.720 casos: quatro
modos de arredondamento, todos os 52 bits líderes possíveis de subnormais,
ambos os sinais, NaNs com payload, zeros, infinito e expoentes que transbordam.
Compara resultados, dados e spills observáveis; flags FP completos não foram
certificados. Cópias usam RAM válida e regiões separadas.

## Lote X1-8 — resto, classificação e conversões de texto

Oito funções passaram em 2.000 comparações cada, sem falhas: wcsncpy
(`825D3958`), conversão de um byte em unidade UTF-16 (`825E9678`),
classificação de infinito/NaN (`825DE3D0`), seleção de status FP
(`825E5270`), tradução de erros pela tabela guest (`825D4FC8`),
consulta ctype por locale (`825DCC78`), resto binary64 (`825D9258`)
e remoção de zeros decimais finais antes de e/E (`825DBF00`).

O resto usa significandos inteiros e devolve o NaN da tabela guest nos
mesmos casos do original. A consulta ctype usa a tabela original de locale,
inclusive EOF, sem assumir que toda classificação seja ASCII. O aparador
de zeros devolve o ponteiro à vírgula/ponto decimal ou ao terminador, como
a rotina original. Tabelas comerciais continuam somente na imagem privada.

A prova extra `nier_crt_contract_probe` passou 7.685 casos: produto dos
especiais IEEE em quatro modos FP, 4.000 pares aleatórios de binary64,
formatos decimais com expoente, buffers opcionais nulos, todos os 256 bytes
na classificação, entradas da tabela de erros e combinações de status.

## Lote X1-11 — extended96 para IEEE

`825E7B88` e `825E82A0` (3.624 bytes originais) compartilham a conversão do
formato BE de 12 bytes: cabeçalho sinal/expoente de 16 bits, significando de
80 bits com bit inteiro explícito. A reconstrução usa um acumulador inteiro
96-bit em `unsigned __int128`, lê as tabelas guest de parâmetros e grava
binary64/binary32. Preserva status 0/1/2, empate truncado, underflow,
overflow e comportamento de entradas não normalizadas; não usa cast do
`long double` do host como substituto.

Ambas passaram em 2.000 comparações aleatórias, sem falhas. A prova extra
passou 1.441.792 casos: todos os 32.768 expoentes, dois sinais, onze padrões
de significando (zeros, extremos, empates e valores logo acima) e duas larguras.
O teste usa as configurações originais da PE; não certifica tabelas corrompidas.

## Lote X1-12 — aproximações trigonométricas, exponencial e logaritmo

Seis kernels (1.512 bytes) preservam redução de intervalo, polinômios/racionais,
ordem das FMA, tabelas guest e casos especiais: seno `825D8E50`, cosseno
`825D8F30`, inversa selecionável `825D9010`, arco-seno `825D9108`, exponencial
`825D9AC0` e logaritmo `825D4568`. São os contratos das aproximações vinculadas
a esta imagem, não substituições pela libm do host. Preservamos também o spill
de argumento do logaritmo, incluindo sua atualização ao escalar subnormais.

Todas passaram em 2.000 comparações. `nier_math_approx_probe` passou 56.064
casos em quatro modos FP, limiares e vizinhos de cada ramo, padrões binary64
aleatórios, todos os bits individuais de payload NaN e ambos os sinais.
A primeira prova encontrou 64 diferenças no sinal de NaNs de seno/cosseno:
o compilador do host escolhia outro sinal nas operações intermediárias.
A reconstrução explicita o NaN quieto positivo, preservando o payload como
o original. A prova certifica resultados e memória ABI, não todos os flags FP.

## Lote X1-16 — captura de contexto

`825D5DE0` (716 bytes) grava o jump-buffer CRT: FPR14–31, GPR13–31, SP,
CR/LR e VMX128 v64–v127, preservando lacunas e stores vetoriais parciais para
buffers desalinhados. Mantém o callback opcional e seus argumentos; o retorno
normal é zero. As dependências de layout de PPCContext têm static_assert.

Passou em 2.000 comparações e em 8.192 casos extras com registradores
aleatórios, todos os 16 alinhamentos e um callback instrumentado. A prova
cobre captura/encaminhamento, não execução de um longjmp ou desenrolamento
de pilha. Os callbacks do teste são sintéticos e não substituem funções
comerciais para produzir equivalência artificial.
