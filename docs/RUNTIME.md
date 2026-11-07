# Runtime do NieR — evidência de boot e relógio

Executável original: SHA-256
`f375d506413f52bd12fc9fd34df03250bbc4f3e7f98730332179f88777ea653b`.
PE privada sem patches de imports: SHA-256
`ff218ae38697223b5f76de3141f3afdb24f9077c90375e7f051c3d4fbdc5b4df`.
Base `0x82000000`, entrada `0x8255CEA0`.

## Boot host observado em 07/11/2026

O executável x86-64 com ReXGlue 0.10.0 registrou **22.411 entradas**, sem
rejeições/duplicatas, e executou a entrada original. O plugin `null` processa
ringbuffer, fences e sincronização; não desenha nem comprova gameplay.

- `MEDIA/LAYER1.CPK` e `MEDIA/LAYER2.CPK` abertos com status `0x0`.
  Leituras confirmadas: cabeçalhos de 2.048 bytes, blocos de 127.840 e
  175.008 bytes, mais blocos de 512 bytes, todos com status de sucesso.
- Mais de 900 swaps de protocolo na rodada de 20 segundos. Não são FPS
  desenhados nem prova de velocidade original.
- Cinco threads de serviço host: XMA Decoder, Audio Worker, GPU Commands,
  GPU VSync e Kernel Dispatch. A rodada detalhada registrou a thread principal
  em `0x8255CEA0` e 23 chamadas de entrada de thread por `0x82569868`.
- Nenhum import kernel/XAM não implementado ou erro fatal registrado nessa
  rodada. Avisos: GameControllerDB ausente e achievement com ID zero recusado.
  Há tentativas de assets soltos inexistentes; a abertura/leitura dos CPKs
  está comprovada, mas não se afirma que todos os assets foram decodificados.
- O áudio SDL inicializou um endpoint de seis canais/48 kHz. Isso comprova
  inicialização, não som correto. A seleção inicial de driver `dummy` foi
  removida porque esse SDK não o oferece.
- Pico de RSS host: 400.760 KiB na rodada de 20 s; 375.360 KiB na rodada
  detalhada de 8 s. Backend sem imagem: não inclui a memória do renderer GLES2.
- Todos os processos de jogo foram encerrados. O supervisor impõe até 120 s
  mais 5 s de corte SIGKILL, independentemente de progresso ou apresentação.

ELF host testado: SHA-256
`6615e0813d311292eaa2a0da07c8edef8dc3d3d507f599300f27a958c2c214c8`.
Logs integrais e recibos permanecem privados, referidos no diário X1.

## Origem do Δt e pontos por quadro

| Endereço | Papel identificado |
| --- | --- |
| `0x82596EA0` | Atualização por quadro: chama método virtual `+36` do objeto de relógio e encaminha o float `this+88` a um subsistema de atualização |
| `0x82596750` | Atualiza o relógio: lê timestamp atual, subtrai `this+80`, converte para segundos, grava float em `this+88` e timestamp em `this+80` |
| `0x825611A0` | Wrapper de contador: executa `mftb` e grava uint64 no ponteiro recebido |
| `0x825966B0` | Conversão de ticks para segundos; divisão pela frequência obtida em `0x8255D188` |
| `0x8255D188` | Chama `KeQueryPerformanceFrequency`, import `0x82625414` |
| `0x825967F0` | Getter do float de Δt em `this+88` |
| `0x82596700` | Inicializa/reinicia timestamp; não deve ser chamado artificialmente a cada draw descartado |
| `0x825A6F48` | Política de intervalo de apresentação identificada pelo C1; escolhe intervalo de vblanks |
| `0x8257AB30` | Wrapper de espera: passa zero em r4 e desvia para `0x8257A708` |
| `0x8257ADDC` | Chamada de `VdSwap`, import `0x82625A24` |

O objeto observado do relógio foi `0x85F505E0`. A vtable começa em
`0x8209F530`; seu slot `+36` aponta para `0x82596750`. Na atualização por
quadro, o acesso ao slot ocorre antes da leitura/propagação de `this+88`.
A classificação como atualização por quadro decorre desse fluxo e das
chamadas repetidas observadas; o laço externo completo ainda não foi nomeado.

