@echo off
REM Scripted DataAsset property edit (asset_edit.py) -- headless REAL editor (Troubleshooting D11).
REM ASCII-only comments on purpose (cmd.exe OEM codepage). Absolute script path on purpose.
REM ",quit" is REQUIRED or the editor never exits and the .bat never returns.
REM Read the verdict from STDOUT (the abslog is truncated on exit): look for "[asset_edit]".
REM !! DO NOT chain these .bat calls back to back. The editor stays alive 1-2 minutes AFTER the final
REM    output (asset registry cache write, EOS session teardown), and a second process opening the same
REM    .uasset makes SaveLoadedAsset fail with Windows ERROR_SHARING_VIOLATION (32). Kill the previous
REM    UnrealEditor-Cmd.exe first:  powershell -NoProfile -Command "Get-Process UnrealEditor-Cmd -EA 0 | Stop-Process -Force"
REM Usage:
REM   run_asset_edit.bat get <AssetPath> <PropertyPath>
REM   run_asset_edit.bat apply <changeset.json> [--dry-run]
set ENGINE=D:\UnrealEngine\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJDIR=E:\Git_Project\FPSRoguelite
"%ENGINE%" "%PROJDIR%\FPSRoguelite.uproject" -nullrhi -unattended -nosplash -ExecCmds="py %PROJDIR%\Scripts\asset_edit.py %*,quit" -stdout -FullStdOutLogOutput -abslog=%PROJDIR%\Saved\asset_edit.log
