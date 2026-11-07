# Atualizações

## 2026-11-07

- Lote 17: lotes 17 e 18 de bibliotecas do X1 (saídas PCM, seletores, provas também em ARM64/QEMU). O comparador agora intercepta exit/abort do runtime durante as execuções.
- Lote 16: mais templates de construtor, encaminhamento e busca (com deslocamentos do rlwinm extraídos por instância) e lote 16 de bibliotecas do X1. HANDOFF reescrito com o estado da rodada.
- GAMEPLAY NO PC com o renderizador GLES2 próprio: abertura do prólogo (cidade em ruínas sob neve) e controle do personagem com HUD e tutorial. Lote 15 de reconstrução (mais templates de ciclo de vida de objetos) e lotes 7–14 de bibliotecas do X1 (CRT numérico, trigonometria, áudio PCM/ADPCM/mixagem).
- Lote 14: templates de construtor e destrutor C++ (destrutor com delete, com alocador global e construtor com base). Das 152 instâncias localizadas, contam só as que executaram em ao menos 500 tentativas sem falha. Comparador com limite de tempo por execução e tratamento de divisão por zero. Lotes 5 e 6 de áudio do X1 (PCM, ADPCM, filtro, filas).
- Lote 13 e lote 4 de bibliotecas: template de inicialização do pool a partir do armazenamento (29 instâncias provadas) e funções numéricas do CRT. Inventário na 4ª rodada: 22.441 entradas, com os stubs cujo endereço é montado no código.
- Lote 12: o template de devolução ao pool (tira da lista em uso e empilha na lista livre) e suas 42 instâncias, cada uma provada pelo comparador. 239 funções verificadas.
- Lote 11: o template de pool (lista livre + lista em uso) foi isolado num cabeçalho; um localizador por estrutura de instruções achou 33 instâncias adicionais, cada uma provada pelo comparador. 196 funções verificadas.
- Lote 10 e lote 3 de bibliotecas: mais 5 pools de objetos (com as duas ordens de atualização do compilador), objeto estático local, e funções do XDK (último erro, timeout NT, troca atômica, filtro de exceção).
- Lote 9: listas duplamente ligadas, inicialização de pools, checagens de vetor e outro gerador aleatório. O comparador agora injeta NaN, infinitos e zero com sinal, e a função de chance foi corrigida para a semântica exata do PowerPC com NaN.
- Lote 8: acessores dos 3 slots (estado, flags, dados, double com fallback), fases da máquina de estados e pedido assíncrono. 129 funções verificadas.
- Lote 7 e lote 2 de bibliotecas: luaO_int2fb, luaO_log2, luaO_rawequalObj, luaC_link e lua_sethook do Lua 5.1; construtores; funções de string ASCII/UTF-16, comparação e busca do CRT.
- Lote 6: API do Lua 5.1 embutido no jogo (index2adr, settop, xmove, pushnil/number/boolean/lightuserdata, typename, luaL_findstring, leitor de string) e comparações de valores de script. O gerador de entradas agora trata faixas que atravessam o zero.
- Lote 5 e primeiro lote da frente de bibliotecas: pools de objetos, testes de campo de bits, acessores, e memcpy/memset/strings UTF-16 do CRT (as que passaram no comparador atual).
- Lote 4 da reconstrução: basename de caminhos, estados e flags de objeto, canais de interpolação (valor, alvo, duração). 53 funções verificadas.
- No PC, com o renderizador GLES2: logos, vídeo de abertura inteiro, tela de título, aviso de save, New Game, escolha de dificuldade e confirmação passam com imagem correta. O nome do herói, que travava no teclado do sistema, agora recebe "Nier" por padrão.
- No Mali-450 (.164): logos e vídeo com imagem correta; todos os shaders compilam e linkam na Mali, nenhum desenho pulado, sem erro de GL, 368–389 MB de RAM.
- Áudio provado no PC: música da abertura/título e efeitos de menu com sinal real. A thread de áudio caiu de 47 s para 0,4 s de CPU por minuto.
- Controles (14 botões, 4 eixos, 2 gatilhos), SELECT+START e saves persistentes provados no PC e no ARM64.
- Lotes 2 e 3 da reconstrução: construtores, cálculo de tiles da EDRAM, listas paginadas de menu, ordenação por nome, tabelas por máscara. 37 funções verificadas; o comparador roda tudo em fatias paralelas em cerca de 30 s.
- Executável ARM64 rodou no Mali-450 (.164): abriu e leu os dois CPK, 56 trocas de quadro por segundo com GPU nula, ~357 MiB de memória, 61→63 °C.
- Esqueleto do renderizador GLES2 próprio (MIT): thread dona do contexto, render interno 854x480, mipmaps em texturas estáticas, decodificação Xenos e ganchos nos 6 pontos do D3D.
- Primeira imagem do jogo no PC com o renderizador GLES2 próprio: logo da CRIWARE, abertura da SQUARE ENIX e partículas da tela de título.
- Lote de reconstrução: `rand()` do CRT e as funções de chance e faixa aleatória do jogo, interruptores com notificação virtual, inicialização de limites e histórico de 8 posições. 14 funções verificadas.
- Reconstruída e verificada a espera ativa da CPU pela GPU do D3D (`0x82579D50`), a função mais quente do boot. No port, a pausa dela cede o núcleo em vez de girar.
- Comparador agora cobre chamadas virtuais: destinos falsos registram alvo e argumentos, e a sequência de chamadas também é comparada. 4 funções verificadas.
- Primeiras 3 funções com decompilação verificada (204 bytes), provadas pelo novo comparador `tools/verify`: a reconstrução roda contra a tradução do original em 2.000 estados aleatórios. Teste de mutação confirmou que o comparador reprova erro de comparação, de limite e de deslocamento.
- Boot no PC provado: o jogo abre e lê `LAYER1.CPK`/`LAYER2.CPK`, roda o laço principal com mais de 900 trocas de quadro em 20 s e não deixa import pendente. O passo de tempo da lógica vem do relógio real (`mftb`), e a soma bate com o tempo de parede em 0,007%.
- Executável ARM64 do NieR compilado com a toolchain NextOS, sem avisos.
- Shaders: 573 programas distintos, 482 já traduzidos e compilados em ESSL 100 (84%); limite medido da Mali-450 de 256 vec4, e nenhum shader usa mais de 17 constantes.
- Extraído o ISO (XGD2) e identificado o `default.xex`: SHA-256 `f375d506…`, Title ID `535107E8`, Media ID `507D2720`, entrada `0x8255CEA0`, XDK D3D 2.0.9328. Imagem PE decifrada fixada por hash.
- Referência externa de timing: Xbox 360 original apresenta 30 quadros distintos/s; RGH destravado e FPS Boost rodam a 60. A trava de 30 é de apresentação (`0x8256BB90`).
- Inventário declarado completo: 22.411 funções, 5.372.176 bytes, conferido byte a byte contra o `.text` e o `PSFD00`. A porcentagem de decompilação verificada passa a ser medida (0% no início).
- Mapa D3D do NieR (XDK 9328): desenho, resolve, troca e texturas localizados. O jogo tem vsync adaptativo em `0x825A6F48`: escolhe 60, 30 ou 15 a cada quadro pelo tempo medido de desenho e lógica; a trava de 30 vem dessa escolha.
- Inventário inicial importado: 22.165 entradas (180 imports), 5.365.100 bytes exclusivos, todas `recompiled`. Ainda parcial: 257 limites compartilhados ou inferidos aguardam revisão, então a porcentagem global segue sem denominador.
- Codegen ReXGlue do NieR: 21.985 funções e 180 imports traduzidos sem avisos; 248 tabelas de salto (243 pelo analisador padrão, 5 pelo detector simulado adaptado do Rayman). Código gerado fica privado.
- Equipe de três terminais iniciada: CPU/recompilação ARM64, renderização GLES2 Mali-450 e coordenação.
- Recebido `NieR (USA, Europe) (En).7z`, SHA-256 `f6a8c194708482d2fd8fa70520b6a5e8dd38b87d016a1c1099cf40d7ac80bc67`; integridade do arquivo aprovada pelo 7-Zip.
- Escrito o plano detalhado de decompilação, runtime, renderização GLES2, gameplay, desempenho e ampliação de cobertura.
- Fixadas as referências aprovadas de DMC3 HD 1.0.1, Rayman, Black e Genma Onimusha, com limites de reaproveitamento.
- Atualizados README, handoff e painel com o marco de recebimento. ISO/XEX ainda não extraídos; inventário e porcentagem de decompilação continuam sem medição.

## 2026-11-06

- Criado o projeto público do NieR original de Xbox 360.
- Preparados o plano de reaproveitamento dos nossos ports e os marcos de execução, renderização e desempenho.
- Implementado acompanhamento por funções e bytes, gráficos SVG e histórico diário, com validação no CI.
- Executável aguardando conclusão do download; inventário, decompilação, build e gameplay ainda não iniciados.