**O Δt observado vem de tempo decorrido por `mftb`, não de uma multiplicação
fixa do intervalo de apresentação.** O codegen traduz `mftb` para
`Clock::QueryGuestTickCount()`. O runtime configura frequência guest de
50.000.000 ticks/s e escala de tempo 1,0; o contador deriva do relógio host.
O jogo converte pela frequência correspondente, preservando segundos reais.
As constantes de apresentação são aproximadamente 1/29,97 em `0x820A1D14`
e 1/59,94 em `0x820A1D18`; sua presença não demonstra a frequência da lógica.

Sonda sem alteração de estado (`NIER_TIMING_PROBE=1`):

| Amostras | Soma do Δt após a primeira | Tempo monotônico host |
| --- | ---: | ---: |
| 300 | 4,990019 s | 4,990352 s |
| 600 | 9,991127 s | 9,991796 s |
| 900 | 15,122707 s | 15,123728 s |

A primeira amostra é excluída da soma porque inclui tempo anterior à primeira
observação. Essa prova relaciona o relógio do runtime ao host nessa cena de
boot; não certifica velocidade de combate/corrida nem frequência original.
A espera instrumentada em `0x8257AB30` foi cerca de 0,001 ms nessas amostras;
a sincronização também existe em outros pontos do protocolo GPU.

No port, manter esse Δt em tempo real. Frameskip deve omitir apresentação,
preservando chamadas de atualização e passagem do relógio. Não fixar Δt em
1/60 por quadro desenhado, não escalá-lo pelo número de draws e não acrescentar
uma segunda espera de vblank. A lógica original continua presumida em 60 Hz
até a evidência externa mantida pelo coordenador; atrasos longos e estabilidade
de simulação precisam de validação própria quando houver gameplay.

## Contrato de integração

`NierApp` expõe `runtime()` e `window()` herdados de `rex::ReXApp`; o SDK cria
a janela SDL3. C1 pode integrar pelos hooks de lifecycle e pelos símbolos
fracos de funções guest, conservando a entrada original `__imp__sub_*`.
Contexto GLES2, desenho e swap permanecem na mesma thread proprietária.
O plugin `null` é diagnóstico, não renderer final.

SELECT+START usa eventos SDL do mesmo controle e solicita saída imediata.
O teste sintético passou: botões isolados e controles diferentes não fecham;
a segunda pressão do combo enfileira saída sem tempo mínimo. Controles físicos
e saída no aparelho ainda dependem da rodada do coordenador.

## Build ARM64 NextOS

Fonte do runtime: ReXGlue `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`,
com a cópia privada cumulativa do SDK Rayman e seu patch NextOS
SHA-256 `ffa82da46855d46707a99bed3fe9798b68a3003a7e41ceeb3a31d394ce879b13`.
A biblioteca reutilizada tem SHA-256
`457f9948741dfb2745b118e545c69e264d4aa5df7a6af2f0a1cb92dd6e70d611`.
As fontes e o SDK foram copiados para o build X1; a referência ficou intacta.

Clang 23.1.4/LLD, sysroot NextOS corrente, `-O3 -DNDEBUG -march=armv8-a`.
ThinLTO **desativado**, igual ao cache ARM64 da referência consultada.
A receita está em `tools/recomp/build_nextos.sh`, com caminhos por argumentos
ou ambiente; usa quatro jobs por padrão. SDL3 é a do firmware.

| Artefato | Bytes | SHA-256 |
| --- | ---: | --- |
| `nier` | 60.035.984 | `1770d5870baf795ce8bef4f404303cad5ee89a9a74f4b1bf1a00ff3b436e6372` |
| `librexruntime.so` | 9.732.016 | `457f9948741dfb2745b118e545c69e264d4aa5df7a6af2f0a1cb92dd6e70d611` |
| `librexgpu-null.so` | 979.656 | `81abe15d964513491c0f53269fcbb70cca58d9bc81eee5e1f86185bd6e995ad7` |

O ELF é AArch64 PIE com RUNPATH `$ORIGIN`, conservando debug_info privado.
Não houve erro/aviso de compilação no build. Esses binários correspondem à
fonte `07ee122`; a documentação posterior não muda os bytes.

