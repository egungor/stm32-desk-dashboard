; Windows installer of the host programs (Inno Setup 6), issue #74.
;
; Per-user install, no administrator rights:
;   %LOCALAPPDATA%\Programs\STM32 Desk Dashboard\
;     dashboard-tray.exe   tray program (serves the controller)
;     dashboard-host.exe   console tools (list, alias, run, startup, ...)
;     LICENSE.txt, THIRD-PARTY-NOTICES.txt
; Start menu: the tray program and the uninstaller.
; Task "Start with Windows" (checked): the same Run value the tray menu and
; "dashboard-host startup on" write, so the tray menu shows it checked.
; After setup the tray program starts. Uninstall closes it and removes the
; Run value; preferences in %APPDATA%\stm32-desk-dashboard are kept.
;
; Build (installer/README.md):
;   iscc /DAppVersion=0.1.0 /DFileVersion=0.1.0.0 /DBinDir=<folder with the exes> stm32-desk-dashboard.iss

#ifndef AppVersion
  #define AppVersion "0.1.0-dev"
#endif
#ifndef FileVersion
  #define FileVersion "0.1.0.0"
#endif
#ifndef BinDir
  #define BinDir "..\build\host\Release"
#endif

#define AppName "STM32 Desk Dashboard"
#define TrayExe "dashboard-tray.exe"
#define HostExe "dashboard-host.exe"

[Setup]
AppId={{5D2524A6-DD16-474E-9E10-45FC837639E7}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=egungor
AppPublisherURL=https://github.com/egungor/stm32-desk-dashboard
VersionInfoVersion={#FileVersion}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=stm32-desk-dashboard-setup-{#AppVersion}
SetupIconFile=..\host\Platform\Windows\dashboard-tray.ico
UninstallDisplayIcon={app}\{#TrayExe}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; A running tray program is closed through the Restart Manager (it stops
; its service and saves the preferences on WM_ENDSESSION) and not
; restarted by Setup: the [Run] entry starts it again.
CloseApplications=yes
RestartApplications=no

[Languages]
; The privacy statement (README, "Privacy"; SignPath Foundation terms, #79)
; is shown before the install.
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"; InfoBeforeFile: "privacy-tr.txt"
Name: "english"; MessagesFile: "compiler:Default.isl"; InfoBeforeFile: "privacy-en.txt"

[CustomMessages]
turkish.StartWithWindows=Windows açılınca başlat
english.StartWithWindows=Start with Windows
turkish.StartNow=STM32 Masa Paneli'ni şimdi başlat
english.StartNow=Start STM32 Desk Dashboard now
turkish.TrayName=STM32 Masa Paneli
english.TrayName=STM32 Desk Dashboard

[Tasks]
Name: "autostart"; Description: "{cm:StartWithWindows}"

[Files]
Source: "{#BinDir}\{#TrayExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\{#HostExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\host\README.md"; DestDir: "{app}"; DestName: "README.md"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{cm:TrayName}"; Filename: "{app}\{#TrayExe}"
Name: "{group}\{cm:UninstallProgram,{cm:TrayName}}"; Filename: "{uninstallexe}"

[Registry]
; Same value name and quoting as WinStartup.cpp / Startup.cpp.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "STM32 Desk Dashboard"; ValueData: """{app}\{#TrayExe}"""; Tasks: autostart; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#TrayExe}"; Description: "{cm:StartNow}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
// Uninstall: close the tray program before its files go. taskkill without
// /F sends WM_CLOSE to its window, so it stops the service and saves the
// preferences first; the wait lets it exit. Nothing happens if it is not
// running.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    Exec(ExpandConstant('{sys}\taskkill.exe'), '/IM {#TrayExe}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Sleep(2000);
    // Still running (no answer to WM_CLOSE): end it; preferences are saved
    // within a second of every change anyway.
    Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM {#TrayExe}', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;

// Without the task, remove a Run value an earlier install or the tray menu
// may have written, so "Start with Windows" off really means off.
procedure CurStepChanged(CurStep: TSetupStep);
begin
  if (CurStep = ssPostInstall) and not WizardIsTaskSelected('autostart') then
    RegDeleteValue(HKEY_CURRENT_USER, 'Software\Microsoft\Windows\CurrentVersion\Run', 'STM32 Desk Dashboard');
end;
