@echo off
rem Builds RDR2DSXRumble.asi with llvm-mingw (https://github.com/mstorsjo/llvm-mingw) or MSVC.
rem Usage: build.bat [path\to\llvm-mingw\bin]
setlocal
cd /d "%~dp0"

if not "%~1"=="" set "PATH=%~1;%PATH%"

where x86_64-w64-mingw32-clang++ >nul 2>nul
if %errorlevel%==0 (
  x86_64-w64-mingw32-clang++ -std=c++17 -O2 -Wall -Wno-unused-parameter -shared -static -s ^
    -o RDR2DSXRumble.asi src\RDR2DSXRumble.cpp -lws2_32 -lhid -lsetupapi -lole32 -lwinmm || goto :fail
  goto :ok
)

where cl >nul 2>nul
if %errorlevel%==0 (
  cl /nologo /std:c++17 /O2 /EHsc /MT /LD /D_CRT_SECURE_NO_WARNINGS src\RDR2DSXRumble.cpp /Fe:RDR2DSXRumble.asi /link /DLL || goto :fail
  del /q RDR2DSXRumble.obj RDR2DSXRumble.lib RDR2DSXRumble.exp 2>nul
  goto :ok
)

echo No compiler found. Pass the llvm-mingw bin folder as argument, or run from a VS Developer prompt.
exit /b 1

:ok
echo Built RDR2DSXRumble.asi
exit /b 0
:fail
echo Build failed.
exit /b 1
