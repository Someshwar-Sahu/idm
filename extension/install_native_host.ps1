# PowerShell script to register IDM Native Messaging Host for Google Chrome & Microsoft Edge

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$manifestPath = Join-Path $scriptDir "com.idm.native_host.json"

if (-not (Test-Path $manifestPath)) {
    Write-Error "Native host manifest not found at: $manifestPath"
    exit 1
}

$manifestPath = (Resolve-Path $manifestPath).Path

Write-Host "Registering IDM Native Messaging Host..." -ForegroundColor Cyan
Write-Host "Manifest Path: $manifestPath" -ForegroundColor Gray

# 1. Google Chrome
$chromeKey = "HKCU:\Software\Google\Chrome\NativeMessagingHosts\com.idm.native_host"
New-Item -Path $chromeKey -Force | Out-Null
Set-ItemProperty -Path $chromeKey -Name "(Default)" -Value $manifestPath
Write-Host "[OK] Registered for Google Chrome ($chromeKey)" -ForegroundColor Green

# 2. Microsoft Edge
$edgeKey = "HKCU:\Software\Microsoft\Edge\NativeMessagingHosts\com.idm.native_host"
New-Item -Path $edgeKey -Force | Out-Null
Set-ItemProperty -Path $edgeKey -Name "(Default)" -Value $manifestPath
Write-Host "[OK] Registered for Microsoft Edge ($edgeKey)" -ForegroundColor Green

Write-Host "`nIDM Native Messaging Host registered successfully!" -ForegroundColor Green
