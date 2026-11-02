@echo off
rem Builds docs-digger.  Usage: build.bat [Debug^|Release^|All]   (default: Debug)
rem clang-cl with Ninja Multi-Config in build\digger; configures on first use (downloads go to thirdparty\).
rem Note: no "exit /b" inside ( ) blocks in these scripts; cmd loses the exit code there.
setlocal
set "ROOT=%~dp0"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
if /i "%CONFIG%"=="All" goto all
if /i "%CONFIG%"=="Debug" goto setup
if /i "%CONFIG%"=="Release" goto setup
echo Unknown configuration "%CONFIG%". Use Debug, Release or All.
exit /b 2

:all
call "%~f0" Debug
if errorlevel 1 exit /b 1
call "%~f0" Release
if errorlevel 1 exit /b 1
exit /b 0

:setup
call "%ROOT%tools\dev-env.cmd"
if errorlevel 1 exit /b 1
set "BUILD_DIR=%ROOT%build\digger"
if exist "%BUILD_DIR%\CMakeCache.txt" goto build
cmake -S "%ROOT%." -B "%BUILD_DIR%" -G "Ninja Multi-Config" -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_LINKER_TYPE=LLD ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if errorlevel 1 exit /b 1

:build
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 exit /b 1
echo Built %CONFIG%: %BUILD_DIR%\%CONFIG%\docs-digger.exe
exit /b 0
