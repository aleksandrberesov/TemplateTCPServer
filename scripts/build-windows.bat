@echo off
rem Native Windows build using whatever C++17 toolchain CMake finds
rem (MSVC if running from a Developer Command Prompt, otherwise MinGW
rem if g++ is on PATH). Usage: scripts\build-windows.bat [extra cmake args]
setlocal

set "ROOT=%~dp0.."
set "BUILD=%ROOT%\build\windows"

cmake -S "%ROOT%" -B "%BUILD%" -DCMAKE_BUILD_TYPE=Release %*
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD%" --config Release
if errorlevel 1 exit /b %errorlevel%

echo.
echo Built. Look in %BUILD%\Release\ (MSVC) or %BUILD%\ (MinGW) for TemplateTCPServer.exe
endlocal
