# X1 — missão H

## X1-H1 — aviso de processadores
- Flag static atomic_flag/relaxed em SetActiveCpu: aviso uma vez por processo, sem mudar afinidade. Patch versionado processor-warning-once.patch; SDK privado cumulativo, rebuild incremental de rexruntime host/ARM64.
- host: `f14e213479de48808531148443427b329b064e2b77c2667552c9ebe0f17b7310`; `/mnt/ARQUIVOS/HOME-FELIPE/ports_compile/nier-build/x1/h1/host/librexruntime.so`.
- arm64: `b79e167a30178221b0f700c7abe82660c18599dcc1754a6536b681e6de5b5041`; `/mnt/ARQUIVOS/HOME-FELIPE/ports_compile/nier-build/x1/h1/arm64/librexruntime.so`.
- Binários anteriores preservados em h1/previous; sem teste no aparelho. Host inclui também o patch C5 já presente na fonte.

## Perfil guest preservado antes da nova prioridade H2
- Coleta host GLES2, save vazio, timeout195+5s, perf cpu-clock:u199Hz/DWARF16KiB a partir140s:88539amostras/444,920sCPU. Captura75s jámostra3D. Top30 emX1-H-hot.json; buffers passam àprioridade porordemdo coordenador.
- Baselineprivado h2-baseline; execução h2-perf; llvmpipeES2, nãoMali. Ranking CPU dominado porGPU/driver; decomp adiada.

## X1-H2 — cache VBO/IBO + formatos compactos
- Cache imutável por endereço/descritor, hash16amostras + comparação EXATA completa; serial preserva frames pendentes. CPU64MiB/4096entradas; GL64MiB, evicção só entreframes. Upload só emmiss. Float1/2/3/4, byte/short e half(OES), fallback para signed-normalized/packed/escala.
- HostllvmpipeES2, medianas ms/quadro nas janelas3D: buffers5,045→1,065 (-78,9%); resolve38,275→45,535; textura2,445→2,585; estado+programa semtextura0,665→0,765; desenho+GPU20,460→19,885. Janelas/carga concorrente variam: não alegar ganho isolado deFPS.
- Imagemreal: quadro3672,156draws3D,22alvos/14.955.532bytes,0diferenças, referênciaestável eimagemoriginaligual;356557pixelsiluminados. Diretório h2-buffers/audit-visible. Sintético480imagensGLES2/491520fetches pixel-exact; ASan/UBSanpass.
- Gateincremental --diff origin/main0afac7c OK. Globaltem violaçõeslegadasC1, semmascará-las. ARM64emh2-buffers/arm64; SHA ELF33c4bd8ccd0e52ded48f11605b7b7395e526c3e3d77b797fe2a1b1ef566ab87f. ReciboX1-H2-buffers.json. MetaMali≤3ms aguarda medição coordenador; baselineF8=134ms. Texturas/resolve/shaders nãoalterados.

## X1-H3 — regressão UI no Mali corrigida para reteste
- F9coordenador:3Dbuffers134,6→21,2ms, masmenu50–77ms. UIagora sempreSTREAM,3paresfixos16MiB+2MiB; glBufferSubData/offsets,semgen/delete por draw. STATIC sóapós2quadrosdistintos; hash16elementos,semmemcmpcompleto porordem. Heurística podenãodetectarmudançasforadasamostras; UIignoraocache.
- PCmenu,medianasms: resolve 0.790→0.840, buffers 0.100→0.060, texture 0.395→0.490, state_without_texture 0.060→0.130, draw_gpu 4.810→7.770. Janelasvariáveis,nãoFPSisolado.
- PC3D,medianasms: resolve 45.535→35.060, buffers 1.065→0.420, texture 2.585→6.800, state_without_texture 0.765→0.950, draw_gpu 19.885→20.350. Janelasvariáveis,nãoFPSisolado.
- Menus1080–3000:zero criações/missesSTATIC/crescimentoanel; contadoresporquadro adicionados. Auditoriaquadro3543:153draws3D,22alvos,0diferenças;480imagensGLES2+ASan/UBSanpass. GateincrementalOK. ARM64emh3-stream/arm64; aceitefísico(UI≤4ms,3D≤21ms)pendente coord. CPUdraftstash90f674d preservado,fora desteELF.

