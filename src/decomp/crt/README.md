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
