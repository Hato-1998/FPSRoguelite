@echo off
REM VoxelChar metre-rig -> cm pipeline. RUN WITH THE UNREAL EDITOR CLOSED.
REM   1) export_voxel_char_fbx.py    Blender -> 9 FBX with armature node scale 0.01
REM   2) verify_voxel_char_fbx.py    Blender -> round-trip check vs the pre-fix backup
REM   3) reimport_voxel_char.py      headless REAL editor (NOT -run=pythonscript: the import
REM                                  path needs Slate and asserts out in a commandlet)
REM ASCII-only on purpose: cmd.exe reads .bat in the OEM codepage; Korean comments
REM corrupted the parse once (Troubleshooting D11).
REM Traps this file avoids (all hit again on 2026-09-05):
REM   - -ExecCmds passed from PowerShell loses its quotes -> script never runs, editor idles forever.
REM   - A relative script path in -ExecCmds resolves against the ENGINE binaries dir. Use absolute.
REM   - If the py script raises before quit_editor, the editor idles forever -> wrapped in try/finally.
set ENGINE=D:\UnrealEngine\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
set PROJDIR=E:\Git_Project\FPSRoguelite
set BLENDER=F:\Blender\blender.exe
set SRC=C:\Users\koras\Desktop\voxel+character+3d+model

cd /d "%PROJDIR%"

if /i "%1"=="export" (
  "%BLENDER%" -b "%SRC%\voxel_character_rigged_v2.blend" -P "%PROJDIR%\Scripts\export_voxel_char_fbx.py" -- "%SRC%" || exit /b 1
  "%BLENDER%" -b --factory-startup -P "%PROJDIR%\Scripts\verify_voxel_char_fbx.py" -- "%SRC%\_pre_scalefix_backup\SKM_VoxelChar.fbx" "%SRC%\SKM_VoxelChar.fbx" || exit /b 1
)

REM stdout is the source of truth for pass/fail -- reading -abslog instead has shown
REM truncated output and made passes look like failures (memory: automation-abslog-truncated-read-stdout).
"%ENGINE%" "%PROJDIR%\FPSRoguelite.uproject" -nullrhi -unattended -nosplash -nosound -ExecCmds="py %PROJDIR%\Scripts\reimport_voxel_char.py" -stdout -FullStdOutLogOutput
