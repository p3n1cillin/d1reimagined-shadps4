@echo off
rem Build and run the D1 unit tests (tests\test_d1_*.cpp): standalone programs that check the fork's
rem pure D1 logic with assert, so they build with -UNDEBUG and need no game, GPU or emulator run.
rem
rem Usage: d1\Run-D1Tests.cmd [source-dir] [out-dir]
rem Tool locations as for Build-D1ShadPS4.cmd: LLVM_BIN, VCVARS.
setlocal enabledelayedexpansion
set "SRC=%~1"
if "%SRC%"=="" set "SRC=%~dp0.."
set "OUT=%~2"
if "%OUT%"=="" set "OUT=%SRC%\build-d1-tests"
if "%LLVM_BIN%"=="" set "LLVM_BIN=C:\Program Files\LLVM\bin"
if "%VCVARS%"=="" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || (echo vcvars64.bat not found: set VCVARS & exit /b 1)
set "PATH=%LLVM_BIN%;%PATH%"
if not exist "%OUT%" mkdir "%OUT%"
set "INC=/I"%SRC%\src" /I"%SRC%\externals\sdl3\include" /I"%SRC%\externals\fmt\include" /I"%SRC%\externals\magic_enum\include" /I"%SRC%\externals\magic_enum\include\magic_enum" /I"%SRC%\externals\json\include""
set FAILED=0
for %%T in ("%SRC%\tests\test_d1_*.cpp") do call :one "%%T" %%~nT
exit /b %FAILED%

:one
clang-cl /nologo /std:c++latest /EHsc /O1 /UNDEBUG /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS %INC% "%~1" /Fe"%OUT%\%2.exe" /Fo"%OUT%\%2.obj" >"%OUT%\%2.log" 2>&1
if errorlevel 1 (
  echo FAIL build %2 ^(see %OUT%\%2.log^)
  set FAILED=1
  goto :eof
)
"%OUT%\%2.exe"
if errorlevel 1 (
  echo FAIL %2
  set FAILED=1
) else (
  echo ok   %2
)
goto :eof

