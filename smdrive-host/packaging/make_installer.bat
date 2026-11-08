@echo off
rem Build the installer with Inno Setup 6 (run build.bat deploy first).
setlocal
set "ISCC=D:\Program Files (x86)\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" ( echo [ERR] ISCC.exe not found: "%ISCC%" & exit /b 1 )
if not exist "%~dp0..\dist\smdrive-host.exe" (
    echo [ERR] ..\dist\smdrive-host.exe missing - run build.bat deploy first
    exit /b 1
)
"%ISCC%" "%~dp0app.iss" || exit /b 1
echo.
echo [OK] installer written to: %~dp0..\installer\
endlocal
