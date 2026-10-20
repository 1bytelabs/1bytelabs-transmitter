# 1Byte Labs Send 0.1.3 — protótipo VST3

Plugin experimental para Windows x64: recebe o áudio estéreo do REAPER,
preserva a saída local e envia uma fonte NDI de áudio, sem vídeo ou captura
da saída do Windows. Usa a API NDI de áudio float planar do Transmitter.
Não há promessa de eliminar engasgos nem de latência mínima.

## Instalação e primeiro teste

1. Baixe o ZIP do artifact **1ByteLabs-Send-0.1.3-prototype-Windows-x64** no workflow
   **1Byte Labs Send VST3 - prototipo Windows** desta branch e extraia-o.
2. Copie a pasta inteira `1Byte Labs Send.vst3` para
   `C:\Program Files\Common Files\VST3`. A DLL NDI deve permanecer em
   `1Byte Labs Send.vst3\Contents\x86_64-win`, junto ao binário.
3. No REAPER, procure novos plugins VST3 e insira **1Byte Labs Send** no final da
   cadeia de efeitos do master, depois dos efeitos usados na mixagem.
4. Na interface do 1Byte Labs Send escolha **Numero da fonte** (1–16) e clique em
   **Ligar envio**. O nome na rede será `1Byte Labs Send 01`, por exemplo, com o
   prefixo do computador acrescentado pelo NDI.
5. No OBS da outra máquina, use a entrada NDI que você já utiliza e selecione
   essa fonte. Desative o envio antigo durante a comparação para evitar duplicação.
6. Teste também no 1Byte Labs Receiver para separar problemas do envio e da recepção.

O REAPER precisa estar processando áudio. Se necessário, habilite seu
processamento de efeitos com o transporte parado. O plugin não transmite
durante renderização offline. Bypass ou suspensão do host interrompem a
alimentação de áudio; O botão de envio é o controle recomendado para desligar o envio.
Ao salvar um projeto, o número da fonte e a opção de envio são preservados.
Use números diferentes para instâncias simultâneas no mesmo computador.

## Interface ao vivo

A janela própria tem tema escuro, botão de envio e seleção de fonte. Conexão,
fila e perdas são lidas diretamente dos atomics a cada 66 ms, inclusive se
não houver novos callbacks de áudio. Medidores L/R mostram o pico dos pacotes de áudio na fila de envio em dBFS, com queda suave e aviso vermelho de pico em 0 dBFS por
1 segundo. Eles funcionam com envio ligado e áudio processando; não confirmam recepção no OBS.
A janela não depende de fechar e reabrir para atualizar.

O timer e toda a pintura ficam na thread da interface; o cálculo de picos fica no worker de rede, fora do callback do REAPER. Os controles notificam o host
para automação e persistência. A interface genérica continua como alternativa.

## Diagnóstico

- **Network status:** Off, Waiting for receiver, Connected ou Runtime or sender error.
- **Dropped samples:** samples recusados porque a fila ficou cheia, limitado
  visualmente a 1.000.000. Reinicia na preparação do processamento.
- **Queued audio:** ocupação aproximada da fila em ms, atualizada enquanto
  o host processa áudio. Não representa a latência total até o OBS.

O callback do host apenas copia samples para armazenamento pré-alocado.
Não chama NDI, não carrega DLL, não espera por mutex e não cria threads.
O worker forma pacotes de 10 ms; o relógio vem do host e o clock NDI fica
desligado, como no Transmitter. A fila admite até 100 ms. Se ficar cheia,
o bloco novo inteiro é descartado e contabilizado, preservando o REAPER.
Não há buffer adaptativo ou correção de relógio de recepção nesta versão.
O Receiver atual possui buffer inicial de aproximadamente 80 ms: seu bom
resultado não implica que o receptor NDI do OBS terá o mesmo comportamento.

Para comparação, use a mesma rede e taxa de amostragem nos dois testes;
48 kHz é um ponto inicial. Registre duração, buffer do REAPER, receptor,
engasgos, contadores e atraso relativo ao vídeo. Faça um teste de pelo menos
30 minutos, remova/reinsira o efeito, reabra o projeto e desconecte/reconecte
o receptor. Não use em produção antes dessa validação em duas máquinas.

## Compilação isolada

Visual Studio 2022 (C++), CMake 3.25+ e Git com acesso ao SDK oficial.
O CMake fixa o SDK VST3 em um commit e não usa VSTGUI/JUCE.

```powershell
cmake -S plugins/zosma-send -B build-send -A x64
cmake --build build-send --config Release --target zosma-send zosma-send-queue-test zosma-send-editor-test zosma-send-processing-test validator --parallel
ctest --test-dir build-send -C Release --output-on-failure
```

O workflow executa o validator VST3 e empacota o runtime NDI da mesma fonte
usada pelo workflow do Transmitter. Não publica releases. O projeto principal
continua sendo compilado pelo seu CMake original.

O teste Windows da interface verifica abertura, botão, seleção de fonte,
telemetria e dez ciclos de fechar/reabrir; salva capturas para inspeção.

Testes portáteis sem SDK/Windows:

```sh
cmake -S plugins/zosma-send -B build-queue -DZOSMA_QUEUE_TESTS_ONLY=ON
cmake --build build-queue
ctest --test-dir build-queue --output-on-failure
```

O SDK VST3 tem seus próprios termos no arquivo incluído no pacote;
os avisos NDI acompanham o artifact. A licença do código 1Byte Labs continua
conforme o README principal. Este plugin não é afiliado à Steinberg ou à NDI.

## Atualização de marca

O nome agora é **1Byte Labs Send**. O identificador VST3 e os parâmetros
foram preservados para compatibilidade com projetos existentes. Ao atualizar,
remova a pasta antiga `Zosma Send.vst3` antes de instalar `1Byte Labs Send.vst3`,
para evitar duas cópias com o mesmo identificador. Faça uma nova varredura no
REAPER e selecione a fonte `1Byte Labs Send 01` (ou o número configurado) no OBS.

## 1Byte Labs

[1bytelabs.com.br](https://1bytelabs.com.br) · [contato@1bytelabs.com.br](mailto:contato@1bytelabs.com.br)

## 0.1.3 — teste de regressão de estabilidade

Após relato de oscilação de volume nas versões com editor, o callback de
áudio `copyAndSend` foi restaurado exatamente ao código da 0.1.0. O cálculo
de picos foi movido para o worker de rede, os getters dos controles graváveis
voltaram à implementação original do SDK e a janela reutiliza o bitmap,
atualizando em aproximadamente 15 Hz. Não foram adicionados ganho, compressor,
AGC, resampling ou mudanças no formato/tamanho dos pacotes NDI.

Esta é uma mitigação para teste A/B, não confirmação da causa ou da solução.
Compare o mesmo trecho e os mesmos ajustes no REAPER/OBS com a 0.1.0, usando
apenas uma instalação do plugin. Teste com editor aberto e fechado. A oscilação
de volume e os engasgos ocasionais são sintomas separados.
