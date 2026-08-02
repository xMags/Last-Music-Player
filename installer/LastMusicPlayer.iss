; Inno Setup script for Last Music Player
; Builds a single, self-contained, per-user installer (no admin, no prerequisites).
; The app is built self-contained (all Windows App SDK runtime DLLs ship in the
; app folder), so this installer just packs the Release output.
;
; Compile:  ISCC.exe installer\LastMusicPlayer.iss
; Output:   installer\Output\LastMusicPlayer-1.1.0-Setup.exe

#define MyAppName "Last Music Player"
#define MyAppVersion "1.1.0"
#define MyAppPublisher "Debashis Das (Last Projects)"
#define MyAppURL "https://lastprojects.com/"
#define MyAppExeName "Last_Music_Player.exe"
#define MyAppId "{41601A2E-D487-4E17-83C3-F81B146BD9E6}"
; AUMID must match SetCurrentProcessExplicitAppUserModelID in App.xaml.cpp so the
; media/volume flyout (SMTC / Win+V) resolves the app name + icon.
#define MyAppUserModelId "LastMusicPlayer.App"
; Source folder holding the self-contained Release build (relative to this script).
#define BuildDir "..\x64\Release"

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
LicenseFile=..\TERMS.txt
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
; Ship the self-contained Release folder, excluding build-only artifacts and
; leftover MSIX-layout files. Everything else (exe, resources.pri, *.xbf, all
; runtime *.dll, Assets, Styles, MainWindow) is required at runtime.
Source: "{#BuildDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion; \
  Excludes: "*.pdb,*.exp,*.lib,*.winmd,*.appxrecipe,*.build.appxrecipe,AppxManifest.xml,Microsoft.Web.WebView2.Core.winmd,microsoft.system.package.metadata\*,LICENSE,TERMS.txt,THIRD-PARTY-NOTICES.txt,Licenses\*"
; Legal documents are sourced from the repository so a stale Release folder
; cannot omit them. LicenseFile above makes TERMS.txt acceptance mandatory.
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\TERMS.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
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
