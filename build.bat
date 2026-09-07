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
set PURE=source\linalg.c source\polar.c source\geometry.c source\camera.c source\collision.c source\cpu_trace.c source\gpu_scene.c source\pick_json.c source\scene_json.c source\spectrum.c source\walk_json.c source\timestep.c

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
cl /nologo /std:c11 /W3 /O2 /DSOKOL_D3D11 /DSOKOL_WIN32_FORCE_MAIN /Fobuild\ /Febuild\m4_glass.exe ^
    examples\m4_glass\main.c source\display.c source\oracle.c %PURE% || exit /b 1
cl /nologo /std:c11 /W3 /O2 /DSOKOL_D3D11 /DSOKOL_WIN32_FORCE_MAIN /Fobuild\ /Febuild\m5_spectral.exe ^
    examples\m5_spectral\main.c source\display.c source\oracle.c %PURE% || exit /b 1
cl /nologo /std:c11 /W3 /O2 /DSOKOL_D3D11 /DSOKOL_WIN32_FORCE_MAIN /Fobuild\ /Febuild\m6_polarization.exe ^
    examples\m6_polarization\main.c source\display.c source\oracle.c %PURE% || exit /b 1
cl /nologo /std:c11 /W3 /O2 /DSOKOL_D3D11 /DSOKOL_WIN32_FORCE_MAIN /Fobuild\ /Febuild\m7_room.exe ^
    examples\m7_room\main.c source\display.c source\oracle.c source\input.c %PURE% || exit /b 1
cl /nologo /std:c11 /W3 /O2 /DSOKOL_D3D11 /DSOKOL_WIN32_FORCE_MAIN /Fobuild\ /Febuild\m8_furnace.exe ^
    examples\m8_furnace\main.c source\display.c source\oracle.c source\input.c %PURE% || exit /b 1
cl /nologo /std:c11 /W3 /O2 /DSOKOL_D3D11 /DSOKOL_WIN32_FORCE_MAIN /Fobuild\ /Febuild\m9_spectrum.exe ^
    examples\m9_spectrum\main.c source\display.c source\oracle.c source\input.c %PURE% || exit /b 1

rem shadows is not a milestone -- see its header. Same shape as the
rem m2..m6 group: display and oracle, no input.
cl /nologo /std:c11 /W3 /O2 /DSOKOL_D3D11 /DSOKOL_WIN32_FORCE_MAIN /Fobuild\ /Febuild\shadows.exe ^
    examples\shadows\main.c source\display.c source\oracle.c %PURE% || exit /b 1

rem lens is not a milestone either -- see its header. Same shape again.
cl /nologo /std:c11 /W3 /O2 /DSOKOL_D3D11 /DSOKOL_WIN32_FORCE_MAIN /Fobuild\ /Febuild\lens.exe ^
    examples\lens\main.c source\display.c source\oracle.c %PURE% || exit /b 1

rem tools\bench times the GPU, so it compiles from the repository
rem root with /I. -- it reaches sokol and hologram.h by the same paths a
rem game would.
cl /nologo /std:c11 /W3 /O2 /DSOKOL_D3D11 /DSOKOL_WIN32_FORCE_MAIN /I. /Fobuild\ /Febuild\bench.exe ^
    tools\bench\bench.c source\display.c %PURE% || exit /b 1
exit /b 0

:tests
set FAILED=0
rem cl's output goes to a LOG AND IS PRINTED ON FAILURE, and a test that did not
rem compile does not run.
rem
rem It used to go to nul while the stale build\test_*.exe ran regardless, which
rem is worse than either half on its own: a test whose source stopped compiling
rem keeps printing the passes of a binary built before the change, for as long
rem as nobody reads the exit code. The same trap was found and fixed in daffodil
rem the same week; this is its twin.
for %%t in (tests\test_*.c) do (
    cl /nologo /std:c11 /W4 /Isource /Itests /Fobuild\ /Febuild\%%~nt.exe %%t %PURE% >build\cl.log 2>&1 && (
        build\%%~nt.exe || set FAILED=1
    ) || (
        echo %%~nt FAILED TO COMPILE
        type build\cl.log
        set FAILED=1
    )
)

rem The Metal dialect cannot be compiled here, but it can be type-checked:
rem tools\metalcheck parses trace.metal as the C++14 it very nearly is, and
rem cl is on PATH inside this script. Nothing else in the build reads that
rem file, which is how it came to be the dialect nobody had checked.
rem
rem Exit 2 is "found no compiler, checked nothing" -- a skip, not a pass and
rem not a failure. Only exit 1 means something is actually wrong. The gotos
rem keep this out of a parenthesised block, where %FAILED% would expand at
rem parse time and swallow the result.
where python >nul 2>&1 || goto :done
python tools\metalcheck\metalcheck.py
if errorlevel 2 goto :done
if errorlevel 1 set FAILED=1
:done
exit /b %FAILED%
