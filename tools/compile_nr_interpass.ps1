param([string]$Python = "python")
$ErrorActionPreference = "Stop"
Push-Location (Join-Path $PSScriptRoot "../OptiScaler/shaders/dlssnr/precompile")
try {
    foreach ($pitch in @(0,16,20,-1,-2,-4,-5)) {
        $stem = switch ($pitch) {
            0 { "DlssNr" }
            -1 { "dlssnr_interpass_fast" }
            -2 { "dlssnr_interpass_low36" }
            -4 { "dlssnr_interpass_cross5" }
            -5 { "dlssnr_interpass_low36_cached" }
            default { "dlssnr_interpass_rgb$pitch" }
        }
        $defines = @()
        if ($pitch -eq -1) {
            $defines = @("-D","DLSSNR_INTERPASS_FAST=1")
        } elseif ($pitch -eq -2) {
            $defines = @("-D","DLSSNR_INTERPASS_LOW36=1")
        } elseif ($pitch -eq -5) {
            $defines = @("-D","DLSSNR_INTERPASS_LOW36=1","-D","DLSSNR_LOW36_CACHE=1")
        } elseif ($pitch -eq -4) {
            $defines = @("-D","DLSSNR_INTERPASS_CROSS5=1")
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
        if ($pitch -eq -5) {
            if ($lds.Count -ne 1 -or $lds[0].Line -notmatch "\[3468 x float\]") { throw "Unexpected Low36 LDS" }
        } elseif ($pitch -lt 0) {
            if ($lds.Count -ne 0) { throw "Fast PSO unexpectedly allocates LDS" }
        } elseif ($pitch) {
            $floats = $pitch * $pitch * 3
            if ($lds.Count -ne 1 -or $lds[0].Line -notmatch "\[$floats x float\]") { throw "Unexpected LDS: $stem" }
        } # Standard shader keeps its existing exposure-meter LDS, without inter-pass cache.
        Write-Host "$stem SHA256=$((Get-FileHash ${stem}_Shader.cso).Hash)"
    }
    foreach ($variant in @("carrier", "resolve", "cross5")) {
        $stem = "dlssnr_superior_$variant"
        $entry = if ($variant -eq "carrier") { "SuperiorCarrierMain" } else { "CSMain" }
        $defines = @("-D", "DLSSNR_SUPERIOR_SCALING=1")
        if ($variant -eq "cross5") { $defines += @("-D", "DLSSNR_INTERPASS_CROSS5=1") }
        & ../../shader_tools/dxc.exe -T cs_6_0 -E $entry -O3 -Qstrip_debug -Qstrip_reflect @defines dlssnr.hlsl -Fo "${stem}_Shader.cso" -Fc "${stem}.asm"
        if ($LASTEXITCODE -ne 0) { throw "DXC failed: $stem" }
        & $Python ../../shader_tools/create_header.py "${stem}_Shader.cso" "${stem}_Shader.h" "${stem}_cso"
        if ($LASTEXITCODE -ne 0) { throw "Header failed: $stem" }
        $header = Join-Path (Get-Location) "${stem}_Shader.h"
        [IO.File]::WriteAllLines($header, @([IO.File]::ReadAllLines($header) | ForEach-Object { $_.TrimEnd() }), [Text.UTF8Encoding]::new($false))
        if ($variant -eq "carrier" -and (Select-String "${stem}.asm" -Pattern 'addrspace\(3\) global')) { throw "Superior carrier unexpectedly uses LDS" }
        Write-Host "$stem SHA256=$((Get-FileHash ${stem}_Shader.cso).Hash)"
    }

} finally { Pop-Location }
