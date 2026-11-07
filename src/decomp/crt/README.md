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
