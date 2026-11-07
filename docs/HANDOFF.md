# Estado para continuação — 07/11/2026

## Equipe em andamento (desde 07/11 01:45)

Três terminais coordenados por D-Bus no Konsole. Regras, missões e entregas ficam fora do Git, em `/mnt/ARQUIVOS/HOME-FELIPE/ports_compile/nier-team/` (`REGRAS-COMUNS.md`, `messages/MISSAO-*.md`, `messages/ENTREGAS.log`).

| Papel | Branch | Frente |
| --- | --- | --- |
| Coordenação (Claude) | `main` | integração, identidade, timing, painel, aparelho |
| X1 (Codex) | `work/x1` | PE decifrada, codegen ReXGlue, inventário, runtime ARM64 |
| C1 (Claude) | `work/c1` | renderizador GLES2 Mali-450, shaders, assets CRI |

Base escolhida: pipeline ReXGlue 0.10.0 + renderizador GLES2 do Rayman Origins X360, com interceptação `D3DDevice_*` no estilo do DMC3, porque o XDK do NieR (9328) é antigo demais para as assinaturas do Rayman (20871).

## Concluído

- Identidade do disco e do executável e referência externa de timing: [identity-20261107.md](../evidence/identity-20261107.md).
- Imagem PE decifrada (privada) com SHA-256 `ff218ae3…`.

- Estrutura pública de engenharia reversa e acompanhamento do NieR original de Xbox 360.
- [Plano completo de decompilação e port](DECOMPILATION_PLAN.md), baseado nas versões aprovadas de DMC3 HD de Xbox 360, Rayman, Black e Genma Onimusha.
- Gerador de métricas e gráficos, histórico diário e validações do tracker.
- Arquivo do jogo recebido e teste integral do 7-Zip aprovado. Identidade e alcance em [input-20261107.md](../evidence/input-20261107.md). Esse marco não incrementa cobertura de código.

## Próxima ação

Iniciar o primeiro bloco descrito no plano: extrair o ISO recebido para a área privada de trabalho, localizar e identificar o `default.xex`, registrar a referência externa de timing e comparar os requisitos com DMC3/Rayman. Em seguida, fixar a base de runtime e iniciar o inventário. Manter dados comerciais em `private/` ou fora do repositório. Ainda não foi selecionado nem executado um analisador do NieR.

## Limites do estado atual

Não houve extração do ISO ou do XEX nesta etapa. Não há código de jogo recuperado, build ARM64 do NieR, renderizador integrado, teste físico ou desempenho medido. Nenhum projeto de referência foi alterado ou reaberto. A próxima sessão deve partir deste arquivo recebido, e não do parcial antigo de outra edição.
