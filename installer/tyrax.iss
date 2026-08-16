; TyraX Windows installer (Inno Setup 7).
;
; WHAT AN INSTALLED TYRAX LOOKS LIKE, and why it is not just an .exe: the editor
; resolves several things RELATIVE TO ITS OWN BINARY, one directory up - the
; bundled Tyra engine it bind-mounts into the build container
; (templates.cpp: engineSourceDir, <exe>/../vendor/tyra), the PS2 deploy tools
; (runner.cpp: findTool, <exe>/../tools), the VS Code extension package
; (app.cpp: installVsCodeExtension) and the VU framework sources it copies into
; a project that has VU scripts (project.cpp: editorSourceDir, <exe>/../src).
; In a development checkout that "one directory up" is the repo root, because
; the binary sits in build/. So the installed layout reproduces the same shape:
;
;   <app>\bin\tyrax-editor.exe
;   <app>\vendor\tyra\...      the engine, as sources
;   <app>\tools\...            ps2client, the ps2link build scripts, the .vsix
;   <app>\src\...              the nine VU framework files a project is given
;   <app>\examples\...         optional component
;
; Shipping a bare .exe compiles nothing: the first game build would report a
; missing engine. If you add a new exe-relative lookup to the editor, add its
; files here in the same commit.
;
; Build it with installer\build-installer.ps1 (that script is where the version
; comes from - src/version.hpp is the single source of truth for it, here and in
; the release workflow alike).

#if VER < EncodeVer(7,0,0)
  #error TyraX's installer requires Inno Setup 7 or newer (jrsoftware.org).
#endif

#ifndef AppVersion
  ; Only ever seen when somebody opens this file straight in the IDE; every
  ; scripted build passes /DAppVersion=<x.y.z>.
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir ".."
#endif
#ifndef EditorExe
  #define EditorExe SourceDir + "\build\tyrax-editor.exe"
#endif

#define AppName "TyraX"
#define AppPublisher "doctorspider42"
#define AppUrl "https://github.com/doctorspider42/tyraX"

