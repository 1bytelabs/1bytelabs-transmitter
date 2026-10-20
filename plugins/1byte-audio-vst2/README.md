# 1Byte Labs Audio 0.4.1

VST2 com Enviar/Receber e fonte nativa de áudio NDI para o OBS. Windows x64.

## Escolha como instalar

- **1ByteLabs-Audio-0.4.1-Setup-x64.exe**: escolha VST2, fonte OBS ou ambos. O instalador tenta localizar o OBS e permite escolher outra instalação, incluindo portable.
- **1ByteLabs-Audio-0.4.1-VST2-Manual-x64.zip**: somente o VST2, runtime NDI e avisos. Extraia na pasta de VST2, por padrão C:\Program Files\VSTPlugins\1Byte Labs.
- **1ByteLabs-Audio-0.4.1-OBS-Manual-x64.zip**: somente os arquivos da fonte OBS. Extraia na raiz do OBS instalado ou portable, mantendo as pastas obs-plugins e data.

Feche OBS e REAPER antes de atualizar. O runtime NDI já acompanha os componentes.

## Receber diretamente no OBS

Requer OBS 31.1+ x64. Em **Fontes → + → 1Byte Labs Audio**, use **Atualizar fontes**, selecione o transmissor e confirme. Não precisa de microfone ou filtro VST nesse fluxo. O volume aparece nas propriedades da fonte e no mixer do OBS.

## Usar o VST2

Adicione **1Byte Labs Audio** no REAPER ou como filtro VST 2.x de uma fonte de áudio ativa do OBS. Escolha **Enviar** ou **Receber** na mesma janela.

Em Enviar, o fader altera somente o sinal NDI. Em Receber, o áudio selecionado substitui a entrada. Desligar a recepção deixa a saída silenciosa; usar o bypass do host volta a passar a entrada local.

O modo, fonte, ganho e estado são armazenados pelo projeto ou pela coleção de cenas do host. Uma fonte ou instância nova começa com as opções padrão. Projetos antigos 0.2.x restauram em Enviar com seu ganho e número de fonte.

## Diagnóstico e informações

O botão **Diagnóstico NDI** do VST2 mostra a conexão e o processamento do host. Na mensagem, Ctrl+C copia o relatório.

O link **1bytelabs.com.br** abre o site no navegador ao ser clicado. No OBS, fica somente nas propriedades da fonte.

Site: https://1bytelabs.com.br
