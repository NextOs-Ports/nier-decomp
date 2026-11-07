# Runtime experimental

Requer SDK ReXGlue 0.10.0; configurar `CMAKE_PREFIX_PATH` para o SDK host.
Criar links privados `port/game` para o disco extraído e `port/generated`
para a área privada de código gerado. Ambos são ignorados pelo Git.

```sh
rexglue codegen port/nier_manifest.toml
cmake -S port -B /private/build/nier -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_PREFIX_PATH="$REXSDK"
cmake --build /private/build/nier --parallel 4
```

O esqueleto foi gerado pelo mesmo SDK usado pelo Rayman. Os endereços nos
hints pertencem exclusivamente à PE do NieR identificada pelo preparo.
Não executar sem limite de tempo durante desenvolvimento. Codegen aprovado
não significa boot, gameplay nem decompilação verificada.

A janela SDL3 e o ciclo de vida vêm de `rex::ReXApp`. O renderer GLES2 será
ligado por `OnPreSetup`/`OnPostSetup`, mantendo contexto, desenho e swap na
thread proprietária. `port/render` pertence ao responsável pelo renderer.
