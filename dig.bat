@echo off
rem Builds docs-digger (Release), refreshes overlunky's compile database and runs the digger on it, with the
rem runtime enum dump from overlunky's docs.
rem Usage: dig.bat [docs-digger options, e.g. -o out.txt --timings]
rem overlunky checkout: %DD_OVERLUNKY_ROOT%, default ..\Playlunky\submodules\overlunky next to this repository.
rem Note: no "exit /b" inside ( ) blocks; cmd loses the exit code there.
setlocal
set "ROOT=%~dp0"
if not defined DD_OVERLUNKY_ROOT set "DD_OVERLUNKY_ROOT=%ROOT%..\Playlunky\submodules\overlunky"
for %%i in ("%DD_OVERLUNKY_ROOT%") do set "DD_OVERLUNKY_ROOT=%%~fi"

call "%ROOT%build.bat" Release
if errorlevel 1 exit /b 1
call "%ROOT%tools\dev-env.cmd"
if errorlevel 1 exit /b 1
cmake "-DOVERLUNKY_ROOT=%DD_OVERLUNKY_ROOT%" "-DBUILD_DIR=%ROOT%build\overlunky" "-DTHIRDPARTY_DIR=%ROOT%thirdparty" ^
    -P "%ROOT%cmake\OverlunkyDatabase.cmake"
if errorlevel 1 exit /b 1

"%ROOT%build\digger\Release\docs-digger.exe" -p "%ROOT%build\overlunky" "--root=%DD_OVERLUNKY_ROOT%\src\game_api" ^
    "--runtime-enums=%DD_OVERLUNKY_ROOT%\docs\game_data\lua_enums.txt" %*
if errorlevel 1 exit /b 1
exit /b 0