[Setup]
; Never change AppId: it is what makes an install an UPDATE of the previous one
; rather than a second copy beside it - which is the whole autoupdate story
; (docs/updates.md).
AppId={{7B1F5E62-9C24-4A3D-9E0B-3F5A6D8C2E71}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppUrl}
AppSupportURL={#AppUrl}/issues
AppUpdatesURL={#AppUrl}/releases
VersionInfoVersion={#AppVersion}.0
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
LicenseFile={#SourceDir}\LICENSE
OutputDir={#SourceDir}\dist
OutputBaseFilename=TyraX-Setup-{#AppVersion}
SetupIconFile={#SourceDir}\resources\icon.ico
UninstallDisplayIcon={app}\bin\tyrax-editor.exe
UninstallDisplayName={#AppName} {#AppVersion}
; Inno Setup 7's own look: the modern (white) wizard, following whatever
; light/dark mode Windows is in. `dynamic` is a 7.x appearance mode - one of the
; reasons this script requires 7 rather than merely tolerating it.
WizardStyle=modern dynamic
Compression=lzma2/max
SolidCompression=yes
; The editor is 64-bit (MinGW-w64) and there is no 32-bit build, so the
; installer is 64-bit too (SetupArchitecture is 7.x; the default is still a
; 32-bit Setup). Nothing here loads a DLL, which is the one thing a 64-bit
; Setup cannot do with 32-bit code.
SetupArchitecture=x64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Windows 10 1809 or newer. Not an arbitrary floor: the update check shells out
; to the system curl.exe, which arrived in 1803, and the editor is not tested
; on anything older.
MinVersion=10.0.17763
; PER-USER BY DEFAULT, AND THAT IS WHAT MAKES THE UPDATER WORK. An admin install
; would put a UAC prompt in the middle of every automatic update; installing
; into %LOCALAPPDATA%\Programs\TyraX asks for nothing. Someone who wants it in
; Program Files for everybody can still say so - the dialog offers it, and
; /ALLUSERS on the command line forces it.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
; An update is installed over a running editor's directory. The editor closes
; itself before it starts us (App::updateApply), but a second window, a headless
; --build or an Explorer preview can still be holding the exe - let the Restart
; Manager close them rather than failing on a locked file.
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Components]
Name: "editor"; Description: "TyraX editor, engine and PS2 tools"; Types: full compact custom; Flags: fixed
Name: "examples"; Description: "Example projects (~80 MB)"; Types: full

[Files]
Source: "{#EditorExe}"; DestDir: "{app}\bin"; Components: editor; Flags: ignoreversion
; The engine the editor compiles every game against, as sources - the build
; container bind-mounts this directory.
Source: "{#SourceDir}\vendor\tyra\*"; DestDir: "{app}\vendor\tyra"; Components: editor; \
    Flags: ignoreversion recursesubdirs createallsubdirs; \
    Excludes: "\.git\*,*.o,*.a,*.elf"
; ps2client.exe (network deploy), the ps2link build scripts and the VS Code
; extension package the editor installs on request.
Source: "{#SourceDir}\tools\*"; DestDir: "{app}\tools"; Components: editor; \
    Flags: ignoreversion recursesubdirs createallsubdirs; \
    Excludes: "\ps2link\work\*,*.o,*.a"
; The VU framework, copied into any project that has VU scripts. Exactly the
; nine files project.cpp's kVuFrameworkFiles lists - the probe it locates them
; with is src\vushader.hpp, so all nine travel or none do.
Source: "{#SourceDir}\src\vuir.hpp"; DestDir: "{app}\src"; Components: editor; Flags: ignoreversion
Source: "{#SourceDir}\src\vuir.cpp"; DestDir: "{app}\src"; Components: editor; Flags: ignoreversion
Source: "{#SourceDir}\src\vusim.hpp"; DestDir: "{app}\src"; Components: editor; Flags: ignoreversion
Source: "{#SourceDir}\src\vusim.cpp"; DestDir: "{app}\src"; Components: editor; Flags: ignoreversion
Source: "{#SourceDir}\src\vugen.hpp"; DestDir: "{app}\src"; Components: editor; Flags: ignoreversion
Source: "{#SourceDir}\src\vugen.cpp"; DestDir: "{app}\src"; Components: editor; Flags: ignoreversion
Source: "{#SourceDir}\src\vushader.hpp"; DestDir: "{app}\src"; Components: editor; Flags: ignoreversion
Source: "{#SourceDir}\src\vushader.cpp"; DestDir: "{app}\src"; Components: editor; Flags: ignoreversion
Source: "{#SourceDir}\src\vumain.cpp"; DestDir: "{app}\src"; Components: editor; Flags: ignoreversion
; The attribution a distributed build carries (LICENSE-EXCEPTION.md explains
; what a game made with it owes, which is nothing).
Source: "{#SourceDir}\LICENSE"; DestDir: "{app}"; Components: editor; Flags: ignoreversion
Source: "{#SourceDir}\LICENSE-EXCEPTION.md"; DestDir: "{app}"; Components: editor; Flags: ignoreversion
Source: "{#SourceDir}\NOTICE"; DestDir: "{app}"; Components: editor; Flags: ignoreversion
Source: "{#SourceDir}\THIRD-PARTY-LICENSES.md"; DestDir: "{app}"; Components: editor; Flags: ignoreversion
Source: "{#SourceDir}\README.md"; DestDir: "{app}"; Components: editor; Flags: ignoreversion
; Build output of an example that was opened in a dev checkout is not content -
; a fresh clone (what CI packages) has none of it either way.
Source: "{#SourceDir}\examples\*"; DestDir: "{app}\examples"; Components: examples; \
    Flags: ignoreversion recursesubdirs createallsubdirs; \
    Excludes: "\*\bin\*,\*\obj\*,*.elf,*.iso,*.history"

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\bin\tyrax-editor.exe"
Name: "{group}\{cm:UninstallProgram,{#AppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\bin\tyrax-editor.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\tyrax-editor.exe"; Description: "{cm:LaunchProgram,{#AppName}}"; \
    Flags: nowait postinstall skipifsilent
; The other half of the in-editor updater: it closes the editor, runs us
; /SILENT (where the finish page with its "Launch TyraX" checkbox never
; appears) and passes /RELAUNCH=1, which is what brings the editor back. Any
; other silent install - a deployment script, somebody's own automation - gets
; the old behaviour, because it passes no such parameter.
Filename: "{app}\bin\tyrax-editor.exe"; Flags: nowait postinstall; \
    Check: RelaunchAfterSilentUpdate

[UninstallDelete]
; Anything a game build wrote inside the installed examples. The editor's own
; settings (%LOCALAPPDATA%\tyra-editor) and the user's projects are deliberately
; left alone - uninstalling the editor is not a reason to delete somebody's work.
Type: filesandordirs; Name: "{app}\examples"

; Last, by convention: everything after this header is Pascal until the next
; section, and there is deliberately none.
[Code]
function RelaunchAfterSilentUpdate: Boolean;
begin
  Result := WizardSilent and (ExpandConstant('{param:RELAUNCH|0}') = '1');
end;
