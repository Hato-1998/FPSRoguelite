@echo off
REM CRIT1 content verification (read-only) -- headless REAL editor (Troubleshooting D11).
REM ASCII-only comments on purpose (cmd.exe OEM codepage). Absolute script path on purpose.
REM ",quit" is REQUIRED or the editor never exits and the .bat never returns.
REM Read the verdict from STDOUT (the abslog is truncated on exit): look for "[CRIT1-VERIFY]".
set ENGINE=D:\UnrealEngine\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJDIR=E:\Git_Project\FPSRoguelite
"%ENGINE%" "%PROJDIR%\FPSRoguelite.uproject" -nullrhi -unattended -nosplash -ExecCmds="py %PROJDIR%\Scripts\verify_crit1_content.py,quit" -stdout -FullStdOutLogOutput -abslog=%PROJDIR%\Saved\verify_crit1.log
