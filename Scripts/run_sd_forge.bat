@echo off
REM ============================================================
REM  Stable Diffusion WebUI Forge launcher  (ENE turnaround track)
REM  Docs/Handoff/PlayerChar_Arcade/SD_Forge_Setup.md
REM
REM  ASCII-only on purpose: cmd.exe console codepage mangles
REM  non-ASCII in .bat files.
REM ============================================================
setlocal

REM Some non-interactive shells (e.g. an agent's tool shell) set this to 1,
REM which stops cmd from resolving batch files in the CURRENT directory.
REM Forge's own webui-user.bat does a bare "call webui.bat", so it dies there.
REM A normal user double-click never has this set; clearing it makes both work.
set "NoDefaultCurrentDirectoryInExePath="

set "FORGE_DIR=F:\StableDiffusion\stable-diffusion-webui-forge"
set "URL=http://127.0.0.1:7860"

echo.
echo  === Stable Diffusion WebUI Forge ===
echo  dir : %FORGE_DIR%
echo  url : %URL%
echo.

REM --- 1. install present? ---
if not exist "%FORGE_DIR%\webui-user.bat" (
  echo  [ERROR] Forge not found at %FORGE_DIR%
  echo          Fix FORGE_DIR at the top of this script.
  goto :end
)

REM --- 2. already running? ---
netstat -ano | findstr /R /C:"LISTENING" | findstr /C:":7860" >nul 2>&1
if not errorlevel 1 (
  echo  [SKIP] Port 7860 is already listening - Forge appears to be running.
  echo         Open %URL% , or run stop_sd_forge.bat first to restart.
  goto :end
)

REM --- 3. --api enabled? (needed to drive it from Claude) ---
findstr /C:"--api" "%FORGE_DIR%\webui-user.bat" >nul 2>&1
if errorlevel 1 (
  echo  [WARN] --api is NOT in webui-user.bat COMMANDLINE_ARGS.
  echo         The WebUI will still work, but REST automation will not.
  echo.
)

REM --- 4. launch (webui-user.bat sets vars then calls webui.bat) ---
echo  Starting... first run after adding models takes longer (model scan).
echo  Press Ctrl+C in this window to stop, or run stop_sd_forge.bat.
echo.
REM NOTE: must be the FULL path. Plain "call webui-user.bat" resolves via PATH
REM (not the current dir) when launched from a non-interactive shell, and fails.
pushd "%FORGE_DIR%"
call "%FORGE_DIR%\webui-user.bat"
popd

:end
echo.
endlocal