A reserva guest mantém mapeamentos virtuais esparsos; a arena usa backing
compartilhado sem nome para preservar aliases virtual/físico e as páginas são
comprometidas sob demanda. Outras reservas usam `MAP_PRIVATE|MAP_ANONYMOUS`
e `PROT_NONE` antes do commit. O tamanho virtual não é a RAM residente e não
se deve substituí-lo por uma alocação/zeragem antecipada de vários GiB.

Para planejamento, o boot null host mediu aproximadamente 391 MiB de pico.
Isso não certifica residência ARM64 ou aparelhos de 1 GB: ainda faltam assets
do gameplay, renderer, texturas, buffers e custos da GPU. O coordenador fará
a medição física. `run_device_probe.sh`, colocado junto aos três artefatos,
impõe 55 s + 5 s para morte forçada do teste inteiro. Não é launcher de jogo
normal nem prepara frontend, dispositivo ou trava de uso.

## B1 — boot ARM64 físico, 07/11/2026

Uma abertura autorizada com o backend null, dados por NFSv4.1 somente leitura,
travas local/remota e supervisor externo de 55 + 5 segundos. Duração total
54,087 s, saída 0, processo ausente ao fim. A montagem e a exportação temporária
foram removidas; nenhum arquivo existente do aparelho foi apagado. O frontend
permaneceu inativo e mascarado. Não houve alteração de vídeo, clocks ou governor.

- Ambos os CPKs abertos/lidos com sucesso.
- 1.966 swaps de protocolo; 56,267 swaps/s entre o primeiro e o último
  (janela de 34,923 s, exclui inicialização). Não são FPS renderizados.
- RSS máximo 366.032 KiB (~357,45 MiB), até 45 threads do processo.
- CPU do sistema durante a janela: núcleos 0/1/2/3 em
  48,19% / 43,68% / 49,59% / 43,45%. Inclui serviços concorrentes do sistema.
- Temperatura passou de 61 para 63 °C.
- Amostra 1.800 do relógio: Δt acumulado 32,119814 s / monotônico 32,106591 s.
- SDL/PulseAudio abriu stream estéreo 48 kHz, não mudo, 100%, associado ao PID
  do jogo. Ganho frontal unitário configurado pela API do SDK. Captura PCM por
  monitor não ficou disponível; esse teste não certifica som audível/correto.

ELF desta rodada: SHA-256
`d1c7d218a608677bd49b6d7715e75087870f782e7f324b59ce8c4ee2645ba745`.
O plugin diagnóstico inclui timestamps opcionais por swap para a medição.

## B2 — perfil CPU do boot null host

Coleta de 60 s por `perf cpu-clock:u`, 199 Hz, pilhas DWARF de 16 KiB.
25.118 amostras totais; 25.115 pertencem ao PID do jogo, cobrindo 28 threads e
126,206 segundos acumulados de CPU user. O exportador gerou 300 entradas guest
globais e listas por thread com tempo próprio/inclusivo, endereço e percentual.
O denominador inclui CPU do runtime; os percentuais inclusivos se sobrepõem.

Maiores custos próprios globais:

| Função | Próprio | Inclusivo |
| --- | ---: | ---: |
| `0x82579D50` | 15,477% | 25,594% |
| `0x82573758` | 9,672% | 46,180% |
| `0x825D17DC` (CRT) | 3,822% | 3,822% |
| `0x825D182C` (CRT) | 3,731% | 3,731% |
| `0x8255D138` | 2,783% | 2,783% |

Dados privados: `messages/X1-quentes.json`; coleta e pilhas sob `nier-build/x1/b2`.
A coleta privilegiada foi restrita ao perf, mantendo o jogo como o usuário
normal. Nenhuma política global de perf foi alterada. Validaram-se os 300
registros, limites dos percentuais e soma da CPU por thread. Não é perfil de
renderização, gameplay ou desempenho do aparelho.

## X1-2c — entradas virtuais de ajuste de `this`

