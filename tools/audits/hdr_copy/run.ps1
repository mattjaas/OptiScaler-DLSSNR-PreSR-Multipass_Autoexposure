param(
    [string]$Python = 'python',
    [switch]$Timing,
    [ValidateSet('normal','aa')][string]$Control = 'normal',
    [ValidateSet('pilot','full')][string]$Profile = 'full'
)
$ErrorActionPreference = 'Stop'
# Run from a VS x64 Native Tools prompt (cl.exe). No process/GPU settings are changed.
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$out = Join-Path $root 'artifacts/nr-hdr-copy-audit'
Push-Location $root
try {
    & $Python (Join-Path $PSScriptRoot 'prepare.py') --out $out
    if ($LASTEXITCODE -ne 0) { throw 'Prototype preparation failed' }
    foreach ($name in @('quality','timing')) {
        & cl /nologo /std:c++17 /EHsc /O2 "/I$root/tests/nr_gpu" (Join-Path $PSScriptRoot "$name.cpp") "/Fe:$out/$name.exe" "/Fo:$out/$name.obj" /link d3d12.lib dxgi.lib
        if ($LASTEXITCODE -ne 0) { throw "Fixture compilation failed: $name" }
    }
    $baseline = Join-Path $out 'baseline'
    $candidate = Join-Path $out 'candidate'
    if ($Timing) {
        & (Join-Path $out 'timing.exe') $baseline $candidate (Join-Path $out "timing-$Control.csv") $Control $Profile
        if ($LASTEXITCODE -ne 0) { throw 'GPU timing failed' }
    } else {
        & (Join-Path $out 'quality.exe') $baseline $candidate
        if ($LASTEXITCODE -ne 0) { throw 'Small image audit failed' }
        & (Join-Path $out 'quality.exe') $baseline $candidate --4k
        if ($LASTEXITCODE -ne 0) { throw '4K image audit failed' }
    }
} finally { Pop-Location }
