#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define Clsid "{{6d4d8ef0-3e69-4f5d-8d5a-1f8cb0f2b9c4}"

[Setup]
AppId={{B7E1C3A2-5F0D-4C57-9B1E-3A6F2D8E4C10}
AppName=ezExplorerFileDropMenu
AppVersion={#AppVersion}
DefaultDirName={localappdata}\ezExplorerFileDropMenu
DisableProgramGroupPage=yes
DisableDirPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
OutputDir=..\artifacts\installer
OutputBaseFilename=ezExplorerFileDropMenu-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes

[Dirs]
Name: "{app}\logs"

[Files]
Source: "..\artifacts\release\ezExplorerFileDropMenu.dll"; DestDir: "{app}"; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "..\artifacts\release\menu.sample.json"; DestName: "menu.json"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall

[Registry]
Root: HKCU; Subkey: "Software\Classes\CLSID\{#Clsid}"; ValueType: string; ValueData: "ezExplorerFileDropMenu"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\CLSID\{#Clsid}\InProcServer32"; ValueType: string; ValueData: "{app}\ezExplorerFileDropMenu.dll"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\CLSID\{#Clsid}\InProcServer32"; ValueType: string; ValueName: "ThreadingModel"; ValueData: "Apartment"
Root: HKCU; Subkey: "Software\Classes\Directory\shellex\DragDropHandlers\ezExplorerFileDropMenu"; ValueType: string; ValueData: "{#Clsid}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Folder\shellex\DragDropHandlers\ezExplorerFileDropMenu"; ValueType: string; ValueData: "{#Clsid}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Directory\Background\shellex\DragDropHandlers\ezExplorerFileDropMenu"; ValueType: string; ValueData: "{#Clsid}"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\Drive\shellex\DragDropHandlers\ezExplorerFileDropMenu"; ValueType: string; ValueData: "{#Clsid}"; Flags: uninsdeletekey
