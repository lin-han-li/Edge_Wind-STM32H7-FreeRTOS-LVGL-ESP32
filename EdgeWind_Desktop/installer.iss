#define AppName "EdgeWind 管理端"
#define AppVersion "1.0.0"
#define AppPublisher "EdgeWind Team"
#define AppCompany "EdgeWind"

#define LocalLangFile SourcePath + "Languages\\ChineseSimplified.isl"
#if FileExists(LocalLangFile)
#define LangName "chinesesimplified"
#define LangFile LocalLangFile
#else
#define LangName "english"
#define LangFile "compiler:Default.isl"
#endif

; Allow overriding dist folder from command line:
;   ISCC /DDistDir="D:\Edge_Wind\Admin\dist" installer.iss
#ifndef DistDir
#define DistDir "dist"
#endif

[Setup]
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppName}
PrivilegesRequired=lowest
DefaultGroupName={#AppName}
DisableDirPage=no
WizardStyle=modern
OutputDir=installer
OutputBaseFilename=EdgeWind_Admin_Setup
Compression=lzma
SolidCompression=yes
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\EdgeWind_Admin.exe
VersionInfoVersion=1.0.0.0
VersionInfoCompany={#AppCompany}
VersionInfoDescription={#AppName}
VersionInfoProductName={#AppName}
#ifdef SetupIcon
SetupIconFile={#SetupIcon}
#endif

[Languages]
Name: "{#LangName}"; MessagesFile: "{#LangFile}"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; Flags: unchecked

[Files]
Source: "{#DistDir}\EdgeWind_Admin.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\EdgeWind 管理端"; Filename: "{app}\EdgeWind_Admin.exe"
Name: "{userdesktop}\EdgeWind 管理端"; Filename: "{app}\EdgeWind_Admin.exe"; Tasks: desktopicon; Check: not IsAdmin
Name: "{commondesktop}\EdgeWind 管理端"; Filename: "{app}\EdgeWind_Admin.exe"; Tasks: desktopicon; Check: IsAdmin

[Run]
Filename: "{app}\EdgeWind_Admin.exe"; Description: "运行 EdgeWind 管理端"; Flags: nowait postinstall skipifsilent

[Code]
const
  WebView2ClientGuid = '{F1E2F1E4-FE0B-4FCD-91C8-74F4EC06F9E6}';
  WebView2DownloadUrl = 'https://go.microsoft.com/fwlink/p/?LinkId=2124703';

function IsPathUnder(const Path, Base: string): Boolean;
var
  P: string;
  B: string;
begin
  P := Lowercase(AddBackslash(ExpandFileName(Path)));
  B := Lowercase(AddBackslash(ExpandFileName(Base)));
  Result := (Copy(P, 1, Length(B)) = B);
end;

function WebView2InstalledByRegistry(): Boolean;
var
  Version: string;
begin
  Result := False;
  if IsWin64 then
  begin
    Result := RegQueryStringValue(HKLM64, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\' + WebView2ClientGuid, 'pv', Version);
    if not Result then
      Result := RegQueryStringValue(HKLM32, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\' + WebView2ClientGuid, 'pv', Version);
  end
  else
  begin
    Result := RegQueryStringValue(HKLM, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\' + WebView2ClientGuid, 'pv', Version);
  end;
  if not Result then
    Result := RegQueryStringValue(HKCU, 'SOFTWARE\Microsoft\EdgeUpdate\Clients\' + WebView2ClientGuid, 'pv', Version);
end;

function WebView2ExecutableExistsIn(const BaseDir: string): Boolean;
var
  FindRec: TFindRec;
  SubDir: string;
begin
  Result := False;
  if not DirExists(BaseDir) then
    exit;
  if FindFirst(BaseDir + '\*', FindRec) then
  try
    repeat
      if (FindRec.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
      begin
        if (FindRec.Name <> '.') and (FindRec.Name <> '..') then
        begin
          SubDir := BaseDir + '\' + FindRec.Name;
          if FileExists(SubDir + '\msedgewebview2.exe') then
          begin
            Result := True;
            exit;
          end;
        end;
      end;
    until not FindNext(FindRec);
  finally
    FindClose(FindRec);
  end;
end;

function WebView2ExecutableExists(): Boolean;
begin
  Result := WebView2ExecutableExistsIn(ExpandConstant('{pf32}\Microsoft\EdgeWebView\Application'));
  if not Result then
    Result := WebView2ExecutableExistsIn(ExpandConstant('{pf}\Microsoft\EdgeWebView\Application'));
end;

function IsWebView2Installed(): Boolean;
begin
  Result := WebView2InstalledByRegistry();
  if not Result then
    Result := WebView2ExecutableExists();
end;

function DownloadWebView2(const InstallerPath: string): Boolean;
var
  ResultCode: Integer;
  PS: string;
begin
  PS := '-NoProfile -ExecutionPolicy Bypass -Command "& { $ProgressPreference=''SilentlyContinue''; Invoke-WebRequest -Uri ''' +
        WebView2DownloadUrl + ''' -OutFile ''' + InstallerPath + ''' }"';
  Result := Exec('powershell.exe', PS, '', SW_HIDE, ewWaitUntilTerminated, ResultCode)
            and (ResultCode = 0) and FileExists(InstallerPath);
end;

function InstallWebView2(const InstallerPath: string): Boolean;
var
  ResultCode: Integer;
begin
  Result := Exec(InstallerPath, '/install /silent /acceptlicense', '', SW_HIDE, ewWaitUntilTerminated, ResultCode)
            and ((ResultCode = 0) or (ResultCode = 3010));
end;

function InitializeSetup(): Boolean;
var
  InstallerPath: string;
begin
  Result := True;
  if not IsWebView2Installed() then
  begin
    MsgBox('未检测到 WebView2 运行时，将自动下载安装。', mbInformation, MB_OK);
    InstallerPath := ExpandConstant('{tmp}\WebView2RuntimeInstaller.exe');
    if not DownloadWebView2(InstallerPath) then
    begin
      MsgBox('WebView2 下载失败，请检查网络或手动安装后重试。' + #13#10 +
             '下载地址：' + WebView2DownloadUrl, mbError, MB_OK);
      Result := False;
      exit;
    end;
    if not InstallWebView2(InstallerPath) then
    begin
      MsgBox('WebView2 安装失败，请手动安装后再运行安装包。' + #13#10 +
             '下载地址：' + WebView2DownloadUrl, mbError, MB_OK);
      Result := False;
      exit;
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if not IsAdmin then
    begin
      MsgBox('提示：当前未使用管理员权限安装。' + #13#10 +
             '如需局域网设备访问，请右键以管理员身份运行一次 EdgeWind Admin，' +
             '以便自动添加防火墙规则。', mbInformation, MB_OK);
    end;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  TargetDir: string;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    TargetDir := WizardDirValue;
    if IsPathUnder(TargetDir, ExpandConstant('{pf}')) or
       IsPathUnder(TargetDir, ExpandConstant('{pf32}')) or
       IsPathUnder(TargetDir, ExpandConstant('{commonpf}')) or
       IsPathUnder(TargetDir, ExpandConstant('{win}')) or
       IsPathUnder(TargetDir, ExpandConstant('{sys}')) then
    begin
      if MsgBox('当前安装目录可能不可写，数据库可能无法保存。' + #13#10 +
                '建议选择可写目录（如 D:\Edge_Wind\EdgeWind Admin）。' + #13#10 +
                '是否继续？', mbConfirmation, MB_YESNO) = IDNO then
      begin
        Result := False;
        exit;
      end;
    end;
  end;
end;
