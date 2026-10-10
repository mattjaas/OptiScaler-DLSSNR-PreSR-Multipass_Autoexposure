param([string]$Python = "python")
$ErrorActionPreference = "Stop"
$shaderDir = Join-Path $PSScriptRoot "../OptiScaler/shaders/dlssnr/precompile"
Push-Location $shaderDir
try {
    $variants = @(
        @{Name="rgb16"; Pitch=16; Rgb=$true; Guide=$false},
        @{Name="guide16"; Pitch=16; Rgb=$false; Guide=$true},
        @{Name="both16"; Pitch=16; Rgb=$true; Guide=$true},
        @{Name="weights20"; Pitch=20; Rgb=$false; Guide=$false},
        @{Name="rgb20"; Pitch=20; Rgb=$true; Guide=$false},
        @{Name="guide20"; Pitch=20; Rgb=$false; Guide=$true},
        @{Name="both20"; Pitch=20; Rgb=$true; Guide=$true}
    )
    $hashes = @()
    foreach ($v in $variants) {
        $stem = "dlssnr_tiled_v14_$($v.Name)"
        $defines = @("-D", "DLSSNR_TILED_FUSED=1", "-D", "DLSSNR_TILED_V10=1",
                     "-D", "DLSSNR_TILED_V10_WEIGHTS=1", "-D", "DLSSNR_TILED_V13_MODE28=1",
                     "-D", "DLSSNR_TILED_PITCH=$($v.Pitch)")
        if ($v.Rgb) { $defines += @("-D", "DLSSNR_TILED_V14_RGB=1") }
        if ($v.Guide) { $defines += @("-D", "DLSSNR_TILED_V14_GUIDE_ONE=1") }
        & "../../shader_tools/dxc.exe" -T cs_6_0 -E CSMain -O3 -Qstrip_debug -Qstrip_reflect @defines "dlssnr.hlsl" -Fo "${stem}_Shader.cso" -Fc "${stem}.asm"
        if ($LASTEXITCODE -ne 0) { throw "DXC failed: $stem" }
        & $Python "../../shader_tools/create_header.py" "${stem}_Shader.cso" "${stem}_Shader.h" "${stem}_cso"
        if ($LASTEXITCODE -ne 0) { throw "Header generation failed: $stem" }
        $floats = $v.Pitch * $v.Pitch * $(if ($v.Rgb) {3} else {4})
        $lds = @(Select-String -Path "${stem}.asm" -Pattern 'addrspace\(3\) global')
        if ($lds.Count -ne 1 -or $lds[0].Line -notmatch "\[$floats x float\]") {
            throw "Unexpected LDS allocation for ${stem}: expected $floats floats"
        }
        $hash = (Get-FileHash "${stem}_Shader.cso" -Algorithm SHA256).Hash
        $hashes += $hash
        Write-Host "$stem SHA256=$hash LDS=$($floats * 4) bytes"
    }
    if (($hashes | Sort-Object -Unique).Count -ne 7) { throw "v14 PSOs must have distinct DXIL" }
} finally { Pop-Location }
