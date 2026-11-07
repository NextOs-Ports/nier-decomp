# Reaproveitamento dos nossos ports

Este projeto começa pelas soluções existentes. Nenhum código dos projetos de referência foi copiado nesta etapa. A integração exige identificar a versão da fonte, revisar licença e adaptar ao executável do NieR.

| Referência | Componente a aproveitar | Limite |
| --- | --- | --- |
| DMC3 HD de Xbox 360 | Rota PowerPC → C++ → ARM64, renderer 3D GLES2, shaders, cadência e funções nativas | Primeira comparação para o caminho 3D; requisitos, hooks e estruturas do NieR ainda precisam ser identificados |
| Rayman Origins de Xbox 360 | Integração ReXGlue, serviços implementados, separação de render e lógica, cache e diagnóstico de banda | Complementa a escolha do runtime; hooks, endereços, estruturas e materiais são específicos do Rayman |
| Black | Diagnóstico de apresentação, custo de renderização e cadência lógica/desenho | Origem Xbox original; seu tradutor de CPU e seu caminho gráfico não são automaticamente os do Xbox 360 |
| Genma Onimusha | Validação externa da velocidade original e investigação de esperas duplicadas | Origem Xbox original; confirmar comportamento no NieR |
| Wario | Receita de cadência fixa e preservação do ritmo de gameplay | Não presumir que o loop do NieR permite os mesmos pontos de interceptação |
| Skate 3 | Ownership da janela/contexto e apresentação opaca em GLES2/SDL | Reusar somente quando os mesmos contratos de SDL e provider se aplicarem |

## Referências fixadas nesta etapa

- DMC3 HD de Xbox 360: versão aprovada/publicada **1.0.1**, fonte `NextOs-Ports/dmc3-hd-nextos`, branch `fps2/main`, commit `9c96aab`. O registro de republicação de 03/11/2026 identifica a recompilação XenonRecomp ARM64, renderer GLES2 próprio, shaders de cena, 37 funções nativas e teto de cadência ampliado. O arquivo `runtime/renderer_gl.cpp` existe no checkout de referência consultado. As configurações de resolução e qualidade dessa entrega são particulares do DMC3 e não são defaults para NieR.
- Black: registro da entrega aprovada de 27/10/2026, fonte `NextOs-Ports/black-nextos`, branch `ports/nextos-gles2`, commit `b3a8560`.
- Genma Onimusha: registro da entrega aprovada de 06–07/11/2026, fonte `NextOs-Ports/genma-onimusha-nextos`, branch `port/nx-x2`, commit `7133884`.

O registro de publicação do Rayman Origins de Xbox 360, de 02/11/2026, identifica a fonte `NextOs-Ports/rayman-origins-compact`, commit `808373501d6a`, recompilação ReXGlue e renderizador GLES2 próprio. Esse registro serve de origem da receita, não de teste do NieR. O checkout foi consultado em modo de leitura; nenhum jogo foi aberto.

Os registros foram consultados em modo de leitura. Antes de importar componentes, recuperar o commit completo, artefatos/hashes e licenças correspondentes. A escolha definitiva da base depende da análise do NieR descrita no [plano](DECOMPILATION_PLAN.md).

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
