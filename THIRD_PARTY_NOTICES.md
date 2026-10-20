# Avisos de terceiros

## NDI SDK

Este projeto utiliza arquivos de interface do NDI SDK e carrega em tempo de
execução a biblioteca redistribuível oficial do NDI.

NDI® é marca registrada da Vizrt NDI AB. Este projeto não é afiliado, patrocinado
ou certificado pela Vizrt NDI AB.

Os arquivos `Processing.NDI.*.h` presentes em `third_party/ndi/include` conservam
integralmente seus avisos de copyright e licença. O uso do SDK e de seu runtime
também está sujeito aos termos próprios do NDI SDK:

https://ndi.link/ndisdk_license

Fonte utilizada para os cabeçalhos públicos:

https://github.com/DistroAV/DistroAV/tree/master/lib/ndi


## Virtual Display Driver / Virtual Driver Control

O pacote do Transmitter inclui a distribuição original Virtual Driver Control
25.7.23 do projeto VirtualDrivers / Virtual Display Driver, com drivers assinados.
Os componentes não foram renomeados, recompilados nem modificados.

Projeto e código-fonte: https://github.com/VirtualDrivers/Virtual-Display-Driver
Distribuição: https://github.com/VirtualDrivers/Virtual-Display-Driver/releases/tag/25.7.23

Copyright (c) 2024 Virtual Display. Licença MIT; texto integral em
`virtual-display/LICENSE-VIRTUAL-DISPLAY.txt`, junto aos avisos originais.
Este componente acompanha somente o Transmitter; o Receiver não o inclui.
