@echo off
setlocal

set "ROOT=%~dp0"
set "OUT=%ROOT%bin"
set "PREBUILT=%ROOT%src\prebuilt"
set "CONFIG=Release"

echo Collecting build output to: %OUT%
if not exist "%OUT%" mkdir "%OUT%"

copy /Y "%ROOT%build\Studiormdl\%CONFIG%\studiormdl.exe"               "%OUT%\" || goto :error
copy /Y "%ROOT%build\tier0\%CONFIG%\tier0.dll"                         "%OUT%\" || goto :error
copy /Y "%ROOT%build\vstdlib\%CONFIG%\vstdlib.dll"                     "%OUT%\" || goto :error
copy /Y "%ROOT%build\filesystem_stdio\%CONFIG%\filesystem_stdio.dll"   "%OUT%\" || goto :error
copy /Y "%ROOT%build\materialsystem\%CONFIG%\materialsystem.dll"       "%OUT%\" || goto :error
copy /Y "%ROOT%build\studiorender\%CONFIG%\studiorender.dll"           "%OUT%\" || goto :error
copy /Y "%ROOT%build\mdllib\%CONFIG%\mdllib.dll"                       "%OUT%\" || goto :error
copy /Y "%ROOT%build\shaderapiempty\%CONFIG%\shaderapiempty.dll"       "%OUT%\" || goto :error

rem -- Prebuilt binaries (not compiled from source) ---------------------------
rem vphysics.dll is a patched Portal 2 binary stored under src/prebuilt/.
rem It does NOT require Portal 2 or any Valve auth tools to be installed.
if exist "%PREBUILT%\vphysics.dll" (
    copy /Y "%PREBUILT%\vphysics.dll" "%OUT%\" || goto :error
    echo Copied prebuilt vphysics.dll
) else (
    echo WARNING: src\prebuilt\vphysics.dll not found - physics collision will not work.
    echo          See src\vphysics_patch.md for instructions to recreate it.
)

echo.
echo Done! Files in %OUT%:
dir /B "%OUT%"
goto :eof

:error
echo.
echo ERROR: One or more files could not be copied.
echo Make sure you have built the solution first (Release^|Win32).
exit /b 1
