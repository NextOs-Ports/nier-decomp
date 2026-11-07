# Atualizações

## 2026-11-07

- Extraído o ISO (XGD2) e identificado o `default.xex`: SHA-256 `f375d506…`, Title ID `535107E8`, Media ID `507D2720`, entrada `0x8255CEA0`, XDK D3D 2.0.9328. Imagem PE decifrada fixada por hash.
- Referência externa de timing: Xbox 360 original apresenta 30 quadros distintos/s; RGH destravado e FPS Boost rodam a 60. A trava de 30 é de apresentação (`0x8256BB90`).
- Equipe de três terminais iniciada: CPU/recompilação ARM64, renderização GLES2 Mali-450 e coordenação.
- Recebido `NieR (USA, Europe) (En).7z`, SHA-256 `f6a8c194708482d2fd8fa70520b6a5e8dd38b87d016a1c1099cf40d7ac80bc67`; integridade do arquivo aprovada pelo 7-Zip.
- Escrito o plano detalhado de decompilação, runtime, renderização GLES2, gameplay, desempenho e ampliação de cobertura.
- Fixadas as referências aprovadas de DMC3 HD 1.0.1, Rayman, Black e Genma Onimusha, com limites de reaproveitamento.
- Atualizados README, handoff e painel com o marco de recebimento. ISO/XEX ainda não extraídos; inventário e porcentagem de decompilação continuam sem medição.

## 2026-11-06

- Criado o projeto público do NieR original de Xbox 360.
- Preparados o plano de reaproveitamento dos nossos ports e os marcos de execução, renderização e desempenho.
- Implementado acompanhamento por funções e bytes, gráficos SVG e histórico diário, com validação no CI.
- Executável aguardando conclusão do download; inventário, decompilação, build e gameplay ainda não iniciados.
