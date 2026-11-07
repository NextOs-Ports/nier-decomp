# Diferença de tarjas Mali × PC

Os pixels capturados no PC comprovam preto com alpha76/255 e blend
SRC_ALPHA/ONE_MINUS_SRC_ALPHA. A declaração001A2086, swizzle688, bytes de
D3DCOLOR4CFFFFFF e textura1x1RGBA(0,0,0,255) concordam. Como a prova física
posterior mostrou clareamento, essa diferença foi reaberta sem trocar canais.

`nier_tarjas_driver_probe` executa o decode e ReplayDraw de produção em FBO,
sem jogo nem apresentação. Recebe os arquivos VS/PS reais como argumentos
privados; nenhum shader gerado do jogo integra o repositório público.
Compara quatro casos, tanto em stream VBO quanto no caminho de referência:

1. Shader real e textura1x1 decodificada pelo renderer.
2. Shader real e preto escrito diretamente na textura GL.
3. Preto com alpha do varying do VS real.
4. Preto com alpha constante76/255.

O fundo sintético(80,120,160) deve resultar em(56,84,112), alpha76. A sonda
registra provider, decode, blend efetivo, pixels antes/depois e erroGL. Recusa
provider que não reporte GLES2. No host Mesa/llvmpipe ES2, os oito casos
passaram com GL0. A primeira execução isolada havia escolhido NVIDIA/ES3.2
apesar do pedido ES2 e foi descartada; a validação usa vendor Mesa explícito.
Os fixtures gamma e LOD também passaram novamente nesse provider GLES2.

```
cmake --build "$BUILD" --target nier_tarjas_driver_probe
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 MESA_GLES_VERSION_OVERRIDE=2.0 "$BUILD/render/nier_tarjas_driver_probe" "$PRIVATE/actual.vert" "$PRIVATE/actual.frag"
```

A execução física e a comparação Mali pertencem ao coordenador. A sonda
informa diferenças; não constitui correção nem homologação da apresentação.
