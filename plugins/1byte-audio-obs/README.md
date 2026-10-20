# 1Byte Labs Audio — Fonte nativa OBS 0.4.1

Windows x64, OBS 31.1 ou posterior. Compilado com headers/libobs oficiais 31.1.1; o CI carrega o modulo no libobs oficial 32.2.2 e recebe um sinal NDI real.

## Uso

Em **Fontes > + > 1Byte Labs Audio**, crie uma fonte, clique em **Atualizar fontes**, selecione o transmissor NDI e confirme. A opcao **Receber audio** liga/desliga a recepcao; **Volume recebido (dB)** ajusta o ganho. O audio aparece no mixer normal do OBS. Nao precisa de captura de microfone, VST como filtro ou DistroAV.

A descoberta NDI roda em segundo plano. Se abrir as propriedades antes de a busca terminar, aguarde um instante e use Atualizar fontes. A selecao e o volume sao salvos na cena. A conexao NDI tenta se recuperar quando a fonte volta a rede. A fonte so reproduz enquanto estiver ativa em uma cena; cenas inativas nao mantem captura de audio.

NDI FrameSync faz conversao de taxa e ajuste de relogio. A fila usa o mesmo backend do VST2. O modulo nao inclui video.

## Instalacao

O instalador oferece **VST2**, **Fonte nativa OBS** ou ambos. Para OBS instalado, procura o caminho no registro e na pasta padrao. Para portable, escolha a raiz do OBS ou a pasta bin/64bit que contem obs64.exe. O instalador valida um executavel x64 antes de copiar e mostra o destino na pagina de confirmacao.

Feche o OBS e o REAPER antes de instalar ou desinstalar. A instalacao nao muda cenas, perfis, dispositivos ou firewall. Os arquivos OBS sao:
- obs-plugins/64bit/1byte-labs-audio.dll
- obs-plugins/64bit/1byte-audio-ndi/Processing.NDI.Lib.x64.dll
- data/obs-plugins/1byte-labs-audio/README.md

Para instalar manualmente em OBS instalado ou portable, use **1ByteLabs-Audio-0.4.1-OBS-Manual-x64.zip** e extraia as pastas obs-plugins e data na raiz do OBS. Esse ZIP nao inclui VST2.

O link **1bytelabs.com.br** fica nas propriedades da fonte e abre https://1bytelabs.com.br no navegador. Ele nao aparece na imagem da transmissao.

## Desenvolvimento

Configure com um prefixo que contenha o SDK oficial libobs (Development). O CI prepara esse SDK pelo bootstrap oficial obs-plugintemplate em commit fixado; nao usa um ABI OBS reimplementado. O codigo deste modulo tem identificador SPDX GPL-2.0-or-later. O runtime NDI e os avisos de terceiros acompanham o pacote.
