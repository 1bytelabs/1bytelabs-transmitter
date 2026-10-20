#ifndef StageDir
  #error StageDir is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif
[Setup]
AppId={{6472EC74-6416-4649-844F-C1646CA170CF}
AppName=1Byte Labs Audio
AppVersion=0.3.1
AppPublisher=1Byte Labs
AppPublisherURL=https://1bytelabs.com.br
DefaultDirName={autopf}\1Byte Labs Audio
DefaultGroupName=1Byte Labs Audio
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir={#OutputDir}
OutputBaseFilename=1ByteLabs-Audio-0.3.1-VST2-Setup-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=1Byte Labs Audio (VST2)
CloseApplications=yes

[Files]
Source: "{#StageDir}\1Byte Labs Audio.dll"; DestDir: "{code:GetVstDir}"; Flags: ignoreversion
Source: "{#StageDir}\Processing.NDI.Lib.x64.dll"; DestDir: "{code:GetVstDir}"; Flags: ignoreversion
Source: "{#StageDir}\LEIA-ME.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\AVISOS-NDI.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\LICENCA-ABI-VST2.txt"; DestDir: "{app}"; Flags: ignoreversion

[Code]
var
  VstPage: TInputDirWizardPage;
procedure InitializeWizard;
begin
  VstPage := CreateInputDirPage(wpSelectDir, 'Pasta do plugin VST2',
    'Escolha a pasta de plugins usada pelo REAPER e pelo OBS.',
    'O instalador cria a pasta e copia o plugin e o runtime NDI. Feche o REAPER e o OBS antes de continuar.', False, '');
  VstPage.Add('Destino VST2 (64 bits):');
  VstPage.Values[0] := GetPreviousData('VstDir', ExpandConstant('{autopf}\VSTPlugins\1Byte Labs'));
  if ExpandConstant('{param:VSTDIR|}') <> '' then
    VstPage.Values[0] := ExpandConstant('{param:VSTDIR|}');
end;
function GetVstDir(Param: String): String;
begin
  Result := VstPage.Values[0];
end;
procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'VstDir', VstPage.Values[0]);
end;
