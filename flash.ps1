#Requires -Version 5.1
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

function Find-IdfProfile {
    $candidates = @(
        "C:\Espressif\tools\Microsoft.v5.5.PowerShell_profile.ps1",
        "C:\Espressif\tools\Microsoft.v5.4.PowerShell_profile.ps1",
        "C:\Espressif\tools\Microsoft.v5.3.PowerShell_profile.ps1"
    )
    foreach ($p in $candidates) {
        if (Test-Path $p) { return $p }
    }
    $found = Get-ChildItem "C:\Espressif\tools" -Filter "Microsoft.v*.PowerShell_profile.ps1" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        Select-Object -First 1
    if ($found) { return $found.FullName }
    return $null
}

Write-Host ""
Write-Host "ESP32-S3 -> Switch Pro Controller"
Write-Host "================================="
Write-Host ""

$profilePath = Find-IdfProfile
if (-not $profilePath) {
    Write-Host "ESP-IDF is not installed yet."
    Write-Host "Go back to the README and finish Step 1, then run this script again."
    exit 1
}

Write-Host "Loading tools..."
. $profilePath

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    Write-Host "Could not load ESP-IDF tools. Open a new PowerShell and try again."
    exit 1
}

Write-Host ""
Write-Host "Ports Windows can see right now:"
$ports = @([System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
if ($ports.Count -eq 0) {
    Write-Host "  (none) - plug the board into the PC, then run this script again."
    exit 1
}
foreach ($p in $ports) { Write-Host "  $p" }
Write-Host ""

if ($ports.Count -eq 1) {
    $port = $ports[0]
    Write-Host "Using $port"
} else {
    $raw = Read-Host "Type the COM number to use (example: 7)"
    if ($raw -match '^\d+$') {
        $port = "COM$raw"
    } elseif ($raw -match '^COM\d+$') {
        $port = $raw.ToUpper()
    } else {
        Write-Host "That doesn't look like a COM port."
        exit 1
    }
}

Write-Host ""
Write-Host "Building (first time can take several minutes)..."
idf.py set-target esp32s3
idf.py build
if ($LASTEXITCODE -ne 0) {
    Write-Host "Build failed."
    exit 1
}

Write-Host ""
Write-Host "Flashing to $port ..."
idf.py -p $port flash
if ($LASTEXITCODE -ne 0) {
    Write-Host "Flash failed. Check the cable/port and try again."
    exit 1
}

Write-Host ""
Write-Host "Done."
Write-Host "1. Unplug the board from the PC if you want."
Write-Host "2. Plug the board port labeled USB into the Switch."
Write-Host "3. On the Switch: Controllers -> Change Grip/Order"
Write-Host ""
