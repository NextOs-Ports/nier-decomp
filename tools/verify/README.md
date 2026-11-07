# nierverify — prova das funções reconstruídas

Compara cada função reconstruída em `src/decomp/` com a função traduzida do executável original, no mesmo processo e a partir do mesmo estado inicial aleatório.

## Como funciona

- A memória do console (4 GB de endereço) é reservada no host. A imagem PE privada é carregada em `0x82000000` e a tabela de chamadas indiretas do ReXGlue é preenchida.
- O oráculo é `__imp__sub_X`, a tradução do código original. A reconstrução é compilada como `decomp_sub_X`.
- As duas chamam as mesmas funções traduzidas para tudo que não foi reconstruído, então qualquer diferença vem da reconstrução.
- A imagem fica protegida contra escrita; cada página escrita é registrada e comparada.
- Compara r1–r4, r13–r31, f1, f2, f14–f31, a memória de teste, a área do r13, a pilha acima do r1 de entrada e falhas de acesso.
- Aprovação: nenhuma divergência em todas as tentativas (padrão 2.000) e ao menos metade delas sem falha de acesso.
- Teste de mutação feito em 07/11: trocar uma comparação, um limite ou um deslocamento reprova a função.

## Escrever uma função

```cpp
#include <nier/decomp.h>
using namespace nier::guest;

// sub_823C6A50 (76 bytes) — explicação do que faz
NIER_DECOMP(sub_823C6A50) {
  ...
}
NIER_VERIFY_SPEC(sub_823C6A50, 0x823C6A50, "r3=ptr r4=range:0..64");
```

Especificação dos argumentos: `ptr[:alinhamento]`, `bptr`, `int`, `u8`, `u16`, `range:a..b`, `const:v`, `image:a..b`, `f[:min..max]`, `f32`. Registradores sem especificação recebem ponteiro válido ou inteiro pequeno.

No jogo, `NIER_DECOMP(sub_X)` define o símbolo forte `sub_X` e substitui o alias fraco gerado: a reconstrução roda no lugar da tradução.

## Rodar

```sh
cmake -S tools/verify -B <build> -G Ninja -DCMAKE_CXX_COMPILER=clang++ \
  -DNIER_GENERATED_DIR=<generated/default privado> -Drexglue_DIR=<sdk linux-amd64>/lib/cmake/rexglue
cmake --build <build>
NIER_PE=<default.pe privado> <build>/nierverify > run.json
python3 tools/verify/apply_results.py run.json
python3 tools/progress.py --snapshot && python3 tools/progress.py --check
```
