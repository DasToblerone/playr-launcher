@echo off
rem Baut SteamGameRandomizer.exe. Doppelklick genuegt, die exe liegt danach in diesem Ordner.
rem Voraussetzung: Visual Studio oder die "Build Tools fuer Visual Studio" mit C++-Werkzeugen.
setlocal
cd /d "%~dp0"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :novs
set "VSDIR="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto :novs

echo Richte die Compiler-Umgebung ein ...
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :fail

echo Baue Icon-Ressource ...
rc /nologo SteamGameRandomizer.rc
if errorlevel 1 goto :fail

echo Baue SteamGameRandomizer.exe ...
cl /nologo /EHsc /O2 /std:c++17 /utf-8 /W3 /MT SteamGameRandomizer.cpp /Fe:SteamGameRandomizer.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib advapi32.lib SteamGameRandomizer.res
if errorlevel 1 goto :fail

del /q SteamGameRandomizer.obj SteamGameRandomizer.res SteamGameRandomizer.exe.manifest 2>nul
echo.
echo Fertig: %~dp0SteamGameRandomizer.exe
goto :end

:novs
echo.
echo Es wurde kein Visual Studio mit C++-Werkzeugen gefunden.
echo Installiere die "Build Tools fuer Visual Studio" mit dem Workload "Desktopentwicklung mit C++".
echo Per Kommandozeile geht das so:
echo   winget install Microsoft.VisualStudio.2022.BuildTools --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
echo Danach dieses Skript noch einmal starten.
goto :fail

:fail
echo.
echo Das Bauen ist fehlgeschlagen.
if /i not "%~1"=="nopause" pause
exit /b 1

:end
if /i not "%~1"=="nopause" pause
exit /b 0
