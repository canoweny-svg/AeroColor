; =============================================================================
;  Inno Setup 6 script — Windows x64 installer for AeroGlow Saturator.
;  Build:  iscc /DArtefacts="..\..\build-win\AeroGlow_artefacts\Release" /DVersion=1.0.0 AeroGlow.iss
;  Installs:
;    VST3        → C:\Program Files\Common Files\VST3\AeroGlow Saturator.vst3   (bundle folder)
;    AAX         → C:\Program Files\Common Files\Avid\Audio\Plug-Ins\           (only if built + PACE-signed)
;    Standalone  → C:\Program Files\AeroGlow Audio\AeroGlow Saturator\
;  Sign the resulting setup.exe with signtool (Authenticode) before distribution.
; =============================================================================
#ifndef Artefacts
  #define Artefacts "..\..\build-win\AeroGlow_artefacts\Release"
#endif
#ifndef Version
  #define Version "1.0.0"
#endif
#define Name "AeroGlow Saturator"

[Setup]
AppId={{6B0E5C1A-7A3F-4E7D-9C1B-AE6100000001}
AppName={#Name}
AppVersion={#Version}
AppPublisher=AeroGlow Audio
DefaultDirName={autopf}\AeroGlow Audio\{#Name}
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\..\dist\windows
OutputBaseFilename=AeroGlow-{#Version}-Windows-x64
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin

[Types]
Name: "full"; Description: "Full installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3";       Description: "VST3 plug-in";         Types: full custom; Flags: fixed
#if DirExists(Artefacts + "\AAX")
Name: "aax";        Description: "AAX plug-in (Pro Tools)"; Types: full custom
#endif
Name: "standalone"; Description: "Standalone application"; Types: full custom

[Files]
Source: "{#Artefacts}\VST3\{#Name}.vst3\*"; DestDir: "{commoncf64}\VST3\{#Name}.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
#if DirExists(Artefacts + "\AAX")
; AAX is included only when it was built (preprocessor check at installer build time)
Source: "{#Artefacts}\AAX\{#Name}.aaxplugin\*"; DestDir: "{commoncf64}\Avid\Audio\Plug-Ins\{#Name}.aaxplugin"; Components: aax; Flags: ignoreversion recursesubdirs createallsubdirs
#endif
Source: "{#Artefacts}\Standalone\{#Name}.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#Name}"; Filename: "{app}\{#Name}.exe"; Components: standalone
