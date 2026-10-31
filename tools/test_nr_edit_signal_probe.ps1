# Standalone research fixture; never changes production shaders or DLLs.
param([switch]$Warp, [string]$TimingCsv = '', [string[]]$DdsPaths = @())
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    New-Item -ItemType Directory -Force artifacts/edit-analysis | Out-Null
    $dxc = 'OptiScaler/shaders/shader_tools/dxc.exe'
    foreach ($name in @('count64', 'count256', 'hist256', 'tiled64', 'tiledhist64')) {
        $threads = if ($name -in @('count256', 'hist256')) { 256 } else { 64 }
        $histogram = if ($name -in @('hist256', 'tiledhist64')) { 1 } else { 0 }
        $tiled = if ($name -in @('tiled64', 'tiledhist64')) { 1 } else { 0 }
        & $dxc -T cs_6_0 -E Probe -O3 -D THREADS=$threads -D HISTOGRAM=$histogram -D TILED=$tiled `
            tests/nr_gpu/edit_signal_probe.hlsl -Fo "artifacts/edit-analysis/${name}_Shader.cso"
        if ($LASTEXITCODE -ne 0) { throw "DXC failed: $name" }
    }
    & $dxc -T cs_6_0 -E Reduce -O3 -D THREADS=64 tests/nr_gpu/edit_signal_probe.hlsl `
        -Fo artifacts/edit-analysis/reduce_Shader.cso
    if ($LASTEXITCODE -ne 0) { throw 'DXC failed: reduce' }
    & $dxc -T cs_6_0 -E Reduce -O3 -D THREADS=64 -D HISTOGRAM=1 tests/nr_gpu/edit_signal_probe.hlsl `
        -Fo artifacts/edit-analysis/reducehist_Shader.cso
    if ($LASTEXITCODE -ne 0) { throw 'DXC failed: reduce histogram' }
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'MSVC not found' }
    $vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
    cmd /c "`"$vcvars`" >nul && cl /nologo /std:c++17 /EHsc /O2 tests/nr_gpu/edit_signal_probe.cpp /Fe:artifacts/edit_signal_probe.exe /Fo:artifacts/edit_signal_probe.obj /link d3d12.lib dxgi.lib"
    if ($LASTEXITCODE -ne 0) { throw 'Probe build failed' }
    $arguments = @()
    if ($Warp) { $arguments += '--warp' }
    if ($TimingCsv) { $arguments += @('--timing', $TimingCsv) }
    $arguments += $DdsPaths
    & ./artifacts/edit_signal_probe.exe @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Probe validation failed' }
} finally { Pop-Location }
