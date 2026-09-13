@echo off
REM Headless automation for CRIT2 (canonical form = Docs/SSOT/Workflow.md 6-6).
REM ASCII-only comments on purpose (cmd.exe OEM codepage). Run from a .bat, not PowerShell
REM (PowerShell strips the -ExecCmds quotes -> runner never starts, editor idles forever: Troubleshooting D11,
REM  memory automation-multi-test-plus-hangs). ONE test per invocation - joining tests with '+' also hangs.
REM Verdict = log line "Result={Success}" and "Automation Test Queue Empty", NOT the exit code (Troubleshooting C4).
REM !! READ THE VERDICT FROM STDOUT, NOT FROM -abslog (abslog is truncated on -TestExit). Always redirect:
REM     cmd /c Scripts\run_crit2_tests.bat FPSRoguelite.Card.Synergy > Saved\crit2_stdout.txt 2>&1
REM Wait for the previous UnrealEditor-Cmd.exe to die before the next call (memory headless-editor-lingers-after-output).
REM 4 "LogAutomationTest: Error: Condition failed" lines appear during engine startup in EVERY run - not this test.
set ENGINE=D:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJDIR=E:\Git_Project\FPSRoguelite
set TESTNAME=%~1
if "%TESTNAME%"=="" set TESTNAME=FPSRoguelite.Card.Synergy
"%ENGINE%" "%PROJDIR%\FPSRoguelite.uproject" -unattended -nopause -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests %TESTNAME%" -TestExit="Automation Test Queue Empty" -stdout -FullStdOutLogOutput
