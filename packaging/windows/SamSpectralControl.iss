; Windows installer of Sam Spectral Control (REL-2, ADR-0014): Inno Setup 6.
;
;   iscc /DAppVersion=1.0.0 /DArtefacts=<...\SamSpectralControl_artefacts\Release> /DOutputDir=<dir> SamSpectralControl.iss
;
; Installs the VST3 bundle to C:\Program Files\Common Files\VST3 and the licences, the manual and
; the uninstaller to C:\Program Files\Sam Audio\Sam Spectral Control. The plug-in binary is signed
; before this script runs and the installer after it (release workflow), when signing is set up.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef Artefacts
  #define Artefacts "..\..\build\src\SamSpectralControl_artefacts\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\build\installer"
#endif
#define AppName "Sam Spectral Control"
#define Publisher "Sam Audio"

[Setup]
; Never change AppId after the first release (ADR-0016): it identifies the installation.
AppId={{BBE8FCC0-05F6-4CCE-ACA0-867AE7F01824}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Publisher}
VersionInfoVersion={#AppVersion}
DefaultDirName={commonpf64}\{#Publisher}\{#AppName}
DisableDirPage=yes
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE.txt
OutputDir={#OutputDir}
OutputBaseFilename=SamSpectralControl-{#AppVersion}-Windows
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
PrivilegesRequired=admin
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName}
CloseApplications=no

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (C:\Program Files\Common Files\VST3)"; Types: full custom; Flags: fixed
Name: "docs"; Description: "User manual"; Types: full

[Files]
Source: "{#Artefacts}\VST3\{#AppName}.vst3\*"; DestDir: "{commoncf64}\VST3\{#AppName}.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\docs\manual.md"; DestDir: "{app}"; DestName: "Manual.md"; Components: docs; Flags: ignoreversion
Source: "..\..\docs\images\editor.png"; DestDir: "{app}\images"; Components: docs; Flags: ignoreversion
Source: "..\..\docs\images\editor-advanced.png"; DestDir: "{app}\images"; Components: docs; Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\{#AppName}.vst3"
