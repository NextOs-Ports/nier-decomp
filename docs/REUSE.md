# Reaproveitamento dos nossos ports

Este projeto começa pelas soluções existentes. Nenhum código dos projetos de referência foi copiado nesta etapa. A integração exige identificar a versão da fonte, revisar licença e adaptar ao executável do NieR.

| Referência | Componente a aproveitar | Limite |
| --- | --- | --- |
| Rayman Origins de Xbox 360 | Rota PowerPC → C++ → ARM64, integração ReXGlue, serviços implementados, separação de render e lógica, organização do backend GLES2 | Referência principal de plataforma; hooks, endereços, estruturas e materiais são específicos do Rayman |
| Black | Diagnóstico de apresentação, custo de renderização e cadência lógica/desenho | Origem Xbox original; seu tradutor de CPU e seu caminho gráfico não são automaticamente os do Xbox 360 |
| Genma Onimusha | Validação externa da velocidade original e investigação de esperas duplicadas | Origem Xbox original; confirmar comportamento no NieR |
| Devil May Cry / Wario | Receita de cadência fixa e preservação do ritmo de gameplay | Não presumir que o loop do NieR permite os mesmos pontos de interceptação |
| Skate 3 | Ownership da janela/contexto e apresentação opaca em GLES2/SDL | Reusar somente quando os mesmos contratos de SDL e provider se aplicarem |

## Referência fixada nesta etapa

O registro de publicação do Rayman Origins de Xbox 360, de 02/11/2026, identifica a fonte `NextOs-Ports/rayman-origins-compact`, commit `808373501d6a`, recompilação ReXGlue e renderizador GLES2 próprio. Esse registro serve de origem da receita, não de teste do NieR. O checkout foi consultado em modo de leitura; nenhum jogo foi aberto.

Componentes a examinar ao iniciar a integração:

- `rex/src/native_renderer_gles2.cpp`: upload de texturas e backend próprio.
- `rex/src/render_thread.*`: renderização fora da thread lógica, respeitando a posse do contexto.
- `rex/src/hooks.cpp`: medições do loop e fases da cadência.

Lição já medida no Rayman: texturas estáticas sem mipmaps podem saturar a memória compartilhada e atrasar a CPU. Aplicar os mips originais quando existirem; tratar texturas dinâmicas, render targets e restrições NPOT separadamente. Medir esse gargalo antes de reescrever funções da CPU.

## Ferramentas públicas a avaliar

- XenonRecomp: https://github.com/hedge-dev/XenonRecomp
- ReXGlue: https://github.com/rexglue/rexglue-sdk
- jeff, suporte à análise/decompilação de XEX: https://github.com/ChimpsAtSea/jeff
- Ferramenta de texturas do NieR original: https://github.com/Banz99/Nier-Texture-Manager

A existência dessas ferramentas não comprova suporte ao executável do NieR. Fixar versões e licenças somente quando escolhidas e usadas. As referências internas não concedem automaticamente permissão para relicenciar ou publicar dependências.
