@echo off
REM Headless smoke: FPSRoguelite.Smoke.ModuleLoads  (canonical form = Docs/SSOT/Workflow.md 6-6)
REM ASCII-only comments on purpose (cmd.exe OEM codepage). Run from a .bat, not PowerShell
REM (PowerShell strips the -ExecCmds quotes -> runner never starts, editor idles forever: Troubleshooting D11,
REM  memory automation-multi-test-plus-hangs). ONE test per invocation - joining tests with '+' also hangs.
REM Verdict = log line "Result={Success}" and "Automation Test Queue Empty", NOT the exit code (Troubleshooting C4).
REM Engine 5.7 -> 5.8 (commit 7967b13f). 5.7 is still installed, so a stale path here does NOT fail loudly:
REM it launches the OLD engine against a 5.8 project and the verdict becomes meaningless.
set ENGINE=D:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJDIR=E:\Git_Project\FPSRoguelite
"%ENGINE%" "%PROJDIR%\FPSRoguelite.uproject" -unattended -nopause -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests FPSRoguelite.Smoke.ModuleLoads" -TestExit="Automation Test Queue Empty" -stdout -FullStdOutLogOutput -abslog=%PROJDIR%\Saved\wpn1_smoke.log
