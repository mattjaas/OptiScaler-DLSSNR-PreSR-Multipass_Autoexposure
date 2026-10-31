# Offline exposure research: executes existing production PSOs, never rebuilds live shaders/DLL.
param([switch]$Warp, [string]$Csv = 'artifacts/edit-analysis/exposure-study-gpu.csv',
      [Parameter(Mandatory=$true)][string[]]$DdsPaths)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Push-Location $root
try {
    New-Item -ItemType Directory -Force artifacts/edit-analysis | Out-Null
    $parent = Split-Path $Csv -Parent
    if ($parent) { New-Item -ItemType Directory -Force $parent | Out-Null }
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'MSVC not found' }
    $vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
    cmd /c "`"$vcvars`" >nul && cl /nologo /std:c++17 /EHsc /O2 tests/nr_gpu/edit_exposure_study.cpp /Fe:artifacts/edit_exposure_study.exe /Fo:artifacts/edit_exposure_study.obj /link d3d12.lib dxgi.lib"
    if ($LASTEXITCODE -ne 0) { throw 'Exposure study build failed' }
    $arguments = @('--csv', $Csv)
    if ($Warp) { $arguments += @('--warp', '--private-only') }
    # Main NR percentile PSO has a known WARP failure; the private PSO is supported there.
    $arguments += @($DdsPaths | Where-Object { $_ -notmatch '\.(depth|motion)\.dds$' })
    & ./artifacts/edit_exposure_study.exe @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Exposure study failed' }
} finally { Pop-Location }
