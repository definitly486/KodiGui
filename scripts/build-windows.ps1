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

# qmake places CONFIG+=release targets in release\\ by default on Windows.
$exeCandidates = @(
    (Join-Path $root "release\kodigui.exe"),
    (Join-Path $root "kodigui.exe")
)
$exe = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $exe) {
    throw "Build completed but kodigui.exe was not found in release\ or the project root."
}
Write-Host "[KodiGui] Build successful: $exe"
