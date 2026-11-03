@echo off
rem Builds docs-digger (Release), refreshes overlunky's compile database and runs the digger on it, with the
rem runtime enum dump from overlunky's docs, the function groups of config\groups.json and the text fragments of
rem config\fragments. Results are cached per translation unit in build\digger-cache.sqlite
rem (valid while the files a unit read are unchanged); --no-cache parses everything.
rem Usage: dig.bat [docs-digger options, e.g. -o out.json --jobs 8 --no-cache]
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
    "--runtime-enums=%DD_OVERLUNKY_ROOT%\docs\game_data\lua_enums.txt" "--groups=%ROOT%config\groups.json" "--lua-types=%ROOT%config\lua_types.json" "--fragments=%ROOT%config\fragments" ^
    "--lua-out=%ROOT%build\lua" "--emmylua-out=%ROOT%build\lua-emmylua" "--lua-prelude=%ROOT%config\lua_prelude.lua" ^
    "--lua-doc-url=https://spelunky-fyi.github.io/overlunky/" "--cache=%ROOT%build\digger-cache.sqlite" %*
if errorlevel 1 exit /b 1

rem Optional: lua-language-server (on PATH, or the VS Code Lua extension's) reports annotation problems in build\lua.
rem It does not fail the run. Its paths must not contain "--", which it mangles.
set "LUALS="
for /f "delims=" %%i in ('where lua-language-server 2^>nul') do set "LUALS=%%i"
if not defined LUALS for /d %%d in ("%USERPROFILE%\.vscode\extensions\sumneko.lua-*") do if exist "%%d\server\bin\lua-language-server.exe" set "LUALS=%%d\server\bin\lua-language-server.exe"
if not defined LUALS goto done
echo Checking the Lua definition files with %LUALS%
"%LUALS%" "--check=%ROOT%build\lua" --checklevel=Warning "--logpath=%ROOT%build\luals-log" --check_format=pretty
:done
exit /b 0
