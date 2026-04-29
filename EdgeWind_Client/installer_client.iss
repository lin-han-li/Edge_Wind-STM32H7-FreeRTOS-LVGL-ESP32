#define AppName "EdgeWind Client"
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
;   ISCC /DDistDir="D:\Edge_Wind\Client\dist" installer_client.iss
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
OutputDir=installer
OutputBaseFilename=EdgeWind_Client_Setup
Compression=lzma
SolidCompression=yes
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\EdgeWind_Client.exe
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
Name: "desktopicon"; Description: "Create a desktop icon"; Flags: unchecked

[Files]
Source: "{#DistDir}\EdgeWind_Client.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "edgewind_client.env"; DestDir: "{userappdata}\EdgeWind_Client"; Flags: onlyifdoesntexist

[Icons]
Name: "{group}\EdgeWind Client"; Filename: "{app}\EdgeWind_Client.exe"
Name: "{userdesktop}\EdgeWind Client"; Filename: "{app}\EdgeWind_Client.exe"; Tasks: desktopicon; Check: not IsAdmin
Name: "{commondesktop}\EdgeWind Client"; Filename: "{app}\EdgeWind_Client.exe"; Tasks: desktopicon; Check: IsAdmin

[Run]
Filename: "{app}\EdgeWind_Client.exe"; Description: "Launch EdgeWind Client"; Flags: nowait postinstall skipifsilent

[Code]
const
  WebView2ClientGuid = '{F1E2F1E4-FE0B-4FCD-91C8-74F4EC06F9E6}';
  WebView2DownloadUrl = 'https://go.microsoft.com/fwlink/p/?LinkId=2124703';

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
