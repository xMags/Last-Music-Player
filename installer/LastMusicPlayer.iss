; Inno Setup script for Last Music Player
; Builds a single, self-contained, per-user installer (no admin, no prerequisites).
; The application folder is made by tools\package.ps1 (the Release build, its Qt
; libraries from windeployqt, and the MSVC runtime DLLs app-local), so this
; installer just packs that folder.
;
; Compile:  ISCC.exe installer\LastMusicPlayer.iss   (after tools\package.ps1)
; Output:   installer\Output\LastMusicPlayer-2.0.0-Setup.exe

#define MyAppName "Last Music Player"
#define MyAppVersion "2.0.0"
#define MyAppPublisher "Debashis Das (Last Projects)"
#define MyAppURL "https://lastprojects.com/"
#define MyAppExeName "Last_Music_Player.exe"
#define MyAppId "{41601A2E-D487-4E17-83C3-F81B146BD9E6}"
; AUMID must match core::AppIdentity::appUserModelId() (src\core\app\AppIdentity.cpp),
; which the app applies with SetCurrentProcessExplicitAppUserModelID, so the
; media/volume flyout (SMTC / Win+V) resolves the app name + icon.
#define MyAppUserModelId "LastMusicPlayer.App"
; Source folder holding the packaged application (relative to this script).
#define BuildDir "..\.build\qt\package"

