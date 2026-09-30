@echo off
rem Builds the Windows installer dist\VHS-<version>-windows.exe. Requires Inno Setup 6 (iscc).
rem Pass --no-build to package the existing build.
cd /d "%~dp0"
if not "%1"=="--no-build" (
    call build_windows.bat
    if errorlevel 1 exit /b 1
)
for /f %%v in ('powershell -NoProfile -Command "(Select-String -Path CMakeLists.txt -Pattern 'project\(VHS VERSION ([0-9.]+)').Matches[0].Groups[1].Value"') do set VERSION=%%v
set ISCC=iscc
where iscc >nul 2>nul || set ISCC="%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
%ISCC% /Qp /DVersion=%VERSION% packaging\windows.iss
if errorlevel 1 exit /b 1
echo Built: dist\VHS-%VERSION%-windows.exe
