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

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  DriverControl: String;
begin
  if (CurStep <> ssPostInstall) or WizardSilent or
     not WizardIsTaskSelected('virtualdisplay') then
    Exit;

  MsgBox('O utilitário oficial será aberto. Na seção de vídeo, clique em ' +
    'Install Driver se o driver ainda não estiver instalado. ' +
    'Se ele já estiver instalado, pode fechar o utilitário. ' +
    'Não é necessário instalar o driver de áudio.', mbInformation, MB_OK);
  DriverControl := ExpandConstant('{app}\virtual-display\VDD Control.exe');
  if not ShellExec('runas', DriverControl, '',
    ExtractFileDir(DriverControl), SW_SHOWNORMAL, ewWaitUntilTerminated, ResultCode) then
    MsgBox('O utilitário da tela virtual não foi aberto. Você pode tentar novamente ' +
      'em Configurar tela virtual no Transmitter.', mbInformation, MB_OK);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usUninstall) and not UninstallSilent and
     FileExists(ExpandConstant('{app}\virtual-display\VDD Control.exe')) then
    MsgBox('O driver de tela virtual, se instalado, permanece no Windows. ' +
      'Para removê-lo, use Uninstall Driver na seção de vídeo do Virtual Driver Control.', mbInformation, MB_OK);
end;
