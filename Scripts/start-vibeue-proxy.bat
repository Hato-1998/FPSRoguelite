@echo off
REM Start the VibeUE MCP proxy in the background (no console window).
REM Safe to run repeatedly: it does nothing if the port is already served.
REM Log: %LOCALAPPDATA%\VibeUE-Proxy\proxy.log

setlocal
set "SCRIPT=%~dp0vibeue-proxy.py"
set "PORT=8089"
set "LOGDIR=%LOCALAPPDATA%\VibeUE-Proxy"
set "LOG=%LOGDIR%\proxy.log"

if not exist "%LOGDIR%" mkdir "%LOGDIR%"

if not exist "%SCRIPT%" (
    echo [%date% %time%] ERROR: script not found: %SCRIPT% >> "%LOG%"
    exit /b 1
)

REM Already listening? Leave the running instance alone.
netstat -ano | findstr /r /c:"TCP.*:%PORT% .*LISTENING" >nul 2>&1
if not errorlevel 1 (
    echo [%date% %time%] port %PORT% already served -- nothing to do >> "%LOG%"
    exit /b 0
)

REM pythonw = no console window. Fall back to python if it is missing.
set "PY=pythonw.exe"
where pythonw.exe >nul 2>&1 || set "PY=python.exe"

echo [%date% %time%] starting proxy via %PY% >> "%LOG%"
REM cmd /c wrapper so stdout/stderr reach the log even though we detach.
start "" /b cmd /c ""%PY%" "%SCRIPT%" >> "%LOG%" 2>&1"
exit /b 0
