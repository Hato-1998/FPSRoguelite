@echo off
REM CRIT1 fragment DataAsset shells -- headless REAL editor (Troubleshooting D11).
REM ASCII-only comments on purpose (cmd.exe OEM codepage). Absolute script path on purpose
REM (relative -ExecCmds paths resolve against the engine binaries dir). Run from a .bat, not PowerShell.
REM Close the UE editor first: a running editor locks the .uasset files it has loaded.
REM The trailing ",QUIT_EDITOR" in -ExecCmds is REQUIRED: without it the script finishes but the editor keeps
REM running forever and the .bat never returns (measured 2026-09-05 - script DONE at t+1s, idle at t+8min).
REM Read the verdict from STDOUT, not the abslog (it is truncated on exit): look for "[CRIT1] DONE".
set ENGINE=D:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJDIR=E:\Git_Project\FPSRoguelite
REM UE 5.8: "quit" is a GAME-engine command the editor ignores (logs "Cmd: quit", never requests exit). The editor exit is QUIT_EDITOR -> UUnrealEdEngine::CloseEditor -> RequestEngineExit (Troubleshooting D14).
"%ENGINE%" "%PROJDIR%\FPSRoguelite.uproject" -nullrhi -unattended -nosplash -ExecCmds="py %PROJDIR%\Scripts\author_crit1_fragments.py,QUIT_EDITOR" -stdout -FullStdOutLogOutput -abslog=%PROJDIR%\Saved\author_crit1_fragments.log
