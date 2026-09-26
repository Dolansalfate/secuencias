; Instalador de Secuencias para Windows (Inno Setup 6). Lo arma GitHub Actions:
;   ISCC.exe /DVersion=0.3.0 windows\instalador.iss
; Instala por usuario (sin pedir administrador) en %LOCALAPPDATA%\Programs\Secuencias, con el
; instalador de los motores de IA y las ruedas de madmom precompiladas (carpeta wheels\).

#ifndef Version
  #define Version "0.0.0"
#endif

[Setup]
AppId={{7B0C6D7E-4E7B-4F53-9C2C-5E1D2A6B9F01}
AppName=Secuencias
AppVersion={#Version}
AppVerName=Secuencias {#Version}
AppPublisher=Secuencias
DefaultDirName={autopf}\Secuencias
DefaultGroupName=Secuencias
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=Secuencias-{#Version}-windows-setup
SetupIconFile=icono.ico
UninstallDisplayIcon={app}\Secuencias.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "Crear un acceso directo en el escritorio"; GroupDescription: "Accesos directos:"

[Files]
Source: "..\build\release-win\Secuencias_artefacts\Release\Secuencias.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\scripts\instalar-ia.ps1"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\dist\wheels\*.whl"; DestDir: "{app}\wheels"; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\LEEME.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Secuencias"; Filename: "{app}\Secuencias.exe"
Name: "{group}\Instalar motores de IA"; Filename: "powershell.exe"; Parameters: "-NoExit -ExecutionPolicy Bypass -File ""{app}\instalar-ia.ps1"""; WorkingDir: "{app}"
Name: "{autodesktop}\Secuencias"; Filename: "{app}\Secuencias.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Secuencias.exe"; Description: "Abrir Secuencias"; Flags: nowait postinstall skipifsilent
