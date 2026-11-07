# Atualização contínua

1. Fixe a identidade do executável em `progress/functions.json` antes de medir cobertura: SHA-256, versão, título e faixas de código executável.
2. Importe o inventário de funções do analisador escolhido. Registre endereço, tamanho original em bytes, sistema e estágio. Revise limites, tabelas de salto e sobreposições. Mantenha `inventory_complete=false` enquanto o levantamento estiver parcial.
3. Para `decompiled` ou `verified`, indique o arquivo de fonte reconstruído. Para `verified`, acrescente pelo menos uma evidência pública sanitizada em `evidence/`, identificando função, hash, método, resultados e limites. Não publique bytes do jogo nem logs brutos pessoais.
4. Atualize os marcos em `progress/milestones.json`. Um marco `done` precisa apontar para uma evidência. Caso o trabalho cubra apenas parte do marco, use `in_progress`.
5. Registre o avanço em `CHANGELOG.md`, execute os comandos abaixo e inclua seus resultados gerados no mesmo commit.

```sh
python3 tools/progress.py --snapshot
python3 -m unittest discover -s tests -v
python3 tools/progress.py --check
git diff --check
```

O snapshot diário é substituído se houver mais de uma atualização no mesmo dia; o histórico Git preserva todos os commits intermediários. Quando mudar o executável ou o inventário, explique a mudança de base e sua influência na porcentagem. O histórico identifica a base de cada ponto e não une bases diferentes na mesma linha.

Os testes do tracker usam dados sintéticos; seus números nunca entram no painel do jogo. O CI verifica consistência dos registros, evidências existentes e atualização dos arquivos gerados. Ele não substitui revisão da análise nem certifica a correção do jogo.
