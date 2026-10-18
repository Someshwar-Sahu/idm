# ==============================================================================
# package.ps1 - Bundles IDM and all required DLLs into a standalone 'dist' folder
# ==============================================================================

$ErrorActionPreference = "Stop"

$QtBin    = if ($env:QtBin) { $env:QtBin } else { "C:\Qt\6.7.2\mingw_64\bin" }
$MinGWBin = if ($env:MinGWBin) { $env:MinGWBin } else { "C:\Qt\Tools\mingw1310_64\bin" }
$DistDir  = ".\dist"

Write-Host "==> [1/4] Preparing clean dist directory..." -ForegroundColor Cyan
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
& "$QtBin\windeployqt.exe" --release --no-translations --no-compiler-runtime "$DistDir\IDMan.exe"

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
