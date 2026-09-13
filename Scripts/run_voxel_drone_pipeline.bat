@echo off
REM Voxel drone enemy mesh pipeline (replaces the chomper) -- run with the EDITOR CLOSED.
REM   1) gen_voxel_drone.py            python  -> Saved\EnemyVoxel\SM_EnemyVoxel_Drone.obj (+ preview obj/mtl, sprites txt)
REM   2) render_voxel_drone_preview.py Blender -> Saved\EnemyVoxel\drone_*.png (pass "render" as arg 1)
REM   3) import_voxel_drone.py         headless REAL editor (Troubleshooting D1-b / D11)
REM   4) author_voxel_drone_material.py commandlet: M_ / MI_ + wires SM slot 0
REM ASCII-only on purpose (cmd OEM codepage, Troubleshooting D11). Traps avoided: PowerShell strips -ExecCmds quotes,
REM relative script paths resolve against the engine dir, py exceptions must reach quit_editor (try/finally).
set ENGINE=D:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJDIR=E:\Git_Project\FPSRoguelite
set BLENDER=F:\Blender\blender.exe

cd /d "%PROJDIR%"
python "%PROJDIR%\Scripts\gen_voxel_drone.py" "%PROJDIR%\Saved\EnemyVoxel" --sprites || exit /b 1
if /i "%1"=="render" "%BLENDER%" -b -P "%PROJDIR%\Scripts\render_voxel_drone_preview.py" -- "%PROJDIR%\Saved\EnemyVoxel"

"%ENGINE%" "%PROJDIR%\FPSRoguelite.uproject" -nullrhi -unattended -nosplash -nosound -ExecCmds="py %PROJDIR%\Scripts\import_voxel_drone.py" -stdout -FullStdOutLogOutput -abslog=%PROJDIR%\Saved\EnemyVoxel\drone_import.log
"%ENGINE%" "%PROJDIR%\FPSRoguelite.uproject" -run=pythonscript -script="%PROJDIR%\Scripts\author_voxel_drone_material.py" -unattended -nopause -nullrhi -nosplash -nosound -abslog=%PROJDIR%\Saved\EnemyVoxel\drone_material.log

findstr /C:"[import]" "%PROJDIR%\Saved\EnemyVoxel\drone_import.log"
findstr /C:"DRONE-MAT" "%PROJDIR%\Saved\EnemyVoxel\drone_material.log"
