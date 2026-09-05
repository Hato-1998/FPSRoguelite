@echo off
REM Card CSV -> DA_Card_* importer (Docs/Specs/CARDCSV_ImporterPipeline.md).
REM ASCII-only comments on purpose (cmd.exe OEM codepage). Run from a .bat, not PowerShell.
REM Close the UE editor first - a running editor locks the .uasset files it would rewrite.
REM Commandlets exit on their own (no ",quit" needed, unlike -ExecCmds py scripts).
REM Read the verdict from STDOUT: redirect it, the abslog is truncated on exit.
set ENGINE=D:\UnrealEngine\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJDIR=E:\Git_Project\FPSRoguelite
"%ENGINE%" "%PROJDIR%\FPSRoguelite.uproject" -run=FPSRImportCards -unattended -nopause -nullrhi -nosplash -stdout -FullStdOutLogOutput -abslog=%PROJDIR%\Saved\import_cards.log
