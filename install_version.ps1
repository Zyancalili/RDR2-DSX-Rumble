# Installs a released version of RDR2 DSX Rumble into the game folder - use it to go back to an older version.
#   powershell -ExecutionPolicy Bypass -File install_version.ps1 -Version 1.5
#   powershell -ExecutionPolicy Bypass -File install_version.ps1            (lists the available versions)
param([string]$Version = '', [string]$GameDir = 'E:\SteamLibrary\steamapps\common\Red Dead Redemption 2')

$releases = Join-Path $PSScriptRoot 'releases'
if (-not $Version) {
    'Available versions:'
    Get-ChildItem $releases -Filter 'RDR2DSXRumble_v*.zip' | ForEach-Object { '  ' + ($_.BaseName -replace 'RDR2DSXRumble_v', '') }
    'Run again with -Version <number>.'
    return
}
$zip = Join-Path $releases "RDR2DSXRumble_v$Version.zip"
if (-not (Test-Path $zip)) { throw "No release v$Version in $releases" }
if (Get-Process RDR2 -ErrorAction SilentlyContinue) { throw 'Close Red Dead Redemption 2 first.' }
if (-not (Test-Path (Join-Path $GameDir 'RDR2.exe'))) { throw "RDR2.exe not found in $GameDir" }

# keep the player's current settings: only the .asi is replaced unless there is no ini yet
$tmp = Join-Path $env:TEMP "rdr2dsx_v$Version"
Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
Expand-Archive $zip $tmp
Copy-Item (Join-Path $tmp 'RDR2DSXRumble.asi') $GameDir -Force
if (-not (Test-Path (Join-Path $GameDir 'RDR2DSXRumble.ini'))) { Copy-Item (Join-Path $tmp 'RDR2DSXRumble.ini') $GameDir }
Remove-Item $tmp -Recurse -Force
"Installed RDR2 DSX Rumble v$Version into $GameDir (your RDR2DSXRumble.ini was kept)."
