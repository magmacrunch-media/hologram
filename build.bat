@echo off
rem hologram Windows build. MSVC only, no other dependencies.
rem   build.bat        build the current example into build\
rem   build.bat test   build and run the host tests

setlocal
set VCVARS="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% (
    echo Could not find vcvars64.bat -- edit VCVARS at the top of build.bat.
    exit /b 1
)
call %VCVARS% >nul

if not exist build mkdir build

if "%1"=="test" goto :tests

rem /W4 is the MSVC face of the -Wall -Wextra rule in AGENTS.md; sokol's own
rem warnings are not ours to fix, so its implementation TU compiles at /W3.
cl /nologo /std:c11 /W3 /O2 /DSOKOL_D3D11 /Fobuild\ /Febuild\m0_window.exe ^
    examples\m0_window\main.c source\display.c source\timestep.c
exit /b %errorlevel%

:tests
set FAILED=0
for %%t in (tests\test_*.c) do (
    cl /nologo /std:c11 /W4 /Isource /Itests /Fobuild\ /Febuild\%%~nt.exe %%t source\timestep.c >nul || set FAILED=1
    build\%%~nt.exe || set FAILED=1
)
exit /b %FAILED%
