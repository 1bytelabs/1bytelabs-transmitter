# Tela virtual — versão experimental

A opção **Tela virtual** transmite um monitor criado pelo projeto open source
[VirtualDrivers / Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver).
O Transmitter continua portable. O driver é um componente instalado separadamente.
Esta versão não inclui os binários do driver nem realiza instalação automática.

## Primeiro teste (Windows x64)

1. Clique em **Configurar tela virtual** e escolha **Sim** para abrir as releases oficiais.
2. Baixe o Virtual Driver Control oficial para x64 e instale o driver como administrador.
   Use uma distribuição assinada; não desative Secure Boot nem habilite modo de teste.
3. Clique novamente em **Configurar tela virtual** e escolha **Não** para abrir Tela no Windows.
4. Selecione **Estender estes vídeos**, mantenha o monitor físico como principal
   e configure o virtual em 1920 × 1080, SDR, inicialmente a 60 Hz.
5. No Transmitter, escolha **Tela virtual** e clique em **↻**.
6. Selecione o monitor virtual e inicie a transmissão normalmente.
   No modo protegido, autorize o receptor como de costume.

Somente monitores ativos do adaptador Virtual Display Driver / MttVDD aparecem
nessa categoria. Um monitor físico não é escolhido como substituto.
Se a lista estiver vazia, confira se o driver está instalado e a tela está estendida.
O driver pode exigir reinicialização ou o Visual C++ Redistributable.

## PowerPoint e outras janelas

No PowerPoint, escolha a tela virtual para a apresentação e habilite o Modo de
Exibição do Apresentador na tela física. Para outros programas, mova as janelas
com **Win + Shift + seta esquerda/direita** ou arraste-as para a segunda tela.
A prévia da tela virtual atualiza continuamente, mas ainda é somente visual:
ela não encaminha mouse ou teclado. A captura de monitor físico mantém a prévia
anterior desativada, e a de janela mantém a imagem de referência.
Tudo que estiver visível no monitor virtual faz parte da transmissão, sujeito
às proteções de privacidade existentes.

## Limitações e validação necessária

Esta integração precisa de teste em Windows real com PowerPoint, navegador,
vídeo e receptor NDI. Não foi validada em ARM. A alteração de posição/resolução
ou desconexão do monitor invalida a captura; pare o envio e atualize as fontes.
Ao encerrar, a tela virtual permanece sob controle do Windows/VDC. Para removê-la,
desative-a pelo utilitário oficial após parar a transmissão.

O projeto do driver alerta sobre conflitos com atualizações de GPU/chipset.
Siga sua documentação, inclusive a recomendação de desinstalar o driver antes
dessas atualizações. Não faça o primeiro teste durante uma transmissão ao vivo.

## Licença

O código do Virtual Display Driver está sob licença MIT:
https://github.com/VirtualDrivers/Virtual-Display-Driver/blob/master/LICENSE
Esta versão apenas abre o site oficial e detecta o monitor; não incorpora código
ou binários do driver. Uma futura redistribuição deverá preservar seus avisos
legais e validar a assinatura do pacote escolhido.
