@echo off
REM Voxel ghost "Chomper" enemy mesh pipeline -- run with the EDITOR CLOSED.
REM   1) gen_voxel_chomper.py          python  -> Saved\EnemyVoxel\*.obj (+ preview obj/mtl)
REM   2) render_voxel_chomper_preview  Blender -> Saved\EnemyVoxel\preview_*.png (optional, eyeball check)
REM   3) import_voxel_chomper.py       headless REAL editor (Troubleshooting D1-b / D11)
REM   4) author_voxel_chomper_material commandlet: M_ / MI_ + wires SM slot 0
REM ASCII-only on purpose: cmd.exe reads .bat in the OEM codepage; Korean comments
REM corrupted the parse once (Troubleshooting D11).
REM Traps this file avoids (all hit again on 2026-09-05):
REM   - -ExecCmds passed from PowerShell loses its quotes -> script never runs, editor idles forever.
REM   - A relative script path in -ExecCmds resolves against the ENGINE binaries dir. Use absolute.
REM   - If the py script raises before quit_editor, the editor idles forever -> wrap in try/finally.
set ENGINE=D:\UnrealEngine\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJDIR=E:\Git_Project\FPSRoguelite
set BLENDER=F:\Blender\blender.exe

cd /d "%PROJDIR%"
python "%PROJDIR%\Scripts\gen_voxel_chomper.py" || exit /b 1
if /i "%1"=="render" "%BLENDER%" -b -P "%PROJDIR%\Scripts\render_voxel_chomper_preview.py" -- "%PROJDIR%\Saved\EnemyVoxel"

"%ENGINE%" "%PROJDIR%\FPSRoguelite.uproject" -nullrhi -unattended -nosplash -nosound -ExecCmds="py %PROJDIR%\Scripts\import_voxel_chomper.py" -stdout -FullStdOutLogOutput -abslog=%PROJDIR%\Saved\EnemyVoxel\import.log
"%ENGINE%" "%PROJDIR%\FPSRoguelite.uproject" -run=pythonscript -script="%PROJDIR%\Scripts\author_voxel_chomper_material.py" -unattended -nopause -nullrhi -nosplash -nosound -abslog=%PROJDIR%\Saved\EnemyVoxel\material.log

findstr /C:"[import]" "%PROJDIR%\Saved\EnemyVoxel\import.log"
findstr /C:"CHOMPER-MAT" "%PROJDIR%\Saved\EnemyVoxel\material.log"
