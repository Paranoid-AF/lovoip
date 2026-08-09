# Render the CC0 test clips through lovoip at three quality settings.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$harness = Join-Path $root 'build\lovoip-harness.exe'
if (-not (Test-Path $harness)) { throw 'Harness not built; run scripts\build.ps1 first.' }
if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) { throw 'ffmpeg not found on PATH.' }

$outDir = Join-Path $root 'build\out'
New-Item -ItemType Directory -Force $outDir | Out-Null

$settings = @(8, 15, 22)

Get-ChildItem (Join-Path $root 'tests\voice_clips') -Filter *.mp3 | ForEach-Object {
    $clip = $_.BaseName
    $tmpWav = Join-Path $outDir "$clip.input.wav"
    & ffmpeg -y -loglevel error -i $_.FullName -ar 48000 -ac 1 $tmpWav
    if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed for $($_.Name)" }
    foreach ($q in $settings) {
        $out = Join-Path $outDir ("{0}_q{1}.wav" -f $clip, $q)
        & $harness $tmpWav $out $q | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "harness failed ($LASTEXITCODE) for $($_.Name) at quality $q" }
        Write-Host "wrote $out"
    }
}
