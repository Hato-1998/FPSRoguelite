@echo off
REM Hard-surface rifle import -- headless REAL editor (Troubleshooting D1-b / D11).
REM ASCII-only on purpose: cmd.exe reads .bat in the OEM codepage, and Korean
REM comments corrupted the parse (2026-09-03: stray "'Cmds' is not recognized").
REM Two traps this file avoids, both measured 2026-09-03:
REM   1) Passing -ExecCmds from PowerShell strips the quotes -> script never runs.
REM      (memory: automation-multi-test-plus-hangs)  Use a .bat.
REM   2) A relative path in -ExecCmds resolves against the ENGINE BINARIES dir,
REM      not the project ("resolved from 'Scripts/...'").  Pass an absolute path.
set ENGINE=D:\UnrealEngine\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJDIR=E:\Git_Project\FPSRoguelite
"%ENGINE%" "%PROJDIR%\FPSRoguelite.uproject" -nullrhi -unattended -nosplash -ExecCmds="py %PROJDIR%\Scripts\import_rifle_hardsurface.py" -stdout -FullStdOutLogOutput -abslog=%PROJDIR%\Saved\rifle_hs_import.log
