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
