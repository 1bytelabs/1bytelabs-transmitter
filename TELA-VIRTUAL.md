# Tela virtual — versão experimental

O Transmitter inclui o driver de vídeo original assinado do projeto
[VirtualDrivers](https://github.com/VirtualDrivers/Virtual-Display-Driver).
O instalador e o ZIP incluem o pacote de vídeo na pasta `virtual-display`.

## Instalar e usar

1. No instalador Windows x64, marque **Instalar suporte à tela virtual**
   ou, no Transmitter, abra **Configurar tela virtual → Instalar driver**.
   Na versão portable, extraia o ZIP inteiro antes de abrir o Transmitter.
2. Autorize a operação no Windows, se solicitado. Quando a tela estiver ativa,
   escolha **Monitor** e selecione a entrada marcada como **(virtual)**.
3. No PowerPoint, escolha a tela virtual para a apresentação e use o Modo de
   Exibição do Apresentador para ver suas anotações na tela física.

O painel mostra se a tela está ativa. Use **Ativar tela virtual** ou
**Desativar tela virtual** para controlar sua disponibilidade. A opção
**Desativar tela virtual ao sair** vem marcada; ao reabrir, o Transmitter
tenta restaurar o monitor. O monitor físico deve permanecer como principal.

Se a tela não aparecer ou não ativar, use **Reparar driver** no painel.
A operação reaplica o pacote assinado ao dispositivo existente. Se o Windows
pedir reinicialização, reinicie antes de continuar.

Use **Desinstalar driver** para remover a tela virtual do Windows. O Windows
preserva o pacote se ele ainda estiver em uso por outro dispositivo. Remover
só o aplicativo pelo Windows preserva o driver.

## Validação e limites

O teste automatizado instala e remove o aplicativo, sem alterar os drivers
do computador de build. Instalação, reparo e desinstalação do driver precisam
de teste em um Windows x64 real antes da publicação. A prévia é somente visual.
Se o Transmitter for encerrado à força, a tela virtual pode permanecer ativa.

O projeto do driver alerta sobre conflitos com atualizações de GPU/chipset.
Siga as instruções do projeto para essas atualizações.

## Licença

O Virtual Display Driver é distribuído sob a licença MIT. O texto acompanha
o pacote em `virtual-display/LICENSE-VIRTUAL-DISPLAY.txt`. O build verifica
o hash fixado do arquivo oficial e a assinatura do catálogo. Os arquivos
originais do driver de vídeo são copiados sem alteração.
