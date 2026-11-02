@echo off
rem Builds docs-digger and runs the tests.  Usage: test.bat [Debug^|Release^|All]   (default: All)
rem set DD_UPDATE_GOLDEN=1 to rewrite tests\expected\ from the current output (review the diff afterwards).
rem Note: no "exit /b" inside ( ) blocks; cmd loses the exit code there.
setlocal
set "ROOT=%~dp0"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=All"
if /i not "%CONFIG%"=="All" goto run
call "%~f0" Debug
if errorlevel 1 exit /b 1
call "%~f0" Release
if errorlevel 1 exit /b 1
exit /b 0

:run
call "%ROOT%build.bat" %CONFIG%
if errorlevel 1 exit /b 1
call "%ROOT%tools\dev-env.cmd"
if errorlevel 1 exit /b 1
ctest --test-dir "%ROOT%build\digger" -C %CONFIG% --output-on-failure --no-tests=error --parallel 8
if errorlevel 1 exit /b 1
echo Tests passed: %CONFIG%
exit /b 0
