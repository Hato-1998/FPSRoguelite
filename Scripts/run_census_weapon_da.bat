@echo off
REM DA_Weapon_* census (read-only) -- headless REAL editor (Troubleshooting D11).
REM ASCII-only comments on purpose (cmd.exe OEM codepage). Absolute script path on purpose
REM (relative -ExecCmds paths resolve against the engine binaries dir). Run from a .bat, not PowerShell.
set ENGINE=D:\UnrealEngine\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJDIR=E:\Git_Project\FPSRoguelite
"%ENGINE%" "%PROJDIR%\FPSRoguelite.uproject" -nullrhi -unattended -nosplash -ExecCmds="py %PROJDIR%\Scripts\census_weapon_da.py" -stdout -FullStdOutLogOutput -abslog=%PROJDIR%\Saved\census_weapon_da.log
