# Build Nam Knobs (Standalone + VST3) with Visual Studio 2022, run the tests.
# Requires: Visual Studio 2022 (Desktop C++ workload), CMake >= 3.22, Git.
$ErrorActionPreference = "Stop"
Set-Location (Join-Path $PSScriptRoot "..")
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
Push-Location build
ctest -C Release --output-on-failure
Pop-Location
Write-Host "VST3:       build\Source\Plugin\NamKnobs_artefacts\Release\VST3\Nam Knobs.vst3"
Write-Host "Standalone: build\Source\Plugin\NamKnobs_artefacts\Release\Standalone\Nam Knobs.exe"
Write-Host "Install the VST3 by copying it to C:\Program Files\Common Files\VST3\"
