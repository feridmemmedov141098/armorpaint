@echo off
rem Builds ArmorPaint and updates the exe your desktop shortcut starts.
rem Double-click it, or run Build.bat from a terminal. Add "run" to start the app after: Build.bat run
setlocal
set ROOT=%~dp0
set BUILD=%ROOT%paint\build

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VCVARS%" goto found
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -find VC\Auxiliary\Build\vcvars64.bat`) do set "VCVARS=%%i"
if exist "%VCVARS%" goto found
echo Visual Studio C++ build tools not found.
goto fail
:found
call "%VCVARS%" >nul 2>nul

echo Building...
rem Built to x64\Dev first, so the build works while ArmorPaint is open
MSBuild.exe "%BUILD%\ArmorPaint.vcxproj" /m /clp:ErrorsOnly /nologo /p:Configuration=Release,Platform=x64 /p:OutDir="%BUILD%\x64\Dev\\"
if errorlevel 1 goto fail

rem The app reads its data from build\out\data
copy /y "%ROOT%paint\assets\plugins\bake_maps.c" "%BUILD%\out\data\plugins\" >nul

:wait
tasklist /fi "imagename eq ArmorPaint.exe" | find /i "ArmorPaint.exe" >nul
if not errorlevel 1 (
	echo ArmorPaint is open. Close it, then press a key to update the shortcut exe.
	pause >nul
	goto wait
)
copy /y "%BUILD%\x64\Dev\ArmorPaint.exe" "%BUILD%\x64\Release\ArmorPaint.exe" >nul
if errorlevel 1 goto fail
echo Done. Your shortcut now starts the new build.

if /i "%1"=="run" (
	pushd "%BUILD%\out"
	start "" "%BUILD%\x64\Release\ArmorPaint.exe"
	popd
)
if /i not "%1"=="run" pause
exit /b 0

:fail
echo Build failed.
pause
exit /b 1
