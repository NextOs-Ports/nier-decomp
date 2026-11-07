# SDK privado: espera direcionada e fila de áudio

Base ReXGlue 0.10.0 `f5337cdc947ff6d4c4196737e2c807a48f2a1fc2`, com a
cópia cumulativa NextOS preservada da referência Rayman. Nunca editar essa
referência nem substituir seu SDK instalado.

`targeted-multiwait.patch` é o patch existente de Rayman Origins, preservado
sob a licença GPL-3.0 do projeto de origem (texto em `../jumptables/LICENSE`).
Parte da correção genérica NextOS de espera por condição e troca a notificação
global por inscrições nos handles realmente esperados. `WaitAny` e `WaitAll`
bloqueiam na condição e preservam consumo de semáforo, reset e timeout.
A cópia usada no NieR já contém esse patch; não aplicá-lo duas vezes.

`audio-queue20.patch` muda a configuração padrão local de 8 para 20 quadros
(256 samples/quadro a 48 kHz, capacidade ~106,7 ms). O valor continua configurável
por `--audio_maxqframes`. Isso amplia a margem da fila, não substitui a espera
bloqueante nem altera o relógio do jogo.

Build host: Release, Clang, `-msse4.1`, SDL3 vendorizada do SDK,
`REXGLUE_USE_SYSTEM_SDL3=OFF`, desktop WSI ON, Tracy/FidelityFX/tests OFF;
alvo `rexruntime`. A SDL em `/usr/local` do ambiente não corresponde à SDL host
atual, por isso o build não a usa.

Build ARM64: receita NextOS existente, Release/ARMv8-a, SDL3 do sysroot,
`REXGLUE_USE_SYSTEM_SDL3=ON`, desktop WSI OFF, Tracy/FidelityFX/tests OFF;
alvo `rexruntime`. Preservar demais patches aprovados. O jogo e plugin seguem
com seus builds incrementais; a nova biblioteca é testada primeiro em stage.

`nier_wait_probe` (CMake em `tools/recomp`) testa timeout, consumo de semáforos,
64 notificações concorrentes, WaitAll e eventos auto/manual-reset, imprimindo
CPU da thread durante espera. `LD_LIBRARY_PATH` seleciona a cópia a testar.
A validação final também usa o boot real com perfil por thread.

Use `build_runtime_sdk.sh PRIVATE_SOURCE PRIVATE_BUILD host|arm64` para
reproduzir os parâmetros. Depois configure o projeto com
`-DNIER_RUNTIME_LIBRARY=PRIVATE_SOURCE/out/linux-amd64/librexruntime.so`
ou a variante `linux-arm64`. Isso evita repor acidentalmente a biblioteca
antiga no próximo pós-build. O SDK original de referência não é sobrescrito.

## Teclado sem UI — X1-C4

`default-keyboard-text.patch` adiciona a cvar UTF-8 `default_keyboard_text`,
padrão `Nier`. Quando o guest fornece texto vazio/nulo, a conclusão headless
ou sem ImGui escreve esse nome em UTF-16 big-endian. Texto guest não vazio é
preservado com leitura correta da ordem de bytes. Limites incluem terminador,
sem cortar um par surrogate; capacidade zero é recusada. A conclusão diferida
do overlapped e as notificações do SDK permanecem no mesmo caminho original.

Reconstruir `rexruntime` nas duas arquiteturas. O ELF do jogo não precisa mudar;
a CLI reconhece automaticamente `--default_keyboard_text=Nome` na nova DLL.
`NIER_KEYBOARD_RUNTIME` no CMake das ferramentas liga o `nier_keyboard_probe`
à biblioteca privada reconstruída. O teste chama o import dentro de XHostThread
e espera a conclusão real do overlapped. Arquivos de teste ficam privados.
