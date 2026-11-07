# Gamma dos samplers do personagem

O grafo de cutscene confirma difusas DXT1/DXT5 com `TextureSign::kGamma`
nos três componentes RGB. Os mapas de normal CTX1 e máscaras permanecem
unsigned. As texturas decodificadas possuem detalhe de rosto, cabelo e roupa;
nenhum dos samplers selecionados era fallback branco. O decoder ignorava
`sign3`, entregando a cor codificada diretamente aos cálculos de iluminação.

A variante de fragment agora aplica a curva PWL da Xenos somente aos
componentes indicados pelo fetch, respeitando seu swizzle. Componentes
constantes e alpha unsigned permanecem intactos. O cache diferencia a máscara
de gamma; samplers unsigned mantêm o shader anterior. A especialização evita
uniformes/branches de configuração por amostra. Não altera precisão do VS.
Intermediários da conversão são menores que 2048 e o fragment usa mediump.

O teste `nier_texture_gamma_test` valida a seleção por fetch/swizzle e compara
256 pixels GLES2 com sete âncoras independentes da curva, gamma ligado e
desligado. Também verifica preservação de alpha. Passou com GL sem erros.

```
cmake --build "$BUILD" --target nier_texture_gamma_test
EGL_PLATFORM=surfaceless MESA_GLES_VERSION_OVERRIDE=2.0 "$BUILD/render/nier_texture_gamma_test"
```

O A/B visual com o tradutor de negação escalar corrigido usa diretórios de
cache/shader novos e o roteiro até a gameplay com corte de 290 s. Provas ficam
privadas em `nier-build/x1/provas/i1d` (antes) e `i1e` (gamma). A correção de
textura não certifica a exposição da gameplay, cuja leitura CPU de luminância
foi demonstrada zerada e está sob correção pelo C1.
