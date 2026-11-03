@echo off
rem Builds the HTML reference from the digger JSON with a generator of src\html\<generator> (default starlight).
rem Usage: site.bat [generator] [digger json, default build\api.json]
rem Inputs: the examples in overlunky's docs\examples (%DD_OVERLUNKY_ROOT%, as dig.bat) and in examples\, and the Lua
rem definition files of build\lua and build\lua-emmylua, staged with a ZIP each in build\site-downloads for the
rem download links.
rem Output: build\site\<generator>, served under %DD_SITE_BASE% (default /overlunky) of %DD_SITE_URL%; then checked.
rem Node.js is the pinned one of cmake\NodePackage.cmake (downloaded into thirdparty\).
rem Packages: "npm ci" into thirdparty\node-<generator> when the lockfile changed, linked into the generator directory
rem as node_modules (a directory junction).
rem Note: no "exit /b" inside ( ) blocks; cmd loses the exit code there.
setlocal EnableDelayedExpansion
set "ROOT=%~dp0"
set "GEN=starlight"
if "%~1"=="" goto args_done
if not exist "%ROOT%src\html\%~1\package.json" goto args_done
set "GEN=%~1"
shift
:args_done
set "JSON=%ROOT%build\api.json"
if not "%~1"=="" set "JSON=%~f1"
if not exist "%JSON%" goto no_json
if not defined DD_OVERLUNKY_ROOT set "DD_OVERLUNKY_ROOT=%ROOT%..\Playlunky\submodules\overlunky"
for %%i in ("%DD_OVERLUNKY_ROOT%") do set "DD_OVERLUNKY_ROOT=%%~fi"
cmake "-DTHIRDPARTY_DIR=%ROOT%thirdparty" -P "%ROOT%cmake\NodePackage.cmake"
if errorlevel 1 goto fail
set /p NODE_HOME=<"%ROOT%thirdparty\node-path.txt"
set "PATH=%NODE_HOME%;%PATH%"

rem Packages: reinstalled only when the lockfile differs from the one of the last good install.
set "SRC=%ROOT%src\html\%GEN%"
set "NODE_DIR=%ROOT%thirdparty\node-%GEN%"
if not exist "%NODE_DIR%" mkdir "%NODE_DIR%"
rem The stamp holds the lockfile and the Node directory of the last good install.
set "STAMP=%NODE_DIR%\installed.txt"
set "WANT=%NODE_DIR%\wanted.txt"
copy /y "%SRC%\package-lock.json" "%WANT%" >nul
echo %NODE_HOME%>>"%WANT%"
if not exist "%NODE_DIR%\node_modules" goto install
fc /b "%WANT%" "%STAMP%" >nul 2>nul
if not errorlevel 1 goto installed
:install
copy /y "%SRC%\package.json" "%NODE_DIR%\package.json" >nul
copy /y "%SRC%\package-lock.json" "%NODE_DIR%\package-lock.json" >nul
pushd "%NODE_DIR%"
call npm ci --no-audit --no-fund
set "ERR=%errorlevel%"
popd
if not "%ERR%"=="0" goto fail
move /y "%WANT%" "%STAMP%" >nul
:installed
if not exist "%SRC%\node_modules" mklink /J "%SRC%\node_modules" "%NODE_DIR%\node_modules" >nul
if errorlevel 1 goto fail

rem Downloads: each dialect's files and a ZIP of them (tools\make_zip.py: the same bytes on every build).
set "STAGE=%ROOT%build\site-downloads"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
mkdir "%STAGE%"
call :stage luals "%ROOT%build\lua"
if errorlevel 1 goto fail
call :stage emmylua "%ROOT%build\lua-emmylua"
if errorlevel 1 goto fail

set "DD_SITE_JSON=%JSON%"
set "DD_SITE_EXAMPLES=%DD_OVERLUNKY_ROOT%\docs\examples"
set "DD_SITE_DIGGER_EXAMPLES=%ROOT%examples"
set "DD_SITE_DOWNLOADS=%STAGE%"
set "DD_SITE_OUT=%ROOT%build\site\%GEN%"
set "DD_SITE_CACHE=%ROOT%build\site\cache-%GEN%"
if not defined DD_SITE_BASE set "DD_SITE_BASE=/overlunky"
set "ASTRO_TELEMETRY_DISABLED=1"
pushd "%SRC%"
call npm run build --silent
set "ERR=%errorlevel%"
popd
if not "%ERR%"=="0" goto fail
echo Site written to %DD_SITE_OUT%

rem Checks: links, the sample symbols' old anchors, the old page's anchors (tools\check_site.py), and the generator's
rem own checks ("npm run check": the search check for starlight).
set "OLD_INDEX="
if exist "%DD_OVERLUNKY_ROOT%\docs\index.html" set "OLD_INDEX=--old-index=%DD_OVERLUNKY_ROOT%\docs\index.html"
python "%ROOT%tools\check_site.py" "%DD_SITE_OUT%" "--base=%DD_SITE_BASE%" "--samples=%ROOT%tools\site_samples.json" %OLD_INDEX%
if errorlevel 1 goto fail
pushd "%SRC%"
call npm run check --if-present --silent -- "%DD_SITE_OUT%" "%ROOT%tools\site_samples.json"
set "ERR=%errorlevel%"
popd
if not "%ERR%"=="0" goto fail
exit /b 0

rem :stage <style> <dir>: copies <dir>\*.lua to %STAGE%\<style> and zips them into %STAGE%\spel2-<style>.zip.
:stage
if not exist "%~2\spel2.lua" exit /b 0
mkdir "%STAGE%\%~1"
copy /y "%~2\*.lua" "%STAGE%\%~1\" >nul
set "FILES="
for %%f in ("%STAGE%\%~1\*.lua") do set FILES=!FILES! "%%f"
python "%ROOT%tools\make_zip.py" "%STAGE%\spel2-%~1.zip" !FILES!
exit /b %errorlevel%

:no_json
echo site.bat: %JSON% not found; run dig.bat first.
exit /b 1
:fail
echo site.bat: failed.
exit /b 1
