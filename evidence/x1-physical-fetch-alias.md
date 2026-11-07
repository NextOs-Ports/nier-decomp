# Tradução única dos aliases físicos dos fetches

O reader de texturas e paletas descartava os bits altos dos endereços sem
aplicar o deslocamento de4KiB dos aliasesE/F. A chave de textura e o readback
já usavam esse deslocamento, criando divergência entre identidade e dados.
O VB também apagava os bits do alias antes de traduzir o endereço.

`Physical` agora usa a mesma `PhysicalOf` da chave/readback. Endereços que já
são físicos são preservados; o endpoint inválido0x20000000 continua inválido.
O VB traduz o endereço completo antes de retirar a informação de tipo. O IB
usa a mesma função, com offset e tamanho verificados em64bits. O readback
continua traduzindo uma única vez: chamar o reader com endereço já normalizado
não acrescenta outra página.

Auditoria de todos os readers do renderer:

- Texturas RGBA/comprimidas/k8 e hash usam o reader central.
- Paletas de VTF, janelas de vértice e paletas copiadas para skinning usam o mesmo reader.
- VB preserva o alias antes da normalização.
- Os dois caminhos de IB reutilizam o endereço físico validado.
- Resolve/readback já usavaPhysicalOf e mantém esse comportamento.
- Objetos e dadosUP continuam no reader virtual do runtime.

A fixture coloca branco na página que o código antigo lia e preto na página
correta deE0001000. Antes, a amostra indevida clareava(80,120,160) para
(132,160,188); o VB devolvia-7 em vez de42 e a checagem do limite falhava.
Depois, quatro combinações de endereço normal/alias e VBO stream/referência
passaram: os16 casos GPU escurecem para(56,84,112), alpha76; VB42, IB/readback
sem deslocamento duplicado, hash, VTF e limites também passaram. Provider
registrado: Mesa/llvmpipe GLES2, sem erroGL.

Isso é uma correção geral demonstrada pela fixture. A sonda e o texel real
no Mali-450 já haviam descartado erro das tarjas; não se atribui a diferença
visual anterior a esta falha de alias. Provas privadas em
`nier-build/x1/provas/tarjas-mali-pc/{alias-before,final-*}.log`.
