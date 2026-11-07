# LOD constante nos destinos de Resolve

No grafo de gameplay, o VS da redução exporta VC0.x para v0.w. O PS usa esse
valor em setTextureLod. Os oito desenhos observados fornecem VC0.x=0…7, mas a
tradução anterior ignorava a instrução e todos amostravam a base 512×512.

Esta especialização conserva o caso demonstrado: um único registro LOD,
originado de um varying que copia uma componente uniforme do VS, constante
em todo o desenho e com nível inteiro. O sampler liga o destino de Resolve
existente com as dimensões daquele nível, que já possui FBO próprio no nível
zero do GLES2. Não requer GL_OES_fbo_render_mipmap nem copia pixels pela CPU.

O reconhecedor rejeita predicação, escritas prévias na componente, múltiplas
instruções setTextureLod e mistura de fetch explícito/automático no mesmo
sampler. Expressões dinâmicas, níveis fracionários e mipmaps de texturas
estáticas permanecem fora deste caso; não se declara suporte geral a LOD.

CMake confere os pins do tradutor fornecido e prepara uma cópia privada no
build. `tools/shaders/prepare_lod_translator.py` somente acrescenta comentários
com operandos de LOD ao GLSL emitido; mantém as correções do tradutor de origem
e não escreve na dependência compartilhada. Reconfigurar sem mudanças mantém
o arquivo anotado estável. Um emissor inesperado é recusado para adaptação.

O teste CPU cobre o vínculo uniforme/componente e rejeições conservadoras.
O teste GPU cria nove destinos de Resolve de cores diferentes e usa
ReplayDraw/TextureFor de produção: todos os níveis 0…8 foram escolhidos
corretamente, com GL sem erros. A prova com o roteiro até gameplay e o
comparativo dos níveis reais ainda fecha a validação desta etapa.

```
cmake --build "$BUILD" --target nier_constant_lod_test nier_constant_lod_gpu_test
"$BUILD/render/nier_constant_lod_test"
EGL_PLATFORM=surfaceless MESA_GLES_VERSION_OVERRIDE=2.0 "$BUILD/render/nier_constant_lod_gpu_test"
```