O abort após START em `0x8249D498` veio de entradas não registradas. A varredura
de `.text` e `PSFD00` encontrou 328 pares `addi r3,r3,simm16; b alvo`, dos quais
135 têm ponteiros em seções de dados. Doze faltavam no mapa gerado; todos foram
incluídos, com quatro limites antigos divididos para não agrupar entradas.
Após regenerar: 22.423 entradas (22.243 funções + 180 imports), zero pendências
nesse padrão e zero avisos do codegen. A soma exclusiva permanece 5.372.176 B.
A passagem do título com imagem pertence à validação do renderer do C1.

## B3 — controle, persistência e áudio

O teste `NIER_VIRTUAL_PAD_PROBE=1` usa um pad virtual SDL3 dentro do binário:
14 botões, quatro eixos e dois gatilhos chegaram corretamente ao `GetState`
XInput do runtime. Solturas foram verificadas. BACK sozinho não fechou;
SELECT+START produziu o registro de saída imediata e encerrou o processo com
status 0. A execução inteira da prova durou 0,223 s; não houve necessidade de
segurar os botões. O teste anterior também separou botões de controles distintos.

O launcher experimental `port/run.sh` não possui timer e fixa saves/conteúdo
em `user-data` ao lado do binário. O launcher de teste também usa essa pasta
persistente; cache e logs ficam separados dos dados do jogo. XContent resolve:

`user-data/<XUID>/<TITLE_ID>/<CONTENT_TYPE>/<PACKAGE>/`

Para NieR, o Title ID é `535107E8` e saved games usam tipo `00000001`.
O teste `nier_services_probe` criou um pacote sintético exclusivo e o reabriu
em outro processo, conferindo payload e resolução VFS. Passou em x86-64 e em
ARM64 sob QEMU, sem executar código guest nem tocar saves reais. Isso valida
a persistência do serviço; um save/reload dentro da campanha ainda exige cena
jogável.

### Correção do custo da AudioWorker

O perfil anterior mostrou tempo em `pthread_mutex_trylock`, varredura de
handles em `WaitMultiple` e consultas de relógio; decodificação XMA e mixagem
não eram o custo dominante dessa thread. A biblioteca host antiga fazia
polling, enquanto a referência ARM64 já tinha uma correção de espera.

Foi reconstruída uma cópia privada do SDK para ambas as arquiteturas com
inscrições de espera nos handles relevantes e condição bloqueante, eliminando
polling/broadcasts desnecessários. O patch direcionado foi reaproveitado da
referência Rayman, sem alterar seu checkout ou SDK instalado. A fila padrão
local passou de **8 para 20 quadros**, com capacidade de aproximadamente
106,7 ms a 48 kHz; `--audio_maxqframes` continua configurável. A decodificação
XMA nativa existente foi preservada. Não houve alteração do Δt do jogo.

| Medição | Antes | Depois |
| --- | ---: | ---: |
| AudioWorker host, CPU user amostrada em 60 s | 47,055 s | 0,402 s |
| Processo host, CPU user amostrada em 60 s | 126,206 s | 78,884 s |
| AudioWorker ARM64, % de um núcleo na janela pós-swap | 6,927% | 6,522% |
| CPU da thread em timeout sintético de 100 ms, host | 0,485 ms | 0,019 ms |

No host, a redução da AudioWorker foi aproximadamente **99,1%**. No ARM64,
a diferença observada foi aproximadamente **5,9%**, pois já havia espera
corrigida. A janela física compara amostras entre dois e 32 segundos após o
primeiro swap, com cerca de 29,3 s úteis por rodada. São provas de boot null,
não de gameplay; a combinação de espera/fila foi testada em conjunto.

Os testes de concorrência passaram com ambas as bibliotecas: timeout,
64 liberações concorrentes de semáforo, consumo único, WaitAll, coalescência
de auto-reset e notificação de dois waiters em evento manual-reset.

A rodada ARM64 posterior durou 53,902 s e encerrou o processo. Os CPKs foram
lidos; a memória máxima foi 365.472 KiB, com 45 threads, temperatura 62→66 °C
e 57,465 swaps de protocolo/s na janela pós-inicialização. A montagem NFS
somente leitura e a exportação foram removidas, preservando frontend parado,
dados e cópias dos binários anteriores.