[Setup]
AppId={{#MyAppId}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
VersionInfoVersion={#MyAppVersion}
; The source license is shown for reference, then the end-user terms require
; explicit acceptance before installation can continue.
InfoBeforeFile=..\LICENSE
LicenseFile=..\licenses\TERMS.txt
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; Per-user install by default (no UAC); user may switch to all-users in the dialog.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
; If a copy is running, prompt to close it (matches the app's single-instance mutex).
AppMutex=Local\LastMusicPlayer.SingleInstance
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
SetupIconFile={#BuildDir}\Assets\AppIcon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
OutputDir=Output
OutputBaseFilename=LastMusicPlayer-{#MyAppVersion}-Setup

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; The packaged folder: the exe, the Qt libraries and plugins, the QML modules,
; the MSVC runtime DLLs and Assets. Legal documents come from the repository
; below, so a stale package cannot omit them.
Source: "{#BuildDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion; \
  Excludes: "LICENSE,TERMS.txt,THIRD-PARTY-NOTICES.txt,Licenses\*"
; Legal documents are sourced from the repository so a stale Release folder
; cannot omit them. LicenseFile above makes TERMS.txt acceptance mandatory.
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\licenses\TERMS.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\licenses\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\licenses\third-party\*"; DestDir: "{app}\Licenses\ThirdParty"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
; Start Menu + (optional) Desktop shortcut, both carrying the AppUserModelID so
; Windows resolves the friendly app name/icon for media controls.
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; AppUserModelID: "{#MyAppUserModelId}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; AppUserModelID: "{#MyAppUserModelId}"; Tasks: desktopicon

[InstallDelete]
; Remove the legacy Start Menu shortcut older builds created on first launch
; (the app no longer self-creates it; the installer owns shortcuts now).
Type: files; Name: "{autoprograms}\Last Music.lnk"
; Upgrading from 1.1.0: its runtime files that 2.0.0 does not ship (the
; Windows App SDK and WinUI libraries, resource indexes, compiled markup,
; language resource folders), named one by one from a 1.1.0 install folder.
; Only {app} is touched; the data folder (%LOCALAPPDATA%\Last Music Player)
; is never deleted here.
Type: files; Name: "{app}\App.xbf"
Type: files; Name: "{app}\CoreMessagingXP.dll"
Type: files; Name: "{app}\DWriteCore.dll"
Type: files; Name: "{app}\DirectML.dll"
Type: files; Name: "{app}\DwmSceneI.dll"
Type: files; Name: "{app}\Last_Music_Player.pri"
Type: files; Name: "{app}\MRM.dll"
Type: files; Name: "{app}\Microsoft.DirectManipulation.dll"
Type: files; Name: "{app}\Microsoft.Graphics.Display.dll"
Type: files; Name: "{app}\Microsoft.Graphics.Imaging.dll"
Type: files; Name: "{app}\Microsoft.InputStateManager.dll"
Type: files; Name: "{app}\Microsoft.Internal.FrameworkUdk.dll"
Type: files; Name: "{app}\Microsoft.UI.Composition.OSSupport.dll"
Type: files; Name: "{app}\Microsoft.UI.Designer.dll"
Type: files; Name: "{app}\Microsoft.UI.Input.dll"
Type: files; Name: "{app}\Microsoft.UI.Windowing.Core.dll"
Type: files; Name: "{app}\Microsoft.UI.Windowing.dll"
Type: files; Name: "{app}\Microsoft.UI.Xaml.Controls.dll"
Type: files; Name: "{app}\Microsoft.UI.Xaml.Controls.pri"
Type: files; Name: "{app}\Microsoft.UI.Xaml.Internal.dll"
Type: files; Name: "{app}\Microsoft.UI.Xaml.Phone.dll"
Type: files; Name: "{app}\Microsoft.UI.dll"
Type: files; Name: "{app}\Microsoft.UI.pri"
Type: files; Name: "{app}\Microsoft.Web.WebView2.Core.dll"
Type: files; Name: "{app}\Microsoft.Windows.AI.ContentSafety.dll"
Type: files; Name: "{app}\Microsoft.Windows.AI.Imaging.dll"
Type: files; Name: "{app}\Microsoft.Windows.AI.MachineLearning.dll"
Type: files; Name: "{app}\Microsoft.Windows.AI.Text.dll"
Type: files; Name: "{app}\Microsoft.Windows.ApplicationModel.Resources.dll"
Type: files; Name: "{app}\Microsoft.Windows.Widgets.dll"
Type: files; Name: "{app}\Microsoft.Windows.Workloads.Resources.dll"
Type: files; Name: "{app}\Microsoft.Windows.Workloads.Resources_ec.dll"
Type: files; Name: "{app}\Microsoft.Windows.Workloads.dll"
Type: files; Name: "{app}\Microsoft.WindowsAppRuntime.Bootstrap.dll"
Type: files; Name: "{app}\Microsoft.WindowsAppRuntime.Insights.Resource.dll"
Type: files; Name: "{app}\Microsoft.WindowsAppRuntime.dll"
Type: files; Name: "{app}\Microsoft.WindowsAppRuntime.pri"
Type: files; Name: "{app}\Microsoft.ui.xaml.dll"
Type: files; Name: "{app}\Microsoft.ui.xaml.resources.19h1.dll"
Type: files; Name: "{app}\Microsoft.ui.xaml.resources.common.dll"
Type: files; Name: "{app}\PushNotificationsLongRunningTask.ProxyStub.dll"
Type: files; Name: "{app}\RestartAgent.exe"
Type: files; Name: "{app}\SessionHandleIPCProxyStub.dll"
Type: files; Name: "{app}\WinUIEdit.dll"
Type: files; Name: "{app}\WindowsAppRuntime.DeploymentExtensions.OneCore.dll"
Type: files; Name: "{app}\WindowsAppRuntime.png"
Type: files; Name: "{app}\WindowsAppSdk.AppxDeploymentExtensions.Desktop-EventLog-Instrumentation.dll"
Type: files; Name: "{app}\WindowsAppSdk.AppxDeploymentExtensions.Desktop.dll"
Type: files; Name: "{app}\dcompi.dll"
Type: files; Name: "{app}\dwmcorei.dll"
Type: files; Name: "{app}\marshal.dll"
Type: files; Name: "{app}\onnxruntime.dll"
Type: files; Name: "{app}\onnxruntime_providers_shared.dll"
Type: files; Name: "{app}\workloads.365.json"
Type: files; Name: "{app}\workloads.j32.json"
Type: files; Name: "{app}\workloads.json"
Type: files; Name: "{app}\workloads.lnl.json"
Type: files; Name: "{app}\workloads.qnn.json"
Type: files; Name: "{app}\workloads.stx.json"
Type: files; Name: "{app}\wuceffectsi.dll"
Type: filesandordirs; Name: "{app}\Microsoft.UI.Xaml"
Type: filesandordirs; Name: "{app}\NpuDetect"
Type: filesandordirs; Name: "{app}\af-ZA"
Type: filesandordirs; Name: "{app}\am-ET"
Type: filesandordirs; Name: "{app}\ar-SA"
Type: filesandordirs; Name: "{app}\as-IN"
Type: filesandordirs; Name: "{app}\az-Latn-AZ"
Type: filesandordirs; Name: "{app}\bg-BG"
Type: filesandordirs; Name: "{app}\bn-IN"
Type: filesandordirs; Name: "{app}\bs-Latn-BA"
Type: filesandordirs; Name: "{app}\ca-ES"
Type: filesandordirs; Name: "{app}\ca-Es-VALENCIA"
Type: filesandordirs; Name: "{app}\cs-CZ"
Type: filesandordirs; Name: "{app}\cy-GB"
Type: filesandordirs; Name: "{app}\da-DK"
Type: filesandordirs; Name: "{app}\de-DE"
Type: filesandordirs; Name: "{app}\el-GR"
Type: filesandordirs; Name: "{app}\en-GB"
Type: filesandordirs; Name: "{app}\en-us"
Type: filesandordirs; Name: "{app}\es-ES"
Type: filesandordirs; Name: "{app}\es-MX"
Type: filesandordirs; Name: "{app}\et-EE"
Type: filesandordirs; Name: "{app}\eu-ES"
Type: filesandordirs; Name: "{app}\fa-IR"
Type: filesandordirs; Name: "{app}\fi-FI"
Type: filesandordirs; Name: "{app}\fil-PH"
Type: filesandordirs; Name: "{app}\fr-CA"
Type: filesandordirs; Name: "{app}\fr-FR"
Type: filesandordirs; Name: "{app}\ga-IE"
Type: filesandordirs; Name: "{app}\gd-gb"
Type: filesandordirs; Name: "{app}\gl-ES"
Type: filesandordirs; Name: "{app}\gu-IN"
Type: filesandordirs; Name: "{app}\he-IL"
Type: filesandordirs; Name: "{app}\hi-IN"
Type: filesandordirs; Name: "{app}\hr-HR"
Type: filesandordirs; Name: "{app}\hu-HU"
Type: filesandordirs; Name: "{app}\hy-AM"
Type: filesandordirs; Name: "{app}\id-ID"
Type: filesandordirs; Name: "{app}\is-IS"
Type: filesandordirs; Name: "{app}\it-IT"
Type: filesandordirs; Name: "{app}\ja-JP"
Type: filesandordirs; Name: "{app}\ka-GE"
Type: filesandordirs; Name: "{app}\kk-KZ"
Type: filesandordirs; Name: "{app}\km-KH"
Type: filesandordirs; Name: "{app}\kn-IN"
Type: filesandordirs; Name: "{app}\ko-KR"
Type: filesandordirs; Name: "{app}\kok-IN"
Type: filesandordirs; Name: "{app}\lb-LU"
Type: filesandordirs; Name: "{app}\lo-LA"
Type: filesandordirs; Name: "{app}\lt-LT"
Type: filesandordirs; Name: "{app}\lv-LV"
Type: filesandordirs; Name: "{app}\mi-NZ"
Type: filesandordirs; Name: "{app}\mk-MK"
Type: filesandordirs; Name: "{app}\ml-IN"
Type: filesandordirs; Name: "{app}\mr-IN"
Type: filesandordirs; Name: "{app}\ms-MY"
Type: filesandordirs; Name: "{app}\mt-MT"
Type: filesandordirs; Name: "{app}\nb-NO"
Type: filesandordirs; Name: "{app}\ne-NP"
Type: filesandordirs; Name: "{app}\nl-NL"
Type: filesandordirs; Name: "{app}\nn-NO"
Type: filesandordirs; Name: "{app}\or-IN"
Type: filesandordirs; Name: "{app}\pa-IN"
Type: filesandordirs; Name: "{app}\pl-PL"
Type: filesandordirs; Name: "{app}\pt-BR"
Type: filesandordirs; Name: "{app}\pt-PT"
Type: filesandordirs; Name: "{app}\quz-PE"
Type: filesandordirs; Name: "{app}\ro-RO"
Type: filesandordirs; Name: "{app}\ru-RU"
Type: filesandordirs; Name: "{app}\sk-SK"
Type: filesandordirs; Name: "{app}\sl-SI"
Type: filesandordirs; Name: "{app}\sq-AL"
Type: filesandordirs; Name: "{app}\sr-Cyrl-BA"
Type: filesandordirs; Name: "{app}\sr-Cyrl-RS"
Type: filesandordirs; Name: "{app}\sr-Latn-RS"
Type: filesandordirs; Name: "{app}\src"
Type: filesandordirs; Name: "{app}\sv-SE"
Type: filesandordirs; Name: "{app}\ta-IN"
Type: filesandordirs; Name: "{app}\te-IN"
Type: filesandordirs; Name: "{app}\th-TH"
Type: filesandordirs; Name: "{app}\tr-TR"
Type: filesandordirs; Name: "{app}\tt-RU"
Type: filesandordirs; Name: "{app}\ug-CN"
Type: filesandordirs; Name: "{app}\uk-UA"
Type: filesandordirs; Name: "{app}\ur-PK"
Type: filesandordirs; Name: "{app}\uz-Latn-UZ"
Type: filesandordirs; Name: "{app}\vi-VN"
Type: filesandordirs; Name: "{app}\zh-CN"
Type: filesandordirs; Name: "{app}\zh-TW"

[UninstallDelete]
; Same legacy shortcut, in case it was created after install by an older binary.
Type: files; Name: "{autoprograms}\Last Music.lnk"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[Code]
// On uninstall, offer to also delete the per-user data folder
// (%LOCALAPPDATA%\Last Music Player\ — settings, library, cached artwork).
// Default is to KEEP it so reinstalls preserve the user's library.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: string;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    DataDir := ExpandConstant('{localappdata}\Last Music Player');
    if DirExists(DataDir) then
    begin
      if MsgBox('Also remove your Last Music Player settings and library data?' + #13#10 +
                '(' + DataDir + ')' + #13#10#13#10 +
                'Choose No to keep them for a future reinstall.',
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
        DelTree(DataDir, True, True, True);
    end;
  end;
end;
