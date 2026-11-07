# Prova privada do PCM do runtime

`NIER_BUILD_PCM_CAPTURE=ON` no CMake de `tools/recomp` constrói um interposer
Linux host. Com `LD_PRELOAD` e `NIER_PCM_DIR` explícitos, captura os buffers
float32 enviados pelo runtime a `SDL_PutAudioStreamData`, preservando canais,
sample rate e ganho. O cabeçalho WAV é atualizado a cada bloco para sobreviver
à saída rápida do SDK. Não grava áudio sem a variável de destino e limita a
captura a 300 segundos por stream. Todos os WAVs ficam fora do Git.

`analyze_pcm.py INPUT.wav PRIVATE_OUTPUT --segment nome:inicio:fim` exporta os
dois canais frontais sem normalizar, calcula RMS/pico por canal, centroide,
entropia e quantidade de bins significativos do espectro. Exige NumPy/SciPy.
As métricas não identificam uma música por si: contexto e comandos enviados
precisam acompanhar o recibo.

O modo opcional `NIER_PCM_MEDIUMP_PROBE=1` adapta somente os prelúdios conhecidos
do shader de vértice e do blit do C1. Fontes originais/convertidas são guardadas
em cache privado por SHA-256. Outro `highp` ou ESSL300 é recusado. Em Mesa, a
prova usa `MESA_GLES_VERSION_OVERRIDE=2.0`. É uma adaptação para a prova de áudio
no PC; não substitui a validação gráfica física do renderer.
