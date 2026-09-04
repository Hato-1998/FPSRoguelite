@echo off
REM ARM1 hands-only mask material for the 1P arms -- headless REAL editor (Troubleshooting D11).
REM ASCII-only on purpose: cmd.exe reads .bat in the OEM codepage; Korean comments corrupted the parse.
REM Traps this file avoids (measured 2026-09-03):
REM   1) -ExecCmds passed from PowerShell loses its quotes -> the script never runs, editor idles forever.
REM   2) A relative path in -ExecCmds resolves against the ENGINE BINARIES dir, not the project.
REM Verdict = log lines "[hands] result=OK" and "[hands] ALLDONE" in Saved\author_handsonly.log, NOT the exit code.
REM Run only with the interactive editor CLOSED (asset saves + DDC).
set ENGINE=D:\UnrealEngine\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJDIR=E:\Git_Project\FPSRoguelite
"%ENGINE%" "%PROJDIR%\FPSRoguelite.uproject" -nullrhi -unattended -nosplash -ExecCmds="py %PROJDIR%\Scripts\author_fparms_handsonly_material.py" -stdout -FullStdOutLogOutput -abslog=%PROJDIR%\Saved\author_handsonly.log
