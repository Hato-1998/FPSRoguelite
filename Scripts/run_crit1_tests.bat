@echo off
REM Headless automation for CRIT1 (canonical form = Docs/SSOT/Workflow.md 6-6).
REM ASCII-only comments on purpose (cmd.exe OEM codepage). Run from a .bat, not PowerShell
REM (PowerShell strips the -ExecCmds quotes -> runner never starts, editor idles forever: Troubleshooting D11,
REM  memory automation-multi-test-plus-hangs). ONE test per invocation - joining tests with '+' also hangs.
REM Verdict = log line "Result={Success}" and "Automation Test Queue Empty", NOT the exit code (Troubleshooting C4).
REM 🚨 READ THE VERDICT FROM STDOUT, NOT FROM -abslog. The abslog file is truncated when the process exits on
REM -TestExit, so it stops BEFORE the Result= line and a passing run reads as a failure. Always redirect:
REM     cmd /c Scriptsun_crit1_tests.bat > Saved\crit1_stdout.txt 2>&1
REM Also note: 4 "LogAutomationTest: Error: Condition failed" lines appear during engine startup in EVERY run
REM (control-checked against the known-good ModuleLoads smoke) - they are not this test.
set ENGINE=D:\UnrealEngine\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJDIR=E:\Git_Project\FPSRoguelite
"%ENGINE%" "%PROJDIR%\FPSRoguelite.uproject" -unattended -nopause -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests FPSRoguelite.Combat.CritResolver" -TestExit="Automation Test Queue Empty" -stdout -FullStdOutLogOutput -abslog=%PROJDIR%\Saved\crit1_resolver.log
