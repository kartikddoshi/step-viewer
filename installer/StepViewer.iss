; Inno Setup script for STEP Viewer
; Build with:  scripts\make-installer.ps1   (after scripts\build.ps1)

#define AppName      "STEP Viewer"
#define AppVersion   "1.0.0"
#define AppExe       "StepViewer.exe"
#define BuildOut     "..\build\out"
#define LicenseDir   "..\build\licenses"
; COM class of the Explorer thumbnail provider (must match src/thumb/ThumbProvider.cpp)
#define ThumbClsid   "{{7B1E6A52-3C1F-4F0B-9D2E-5A4C8E61B0D3}"
; IThumbnailProvider shell extension key
#define ThumbShellEx "{{e357fccd-a995-4576-b01f-234630154e96}"
#define ProgId       "StepViewer.StepFile"

[Setup]
AppId={{B4C1D7E2-6A3F-4E8B-9C21-7D5E3F0A1B42}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=STEP Viewer contributors
AppPublisherURL=https://github.com/kartikddoshi/step-viewer
AppSupportURL=https://github.com/kartikddoshi/step-viewer/issues
AppUpdatesURL=https://github.com/kartikddoshi/step-viewer/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Per-user install by default (no admin needed); user can choose "all users" in the dialog.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=..\dist
OutputBaseFilename=StepViewer-Setup-{#AppVersion}
SetupIconFile=..\resources\app.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "assoc"; Description: "Open .step / .stp files with {#AppName} (double-click)"; GroupDescription: "File types:"
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#BuildOut}\*"; DestDir: "{app}"; Excludes: "StepThumb.dll,*.lib,*.exp,*.pdb,*.ilk"; Flags: ignoreversion recursesubdirs createallsubdirs
; the thumbnail DLL may be held open by Explorer's COM Surrogate (dllhost.exe)
Source: "{#BuildOut}\StepThumb.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#LicenseDir}\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; ---- Explorer thumbnail provider (COM in-proc server). HKA = HKLM for all-users install, HKCU for per-user.
Root: HKA; Subkey: "Software\Classes\CLSID\{#ThumbClsid}"; ValueType: string; ValueData: "STEP Thumbnail Provider"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\CLSID\{#ThumbClsid}\InprocServer32"; ValueType: string; ValueData: "{app}\StepThumb.dll"
Root: HKA; Subkey: "Software\Classes\CLSID\{#ThumbClsid}\InprocServer32"; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"
Root: HKA; Subkey: "Software\Classes\.step\ShellEx\{#ThumbShellEx}"; ValueType: string; ValueData: "{#ThumbClsid}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\.stp\ShellEx\{#ThumbShellEx}"; ValueType: string; ValueData: "{#ThumbClsid}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.step\ShellEx\{#ThumbShellEx}"; ValueType: string; ValueData: "{#ThumbClsid}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\SystemFileAssociations\.stp\ShellEx\{#ThumbShellEx}"; ValueType: string; ValueData: "{#ThumbClsid}"; Flags: uninsdeletekey

; ---- ProgID for STEP files
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueData: "STEP 3D Model"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#ProgId}\DefaultIcon"; ValueType: string; ValueData: "{app}\{#AppExe},0"
Root: HKA; Subkey: "Software\Classes\{#ProgId}\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\{#ProgId}\ShellEx\{#ThumbShellEx}"; ValueType: string; ValueData: "{#ThumbClsid}"
; show the rendered thumbnail without an app-icon overlay
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: string; ValueName: "TypeOverlay"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\{#ProgId}"; ValueType: dword; ValueName: "Treatment"; ValueData: 0

; ---- "Open with" list
Root: HKA; Subkey: "Software\Classes\.step\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.stp\OpenWithProgids"; ValueType: string; ValueName: "{#ProgId}"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".step"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\SupportedTypes"; ValueType: string; ValueName: ".stp"; ValueData: ""
Root: HKA; Subkey: "Software\Classes\Applications\{#AppExe}\shell\open\command"; ValueType: string; ValueData: """{app}\{#AppExe}"" ""%1"""

; ---- default handler (optional task). Windows may still ask once if another app was chosen before.
Root: HKA; Subkey: "Software\Classes\.step"; ValueType: string; ValueData: "{#ProgId}"; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.stp"; ValueType: string; ValueData: "{#ProgId}"; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.step"; ValueType: string; ValueName: "PerceivedType"; ValueData: "document"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.stp"; ValueType: string; ValueName: "PerceivedType"; ValueData: "document"; Tasks: assoc

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent

[Code]
// Refresh the shell icon cache so the new file icon / thumbnails show up without a re-login.
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep = ssPostInstall then
    Exec(ExpandConstant('{sys}\ie4uinit.exe'), '-show', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;
