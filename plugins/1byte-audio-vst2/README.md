# 1Byte Labs Audio 0.2.1 — protótipo VST2 x64 (Enviar)

Primeira etapa da migração do Send VST3 0.1.4 para VST2. Este pacote envia
áudio estéreo por NDI. **O modo Receber ainda não foi implementado.**
Não é um plugin nativo do OBS, não instala driver ou cabo virtual.

## Instalar

Feche o REAPER e o OBS. Execute o instalador
`1ByteLabs-Audio-0.2.1-VST2-Setup-x64.exe`. O destino VST2 padrão é:

`C:\Program Files\VSTPlugins\1Byte Labs`

O instalador cria a pasta, permite escolher outro destino e coloca nela
`1Byte Labs Audio.dll` e `Processing.NDI.Lib.x64.dll`. Documentação e avisos
ficam na pasta de instalação 1Byte Labs Audio. Há desinstalador pelo Windows.
Também está disponível um ZIP para copiar os arquivos manualmente.

O executável do instalador é um protótipo sem assinatura digital. Ele não
instala nada em máquinas remotas e não altera automaticamente as configurações
de busca do REAPER, do OBS ou as regras do firewall.

## Testar primeiro no REAPER

1. Abra a busca de plugins VST do REAPER e confirme que `C:\Program Files\VSTPlugins`
   está incluída; faça uma nova varredura.
2. Adicione **1Byte Labs Audio**, formato VST, na mesma posição da cadeia
   em que o Send VST3 foi testado. Desligue a outra instância para evitar
   duas transmissões concorrentes com o mesmo nome.
3. Ligue o envio, escolha o número da fonte e ajuste **Volume de envio**.
   O nome NDI continua sendo `1Byte Labs Send 01`, ou o número selecionado,
   para permitir comparação com o protótipo anterior no receptor já usado.
4. No OBS receptor, continue usando sua entrada NDI atual durante esta etapa.
5. Compare o mesmo trecho em 44,1 e 48 kHz se fizer parte do seu uso. Teste
   editor aberto/fechado, pelo menos 30 minutos de transmissão, reabertura do
   projeto, duas instâncias com fontes diferentes e reconexão do receptor.

O processamento local preserva o áudio de entrada. O fader (-60 a +6 dB)
afeta somente os pacotes NDI, suavizado em 20 ms pelo worker original. Os
medidores mostram o áudio após esse fader, não o que o OBS efetivamente tocou.
Fila de 100 ms no máximo e pacotes de 10 ms são os mesmos da VST3 0.1.4.
Essa migração não pretende corrigir por si só o stutter ocasional já relatado.

O estado VST2 salva envio, número da fonte e volume em um chunk validado.
Ele não importa automaticamente as configurações da instância VST3: ao trocar
de formato, configure a nova instância uma vez. A versão VST3 permanece intacta
no repositório e não é removida pelo instalador.

## OBS nesta etapa

O plugin pode ser carregado como filtro **VST 2.x** de uma fonte de áudio
para **enviar o áudio dessa fonte**. Isso permite testar o formato no OBS,
mas não substitui o DistroAV para receber: o modo de recepção será a próxima
etapa. Não configure o envio para retransmitir sua própria entrada NDI.
Suporte destinado a hosts Windows de 64 bits. Hosts de 32 bits não são suportados.

O plugin respeita suspensão/bypass. Renderização offline não alimenta o NDI
quando o host informa o nível de processamento VST2. Hosts que não fornecem
essa informação não permitem distinguir offline de realtime por essa API.

## Implementação e validação

O wrapper usa a interface independente Xaymar/vst2sdk, fixada no commit
`339d4f31590bf77c0d0d248e09a380ac6285e069`, e preserva sua licença BSD-3-Clause.
Não usa o SDK VST2 proprietário nem depende do SDK VST3 ou de JUCE/DPF.
O transporte, a fila e o ganho são compilados diretamente a partir de
`plugins/zosma-send`, sem alterar a implementação validada da versão 0.1.4.
A interface Win32 foi adaptada para o ciclo de vida VST2 em código independente.

Requisitos de build: Windows x64, Visual Studio C++, CMake 3.24+, Git; Inno
Setup 6 para gerar o instalador. O workflow `1Byte Labs Audio VST2 - Send prototype`
gera pacote ZIP e instalador, sem publicar release ou alterar o Send VST3.

```powershell
cmake -S plugins/1byte-audio-vst2 -B build-vst2 -A x64
cmake --build build-vst2 --config Release --parallel
ctest --test-dir build-vst2 -C Release --output-on-failure
```

Testes do host carregam a DLL real pelo símbolo `VSTPluginMain`, verificam a
ABI x64 e a sequência de inicialização usada pelo OBS, float/double e áudio
local bit-exact, parâmetros/chunks, ganho nos pacotes com NDI simulado,
offline/bypass, GUI/automação e descarregamento/recarregamento. O workflow
também inicia o pacote com runtime NDI real e testa instalação/desinstalação
em pastas temporárias. O runtime simulado é somente fixture de teste e nunca
é copiado ao pacote; a DLL distribuída é conferida por hash contra o runtime
do pacote publicado do Transmitter.

Esses testes não substituem o teste dentro do REAPER/OBS nas duas máquinas.

## Créditos

Interface VST2 independente: Xaymar Dirks e contribuidores, Xaymar/vst2sdk,
BSD-3-Clause. O texto integral acompanha `LICENCA-ABI-VST2.txt`.
NDI e seus termos/avisos acompanham `AVISOS-NDI.md`. A licença do código
1Byte Labs permanece conforme o repositório principal. Este software não é
afiliado à Steinberg nem à Vizrt NDI AB.

[1bytelabs.com.br](https://1bytelabs.com.br)

## Diagnostico 0.2.1

O botao **Diagnostico NDI** exibe o executavel do host, caminho da DLL, etapa de falha e codigo Windows quando disponivel. Na janela de mensagem, Ctrl+C copia o relatorio completo. O runtime continua incluido no instalador; esta versao nao altera a fila, o ganho ou o processamento de audio.
