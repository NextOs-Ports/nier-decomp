# Detector simulado

Adaptado de `NextOs-Ports/rayman-origins-compact`, commit
`80837358e9577d755297831e5301eb70ef82593c`,
`tools/jumptables/rayman_jumptables.cpp`, sob GPL-3.0 (LICENSE nesta pasta).
A licença MIT da raiz não substitui a licença deste componente.

Os adaptadores locais leem a PE como imagem de memória e usam o disassembler
externo do SDK. Definir `REXGLUE_SOURCE` no CMake de `tools/recomp`.
Uso: `nier_jumptables memory.pe switches.toml functions.toml rex-hints.toml`.
Outputs são privados. O detector é heurístico: candidatos por ponteiro e
endereços não resolvidos exigem revisão; não importar todos automaticamente.
Foi adicionada uma guarda para operandos que não são registradores GPR.