O caminho observado foi XMA/MMIO e callback guest → XAudioSubmitRenderDriverFrame
→ SDLAudioDriver → SDL3/PulseAudio → HDMI/ALSA, estéreo 48 kHz. O ganho frontal
unitário foi aplicado, e a conversão BE planar 5.1 → LE estéreo passou também
na implementação ARM64. A captura de 7,875 s do stream do próprio PID contém
somente nível próximo da quantização (pico de um LSB de 16 bits), portanto
**não certifica música ou efeitos audíveis** nessa cena de boot. Essa prova
continua pendente de uma cena que produza áudio significativo.

O teste físico e os perfis de áudio antecedem apenas a adição dos 12 thunks
X1-2c; estes foram compilados incrementalmente depois. Nenhuma nova abertura
física foi feita para repetir partes preservadas. Logs e recibos completos
ficam em `nier-build/x1/b3-*` e `messages/X1-B3.json`.

Artefatos finais B3 + X1-2c (as bibliotecas de referência continuam preservadas):

| Artefato | SHA-256 |
| --- | --- |
| ARM64 `nier` | `1ec3c9de56edee8fa2617138f1acdcbc8ec2de25d6ea44d2c0cf34204706b7ba` |
| ARM64 `librexruntime.so` | `be32b87d6202371cb8c27d23e5e5dce5fcf2ea2d5e10318eb20d115d2b61429f` |
| ARM64 `librexgpu-null.so` | `995fb294819cc4ace6d534d0e30aa0d6b2e441f4b56e2cebf5ee355fc6c09233` |
| Host `librexruntime.so` | `d1176059a4a0ba942bc65e0900f8c3d075338babf5be2f128f185846161e64e4` |

A prova de pad virtual/saída e a reabertura XContent host foram repetidas com
a biblioteca final para verificar a troca de SDK. Ambas passaram. O perfil
`messages/X1-quentes.json` agora corresponde ao SDK corrigido; o perfil anterior
foi preservado como `X1-quentes-antes-audio.json` para a comparação.

## X1-C1 — PCM significativo da abertura/título e dos menus

Com o build GLES2 do C1 copiado para uma área privada do X1, a captura anterior
quase silenciosa foi substituída por uma prova direta da submissão SDL. Roteiro
finito: START no quadro 2.700 pula a abertura; outro START no 3.300 entra pelo
título; A no 3.900 confirma o menu. Baixo/cima foram enviados uma vez cada nos
quadros 4.500/4.800. Execução limitada a 100 segundos e encerrada pelo supervisor.

| Trecho privado, sem normalização | Duração | RMS L/R | Pico L/R | Centroide | Entropia espectral |
| --- | ---: | --- | --- | ---: | ---: |
| Música da sequência de abertura/título | 25 s | 0,05461 / 0,07099 | 0,45123 / 0,61722 | 1.205,8 Hz | 15,248 bits |
| Efeito de START no título | 4 s | 0,05442 / 0,05197 | 0,32633 / 0,30386 | 265,6 Hz | 10,069 bits |
| Efeito de confirmação A | 4 s | 0,04891 / 0,04933 | 0,36921 / 0,38595 | 208,8 Hz | 9,216 bits |

O PCM original é float32/48 kHz/seis canais. Os trechos preservam os frontais;
centro/LFE são zero e surrounds residuais são desprezíveis nesse trecho.
O estado estacionário “Press START” após pular a abertura fica quase silencioso;
não se afirma que existe música contínua nesse estado. A música comprovada é
a da sequência de abertura/título e os efeitos são transientes alinhados aos
dois comandos, com intervalos silenciosos entre eles. Isso distingue áudio real
de ruído de quantização. Não é validação de toda a campanha ou de saída física.

WAVs e gráfico de forma de onda/espectro permanecem em `nier-build/x1/c1/proof`;
metadados e hashes em `messages/X1-C1-audio.json`. O renderer do C1 não foi
modificado: a prova PC usou ES 2.0 e adaptação privada dos prelúdios conhecidos
para mediump. As capturas mostram o contexto, sem certificar os defeitos visuais
restantes do renderer experimental.

## X1-C2 — build ARM64 com renderer do C1

