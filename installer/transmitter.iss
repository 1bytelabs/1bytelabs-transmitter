#ifndef TransmitterVersion
  #error TransmitterVersion must be supplied by the build
#endif

[Setup]
AppId=1ByteLabs.Transmitter
AppName=1Byte Labs Transmitter
AppVersion={#TransmitterVersion}
AppPublisher=1Byte Labs
AppPublisherURL=https://1bytelabs.com.br/
DefaultDirName={localappdata}\Programs\1Byte Labs\Transmitter
DefaultGroupName=1Byte Labs
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
UsePreviousTasks=no
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0
OutputDir=..\dist-installer
OutputBaseFilename=1ByteLabs-Transmitter-Setup-Windows-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Tasks]
Name: "virtualdisplay"; Description: "Instalar suporte à tela virtual (opcional)"; Flags: unchecked

[Files]
Source: "..\dist-transmitter\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\1Byte Labs\1Byte Labs Transmitter"; Filename: "{app}\1Byte Labs Transmitter.exe"; WorkingDir: "{app}"

[InstallDelete]
Type: files; Name: "{app}\virtual-display\VDD Control.exe"

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if (CurStep <> ssPostInstall) or WizardSilent or
     not WizardIsTaskSelected('virtualdisplay') then
    Exit;
  if not ShellExec('runas', ExpandConstant('{app}\1Byte Labs Transmitter.exe'),
    '--install-virtual-driver', ExpandConstant('{app}'),
    SW_SHOWNORMAL, ewWaitUntilTerminated, ResultCode) then
    MsgBox('A instalação da tela virtual não foi autorizada. Você pode instalá-la depois pelo Transmitter.',
      mbInformation, MB_OK)
  else if ResultCode = 3010 then
    MsgBox('Reinicie o Windows para concluir a instalação da tela virtual.', mbInformation, MB_OK)
  else if ResultCode <> 0 then
    MsgBox('O Windows não instalou a tela virtual (código ' + IntToStr(ResultCode) +
      '). Você pode tentar novamente em Configurar tela virtual no Transmitter.', mbInformation, MB_OK);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usUninstall) and not UninstallSilent then
    MsgBox('A remoção do aplicativo preserva o driver de tela virtual. ' +
      'Se quiser removê-lo, use Desinstalar driver em Configurar tela virtual antes de continuar.',
      mbInformation, MB_OK);
end;
