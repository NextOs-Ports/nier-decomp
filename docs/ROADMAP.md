# Rota até o gameplay

O [plano completo de decompilação e port](DECOMPILATION_PLAN.md) detalha ações, referências aprovadas, saídas e critérios de validação de cada etapa.

| Etapa | Entrega observável |
| --- | --- |
| Estrutura pública | Repositório, regras de medição, gráficos reproduzíveis e CI |
| Entrada recebida | Arquivo identificado por SHA-256 e integridade do 7z verificada; concluído em 07/11/2026 |
| Identidade e referência | Versão/hash do XEX e taxa original conferida externamente por movimento e combate |
| Inventário | Funções, limites, bibliotecas/imports, formatos de dados e caminho gráfico identificados |
| Execução ARM64 | Código compilado com runtime; carregamento e serviços usados funcionais |
| Primeira área em GLES2 | Cenário, personagem, câmera, interface e combate com imagem real |
| Funcionalidade | Áudio, controles, transições, saída e save/reabertura nas cenas verificadas |
| Desempenho | FPS desenhados, velocidade lógica, custo de CPU/GPU e memória medidos no gameplay |
| Cobertura ampliada | Novas áreas, inimigos, efeitos e sistemas com evidências e regressões dirigidas |

Comparar primeiro DMC3 HD de Xbox 360 e Rayman para selecionar a base compatível de tradutor/runtime e renderização. Reconstruir os sistemas específicos do NieR e verificar suas funções, preservando a velocidade original ao reduzir a frequência de desenho. A análise ainda não estabeleceu meta numérica de FPS ou prazo.

O marco de primeira área é uma etapa de desenvolvimento. O escopo de campanha, cenas e funcionalidades demonstradas deve permanecer explícito durante toda a evolução.
