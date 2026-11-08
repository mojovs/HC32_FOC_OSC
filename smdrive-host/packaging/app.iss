; SMotorDrive 上位机安装脚本（Inno Setup 6）
; 用法：build.bat deploy 之后执行 packaging\make_installer.bat
; 源目录为 ..\dist（windeployqt 产出），输出到 ..\installer

#define AppName "SMotorDrive 上位机"
#define AppVersion "1.0.0"
#define AppExe "smdrive-host.exe"
#define AppPublisher "SMotorDrive"

[Setup]
AppId={{8E31C6A2-2C4B-4E1D-9F27-2A6B7C0D5E11}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\SMotorDrive Host
DefaultGroupName=SMotorDrive
DisableProgramGroupPage=yes
OutputDir=..\installer
OutputBaseFilename=smdrive-host-{#AppVersion}-setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=lowest
UninstallDisplayIcon={app}\{#AppExe}
VersionInfoVersion=1.0.0.0

[Languages]
Name: "chinese"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务:"; Flags: checkedonce
Name: "vcredist"; Description: "安装 Microsoft VC++ 运行库（首次安装建议勾选）"; GroupDescription: "运行库:"; Flags: checkedonce

[Files]
Source: "..\dist\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\卸载 {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "正在安装 VC++ 运行库…"; Flags: runhidden waituntilterminated skipifdoesntexist; Tasks: vcredist
Filename: "{app}\{#AppExe}"; Description: "立即启动 {#AppName}"; Flags: nowait postinstall skipifsilent
