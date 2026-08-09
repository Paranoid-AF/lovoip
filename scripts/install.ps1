# Install lovoip.clap into the per-user CLAP plugin directory.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root 'build\lovoip.clap'
if (-not (Test-Path $src)) { throw 'build\lovoip.clap not found; run scripts\build.ps1 first.' }

$dstDir = Join-Path $env:LOCALAPPDATA 'Programs\Common\CLAP'
New-Item -ItemType Directory -Force $dstDir | Out-Null
Copy-Item $src (Join-Path $dstDir 'lovoip.clap') -Force
Write-Host "Installed lovoip.clap to $dstDir"
Write-Host 'Rescan plugins (or restart) in your CLAP host, e.g. Kushview Element.'
