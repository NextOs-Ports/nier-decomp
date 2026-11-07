# Como a porcentagem é calculada

## Base e denominador

`progress/functions.json` identifica uma versão exata por SHA-256 do XEX, faixas de código e inventário de funções com endereço e tamanho original. O denominador é a soma dos bytes de todas as funções desse inventário. Os intervalos não podem se sobrepor e devem estar contidos nas faixas de código declaradas. Padding e tabelas de dados não são automaticamente funções.

Uma análise parcial pode registrar funções, mas não produz porcentagem global: `inventory_complete` deve ser `true`, com SHA-256, faixas e uma evidência de revisão, para liberar a medição. O termo "completo" é uma declaração revisada do inventário, não uma garantia automática do analisador. Mudanças de inventário alteram a assinatura da base e devem ser explicadas no changelog.

## Estágios das funções

| Estágio | Significado | Conta como decompilação verificada? |
| --- | --- | --- |
| `discovered` | Função identificada, sem fonte reconstruída | Não |
| `recompiled` | Tradução estática disponível | Não |
| `decompiled` | Fonte reconstruída e identificada; validação pendente | Não |
| `verified` | Fonte reconstruída com evidência de equivalência dentro do escopo descrito | Sim |

O painel mostra três coberturas cumulativas: tradução ou reconstrução (`recompiled` e posteriores), fonte reconstruída (`decompiled` e `verified`) e decompilação verificada (`verified`). O indicador principal é o último:

```text
100 × bytes originais das funções verified / bytes originais do inventário completo
```

Também são publicados os números de funções. Usar bytes evita dar o mesmo peso a uma função de oito bytes e uma função de oito mil bytes. O tracker exige arquivo de fonte e evidência existentes; a revisão humana/técnica continua responsável pela validade da evidência. Não se exige matching byte a byte para o port, e o método usado deve ser descrito.

## Marcos e desempenho

Os marcos do port são um checklist separado, sem virar "porcentagem do jogo". Um runtime que abre não comprova gameplay. Uma função verificada não comprova uma campanha completa. FPS e velocidade só entram com medição identificada por versão, cena e aparelho.

Antes do inventário completo, os valores são `null` no JSON e "não medida" no painel. A curva histórica mostra lacunas para períodos sem medição e separa bases diferentes. Ela não cria uma linha artificial de 0%.
