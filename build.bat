@echo off
setlocal
rem Builds DR2AP.asi (the Archipelago plugin) (32-bit, Release) with Visual Studio 2022.
rem Needs the "Desktop development with C++" workload, which includes CMake.
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Could not find Visual Studio Installer ^(vswhere.exe^). Is Visual Studio 2022 installed?
  goto :fail
)
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
  echo Visual Studio was found, but not its C++ tools. Open Visual Studio Installer and add "Desktop development with C++".
  goto :fail
)
call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=x86 -host_arch=x64 >nul
where cmake >nul 2>nul
if errorlevel 1 (
  echo CMake was not found. Open Visual Studio Installer, Modify, and tick "C++ CMake tools for Windows".
  goto :fail
)

echo === Configuring ===
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
if errorlevel 1 goto :fail
echo === Building  ===
cmake --build build --config Release
if errorlevel 1 goto :fail

if not exist "build\Release\DR2AP.asi" (
  echo Build finished but build\Release\DR2AP.asi is missing.
  goto :fail
)
if not exist "build\Release\DR2AP_core.dll" (
  echo Build finished but build\Release\DR2AP_core.dll is missing.
  goto :fail
)
copy /y "build\Release\DR2AP.asi" "DR2AP.asi" >nul
copy /y "build\Release\DR2AP_core.dll" "DR2AP_core.dll" >nul
echo.
echo SUCCESS: DR2AP.asi and DR2AP_core.dll are in this folder.

rem Put the new plugin straight into the game folder when the game is in the usual Steam place.
rem The core can be replaced while the game runs (send "reload" afterwards); the loader cannot, and that is fine.
set "GAMEDIR=C:\Program Files (x86)\Steam\steamapps\common\Dead Rising 2"
if exist "%GAMEDIR%\deadrising2.exe" (
  copy /y "build\Release\DR2AP_core.dll" "%GAMEDIR%\DR2AP_core.dll" >nul 2>nul && echo Copied DR2AP_core.dll to the game folder.
  copy /y "build\Release\DR2AP.asi" "%GAMEDIR%\DR2AP.asi" >nul 2>nul && echo Copied DR2AP.asi to the game folder.
) else (
  echo Game folder not found at "%GAMEDIR%", so nothing was copied there.
)
echo.
pause
exit /b 0

:fail
echo.
echo BUILD FAILED. Copy the text above when asking for help.
echo.
pause
exit /b 1
