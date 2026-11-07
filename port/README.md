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

O launcher experimental `run.sh` mantém conteúdo/saves em `user-data` junto
ao binário e não tem limite de sessão. `run_device_probe.sh` é exclusivo para
provas com corte de tempo. Ambos preservam a seleção de áudio do firmware;
PulseAudio ativo não é substituído por ALSA direto.

Para o SDK com espera corrigida e fila de 20 quadros, ver
`../tools/recomp/sdk/README.md` e `../tools/recomp/build_runtime_sdk.sh`.
Ao configurar o jogo, `NIER_RUNTIME_LIBRARY` pode apontar para a biblioteca
privada reconstruída; o pós-build a coloca junto ao ELF sem mudar o SDK de
referência. `build_nextos.sh` seleciona a biblioteca da cópia privada.

Provas sintéticas opcionais: `NIER_BUILD_LIFECYCLE_PROBE` e
`NIER_BUILD_SERVICE_PROBE` no CMake. `NIER_VIRTUAL_PAD_PROBE=1` valida o mapa
SDL/XInput no binário e termina pelo combo; não é opção de jogo normal.
`nier_services_probe USER_ROOT create|verify GAME_ROOT` verifica downmix e
persistência, usando um pacote exclusivo. A segunda execução reabre o pacote.
