[Setup]
AppId=HS-STV
AppName=HS-STV
AppVersion={#AppVersion}
DefaultDirName={localappdata}\Programs\HS-STV
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.22000
OutputBaseFilename=HS-STV-Setup-x64
UninstallDisplayIcon={app}\bin\hs-stv.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Dirs]
Name: "{localappdata}\HS-STV"; Flags: uninsneveruninstall

[Files]
Source: "{#PackageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\HS-STV"; Filename: "{app}\bin\hs-stv.exe"; WorkingDir: "{localappdata}\HS-STV"
Name: "{autodesktop}\HS-STV"; Filename: "{app}\bin\hs-stv.exe"; WorkingDir: "{localappdata}\HS-STV"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\hs-stv.exe"; WorkingDir: "{localappdata}\HS-STV"; Description: "{cm:LaunchProgram,HS-STV}"; Flags: postinstall nowait skipifsilent
