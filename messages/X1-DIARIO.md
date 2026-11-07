# X1 — missão H

## X1-H1 — aviso de processadores
- Flag static atomic_flag/relaxed em SetActiveCpu: aviso uma vez por processo, sem mudar afinidade. Patch versionado processor-warning-once.patch; SDK privado cumulativo, rebuild incremental de rexruntime host/ARM64.
- host: `f14e213479de48808531148443427b329b064e2b77c2667552c9ebe0f17b7310`; `/mnt/ARQUIVOS/HOME-FELIPE/ports_compile/nier-build/x1/h1/host/librexruntime.so`.
- arm64: `b79e167a30178221b0f700c7abe82660c18599dcc1754a6536b681e6de5b5041`; `/mnt/ARQUIVOS/HOME-FELIPE/ports_compile/nier-build/x1/h1/arm64/librexruntime.so`.
- Binários anteriores preservados em h1/previous; sem teste no aparelho. Host inclui também o patch C5 já presente na fonte.
