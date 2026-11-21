$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$qmake = Get-Command qmake -ErrorAction SilentlyContinue
if (-not $qmake) {
    throw "Qt 6 qmake was not found. Install Qt 6.8.3 MSVC 2022 x64 with Qt WebEngine."
}
$nmake = Get-Command nmake -ErrorAction SilentlyContinue
if (-not $nmake) {
    throw "nmake was not found. Run from a Visual Studio 2022 x64 developer environment."
}

Write-Host "[KodiGui] Generating the MSVC release build..."
& qmake .\kodigui.pro "CONFIG+=release"
if ($LASTEXITCODE -ne 0) { throw "qmake failed with exit code $LASTEXITCODE" }

Write-Host "[KodiGui] Compiling..."
& nmake /NOLOGO
if ($LASTEXITCODE -ne 0) { throw "nmake failed with exit code $LASTEXITCODE" }

if (-not (Test-Path .\kodigui.exe)) {
    throw "Build completed but kodigui.exe was not produced."
}
Write-Host "[KodiGui] Build successful: $root\kodigui.exe"
