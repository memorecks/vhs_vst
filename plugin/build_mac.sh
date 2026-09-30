#!/bin/bash
# Builds universal (arm64 + x86_64) VST3, AU and Standalone. Pass --install to copy the
# plug-ins to ~/Library/Audio/Plug-Ins.
set -e
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel "$(sysctl -n hw.ncpu)"
echo "Built: build/VHS_artefacts/Release/{VST3,AU,Standalone}"
if [ "$1" == "--install" ]; then
    mkdir -p ~/Library/Audio/Plug-Ins/VST3 ~/Library/Audio/Plug-Ins/Components
    rm -rf ~/Library/Audio/Plug-Ins/VST3/VHS.vst3 ~/Library/Audio/Plug-Ins/Components/VHS.component
    cp -R build/VHS_artefacts/Release/VST3/VHS.vst3 ~/Library/Audio/Plug-Ins/VST3/
    cp -R build/VHS_artefacts/Release/AU/VHS.component ~/Library/Audio/Plug-Ins/Components/
    killall -9 AudioComponentRegistrar 2>/dev/null || true
    echo "Installed to ~/Library/Audio/Plug-Ins"
fi
