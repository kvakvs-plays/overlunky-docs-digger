@echo off
rem Shared build environment for quality.bat (call it; it has no setlocal on purpose).
rem Enters the vcvars64 environment once (Windows SDK, MSVC STL, ninja) and puts LLVM first on PATH.
if defined DD_DEV_ENV exit /b 0

set "DD_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "DD_VSDIR="
for /f "usebackq delims=" %%i in (`"%DD_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "DD_VSDIR=%%i"
if defined DD_VSDIR goto vcvars
echo Visual Studio with the C++ x64 tools was not found.
exit /b 1

:vcvars
rem vcvars64 prints a harmless "'vswhere.exe' is not recognized" line; its exit code still counts.
call "%DD_VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if errorlevel 1 exit /b 1

rem PATH holds "(x86)", so it is changed outside any parenthesized block.
if exist "%ProgramFiles%\LLVM\bin\clang-cl.exe" goto llvm
echo clang-cl not found in "%ProgramFiles%\LLVM\bin". Install LLVM.
exit /b 1
:llvm
set "PATH=%ProgramFiles%\LLVM\bin;%PATH%"
set "DD_DEV_ENV=1"
exit /b 0
