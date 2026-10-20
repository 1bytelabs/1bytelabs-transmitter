#ifndef StageDir
  #error StageDir is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif
[Setup]
AppId={{6472EC74-6416-4649-844F-C1646CA170CF}
AppName=1Byte Labs Audio
AppVersion=0.4.0
AppPublisher=1Byte Labs
AppPublisherURL=https://1bytelabs.com.br
DefaultDirName={autopf}\1Byte Labs Audio
DefaultGroupName=1Byte Labs Audio
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir={#OutputDir}
OutputBaseFilename=1ByteLabs-Audio-0.4.0-Setup-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=1Byte Labs Audio
CloseApplications=yes

[Types]
Name: "full"; Description: "VST2 e fonte nativa OBS"
Name: "vstonly"; Description: "Somente VST2"
Name: "obsonly"; Description: "Somente fonte nativa OBS"
Name: "custom"; Description: "Personalizada"; Flags: iscustom

[Components]
Name: "vst"; Description: "VST2 - Enviar e Receber (REAPER e filtros OBS)"; Types: full vstonly
Name: "obs"; Description: "Fonte nativa OBS - Receber sem microfone"; Types: full obsonly

[Files]
Source: "{#StageDir}\1Byte Labs Audio.dll"; DestDir: "{code:GetVstDir}"; Components: vst; Flags: ignoreversion
Source: "{#StageDir}\Processing.NDI.Lib.x64.dll"; DestDir: "{code:GetVstDir}"; Components: vst; Flags: ignoreversion
Source: "{#StageDir}\obs\obs-plugins\64bit\1byte-labs-audio.dll"; DestDir: "{code:GetObsDir}\obs-plugins\64bit"; Components: obs; Flags: ignoreversion
Source: "{#StageDir}\obs\obs-plugins\64bit\1byte-audio-ndi\Processing.NDI.Lib.x64.dll"; DestDir: "{code:GetObsDir}\obs-plugins\64bit\1byte-audio-ndi"; Components: obs; Flags: ignoreversion
Source: "{#StageDir}\obs\data\obs-plugins\1byte-labs-audio\README.md"; DestDir: "{code:GetObsDir}\data\obs-plugins\1byte-labs-audio"; Components: obs; Flags: ignoreversion
Source: "{#StageDir}\LEIA-ME.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\AVISOS-NDI.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\LICENCA-ABI-VST2.txt"; DestDir: "{app}"; Components: vst; Flags: ignoreversion

[Code]
var
  VstPage, ObsPage: TInputDirWizardPage;

function GetBinaryTypeW(FileName: String; var BinaryType: LongWord): Boolean;
  external 'GetBinaryTypeW@kernel32.dll stdcall';

function NormalizeObsRoot(Path: String): String;
begin
  Result := RemoveBackslashUnlessRoot(Path);
  if FileExists(AddBackslash(Result) + 'obs64.exe') then
    Result := ExpandFileName(AddBackslash(Result) + '..\..');
end;

function ValidObsRoot(Path: String): Boolean;
var
  BinaryType: LongWord;
  ExePath: String;
begin
  ExePath := AddBackslash(Path) + 'bin\64bit\obs64.exe';
  Result := FileExists(ExePath);
  if Result then
    Result := GetBinaryTypeW(ExePath, BinaryType);
  if Result then
    Result := BinaryType = 6;
end;

function DetectObsRoot: String;
var
  Candidate: String;
begin
  Result := '';
  Candidate := GetPreviousData('ObsDir', '');
  if ValidObsRoot(Candidate) then begin Result := Candidate; Exit; end;
  if RegQueryStringValue(HKLM64, 'SOFTWARE\OBS Studio', '', Candidate) then
    if ValidObsRoot(NormalizeObsRoot(Candidate)) then begin Result := NormalizeObsRoot(Candidate); Exit; end;
  if RegQueryStringValue(HKCU64, 'SOFTWARE\OBS Studio', '', Candidate) then
    if ValidObsRoot(NormalizeObsRoot(Candidate)) then begin Result := NormalizeObsRoot(Candidate); Exit; end;
  if RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio', 'InstallLocation', Candidate) then
    if ValidObsRoot(NormalizeObsRoot(Candidate)) then begin Result := NormalizeObsRoot(Candidate); Exit; end;
  Candidate := ExpandConstant('{autopf}\obs-studio');
  if ValidObsRoot(Candidate) then Result := Candidate;
end;

procedure InitializeWizard;
begin
  VstPage := CreateInputDirPage(wpSelectComponents, 'Pasta do plugin VST2',
    'Escolha a pasta de plugins usada pelo REAPER e pelos filtros OBS.',
    'O instalador cria a pasta VST2 e copia o plugin e seu runtime NDI. Feche OBS e REAPER antes de continuar.', False, '');
  VstPage.Add('Destino VST2 (64 bits):');
  VstPage.Values[0] := GetPreviousData('VstDir', ExpandConstant('{autopf}\VSTPlugins\1Byte Labs'));
  if ExpandConstant('{param:VSTDIR|}') <> '' then
    VstPage.Values[0] := ExpandConstant('{param:VSTDIR|}');

  ObsPage := CreateInputDirPage(VstPage.ID, 'Pasta do OBS Studio',
    'Confirme o OBS instalado ou escolha outra instalacao, incluindo portable.',
    'Use Procurar para selecionar a raiz do OBS ou a pasta bin\64bit que contem obs64.exe. O instalador valida o executavel x64 antes de copiar.', False, '');
  ObsPage.Add('OBS instalado ou portable (64 bits):');
  ObsPage.Values[0] := DetectObsRoot;
  if ExpandConstant('{param:OBSDIR|}') <> '' then
    ObsPage.Values[0] := NormalizeObsRoot(ExpandConstant('{param:OBSDIR|}'));
end;

function GetVstDir(Param: String): String;
begin
  Result := VstPage.Values[0];
end;

function GetObsDir(Param: String): String;
begin
  Result := NormalizeObsRoot(ObsPage.Values[0]);
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := ((PageID = VstPage.ID) and not WizardIsComponentSelected('vst')) or
    ((PageID = ObsPage.ID) and not WizardIsComponentSelected('obs'));
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID = ObsPage.ID) and WizardIsComponentSelected('obs') then begin
    ObsPage.Values[0] := NormalizeObsRoot(ObsPage.Values[0]);
    Result := ValidObsRoot(ObsPage.Values[0]);
    if not Result then
      MsgBox('Selecione uma instalacao OBS x64 valida. Precisa existir bin\64bit\obs64.exe nessa pasta.', mbError, MB_OK);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := '';
  if WizardIsComponentSelected('obs') and not ValidObsRoot(GetObsDir('')) then
    Result := 'Pasta OBS invalida: escolha a raiz do OBS x64 ou bin\64bit com obs64.exe.';
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := MemoDirInfo + NewLine + NewLine + MemoComponentsInfo;
  if WizardIsComponentSelected('vst') then
    Result := Result + NewLine + NewLine + 'Plugin VST2:' + NewLine + Space + GetVstDir('');
  if WizardIsComponentSelected('obs') then
    Result := Result + NewLine + NewLine + 'OBS instalado ou portable:' + NewLine + Space + GetObsDir('');
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  if WizardIsComponentSelected('vst') then
    SetPreviousData(PreviousDataKey, 'VstDir', VstPage.Values[0]);
  if WizardIsComponentSelected('obs') then
    SetPreviousData(PreviousDataKey, 'ObsDir', GetObsDir(''));
end;
