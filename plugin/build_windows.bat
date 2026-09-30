@echo off
rem Builds the Windows VST3 (x64). Requires Visual Studio 2019/2022 with C++ and CMake 3.22+.
cd /d "%~dp0"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1
cmake --build build --config Release --parallel
if errorlevel 1 exit /b 1
echo Built: build\VHS_artefacts\Release\VST3\VHS.vst3
echo Copy VHS.vst3 to "C:\Program Files\Common Files\VST3" to install.
