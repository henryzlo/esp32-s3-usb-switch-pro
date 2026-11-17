#Requires -Version 5.1
$ErrorActionPreference = "Stop"

Write-Host ""
Write-Host "PC smoke test (no Switch needed)"
Write-Host "================================"
Write-Host ""
Write-Host "This only checks that Windows can see the board as a Pro Controller."
Write-Host "Plug the board port labeled USB into this PC, then press Enter."
Write-Host "(COM can stay plugged in too.)"
Write-Host ""
Read-Host "Press Enter when USB is plugged into the PC"

$expectedVid = "VID_057E"
$expectedPid = "PID_2009"

$matches = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object {
    $_.InstanceId -match "$expectedVid.*$expectedPid" -or
    $_.FriendlyName -match "Pro Controller|Nintendo"
}

Write-Host ""
if (-not $matches) {
    Write-Host "FAIL: Windows does not see a Nintendo Pro Controller (057E:2009)."
    Write-Host ""
    Write-Host "Try this:"
    Write-Host "  1. Use the board port labeled USB (not COM)"
    Write-Host "  2. Use a data USB cable"
    Write-Host "  3. Unplug/replug USB"
    Write-Host "  4. Run this script again"
    Write-Host ""
    Write-Host "Devices that look USB-related right now:"
    Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
        Where-Object { $_.InstanceId -like "USB\VID_*" } |
        Select-Object -ExpandProperty FriendlyName -Unique |
        ForEach-Object { Write-Host "  - $_" }
    exit 1
}

Write-Host "PASS: Windows can see the controller."
Write-Host ""
$matches | Select-Object Status, Class, FriendlyName, InstanceId | Format-List
Write-Host "What this means:"
Write-Host "  - The firmware is running"
Write-Host "  - The USB port is the right one"
Write-Host "  - The board is advertising itself as a Pro Controller"
Write-Host ""
Write-Host "What this does NOT prove:"
Write-Host "  - Full Switch handshake / in-game buttons"
Write-Host "  - For that, use a real Switch (Change Grip/Order)"
Write-Host ""
