<#
.SYNOPSIS
  FPSRoguelite Game 로컬라이제이션 타깃 원버튼 왕복(gather -> CSV 번역 주입 -> LocRes 컴파일) (LOC0 §5).
.DESCRIPTION
  UnrealEditor-Cmd로 -run=GatherText를 세 config 파일을 이어붙여(-config=A;B;C) 무인 실행한다:
    Game_Gather.ini              — 소스 매크로(LOCTABLE_FROMFILE_GAME) 스캔 -> 매니페스트/아카이브 기록
    Game_ImportCsvTranslations.ini — CSV en/ja 컬럼 -> 아카이브 주입 (UFPSRImportCsvTranslationsCommandlet)
    Game_Compile.ini             — 매니페스트+아카이브 -> Content/Localization/Game/{culture}/Game.locres
  세 config가 한 번의 GatherText 실행 안에서 phase 순서(UpdateManifests -> UpdateArchives -> Import -> Compile)
  대로 스케줄되므로 -config= 를 세미콜론으로 이어 붙여 단일 호출로 넘긴다(GatherTextCommandlet.cpp
  UGatherTextCommandlet::Execute -config 파싱 참조).
  -Sync 지정 시 Scripts\sync-authoring-csv.ps1을 먼저 실행하고, 그 스크립트가 실패(exit != 0)하면
  gather는 시도하지 않고 그대로 중단한다.
  엔진 경로 해석·UnrealEditor-Cmd 인보크 패턴은 Scripts\validate-data.ps1을 그대로 미러한다
  (Windows PowerShell 5.1 호환, pwsh 전용 문법 없음).
.PARAMETER Sync
  지정 시 gather 실행 전에 sync-authoring-csv.ps1(전체 시트)을 먼저 실행한다.
.PARAMETER EnginePath
  UE 5.7 엔진 루트. 기본값은 이 저장소가 개발 중인 머신의 고정 경로.
.EXAMPLE
  powershell -File Scripts\localization-gather.ps1
.EXAMPLE
  powershell -File Scripts\localization-gather.ps1 -Sync
#>
[CmdletBinding()]
param(
    [switch]$Sync,
    [string]$EnginePath = 'D:\UnrealEngine\UE_5.7',
    [string]$LogName = ("localization-gather_{0}.log" -f (Get-Date -Format 'yyyyMMdd_HHmmss'))
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot

if ($Sync) {
    $SyncScript = Join-Path $PSScriptRoot 'sync-authoring-csv.ps1'
    Write-Host "-Sync 지정 — 먼저 실행: $SyncScript"
    & powershell -NoProfile -File $SyncScript
    $SyncExitCode = $LASTEXITCODE
    if ($SyncExitCode -ne 0) {
        Write-Error "sync-authoring-csv.ps1 실패 (exit $SyncExitCode) — gather를 중단합니다."
        exit $SyncExitCode
    }
}

$UProject = Join-Path $RepoRoot 'FPSRoguelite.uproject'
$EditorCmd = Join-Path $EnginePath 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'

if (-not (Test-Path $UProject)) {
    Write-Error "uproject를 찾을 수 없습니다: $UProject"
    exit 1
}
if (-not (Test-Path $EditorCmd)) {
    Write-Error "UnrealEditor-Cmd.exe를 찾을 수 없습니다: $EditorCmd (-EnginePath로 엔진 위치를 지정하세요)"
    exit 1
}

$ConfigList = @(
    'Config/Localization/Game_Gather.ini',
    'Config/Localization/Game_ImportCsvTranslations.ini',
    'Config/Localization/Game_Compile.ini'
) -join ';'

$LogDir = Join-Path $RepoRoot 'Saved\Logs'
if (-not (Test-Path $LogDir)) {
    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
}
$AbsLog = Join-Path $LogDir $LogName

Write-Host "GatherText 실행: $EditorCmd"
Write-Host "Config: $ConfigList"
Write-Host "로그: $AbsLog"

& $EditorCmd $UProject "-run=GatherText" "-config=$ConfigList" -log "-abslog=$AbsLog"
$ExitCode = $LASTEXITCODE

if ($ExitCode -ne 0) {
    Write-Warning "GatherText 실패 (exit $ExitCode). 로그: $AbsLog"
}
else {
    Write-Host "GatherText 통과 (exit 0). LocRes: Content/Localization/Game/{en,ja}/Game.locres"
}

exit $ExitCode
