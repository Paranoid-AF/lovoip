# Build lovoip.clap and lovoip-harness.exe with MSVC.
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

function Enter-MsvcEnv {
    if ($env:INCLUDE -and (Get-Command cl.exe -ErrorAction SilentlyContinue)) { return }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found; install Visual Studio Build Tools.' }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw 'No Visual Studio installation with C++ tools found.' }
    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    $envOut = cmd /c "`"$vcvars`" >NUL 2>&1 && set"
    foreach ($line in $envOut) {
        if ($line -match '^(.*?)=(.*)$') {
            [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2])
        }
    }
}

Enter-MsvcEnv

$buildDir = Join-Path $root 'build'
New-Item -ItemType Directory -Force $buildDir | Out-Null

$inc = @("/I`"$root\external\clap\include`"", "/I`"$root\include`"")
$common = @('/nologo', '/std:c++17', '/EHsc', '/W4', '/utf-8') + $inc
$opts = if ($Config -eq 'Release') { @('/O2', '/DNDEBUG') } else { @('/Od', '/Zi', '/DDEBUG') }

Push-Location $buildDir
try {
    Write-Host "Compiling lovoip.clap ($Config)..."
    & cl.exe @common @opts /LD "$root\src\plugin.cpp" "$root\src\dsp.cpp" /link /OUT:lovoip.clap /IMPLIB:lovoip.lib
    if ($LASTEXITCODE -ne 0) { throw "cl failed with exit code $LASTEXITCODE" }

    Write-Host "Compiling lovoip-harness.exe ($Config)..."
    & cl.exe @common @opts "$root\tests\harness.cpp" /link /OUT:lovoip-harness.exe
    if ($LASTEXITCODE -ne 0) { throw "cl failed with exit code $LASTEXITCODE" }
}
finally { Pop-Location }

Write-Host "Built: $buildDir\lovoip.clap"
Write-Host "Built: $buildDir\lovoip-harness.exe"
