@echo off
REM STAT1 content verification -- headless REAL editor (Troubleshooting D11).
REM ASCII-only comments on purpose (cmd.exe OEM codepage). Absolute script path on purpose
REM (relative -ExecCmds paths resolve against the engine binaries dir, not the project).
REM Close the UE editor first (it locks loaded .uasset files).
REM The trailing ",quit" is REQUIRED or the editor never exits and the .bat never returns.
REM Verdict = STDOUT line "[VERIFY] RESULT PASS|FAIL". Do NOT judge by exit code (it is 255 on a
REM normal headless quit) and do NOT judge by the abslog alone (truncated on exit).
set ENGINE=D:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJDIR=E:\Git_Project\FPSRoguelite
"%ENGINE%" "%PROJDIR%\FPSRoguelite.uproject" -nullrhi -unattended -nosplash -ExecCmds="py %PROJDIR%\Scripts\verify_stat1_content.py,quit" -stdout -FullStdOutLogOutput -abslog=%PROJDIR%\Saved\verify_stat1_content.log
