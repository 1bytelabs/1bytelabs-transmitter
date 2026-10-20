# 1Byte Labs Audio VST2 0.3.1 — Enviar e Receber

Um unico plugin Windows x64, com instalador, para REAPER e filtros VST 2.x do OBS.

## Enviar

Escolha **Enviar**, selecione o numero da fonte e ligue. O fader altera apenas o audio enviado pelo NDI; a saida local permanece igual a entrada.

## Receber

Escolha **Receber** na mesma janela. A lista de fontes NDI se atualiza automaticamente, incluindo fontes de outros PCs e fontes locais. Escolha a fonte e clique **Ligar recepcao**. O fader controla o audio recebido.

O audio recebido **substitui** a entrada do plugin. Com a recepcao desligada ou sem sinal, a saida fica silenciosa. O bypass do host volta a passar a entrada local.

No OBS: adicione o plugin em **Filtros > VST 2.x** de uma fonte de audio ativa (por exemplo, uma captura de entrada com o dispositivo silenciado antes do filtro). O OBS precisa processar essa fonte para chamar o VST; o plugin nao cria uma fonte OBS por conta propria. Evite adicionar outra captura do mesmo audio para nao duplicar o som.

No REAPER: adicione o plugin em uma track com o processamento ativo; escolha a fonte no modo Receber.

## Buffer e sincronizacao

A recepcao usa NDI FrameSync para converter a taxa de amostragem/canais para estereo na taxa do host e corrigir diferencas de relogio. O worker preenche uma fila de aproximadamente 50 ms (mais para blocos grandes); a fila e limitada a um segundo. NDI e rede acrescentam sua propria latencia. O callback do host nao carrega DLLs, chama NDI, aloca memoria ou usa mutexes.

A selecao da fonte e salva pelo nome no projeto/cena. Se a fonte desaparecer, o NDI tenta reconectar. Tags de geracao impedem que audio de uma fonte anterior seja reproduzido apos trocar de fonte.

## Compatibilidade e instalacao

Feche OBS e REAPER antes de instalar. Use a mesma pasta VST2 da versao anterior, por padrao **C:\\Program Files\\VSTPlugins\\1Byte Labs**. O instalador inclui o runtime NDI x64 e cria a pasta quando necessario.

Projetos da versao 0.2.x continuam em modo Enviar com numero, ganho e estado originais. O identificador VST2 e os seis parametros existentes foram preservados; Modo foi acrescentado como parametro 7.

**Diagnostico NDI** mostra o caminho da DLL e a etapa de erro. Na mensagem, Ctrl+C copia o relatorio.

## Verificacao

CI Windows: testes de ABI VST2, float/double, modo Enviar, recepcao com fixture NDI, descoberta, stride planar, troca de fonte, reconexao, bypass, estado antigo/novo, ganho, fechamento do editor e liberacao dos objetos; smoke do runtime real; loopback real Send/Receive; instalacao e desinstalacao isoladas.

O teste automatico de host nao substitui a validacao manual dentro do OBS/REAPER e entre dois PCs.

## Ajustes 0.3.1

A atualizacao da interface nao redefine selecoes enquanto os dropdowns estao abertos. O diagnostico agora inclui chamadas/blocos de audio do host, conexoes, formato NDI de entrada, frames capturados, samples entregues e faltas de buffer. Esses dados distinguem descoberta de fonte, conexao NDI, sinal de entrada e processamento do filtro. A causa de eventual silencio dentro do OBS depende desse relatorio; a correcao do dropdown por si so nao confirma a recepcao no host.
