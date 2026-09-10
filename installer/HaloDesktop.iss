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
#define AppPublisher "lastprojects"
#define AppWebsite "https://lastprojects.com"
#define AppExeName "HaloDesktop.exe"
#define RepositoryRoot ".."

#ifdef Arm64Build
  #define SourceRoot "..\ARM64\Release\HaloDesktop"
  #define InstallerArchitecture "arm64"
  #define OutputArchitecture "-ARM64"
#else
  #define SourceRoot "..\x64\Release\HaloDesktop"
  #define InstallerArchitecture "x64compatible"
  #define OutputArchitecture ""
#endif

[Setup]
; Derived from Halo's existing package identity so upgrades keep finding the
; previous installation. Never change this: a new value orphans installs.
AppId={{56fcb18b-d21c-4111-93fb-bef0ffa36c43}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppWebsite}
AppSupportURL={#AppWebsite}
AppUpdatesURL={#AppWebsite}
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}

; {autopf} follows PrivilegesRequired. With an all-users install below it is
; always Program Files.
DefaultDirName={autopf}\Halo Desktop
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto

; All-users only, deliberately.
;
; A per-user install drops an unsigned executable into a user-writable
; directory, registers a Start Menu shortcut, and writes an uninstall key,
; all without ever prompting. That combination is what Defender's
; machine-learning heuristics score as an unwanted installer, and it did:
; Trojan:Win32/Bearfoos.A!ml removed the executable, the shortcut, and the
; uninstall key from a per-user install, which reads to the user as the app
; crashing and its shortcut going dead. Installing under Program Files behind
; an elevation prompt removes that signal. It is not a substitute for signing.
;
; PrivilegesRequiredOverridesAllowed is deliberately absent: offering the
; per-user choice again would just restore the layout above.
PrivilegesRequired=admin

ArchitecturesAllowed={#InstallerArchitecture}
ArchitecturesInstallIn64BitMode={#InstallerArchitecture}
MinVersion=10.0.17763

Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

OutputDir=Output
OutputBaseFilename=HaloDesktop-{#AppVersion}{#OutputArchitecture}-Setup
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
; runasoriginaluser matters now that setup is elevated: without it the app
; inherits setup's administrator token, so it would run as administrator and
; resolve {localappdata} to the elevating account, writing its sign-in and
; downloads somewhere the user's own session will never read them again.
Filename: "{app}\{#AppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(AppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent runasoriginaluser

[Code]
// Setup used to install per-user, which put the files under
// %LOCALAPPDATA%\Programs and the uninstall entry in HKCU. Setup now runs
// elevated, and Inno only looks for a previous version of itself in HKLM, so
// that older install is invisible to this one: without this it survives as a
// stale copy on disk and a Start Menu entry pointing into it, alongside the
// new Program Files install. Remove it before laying the new one down.
//
// This GUID must stay in step with AppId above, which never changes.
const
  PerUserUninstallKey =
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\{56fcb18b-d21c-4111-93fb-bef0ffa36c43}_is1';

procedure RemovePreviousPerUserInstall;
var
  UninstallCommand: String;
  ResultCode: Integer;
begin
  // Setup is elevated, so HKCU is the elevating account's hive. That is the
  // same account in the ordinary single-user case; setup has no reliable way
  // to reach a different user's hive, so a per-user install belonging to
  // someone else is left alone rather than guessed at.
  if not RegQueryStringValue(HKEY_CURRENT_USER, PerUserUninstallKey,
                             'UninstallString', UninstallCommand) then
    Exit;

  UninstallCommand := RemoveQuotes(UninstallCommand);
  if not FileExists(UninstallCommand) then
    Exit;

  // SUPPRESSMSGBOXES is load-bearing, not tidiness: a silent uninstall makes
  // UninstallSilent true, which is what stops CurUninstallStepChanged below
  // from offering to delete the sign-in, settings and downloads that this
  // upgrade exists to preserve.
  Exec(UninstallCommand, '/SILENT /SUPPRESSMSGBOXES /NORESTART', '',
       SW_SHOW, ewWaitUntilTerminated, ResultCode);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then
    RemovePreviousPerUserInstall;
end;

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

    // The uninstaller is elevated, so this is the elevating account's
    // LocalAppData. That is the right directory whenever the person removing
    // Halo is the person who used it, and there is no dependable way to find
    // another account's data from here, so a mismatch keeps the data instead.
    DataDirectory := ExpandConstant('{localappdata}\Halo Desktop');
    if DirExists(DataDirectory) then
    begin
      if MsgBox('Also remove your Halo data?' + #13#10 + #13#10 +
                DataDirectory + #13#10 + #13#10 +
                'This removes your sign-in, settings, watch history, and downloads in Halo''s default folder.' + #13#10 +
                'Downloads in a folder you chose are kept.' + #13#10 +
                'Choose No to keep it for a future reinstall.',
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      begin
        DelTree(DataDirectory, True, True, True);
      end;
    end;
  end;
end;
