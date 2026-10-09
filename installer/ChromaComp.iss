; Inno Setup script: builds "ChromaComp-Setup.exe", a normal Windows installer that copies the VST3 into
; C:\Program Files\Common Files\VST3 (where every DAW looks) and, optionally, the standalone app.
; The GitHub workflow builds it automatically (job "Windows installer"); to build it by hand, put the downloaded
; ChromaComp-Windows artifact in a folder called "payload" next to the "installer" folder and run ISCC on this file.

#define AppName "Chroma Comp"
#define AppVersion "1.0.0"

[Setup]
AppId={{6B0F1E52-3C0B-4D7A-9E55-C4A1D0B2F7A1}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=Him'z DSP
AppCopyright=Copyright (C) Him'z DSP
VersionInfoCompany=Him'z DSP
VersionInfoProductName={#AppName}
VersionInfoDescription={#AppName} Setup
VersionInfoVersion={#AppVersion}.0
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
OutputDir=Output
OutputBaseFilename=ChromaComp-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}

[Types]
Name: "full"; Description: "VST3 plugin + standalone application"
Name: "vst3"; Description: "VST3 plugin only"

[Components]
Name: "vst3"; Description: "VST3 plugin (installed to Common Files\VST3)"; Types: full vst3; Flags: fixed
Name: "standalone"; Description: "Standalone application"; Types: full

[Files]
Source: "..\payload\VST3\Chroma Comp.vst3\*"; DestDir: "{commoncf64}\VST3\Chroma Comp.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\payload\Standalone\Chroma Comp.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\Chroma Comp.exe"; Components: standalone
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\Chroma Comp.exe"; Description: "Start Chroma Comp"; Components: standalone; Flags: nowait postinstall skipifsilent unchecked
