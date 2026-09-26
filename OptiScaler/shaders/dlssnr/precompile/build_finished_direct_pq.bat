@echo off
setlocal
set "DXC=%~dp0..\..\shader_tools\dxc.exe"
set "HEADER=%~dp0..\..\shader_tools\create_header.py"
set "SOURCE=%~dp0dlssnr_finished_direct_pq.hlsl"

call :build FINISHED_DIRECT_PQ_DECODE dlssnr_finished_decode_pq dlssnr_finished_decode_pq_cso
if errorlevel 1 exit /b 1
call :build FINISHED_DIRECT_PQ_ENCODE dlssnr_finished_encode_pq dlssnr_finished_encode_pq_cso
if errorlevel 1 exit /b 1

echo Specialized direct HDR10 finished-picture shaders rebuilt.
exit /b 0

:build
"%DXC%" -T cs_6_0 -E CSMain -O3 -Qstrip_debug -Qstrip_reflect -D %1=1 "%SOURCE%" -Fo "%~dp0%2_Shader.cso"
if errorlevel 1 exit /b 1
python "%HEADER%" "%~dp0%2_Shader.cso" "%~dp0%2_Shader.h" %3
if errorlevel 1 exit /b 1
exit /b 0
