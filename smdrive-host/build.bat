@echo off
rem ---------------------------------------------------------------------------
rem One-key build: Qt 5.15.2 / MSVC2019 x64  (see Qt dev plan section 3.1)
rem   build.bat            configure + build
rem   build.bat test       configure + build + ctest
rem   build.bat deploy     build + windeployqt into dist\
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion
set "ROOT=%~dp0"
set "VCVARS=D:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\VC\Auxiliary\Build\vcvarsall.bat"
set "CMAKEDIR=D:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake"
set "CMAKE=%CMAKEDIR%\CMake\bin\cmake.exe"
set "CTEST=%CMAKEDIR%\CMake\bin\ctest.exe"
set "NINJA=%CMAKEDIR%\Ninja\ninja.exe"
set "QTDIR=G:/Qt/5.15.2/msvc2019_64"
set "BUILD=%ROOT%build"

if not exist "%VCVARS%" ( echo [ERR] vcvarsall.bat not found: "%VCVARS%" & exit /b 1 )
if not exist "%CMAKE%"  ( echo [ERR] cmake.exe not found: "%CMAKE%" & exit /b 1 )
if not exist "%QTDIR%"   ( echo [ERR] Qt kit not found: %QTDIR% & exit /b 1 )

call "%VCVARS%" x86_amd64 >nul || exit /b 1
set "PATH=%QTDIR%\bin;%PATH%"

"%CMAKE%" -G Ninja -S "%ROOT%." -B "%BUILD%" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%QTDIR% -DCMAKE_MAKE_PROGRAM="%NINJA%" || exit /b 1

"%CMAKE%" --build "%BUILD%" || exit /b 1

if /I "%~1"=="test" (
    "%CTEST%" --test-dir "%BUILD%" --output-on-failure || exit /b 1
)

if /I "%~1"=="deploy" (
    set "DIST=%ROOT%dist"
    if exist "!DIST!" rd /s /q "!DIST!"
    mkdir "!DIST!"
    copy /y "%BUILD%\smdrive-host.exe" "!DIST!\" >nul
    "%QTDIR%\bin\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw "!DIST!\smdrive-host.exe" || exit /b 1
    rem headless (offscreen) and CI need these platform plugins; windeployqt ships qwindows only
    copy /y "%QTDIR%\plugins\platforms\qoffscreen.dll" "!DIST!\platforms\" >nul 2>&1
    copy /y "%QTDIR%\plugins\platforms\qminimal.dll" "!DIST!\platforms\" >nul 2>&1
    rem LGPLv3 compliance: ship Qt license texts alongside the Qt DLLs
    if not exist "!DIST!\licenses" mkdir "!DIST!\licenses"
    copy /y "G:\Qt\5.15.2\Src\LICENSE.LGPLv3" "!DIST!\licenses\LICENSE.LGPLv3.txt" >nul 2>&1
    copy /y "G:\Qt\5.15.2\Src\LICENSE.GPL3-EXCEPT" "!DIST!\licenses\LICENSE.GPL3-EXCEPT.txt" >nul 2>&1
    echo [OK] distributable dir: !DIST!
)

echo.
echo [OK] artifact: %BUILD%\smdrive-host.exe
echo      run needs %QTDIR%\bin in PATH, or use: build.bat deploy
endlocal
