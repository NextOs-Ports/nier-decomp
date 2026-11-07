# Perfil de CPU sem perf no aparelho

Diagnóstico opt-in, desligado por padrão. `NierStartCpuProfile` instala timers
POSIX baseados no tempo de CPU de cada thread e envia SIGPROF ao TID correspondente.
O handler só lê o PC e atualiza atomics lock-free. Não aloca, simboliza, faz I/O
ou usa mutex. Uma thread separada escreve JSON atomicamente a cada 5 s e ao fim.
Não altera clocks/governor, lógica, renderização ou duração da sessão do jogo.

Na rodada autorizada do coordenador, com o corte existente ≤300 s, definir:

```sh
export NIER_CPU_PROFILE="$RUN/cpu-h5.json" # diretório privado gravável
export NIER_CPU_PROFILE_DELAY=140
export NIER_CPU_PROFILE_SECONDS=45
export NIER_CPU_PROFILE_HZ=100
# Abrir pelo roteiro existente; encerrar o processo até 300 s.
```

Os padrões são 140 s de espera, 45 s de coleta e 100 Hz por thread de CPU.
O coletor encerra seus timers sozinho; não fecha o jogo. A cópia periódica
permite recuperar dados mesmo se o launcher encerrar o processo por timeout.
SIGPROF já pertencente a outro componente desabilita o coletor, sem substituí-lo.

Copiar o JSON privado para o PC e usar **os ELFs exatos da coleta**:

```sh
python3 tools/inventory/export_cpu_samples.py cpu-h5.json \
  --elf "$BUILD/nier" --elf "$BUILD/librexruntime.so" \
  --inventory "$PRIVATE/X1-inventario.json" --output messages/X1-H-hot.json
```

Os GNU Build IDs dos módulos vêm da memória do processo. O exportador recusa
ELF com Build ID diferente, inclusive se o caminho do binário tiver sido
reconstruído durante a coleta. O SHA-256 dos ELFs usados entra no resultado.
Dados brutos, mapas e capturas ficam privados; publicar só o resumo apropriado.

## Limites e prova

É amostragem do PC próprio, sem pilha inclusiva. Mede CPU user+system nos clocks;
trabalho de kernel aparece no PC de entrada/retorno do syscall. Threads novas
são descobertas em ~250 ms; threads muito curtas podem não ser amostradas.
Threads que bloqueiam SIGPROF são listadas separadamente com CPU observada.
O relatório informa cobertura aproximada, overruns e perdas da tabela. Percentuais
dos símbolos são dos **samples entregues**, não de CPU que ficou sem cobertura.
No PC, workers llvmpipe bloqueiam SIGPROF: não esconder essa lacuna. No aparelho,
conferir a cobertura junto com a captura que comprova a cena 3D. **Não mede FPS.**

`port/tests/cpu_profile_probe.cpp` verifica thread ocupada versus thread que dorme,
thread ocupada com SIGPROF bloqueado e preservação de handler preexistente.
Passou em x86 e ARM64/QEMU. O exportador foi testado com símbolo conhecido e
rejeição de ELF incorreto. A prova do jogo no PC só valida funcionamento; FPS e
aceite de desempenho pertencem ao .164, conforme ordem do coordenador.

APIs/ABI consultadas:
- https://man7.org/linux/man-pages/man2/timer_create.2.html
- https://man7.org/linux/man-pages/man7/signal-safety.7.html
- https://codebrowser.dev/glibc/glibc/nptl/pthread_getcpuclockid.c.html
