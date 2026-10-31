# Research-only shared CNN/transformer score; no production shader/blob changes.
param([switch]$Warp, [string]$TimingCsv = '', [string[]]$DdsPaths = @())
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    New-Item -ItemType Directory -Force artifacts/edit-analysis | Out-Null
    $dxc = 'OptiScaler/shaders/shader_tools/dxc.exe'
    foreach ($name in @('common64', 'common256', 'common512', 'common1024', 'common_tiled64')) {
        $threads = switch ($name) { 'common256' { 256 } 'common512' { 512 } 'common1024' { 1024 } default { 64 } }
        $tiled = if ($name -eq 'common_tiled64') { 1 } else { 0 }
        & $dxc -T cs_6_0 -E CommonProbe -O3 -D THREADS=$threads -D TILED=$tiled `
            tests/nr_gpu/edit_signal_common.hlsl -Fo "artifacts/edit-analysis/${name}_Shader.cso"
        if ($LASTEXITCODE -ne 0) { throw "DXC failed: $name" }
    }
    & $dxc -T cs_6_0 -E CommonReduce -O3 -D THREADS=64 tests/nr_gpu/edit_signal_common.hlsl `
        -Fo artifacts/edit-analysis/common_reduce_Shader.cso
    if ($LASTEXITCODE -ne 0) { throw 'DXC failed: common reduce' }
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'MSVC not found' }
    $vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
    cmd /c "`"$vcvars`" >nul && cl /nologo /std:c++17 /EHsc /O2 tests/nr_gpu/edit_signal_common.cpp /Fe:artifacts/edit_signal_common.exe /Fo:artifacts/edit_signal_common.obj /link d3d12.lib dxgi.lib"
    if ($LASTEXITCODE -ne 0) { throw 'Common probe build failed' }
    $arguments = @()
    if ($Warp) { $arguments += '--warp' }
    if ($TimingCsv) { $arguments += @('--timing', $TimingCsv) }
    $arguments += $DdsPaths
    & ./artifacts/edit_signal_common.exe @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Common probe validation failed' }
} finally { Pop-Location }
