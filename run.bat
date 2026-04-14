@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "BUILD_DIR=build\debug"
set "CONFIG=Debug"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not defined VSCMD_ARG_TGT_ARCH (
  if exist "!VSWHERE!" (
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
  )
  if defined VSINSTALL (
    call "!VSINSTALL!\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
    if errorlevel 1 exit /b %ERRORLEVEL%
  ) else (
    echo Could not find Visual Studio C++ tools. Run this from a Developer Command Prompt or install MSVC build tools.
    exit /b 1
  )
)

if not exist "%BUILD_DIR%\CMakeCache.txt" goto configure
findstr /C:"CMAKE_BUILD_TYPE:STRING=%CONFIG%" "%BUILD_DIR%\CMakeCache.txt" >nul 2>nul
if errorlevel 1 goto configure
goto build

:configure
cmake -S . -B "%BUILD_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=%CONFIG%
if errorlevel 1 exit /b %ERRORLEVEL%

:build
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel 8
if errorlevel 1 exit /b %ERRORLEVEL%

"%BUILD_DIR%\flecs_engine.exe" %*