Renderer `1212ec2` integrado pela main/work/c1, mantendo os thunks X1-2c,
a espera de áudio corrigida e os controles. Build AArch64 com
`NIER_RENDER_GLES2=ON`, `-O3 -mcpu=cortex-a53`, ISA ARMv8-a e ThinLTO OFF.
Dependências gráficas são GLESv2/EGL/SDL3 do firmware; nenhum governor foi alterado.
O ELF e suas dependências passaram na conferência de arquitetura/linkagem, sem
avisos de compilação. **O X1 não abriu esse build no aparelho; C1 valida a imagem.**

ELF: SHA-256 `62ce45a579fa8f8b8afd06dd9b4d9b1efba6a8725f65f3ac69bd0f46698b276c`.
Artefatos privados e `SHA256SUMS`: `nier-build/x1/c2/arm64/`.
Recibo para integração: `messages/X1-C2-ARM64.json`.

`build_render_nextos.sh` reproduz as opções. O launcher `run_render_probe.sh`
liga o renderer e chama o supervisor de 55 + 5 s, preservando SELECT+START e
user-data persistente. Os dois scripts devem ficar juntos ao ELF. A trava do
aparelho, preparação do frontend e dados ficam sob o roteiro do C1/coordenador.

## X1-C4 — nome do herói sem teclado

O `default_text` vazio fazia a implementação headless devolver uma string
vazia e o jogo repetir o pedido de nome. O patch do SDK usa a cvar
`default_keyboard_text` (padrão `Nier`) apenas quando o texto guest é vazio/nulo.
A cvar é UTF-8; o resultado é UTF-16 big-endian, terminado e limitado ao buffer.
A leitura de defaults não vazios também foi corrigida para preservar Unicode.
Os caminhos headless e sem ImGui usam o mesmo callback e completam o overlapped
com sucesso, mantendo o retorno inicial `X_ERROR_IO_PENDING`.

Prova do import real, em XHostThread: oito casos passaram no host e oito no
ARM64/QEMU, incluindo nome existente, acento, caractere fora do BMP, truncamento
sem meio surrogate, fallback vazio, buffer pequeno e capacidade zero. C1 faz
a confirmação no fluxo do jogo e no aparelho; o X1 não abriu o aparelho.

Novos SHA-256 de `librexruntime.so`:

- Host: `24e2f84e799240ff10fcd749b2eda4388fdf04448f65ba98d36783b62704d741`.
- ARM64: `01c5092956c31982047969c7ddd3d08a61c0da72179d64df8241f4bb1b91c5f1`.

As cópias em `nier-build/x1/host`, `arm64` e `c2/arm64` foram atualizadas;
o ELF C2 permanece com o SHA anterior. Recibo: `messages/X1-C4-keyboard.json`.

## X1-C5 — perfil local e atribuição do controle

A biblioteca já devolvia `XamUserGetSigninState(0)=1`, XUID
`B13EBABEBABEBABE` e gamertag `User` em PC e ARM. A diferença reproduzida foi
a atribuição de dispositivos: com dois pads, o START do segundo era associado
ao usuário 1, cujo estado é desconectado. O roteiro do C1 adiciona um pad SDL
a uma máquina que pode já ter controle físico.

O patch `tools/recomp/sdk/local-profile-input.patch` usa SharedAssignment por
padrão neste port single-player (`local_profile_shared_input=true`). Os controles
passam a usar o perfil local 0; os demais usuários continuam desconectados.
A identidade e os diretórios de saves/perfil são preservados. A prova chamou
os imports reais e a política real de input, com dois dispositivos sintéticos:
antes `active_user=1, signin=0`; depois `active_user=0, signin=1`.

A nova DLL ARM64, sem recompilar ELF ou runtime host, tem SHA-256
`f828329c2e4c1cb0f491667412a406b46d44b5390ce0aeb23196073deab2ed64`.
Está em `nier-build/x1/c5/arm64/librexruntime.so` e nos stages ARM canônicos.
QEMU passou também na reabertura de um setting de perfil em outro processo,
no caminho `user-data/535107E8/profile/User`. O teste não alterou dados reais
nem abriu o aparelho; o C1 valida a última rodada física.
