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
- HostllvmpipeES2, medianas ms/quadro nas janelas3D: buffers5,045→1,065 (-78,9%); resolve38,275→45,535; textura2,445→2,585; estado+programa semtextura0,665→0,805; desenho+GPU20,460→20,855. Janelas/carga concorrente variam: não alegar ganho isolado deFPS.
- Imagemreal: quadro3672,156draws3D,22alvos/14.955.532bytes,0diferenças, referênciaestável eimagemoriginaligual;356557pixelsiluminados. Diretório h2-buffers/audit-visible. Sintético480imagensGLES2/491520fetches pixel-exact; ASan/UBSanpass.
- Gateincremental --diff origin/main0afac7c OK. Globaltem violaçõeslegadasC1, semmascará-las. ARM64emh2-buffers/arm64; SHA ELF33c4bd8ccd0e52ded48f11605b7b7395e526c3e3d77b797fe2a1b1ef566ab87f. ReciboX1-H2-buffers.json. MetaMali≤3ms aguarda medição coordenador; baselineF8=134ms. Texturas/resolve/shaders nãoalterados.
