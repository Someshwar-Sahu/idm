; ==============================================================================
; installer.iss - Inno Setup Script for Internet Download Manager
; ==============================================================================

#define MyAppName "Internet Download Manager"
#define MyAppVersion "6.42"
#define MyAppPublisher "IDM Project Team"
#define MyAppExeName "IDMan.exe"

[Setup]
; Unique AppId generated for your app so Windows identifies it consistently
AppId={{D37E84B1-3E5A-4C21-888A-70A5F1C931B2}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\Internet Download Manager
DefaultGroupName={#MyAppName}
AllowNoIcons=yes

; Output file configuration
OutputDir=installer_output
OutputBaseFilename=IDMan_Setup_v6.42
SetupIconFile=src\gui_qt\idm.ico
UninstallDisplayIcon={app}\{#MyAppExeName}

; Compression settings (ultra LZMA2 compression for minimal setup size)
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern

; 64-bit Windows installation mode
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UsedUserAreasWarning=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "startup"; Description: "Launch IDM automatically when Windows starts"; GroupDescription: "System Integration:"

[Files]
; Packages the entire contents of the dist folder created by package.ps1
Source: "dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
; Start Menu shortcuts
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
; Desktop shortcut (optional task)
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Windows Startup registry entry (HKCU Run key)
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "IDMan"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: startup

; Chrome / Edge Native Messaging Host registration
Root: HKCU; Subkey: "Software\Google\Chrome\NativeMessagingHosts\com.idm.native_host"; ValueType: string; ValueData: "{app}\extension\com.idm.native_host.json"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Microsoft\Edge\NativeMessagingHosts\com.idm.native_host"; ValueType: string; ValueData: "{app}\extension\com.idm.native_host.json"; Flags: uninsdeletekey

[Run]
; Checkbox to launch IDM immediately after installation finishes
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
// Prevent installation or upgrade while IDMan is currently running
function InitializeSetup(): Boolean;
begin
  Result := True;
  if CheckForMutexes('Global\IDMan_SingleInstance_Mutex_Qt') then
  begin
    if MsgBox('Internet Download Manager is currently running.' + #13#10 + #13#10 +
              'Please close IDM before proceeding with the installation.' + #13#10 + #13#10 +
              'Click Retry after closing IDM, or Cancel to abort.',
              mbConfirmation, MB_RETRYCANCEL) = idRetry then
    begin
      Result := InitializeSetup();
    end
    else
    begin
      Result := False;
    end;
  end;
end;