### H3 integração concluída
- BaseC1 2d5a41e integrada; check_leve global e incrementalOK. Auditoria agora segue holder deResolve como apresentação:156draws/21alvos/16.000.896bytes,0diferenças,replaysestáveis eoriginaligual. Teste90sencerrado. ARM64 de96ddd1e permaneceintacto duranteprova .164; ajuste posterior sódiagnósticoopt-in.
- PCintegrado menu,ms: resolve=0.000, buffers=0.040, texture=0.010, state_without_texture=0.055, draw_gpu=4.460.
- PCintegrado 3d,ms: resolve=1.170, buffers=0.410, texture=0.160, state_without_texture=0.370, draw_gpu=22.400.

## X1-H4 — upload dinâmico único por quadro
- F11Mali:menus186–271mscomanelSubData; PCnãoaprova buffers. Agora stagingCPUcompleto+1glBufferData(dados,STREAM_DRAW)porVBO/IBOantesdetodosdraws;rodízio3pares. SemSubDatadinâmico; STATICapós2quadrosmantido. Contadorconfirma2uploads/quadro; buffersincluicópia+upload.
- PCapenasimagem/contadores:157draws3D/21alvos/16.000.896bytes,0diferenças,originaligual;rodada95sencerrada. Gatesglobal/incrementalOK. ARM64h4-upload/arm64/nier SHA1136e216e9dc12947262d93282999fee10d0196a767c1937d94b5c90e60b4ec7. AceiteMali(UI≤4,3D<21ms)aguardacoordenador.
- CPUdraftcorrigido preservado emstash46abe590;16384bordas4modosFPpassaram,sem ganhoisoladoPC(10,589→10,640ns). Fora doELF entregue.

## X1-H4 — aceite físico recebido (F12)
- Coordenador aprovou o ELF 1136e216 no .164: menus 15–33 ms/quadro, buffers 0,1–4 ms; buffers 3D 7,9–12,4 ms. Meta final de 3 ms ainda não atingida, mas buffers deixaram de ser o gargalo principal. FPS permanece medido somente no aparelho.

## X1-H5a — perfil para D1 e coletor ARM
- Lista host pós-H4 atualizada em X1-H-hot.json e entregue no diretório compartilhado. D1 é dono da decomp; rascunho de feedback de áudio foi identificado como NÃO aprovado devido a NaNs sinalizantes e entregue separadamente.
- Coletor opt-in: timers de CPU por thread, SIGPROF dirigido, handler apenas com atomics lock-free; JSON periódico e Build IDs. Informa threads bloqueadas, cobertura, overruns e perdas. Testes host/ARM-QEMU passaram; ELF incorreto foi recusado.
- Base 7a7c9df integrada. ARM em h5-sampler/arm64, com DLL anterior para coleta inicial. Mesmo ELF pode usar a DLL TimerQueue candidata. Sem perf no aparelho; env padrão delay140/duração45/100Hz. Coordenador mantém corte ≤300s e envia JSON para simbolização. Recibo X1-H5-sampler.json.

## X1-H5b — TimerQueue bloqueante
- A fila usava spin até os deadlines. Patch passa a condition_variable e corrige a ordem dos argumentos nas duas esperas temporizadas da dependência. Prazos, recorrência e cancelamento preservados.
- Host: 400 ms com timer de 1 ms consumiam 64,402 ms de CPU; agora 2,564 ms (-96,0%). Provas: 1200 timers concorrentes, prazo mais cedo, 399/400 callbacks periódicos, auto-cancelamento e cancelamento durante callback passaram. ARM/QEMU passou na correção. Não é medição de FPS.
- DLL candidata ARM fc24bbed em h5-timer/arm64, junto ao mesmo ELF b5232912 do coletor. Baseline continua em h5-sampler/arm64 (DLL b79e167a). Prova host 3D bounded85s e smoke da base 7a7c9df bounded10s; aceite/FPS no .164 pendente do coordenador. Recibo X1-H5-timer.json.
- Nova prioridade X1-I: imagem correta. Coleta ARM H5 segue com o coordenador; lista será atualizada quando o JSON chegar.
