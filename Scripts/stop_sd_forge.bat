@echo off
REM ============================================================
REM  Stop Stable Diffusion WebUI Forge  (ENE turnaround track)
REM
REM  Kills ONLY the process that holds TCP port 7860.
REM  Use when Forge lingers after closing its window, or before
REM  a restart (new models / changed COMMANDLINE_ARGS).
REM
REM  ASCII-only on purpose (cmd.exe codepage).
REM ============================================================
setlocal enabledelayedexpansion

set "FOUND="
for /f "tokens=5" %%P in ('netstat -ano ^| findstr /R /C:"LISTENING" ^| findstr /C:":7860"') do (
  set "FOUND=%%P"
)

if not defined FOUND (
  echo  [OK] Nothing is listening on port 7860 - Forge is not running.
  goto :end
)

echo  Port 7860 is held by PID %FOUND%:
tasklist /FI "PID eq %FOUND%" /FO LIST | findstr /C:"Image Name" /C:"PID"
echo.
choice /C YN /N /M "  Kill this process? (Y/N): "
if errorlevel 2 (
  echo  Cancelled.
  goto :end
)

taskkill /PID %FOUND% /T /F
echo.
echo  Stopped. Run run_sd_forge.bat to start again.

:end
echo.
endlocal
