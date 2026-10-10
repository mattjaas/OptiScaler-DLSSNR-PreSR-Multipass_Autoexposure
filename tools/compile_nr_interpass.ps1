param([string]$Python = "python")
$ErrorActionPreference = "Stop"
Push-Location (Join-Path $PSScriptRoot "../OptiScaler/shaders/dlssnr/precompile")
try {
    foreach ($pitch in @(0,16,20,-1)) {
        $stem = if ($pitch -eq 0) {"DlssNr"} elseif ($pitch -eq -1) {"dlssnr_interpass_fast"} else {"dlssnr_interpass_rgb$pitch"}
        $defines = @()
        if ($pitch -eq -1) {
            $defines = @("-D","DLSSNR_INTERPASS_FAST=1")
        } elseif ($pitch) {
            $defines = @("-D","DLSSNR_TILED_FUSED=1","-D","DLSSNR_TILED_PITCH=$pitch",
                         "-D","DLSSNR_INTERPASS_WEIGHTS=1",
                         "-D","DLSSNR_INTERPASS_MODE28=1")
        }
        & ../../shader_tools/dxc.exe -T cs_6_0 -E CSMain -O3 -Qstrip_debug -Qstrip_reflect @defines dlssnr.hlsl -Fo "${stem}_Shader.cso" -Fc "${stem}.asm"
        if ($LASTEXITCODE -ne 0) { throw "DXC failed: $stem" }
        & $Python ../../shader_tools/create_header.py "${stem}_Shader.cso" "${stem}_Shader.h" "${stem}_cso"
        if ($LASTEXITCODE -ne 0) { throw "Header failed: $stem" }
        $header = Join-Path (Get-Location) "${stem}_Shader.h"
        [IO.File]::WriteAllLines($header, @([IO.File]::ReadAllLines($header) | ForEach-Object { $_.TrimEnd() }), [Text.UTF8Encoding]::new($false))
        $lds = @(Select-String "${stem}.asm" -Pattern 'addrspace\(3\) global')
        if ($pitch -eq -1) {
            if ($lds.Count -ne 0) { throw "Fast PSO unexpectedly allocates LDS" }
        } elseif ($pitch) {
            $floats = $pitch * $pitch * 3
            if ($lds.Count -ne 1 -or $lds[0].Line -notmatch "\[$floats x float\]") { throw "Unexpected LDS: $stem" }
        } # Standard shader keeps its existing exposure-meter LDS, without inter-pass cache.
        Write-Host "$stem SHA256=$((Get-FileHash ${stem}_Shader.cso).Hash)"
    }
} finally { Pop-Location }
