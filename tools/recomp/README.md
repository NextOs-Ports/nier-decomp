# Preparação privada do executável

Compilar com CMake e `CMAKE_PREFIX_PATH` apontando para o SDK ReXGlue 0.10.0
host. `nier_dump_pe input.xex output.pe` usa o loader externo em modo de
ferramenta, sem executar instruções guest. Exporta antes de `LoadContinue`,
portanto antes de qualquer patch de imports. Nenhuma chave é incorporada aqui.

`describe_xex.py input.xex output.pe xex-info.json --key-family retail` descreve
seções, flags, endereço base, entrada, imports/ordinais, compressão e hashes.
A família da chave deve corresponder ao resultado impresso pelo loader.
A PE é uma imagem de memória: offset de arquivo = RVA, não PointerToRawData.
Todos os outputs e builds devem permanecer em diretórios privados fora do Git.

No executável base do NieR, foram conferidos MZ/PE, igualdade das entradas
XEX/PE e registros de imports antes de patches. A imagem não é um executável
host e não constitui prova de boot ou decompilação verificada.
