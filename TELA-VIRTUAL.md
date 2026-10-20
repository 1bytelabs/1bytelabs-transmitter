# Tela virtual — versão experimental

A opção **Tela virtual** transmite um monitor criado pelo projeto open source
[VirtualDrivers / Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver).
O Transmitter continua portable. A pasta `virtual-display` inclui o utilitário oficial
Virtual Driver Control 25.7.23 com seus componentes originais, sem modificar
os arquivos assinados. Somente o driver exige instalação inicial no Windows.

## Primeiro teste (Windows x64)

1. Extraia **todo o pacote**, mantendo `virtual-display` ao lado do Transmitter.
2. Clique em **Configurar tela virtual**, junto à seleção do que transmitir.
3. Se não houver tela virtual, escolha **Sim** para abrir o Virtual Driver Control
   incluído e autorize como administrador. Na seção de vídeo, clique em **Install Driver**.
   Não é necessário instalar o driver de áudio. Não desative Secure Boot nem habilite modo de teste.
4. Abra as configurações de Tela pelo mesmo botão e selecione **Estender estes vídeos**.
   Mantenha o monitor físico como principal e configure o virtual em 1920 × 1080,
   SDR, inicialmente a 60 Hz.
5. No Transmitter, escolha **Tela virtual** e clique em **↻**.
6. Selecione o monitor virtual e inicie a transmissão normalmente.
   No modo protegido, autorize o receptor como de costume.

Quando uma tela virtual já é detectada, **Sim** abre as configurações de Tela
para ajustar resolução e extensão; **Não** abre o utilitário oficial para gerenciar
instalação, ativação e remoção. A instalação ainda é confirmada no utilitário original;
o botão não instala nem remove drivers silenciosamente.

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
A distribuição inclui a licença em `virtual-display/LICENSE-VIRTUAL-DISPLAY.txt`
e preserva os demais avisos e componentes do pacote original.
O build verifica o SHA-256 fixado do ZIP oficial e as assinaturas do utilitário
e dos catálogos antes de montar o pacote. Não é necessário certificado próprio
para esses componentes originais. O executável do Transmitter continua sem assinatura própria.
