@echo off
rem Build shadPS4 for D1 Reimagined (upstream v0.19.0 + the d1 branch) with clang-cl and the
rem MSVC/Windows SDK environment. Discord RPC and the updater are off: a lab binary must not reach
rem the network on its own. HAVE_ENDIAN_H=OFF: LLVM 23 ships an endian.h without the Windows
rem byte-order helpers, which would otherwise make the vendored LibreSSL skip its own definitions.
rem
rem Usage: d1\Build-D1ShadPS4.cmd [source-dir] [build-dir]
rem Tool locations (set them when yours differ):
rem   LLVM_BIN   folder with clang-cl.exe            (default: C:\Program Files\LLVM\bin)
rem   VCVARS     vcvars64.bat of VS 2022 Build Tools (default: the standard install path)
rem   NINJA_DIR  folder with ninja.exe               (default: ninja on PATH)
setlocal
set "SRC=%~1"
if "%SRC%"=="" set "SRC=%~dp0.."
set "OUT=%~2"
if "%OUT%"=="" set "OUT=%SRC%\build-d1"
if "%LLVM_BIN%"=="" set "LLVM_BIN=C:\Program Files\LLVM\bin"
if "%VCVARS%"=="" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || (echo vcvars64.bat not found: set VCVARS & exit /b 1)
set "PATH=%LLVM_BIN%;%NINJA_DIR%;C:\Program Files\CMake\bin;%PATH%"
cmake -S "%SRC%" -B "%OUT%" -G Ninja -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
  -DENABLE_DISCORD_RPC=OFF -DENABLE_UPDATER=OFF ^
  -DHAVE_ENDIAN_H=OFF || exit /b 2
cmake --build "%OUT%" --parallel || exit /b 3
echo built: %OUT%\shadPS4.exe
