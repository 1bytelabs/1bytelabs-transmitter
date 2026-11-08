# Tela virtual — versão experimental

A opção **Monitor** também transmite a tela virtual criada pelo projeto open source
[VirtualDrivers / Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver).
O Transmitter continua portable. A pasta `virtual-display` inclui o utilitário oficial
Virtual Driver Control 25.7.23 com seus componentes originais, sem modificar
os arquivos assinados. Somente o driver exige instalação inicial no Windows.

## Instalador do Transmitter (Windows x64)

O instalador de teste instala o Transmitter para o usuário atual. Se precisar
do monitor virtual, marque **Instalar suporte à tela virtual**. A opção vem
desmarcada. No fim, o Virtual Driver Control abre como administrador; escolha
**Install Driver** na seção de vídeo se o driver ainda não estiver instalado.
Se o driver já estiver presente, basta fechar o utilitário. Depois, no Windows,
configure a tela como **Estender** e escolha o monitor **(virtual)** no Transmitter.

Você pode deixar essa opção desmarcada e configurar a tela mais tarde pelo
próprio Transmitter. Desinstalar o aplicativo preserva o driver, que pode ser
compartilhado com outros programas. Para removê-lo, pare a transmissão e use
**Uninstall Driver** na seção de vídeo do Virtual Driver Control, antes de
desinstalar o Transmitter.

A versão ZIP continua disponível. Os testes automatizados instalam e removem
o aplicativo, sem instalar o driver de vídeo. O fluxo com driver e o controle
automático da tela ainda precisam de validação em um computador Windows.

## Primeiro teste (Windows x64)

1. Extraia **todo o pacote**, mantendo `virtual-display` ao lado do Transmitter.
2. Clique em **Configurar tela virtual**, junto à seleção do que transmitir.
3. Se não houver tela virtual, escolha **Abrir Virtual Driver Control** para abrir o utilitário
   incluído e autorize como administrador. Na seção de vídeo, clique em **Install Driver**.
   Não é necessário instalar o driver de áudio. Não desative Secure Boot nem habilite modo de teste.
4. Abra as configurações de Tela pelo mesmo botão e selecione **Estender estes vídeos**.
   Mantenha o monitor físico como principal e configure o virtual em 1920 × 1080,
   SDR, inicialmente a 60 Hz.
5. No Transmitter, escolha **Monitor** e clique em **↻**.
6. Selecione o monitor virtual e inicie a transmissão normalmente.
   No modo protegido, autorize o receptor como de costume.

**Configurar tela virtual** abre um painel com controles para ativar ou desativar
a tela, abrir Tela no Windows ou abrir o Virtual Driver Control. A opção
**Desativar tela virtual ao sair** vem marcada e vale ao encerrar o aplicativo
normalmente, inclusive pela bandeja. O driver fica instalado. Se o Transmitter
desativar a tela, tenta reativá-la na próxima abertura. Ao reabrir, o Transmitter verifica se o monitor voltou à área de trabalho.
Se o driver iniciar com a tela desconectada, tenta estender a área de trabalho
até ela. Se o Windows não aplicar a alteração, use **Estender** nas
configurações de Tela.

O controle direto é limitado a uma tela virtual do driver suportado e requer
um monitor físico como principal. Se o Windows rejeitar a alteração, use
as configurações de Tela ou o utilitário oficial.

A lista de monitores inclui telas físicas e virtuais. A tela do adaptador
Virtual Display Driver / MttVDD aparece com a indicação **(virtual)**.
Escolha essa tela para transmitir seu conteúdo. Se ela não aparecer, confira
se o driver está instalado e a tela está estendida.
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
Se o aplicativo for encerrado à força, a tela pode permanecer ativa. Para remover
o driver, use o utilitário oficial após parar a transmissão.

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
