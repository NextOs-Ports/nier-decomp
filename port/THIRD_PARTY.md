# Componentes reutilizados

- Esqueleto e helper CMake: gerador ReXGlue SDK 0.10.0, BSD-3-Clause.
- `src/diagnostic_null_gpu.cpp` e receita `cmake/aarch64.cmake`: derivados de
  NextOs-Ports/rayman-origins-compact, commit
  80837358e9577d755297831e5301eb70ef82593c, GPL-3.0. Texto integral em
  `../tools/recomp/jumptables/LICENSE`. A licença MIT da raiz não os relicencia.
- SDK e fontes externas não são vendorizados. Preservar as licenças de suas
  dependências ao distribuir binários. Código guest gerado permanece privado.
