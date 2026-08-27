; Halo installer.
;
; Build it with tools\Build-Installer.ps1 rather than by opening this file in
; the Inno Setup IDE. That script verifies the pinned native payloads and the
; dependency closure of the Release folder first, so the installer can only be
; produced from an output that is actually self-contained.
;
; AppVersion is supplied by the build script via /DAppVersion so the version
; lives in one place.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

#define AppName "Halo"
#define AppPublisher "Debashis"
#define AppExeName "HaloDesktop.exe"
#define SourceRoot "..\x64\Release\HaloDesktop"
#define RepositoryRoot ".."

[Setup]
; Derived from Halo's existing package identity so upgrades keep finding the
; previous installation. Never change this: a new value orphans installs.
AppId={{56fcb18b-d21c-4111-93fb-bef0ffa36c43}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
VersionInfoVersion={#AppVersion}

; {autopf} resolves to Program Files for an all-users install and to
; %LOCALAPPDATA%\Programs for a per-user one, so a single line serves both.
DefaultDirName={autopf}\Halo Desktop
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto

; Per-user by default so no elevation prompt appears; the user can still choose
; an all-users install from the privileges dialog.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763

Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

OutputDir=Output
OutputBaseFilename=HaloDesktop-{#AppVersion}-Setup
SetupIconFile={#RepositoryRoot}\src\HaloDesktop\Assets\Halo.ico
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName}
LicenseFile={#RepositoryRoot}\LICENSE

; Halo deliberately supports multiple instances, so there is no single-instance
; mutex to wait on. Restart Manager closes any running copies during an upgrade
; instead, and does not relaunch them; the post-install task handles launching.
CloseApplications=yes
CloseApplicationsFilter=*.exe
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Build-only metadata is excluded. The .winmd files are WinRT metadata used at
; compile time; a compiled C++ app does not read them at run time. resources.pri
; is NOT excluded: XAML cannot resolve ms-appx:/// without it.
Source: "{#SourceRoot}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; \
    Excludes: "*.pdb,*.lib,*.exp,*.winmd,*.ipdb,*.iobj,*.appxrecipe,AppxManifest.xml"

Source: "{#RepositoryRoot}\LICENSE"; DestDir: "{app}\licenses"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#RepositoryRoot}\licenses\TERMS.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#RepositoryRoot}\licenses\third-party\*"; DestDir: "{app}\licenses\third-party"; Flags: ignoreversion recursesubdirs createallsubdirs
; Halo redistributes libmpv under the LGPL, so the corresponding-source notice
; ships with the binary rather than only living in the repository.
Source: "{#RepositoryRoot}\external\mpv\CORRESPONDING-SOURCE.md"; DestDir: "{app}\licenses"; DestName: "libmpv-CORRESPONDING-SOURCE.md"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExeName}"; AppUserModelID: "HaloDesktop.App"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon; AppUserModelID: "HaloDesktop.App"

[Run]
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
// Halo's data lives outside {app}, so uninstall would otherwise leave it
// behind silently. Ask, defaulting to keeping it: someone uninstalling to
// reinstall a different build should not lose their sign-in and downloads,
// and the downloads directory can be very large to recreate.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDirectory: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    // A silent uninstall has nobody to answer the question. Deleting on an
    // assumed answer could destroy a sign-in and a downloads library, so
    // silence always means keep.
    if UninstallSilent then
      Exit;

    DataDirectory := ExpandConstant('{localappdata}\Halo Desktop');
    if DirExists(DataDirectory) then
    begin
      if MsgBox('Also remove your Halo data?' + #13#10 + #13#10 +
                DataDirectory + #13#10 + #13#10 +
                'This holds your sign-in, settings, watch history, and downloaded files.' + #13#10 +
                'Choose No to keep it for a future reinstall.',
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      begin
        DelTree(DataDirectory, True, True, True);
      end;
    end;
  end;
end;
