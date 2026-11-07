# Atualizações

## 2026-11-07

- Executável ARM64 rodou no Mali-450 (.164): abriu e leu os dois CPK, 56 trocas de quadro por segundo com GPU nula, ~357 MiB de memória, 61→63 °C.
- Esqueleto do renderizador GLES2 próprio (MIT): thread dona do contexto, render interno 854x480, mipmaps em texturas estáticas, decodificação Xenos e ganchos nos 6 pontos do D3D.
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
