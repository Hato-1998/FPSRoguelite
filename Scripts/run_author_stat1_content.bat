@echo off
REM STAT1 status content DataAssets -- headless REAL editor (Troubleshooting D11).
REM ASCII-only comments on purpose (cmd.exe OEM codepage). Absolute script path on purpose
REM (relative -ExecCmds paths resolve against the engine binaries dir). Run from a .bat, not PowerShell
REM (PowerShell strips the -ExecCmds quotes).
REM Close the UE editor first: a running editor locks the .uasset files it has loaded.
REM The trailing ",quit" in -ExecCmds is REQUIRED: without it the script finishes but the editor keeps
REM running forever and the .bat never returns.
REM Read the verdict from STDOUT, not the abslog (truncated on exit): look for "[STAT1] DONE" and
REM the created/skipped/failed counts on the line above it.
REM The script is IDEMPOTENT -- an asset that already exists is left completely untouched, so re-running
REM after the user has tuned values cannot clobber that work.
set ENGINE=D:\UnrealEngine\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJDIR=E:\Git_Project\FPSRoguelite
"%ENGINE%" "%PROJDIR%\FPSRoguelite.uproject" -nullrhi -unattended -nosplash -ExecCmds="py %PROJDIR%\Scripts\author_stat1_content.py,quit" -stdout -FullStdOutLogOutput -abslog=%PROJDIR%\Saved\author_stat1_content.log
