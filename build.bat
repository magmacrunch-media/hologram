@echo off
rem hologram Windows build. MSVC only, no other dependencies.
rem   build.bat        build the examples into build\
rem   build.bat test   build and run the host tests

setlocal
set VCVARS="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% (
    echo Could not find vcvars64.bat -- edit VCVARS at the top of build.bat.
    exit /b 1
)
call %VCVARS% >nul 2>nul

if not exist build mkdir build

rem The pure modules: no sokol, no window, safe to link into tests and
rem console tools alike.
set PURE=source\linalg.c source\geometry.c source\camera.c source\cpu_trace.c source\gpu_scene.c source\timestep.c

if "%1"=="test" goto :tests

rem /W4 is the MSVC face of the -Wall -Wextra rule in AGENTS.md; sokol's own
rem warnings are not ours to fix, so display.c (its implementation TU)
rem compiles at /W3.
cl /nologo /std:c11 /W3 /O2 /DSOKOL_D3D11 /DSOKOL_WIN32_FORCE_MAIN /Fobuild\ /Febuild\m0_window.exe ^
    examples\m0_window\main.c source\display.c %PURE% || exit /b 1
cl /nologo /std:c11 /W4 /O2 /Fobuild\ /Febuild\m1_cpu.exe ^
    examples\m1_cpu\main.c %PURE% || exit /b 1
cl /nologo /std:c11 /W3 /O2 /DSOKOL_D3D11 /DSOKOL_WIN32_FORCE_MAIN /Fobuild\ /Febuild\m2_gpu.exe ^
    examples\m2_gpu\main.c source\display.c source\oracle.c %PURE% || exit /b 1
cl /nologo /std:c11 /W3 /O2 /DSOKOL_D3D11 /DSOKOL_WIN32_FORCE_MAIN /Fobuild\ /Febuild\m3_mirrors.exe ^
    examples\m3_mirrors\main.c source\display.c source\oracle.c %PURE% || exit /b 1
exit /b 0

:tests
set FAILED=0
for %%t in (tests\test_*.c) do (
    cl /nologo /std:c11 /W4 /Isource /Itests /Fobuild\ /Febuild\%%~nt.exe %%t %PURE% >nul || set FAILED=1
    build\%%~nt.exe || set FAILED=1
)
exit /b %FAILED%
