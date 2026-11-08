# Textura half29 com intervalo HDR em um RGBA8

A textura256×256 usada por A1DEA1/T9 contém half com picosRGB até
253,125/302,75/325,5 e alpha constante1. O caminho anterior convertia cada
componente para unorm0…1, perdendo os valores acima de1.

O caminho novo usa um único RGBA8, um fetch e escala/bias por componente da
textura. Na preparação, codifica sqrt((half-bias)/escala) em8bits. O fragment
multiplica pelo coeficiente sqrt(escala/4), eleva ao quadrado, soma bias/4 e
restaura o fator4. Os intermediários e uniformes cabem no mediump do Mali;
o limite final representa o intervalo finito de half. Componentes constantes,
incluindo alpha1 deste recurso, usam escala0 e bias do valor original.

Os mipmaps são médias dos floats originais antes da curva. Não há textura/FBO
float, plano adicional, pow no fragment ou múltiplos fetches de reconstrução.
O cache de programa distingue samplers HDR; os demais formatos mantêm o
caminho anterior. Os coeficientes só são reenviados quando mudam.

## Perda aceita e medida

Esta é uma aproximação orientada a desempenho, não uma saída16bits linear.
A filtragem nativa interpola valores codificados e pode escurecer bordas de
alto contraste. Mips prontos em espaço linear reduzem o acúmulo desse viés.

No recurso real, comparando262144 componentes com os half originais:

- Erro absoluto máximo de texel:0,84564 no intervalo até325,5.
- ErroRGB ponderado pela energia dos valores:1,14676%; o clamp antigo tinha87,26342%.
- Nos pontos médios horizontais, erro ponderado:7,86952%; pior erro absoluto
  normalizado pela escala do canal:12,19983%.
- O p95 do erro relativo dos componentesRGB com valor>=0,01 é52,30698%.
  A perda relativa nos tons baixos é maior e está registrada, além da média.

A fixtureCPU passou ASan/UBSan, limites de quantização da curva, médias de
mips conhecidas, bias não zero e alpha constante. A fixtureGPU passou26
comparações de texel/bilinear no caminho de produção, provider Mesa/llvmpipe
GLES2 real, GL0. Ela confere a curva no espaço codificado com tolerância de
filtragem8bits e aritmética mediump; a perda em relação à filtragem half
linear é medida separadamente acima.

## Imagem e reprodução

A provaPC Mali-like usa o roteiro-gameplay-v2,640×360, caches novos e corte
290s. Chegou à gameplay com cor, personagem/HUD e as ações do roteiro; não
houve shader/formato recusado. As capturas de cenário, personagem e UI foram
comparadas com a prova anterior do renderer da main. Não se alega identidade
pixel a pixel entre animações nem ganho de FPS medido no aparelho.

Dados originais, raw, logs e imagens permanecem privados em
`nier-build/x1/provas/{i2,hdr29}`. O protótipo de dois planos foi abandonado;
somente este codec de um plano entra no build.

```
cmake --build "$BUILD" --target nier_texture_hdr8_test nier_texture_hdr8_gpu_test
"$BUILD/render/nier_texture_hdr8_test" "$PRIVATE/raw_1A971000_f29.bin"
__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json EGL_PLATFORM=surfaceless LIBGL_ALWAYS_SOFTWARE=1 MESA_GLES_VERSION_OVERRIDE=2.0 "$BUILD/render/nier_texture_hdr8_gpu_test" "$PRIVATE/raw_1A971000_f29.bin"
```
