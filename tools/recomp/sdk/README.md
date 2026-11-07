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

## Perfil local e controle — X1-C5

`local-profile-input.patch` habilita `local_profile_shared_input=true` na cópia
SDK deste port single-player. A política SharedAssignment já existente une
controles físicos, teclado e pad SDL do roteiro no usuário 0. Com SlotAssignment,
o segundo pad acionava o usuário 1, sem perfil, reproduzindo o aviso de continuar
sem login. O perfil 0 já estava conectado; não era ausência do arquivo de perfil.
Não são criados usuários fictícios adicionais nem alterados XUID/gamertag.
`--local_profile_shared_input=false` restaura a distribuição por slots.

Somente `rexruntime` ARM64 foi recompilado para esta entrega. Identidade local
mantida igual ao PC: usuário 0, state 1, XUID B13EBABEBABEBABE, gamertag User.
O launcher mantém `user_data_root` na pasta `user-data` do port. Os settings
do perfil ficam em `<user_data_root>/535107E8/profile/User`; os saves XContent
continuam na árvore por XUID. Nenhum save existente é removido ou substituído.

`NIER_PROFILE_RUNTIME` habilita `nier_profile_probe`: chama os imports reais
GetSigninState/GetXUID/GetName/GetGamerTag/GetSigninInfo dentro de XHostThread,
exercita a política real InputSystem com um/dois dispositivos sintéticos e
confere escrita/reabertura de um setting numa pasta privada de teste. No QEMU,
dois pads passaram de usuário ativo 1/state 0 para usuário ativo 0/state 1.
O teste físico do menu e gameplay cabe ao C1, com a DLL entregue e o mesmo ELF.

## Criação de conteúdo — X1-C6

Com `NIER_PROFILE_RUNTIME`, o alvo `nier_save_create_probe` prova os imports
reais XContent e NT I/O, incluindo a conclusão diferida e reabertura em outro
processo: `nier_save_create_probe GAME_ROOT PRIVATE_USER_DATA create|open`.
CREATE_NEW evita sobrescrever uma fixture existente. A raiz de testes é
privada e o payload SYSTEM_DATA é sintético. A DLL C5 passou em ARM64/QEMU sem
nova alteração. A correção de implantação está no launcher e no patch da receita
C1, separados do NFS de dados e com verificação de escrita antes do jogo.

## Aviso de CPU — X1-H1

`processor-warning-once.patch` limita o aviso de menos de seis processadores
lógicos a uma emissão por processo. Usa `static std::atomic_flag` com ordem
relaxada: somente arbitra o aviso entre threads, sem alterar afinidades.
Recompilar apenas `rexruntime` nas duas arquiteturas com os caches existentes.

## TimerQueue sem espera ativa — X1-H5

Aplicar na cópia privada com `git apply --unidiff-zero blocking-timer-queue.patch`.

`blocking-timer-queue.patch` troca somente a estratégia de espera da fila por
condition_variable, mantendo deadlines absolutos, ordem, recorrência e estados
de cancelamento. Corrige também a ordem dos argumentos wait_for/wait_until do
helper blocking da dependência, que antes não era instanciado neste caminho.

`nier_timer_queue_probe` exercita 1200 timers com quatro produtores, novo prazo
mais cedo, recorrência de 1 ms, auto-cancelamento e cancelamento enquanto o
callback executa. No host, CPU de 400 ms de recorrência passou de 64,4 para
2,56 ms; 399/400 callbacks, sem mudança de intervalo. ARM64/QEMU passou na
correção, sem certificar velocidade do aparelho. A espera ociosa também passou.

Bibliotecas anteriores são preservadas para comparação. O FPS e a cadência
percebida precisam ser medidos pelo coordenador no .164. O coletor de CPU usa
timers POSIX independentes; não depende da TimerQueue do SDK.
