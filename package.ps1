# ==============================================================================
# package.ps1 - Bundles IDM and all required DLLs into a standalone 'dist' folder
# ==============================================================================

$ErrorActionPreference = "Stop"

# Auto-resolve Qt bin folder containing windeployqt.exe
$QtBin = if ($env:QtBin -and (Test-Path (Join-Path $env:QtBin "windeployqt.exe"))) {
    $env:QtBin
} else {
    $found = (Get-Command windeployqt.exe -ErrorAction SilentlyContinue)
    if ($found) {
        $found.Source | Split-Path -Parent
    } else {
        $searchDirs = @($env:RUNNER_WORKSPACE, "D:\a", "C:\Qt") | Where-Object { $_ -and (Test-Path $_) }
        $match = Get-ChildItem -Path $searchDirs -Recurse -Filter "windeployqt.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($match) { $match.DirectoryName } else { "C:\Qt\6.7.2\mingw_64\bin" }
    }
}

# Auto-resolve MinGW bin folder containing runtime DLLs
$MinGWBin = if ($env:MinGWBin -and (Test-Path (Join-Path $env:MinGWBin "g++.exe"))) {
    $env:MinGWBin
} else {
    $searchDirs = @($env:RUNNER_WORKSPACE, "D:\a", "C:\Qt") | Where-Object { $_ -and (Test-Path $_) }
    $match = Get-ChildItem -Path $searchDirs -Recurse -Filter "g++.exe" -ErrorAction SilentlyContinue | Where-Object { $_.FullName -like "*mingw1310_64*" } | Select-Object -First 1
    if ($match) { $match.DirectoryName } else { "C:\Qt\Tools\mingw1310_64\bin" }
}

$DistDir = ".\dist"

Write-Host "==> Configured paths:" -ForegroundColor Cyan
Write-Host "    QtBin:    $QtBin"
Write-Host "    MinGWBin: $MinGWBin"

Write-Host "`n==> [1/4] Preparing clean dist directory..." -ForegroundColor Cyan
if (Test-Path $DistDir) {
    Remove-Item -Recurse -Force $DistDir
}
New-Item -ItemType Directory -Path $DistDir | Out-Null

Write-Host "==> [2/4] Copying application executables and assets..." -ForegroundColor Cyan
Copy-Item ".\build\IDMan.exe" -Destination "$DistDir\IDMan.exe"
if (Test-Path ".\build\NativeHost.exe") {
    Copy-Item ".\build\NativeHost.exe" -Destination "$DistDir\NativeHost.exe"
}
Copy-Item ".\src\gui_qt\idm.ico" -Destination "$DistDir\idm.ico"
Copy-Item -Recurse ".\extension" -Destination "$DistDir\extension"

Write-Host "==> [3/4] Running windeployqt to collect Qt6 DLLs and plugins..." -ForegroundColor Cyan
$env:PATH = "$QtBin;$MinGWBin;" + $env:PATH
$windeploy = Join-Path $QtBin "windeployqt.exe"
if (-not (Test-Path $windeploy)) {
    $cmd = (Get-Command windeployqt.exe -ErrorAction SilentlyContinue)
    if ($cmd) { $windeploy = $cmd.Source }
}
Write-Host "    Executing: $windeploy"
& "$windeploy" --release --no-translations --no-compiler-runtime "$DistDir\IDMan.exe"

Write-Host "==> [4/4] Copying GCC/MinGW C++ runtime libraries..." -ForegroundColor Cyan
$runtimes = @(
    "libgcc_s_seh-1.dll",
    "libstdc++-6.dll",
    "libwinpthread-1.dll"
)
foreach ($dll in $runtimes) {
    $src = Join-Path $MinGWBin $dll
    if (Test-Path $src) {
        Copy-Item $src -Destination "$DistDir\$dll"
        Write-Host "    Copied: $dll"
    } else {
        Write-Warning "Runtime DLL not found: $src"
    }
}

Write-Host "`n[SUCCESS] 'dist' folder is fully prepared for installer packaging!" -ForegroundColor Green
