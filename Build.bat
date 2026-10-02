@echo off

cd /d %~dp0

set PROGRAMFILES32=%PROGRAMFILES(x86)%
if not exist "%PROGRAMFILES(x86)%" set PROGRAMFILES32=%PROGRAMFILES%

set VSWHERE=%PROGRAMFILES32%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (
	echo the vswhere.exe tool is missing.
	exit /b
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
	set INSTALLDIR=%%i
)

if "%INSTALLDIR%"=="" (
	echo VS installation directory does not exist.
	exit /b
)

@echo on
call "%INSTALLDIR%\VC\Auxiliary\Build\vcvarsall.bat" amd64
@echo off

set OutDir=locales
set OutFile=%OutDir%\patcher.exe
mkdir %OutDir%

echo [36mCompiling. . .[0m
call:compile "src\*.cpp"

echo [36mLinking. . .[0m
link /NOLOGO "%OutDir%\*.obj" /LIBPATH:".\lib\x64" /OUT:"%OutFile%" /MACHINE:X64 /SUBSYSTEM:CONSOLE /DYNAMICBASE:NO /INCREMENTAL:NO /OPT:REF /OPT:ICF /ERRORREPORT:NONE "kernel32.lib" "user32.lib" "gdi32.lib" "winspool.lib" "comdlg32.lib" "advapi32.lib" "shell32.lib" "ole32.lib" "oleaut32.lib" "uuid.lib" "odbc32.lib" "odbccp32.lib"
if %ERRORLEVEL% neq 0 goto:error

echo [1;42m  Output file: %OutFile% [0m
call:clean
set /p answer=ÔËÐÐ? (yes / no): 
if /i "%answer:~0,1%"=="y" (
cls
cd /d "%OutDir%"
call "%~dp0%OutFile%" TEST
pause
)
goto:eof


:clean
del /s /q "%~dp0%OutDir%\*.obj"
goto:eof

:compile
cl /nologo /c %* /I ".\include" /Fo"%OutDir%\\" /MD /O2 /GF /W3 /EHsc /GS- /fp:precise /Zc:wchar_t /Zc:forScope /Zc:inline /errorReport:none /D "WIN32" /D "_CONSOLE" /D "NDEBUG" /D "_UNICODE" /D "UNICODE"
if %ERRORLEVEL% neq 0 goto:error
goto:eof

:error
echo [1;41m  Error Code: %ERRORLEVEL% [0m
pause
call:clean
exit %ERRORLEVEL%
