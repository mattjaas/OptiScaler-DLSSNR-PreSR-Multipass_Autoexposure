param([string]$Python = "python")
$ErrorActionPreference = "Stop"
Push-Location (Join-Path $PSScriptRoot "../OptiScaler/shaders/dlssnr/precompile")
try {
    foreach ($pitch in @(16,20)) {
        foreach ($kind in @("wide","pair","both")) {
            $wide = $kind -ne "pair"
            $width = if ($wide) { if ($pitch -eq 16) {28} else {34} } else {$pitch}
            $stem = "dlssnr_tiled_v16_${kind}${pitch}"
            $defines = @("-D","DLSSNR_TILED_FUSED=1","-D","DLSSNR_TILED_V10=1",
                         "-D","DLSSNR_TILED_V10_WEIGHTS=1","-D","DLSSNR_TILED_V13_MODE28=1",
                         "-D","DLSSNR_TILED_V14_RGB=1","-D","DLSSNR_TILED_PITCH=$width",
                         "-D","DLSSNR_TILE_HEIGHT=$pitch")
            if ($wide) { $defines += @("-D","DLSSNR_GROUP_X=16") }
            if ($kind -ne "wide") { $defines += @("-D","DLSSNR_TILED_V16_PAIR=1") }
            & ../../shader_tools/dxc.exe -T cs_6_0 -E CSMain -O3 -Qstrip_debug -Qstrip_reflect @defines dlssnr.hlsl -Fo "${stem}_Shader.cso" -Fc "${stem}.asm"
            if ($LASTEXITCODE -ne 0) { throw "DXC failed: $stem" }
            & $Python ../../shader_tools/create_header.py "${stem}_Shader.cso" "${stem}_Shader.h" "${stem}_cso"
            if ($LASTEXITCODE -ne 0) { throw "Header failed: $stem" }
            $floats = $width * $pitch * 3
            $lds = @(Select-String "${stem}.asm" -Pattern 'addrspace\(3\) global')
            if ($lds.Count -ne 1 -or $lds[0].Line -notmatch "\[$floats x float\]") { throw "Unexpected LDS: $stem" }
            Write-Host "$stem SHA256=$((Get-FileHash ${stem}_Shader.cso).Hash) LDS=$($floats*4)"
        }
    }
    $stem = "dlssnr_v16_low"
    & ../../shader_tools/dxc.exe -T cs_6_0 -E CSMain -O3 -Qstrip_debug -Qstrip_reflect -D DLSSNR_V16_MODE18=1 dlssnr.hlsl -Fo "${stem}_Shader.cso" -Fc "${stem}.asm"
    if ($LASTEXITCODE -ne 0) { throw "DXC failed: Mode18" }
    & $Python ../../shader_tools/create_header.py "${stem}_Shader.cso" "${stem}_Shader.h" "${stem}_cso"
    if ($LASTEXITCODE -ne 0) { throw "Header failed: Mode18" }
    if (Select-String "${stem}.asm" -Pattern 'addrspace\(3\) global') { throw "Mode18 must not allocate LDS" }
    Write-Host "$stem SHA256=$((Get-FileHash ${stem}_Shader.cso).Hash) LDS=0"
} finally { Pop-Location }
