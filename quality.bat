@echo off
rem Quality gate: clang-format (src/, tests/), Lizard (cyclomatic complexity) and clang-tidy over src/.
rem Usage: quality.bat [changed^|all]   (default: changed = files changed since HEAD plus includers of changed headers)
rem Creates thirdparty\venv-quality from tools\requirements-quality.txt and a Ninja + clang-cl build in build\quality on first use.
rem Note: no "exit /b" inside ( ) blocks; cmd loses the exit code there.
setlocal
set "ROOT=%~dp0"
set "SCOPE=%~1"
if "%SCOPE%"=="" set "SCOPE=changed"
set "TARGET="
if /i "%SCOPE%"=="changed" set "TARGET=check-quality"
if /i "%SCOPE%"=="all" set "TARGET=check-quality-all"
if defined TARGET goto venv
echo Unknown scope "%SCOPE%". Use changed or all.
exit /b 2

:venv
rem Pinned analyzers; reinstall whenever the requirements file changes.
set "VENV=%ROOT%thirdparty\venv-quality"
set "REQS=%ROOT%tools\requirements-quality.txt"
if exist "%VENV%\Scripts\python.exe" goto requirements
python -m venv "%VENV%"
if errorlevel 1 exit /b 1
:requirements
fc /b "%REQS%" "%VENV%\dd-requirements.txt" >nul 2>nul
if not errorlevel 1 goto environment
"%VENV%\Scripts\python.exe" -m pip install --disable-pip-version-check -q -r "%REQS%"
if errorlevel 1 exit /b 1
copy /y "%REQS%" "%VENV%\dd-requirements.txt" >nul

:environment
call "%ROOT%tools\dev-env.cmd"
if errorlevel 1 exit /b 1

rem Configure only: clang-tidy needs compile_commands.json, not built objects.
if exist "%ROOT%build\quality\CMakeCache.txt" goto check
cmake -S "%ROOT%." -B "%ROOT%build\quality" -G Ninja -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if errorlevel 1 exit /b 1

:check
rem -k 0: run every check even when one fails, so all findings show at once.
cmake --build "%ROOT%build\quality" --target %TARGET% -- -k 0
if errorlevel 1 exit /b 1
exit /b 0
