# Standalone tests of the shipped preview shader, smoother and production GPU lifetime.
# Only the game-wrapper identity adapter is stubbed: these fixtures use real D3D12 COM objects.
param([switch]$Hardware, [switch]$Timing, [string]$VcVars)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Push-Location $repo
try {
    if (!$VcVars) {
        $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
        $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $VcVars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
    }
    $out = 'artifacts/validation/gain-preview'
    New-Item -ItemType Directory -Force $out | Out-Null
    Set-Content "$out/Util.h" '#pragma once
namespace Util { template<class T> void* RealObjectIdentity(T* p) { return p; } }'
    cmd /c "`"$VcVars`" >nul && cl /nologo /std:c++20 /EHsc /O2 tests/nr_gain_preview_smoke.cpp /Fe:$out/cpu.exe /Fo:$out/cpu.obj"
    if ($LASTEXITCODE -ne 0) { throw 'Preview CPU compile failed' }
    & "./$out/cpu.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Preview CPU tests failed' }
    cmd /c "`"$VcVars`" >nul && cl /nologo /std:c++20 /EHsc /O2 /Itests/nr_gpu /I$out tests/nr_gpu/gain_preview_quality.cpp OptiScaler/dlssnr/DlssNr_GpuLifetime.cpp /Fe:$out/gpu.exe /Fo:$out/ /link d3d12.lib dxgi.lib ole32.lib"
    if ($LASTEXITCODE -ne 0) { throw 'Preview GPU/runtime compile failed' }
    if ($Timing) {
        & ./OptiScaler/shaders/shader_tools/dxc.exe -T cs_6_0 -E GainPreviewMain -O3 -Qstrip_debug -Qstrip_reflect -D DLSSNR_GAIN_PREVIEW_GROUPS=1 OptiScaler/shaders/dlssnr/superior/GainPreview.hlsl -Fo "$out/single_Shader.cso"
        if ($LASTEXITCODE -ne 0) { throw 'Single-group timing reference compile failed' }
    }
    $mode = if ($Timing) { '--timing' } elseif ($Hardware) { '--hardware' } else { '--warp' }
    & "./$out/gpu.exe" $mode
    if ($LASTEXITCODE -ne 0) { throw 'Preview GPU/runtime tests failed' }
} finally { Pop-Location }
