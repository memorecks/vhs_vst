; Inno Setup script for the Windows installer (VST3 x64). Built by package_windows.bat:
;   iscc /DVersion=1.2.3 packaging\windows.iss   ->   dist\VHS-1.2.3-windows.exe
#ifndef Version
  #define Version "0.0.0"
#endif

[Setup]
AppId={{6A1D3C52-8E0B-4B7E-9C4E-5B6F0D2A7E31}
AppName=VHS
AppVersion={#Version}
AppPublisher=Memorecks
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=..\dist
OutputBaseFilename=VHS-{#Version}-windows
Compression=lzma2
SolidCompression=yes
UninstallDisplayName=VHS {#Version}

[Files]
Source: "..\build\VHS_artefacts\Release\VST3\VHS.vst3\*"; DestDir: "{commoncf64}\VST3\VHS.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\VHS.vst3"
