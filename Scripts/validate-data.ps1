<#
.SYNOPSIS
  FPSRoguelite 앵커드 데이터 검증(P0 데이터 밸리데이션 심) 헤드리스 커맨드릿 래퍼.
.DESCRIPTION
  UFPSRValidateAnchoredDataCommandlet(FPSRogueliteEditor 모듈)을 UnrealEditor-Cmd로 무인 실행한다.
  앵커(카드 풀/런 스케줄/로드아웃 풀) + 거기서 도달 가능한 모든 자산만 검증한다(Content/ 전체 아님) —
  버려진 초안 자산이 빌드를 막지 않는다. 고아 자산(어떤 앵커에서도 도달 불가)은 경고만 남기고 실패시키지 않는다.
  종료 코드: 0 = 전부 유효, 1 = 검증 실패 또는 앵커 0개(커맨드릿의 false-green 가드).
  Windows PowerShell 5.1 호환(pwsh 전용 문법 없음).
.EXAMPLE
  powershell -File Scripts\validate-data.ps1
#>
[CmdletBinding()]
param(
    [string]$EnginePath = 'D:\UnrealEngine\UE_5.7',
    [string]$LogName = ("validate-data_{0}.log" -f (Get-Date -Format 'yyyyMMdd_HHmmss'))
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
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

$LogDir = Join-Path $RepoRoot 'Saved\Logs'
if (-not (Test-Path $LogDir)) {
    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
}
$AbsLog = Join-Path $LogDir $LogName

Write-Host "FPSRValidateAnchoredData 실행: $EditorCmd"
Write-Host "로그: $AbsLog"

# 커맨드릿 이름 = 클래스명에서 'U' 접두사와 'Commandlet' 접미사를 뺀 것 (UFPSRValidateAnchoredDataCommandlet
# -> FPSRValidateAnchoredData). -run= 뒤에 그대로 전달한다.
& $EditorCmd $UProject -run=FPSRValidateAnchoredData -unattended -nopause -nullrhi -nosplash -nosound "-abslog=$AbsLog"
$ExitCode = $LASTEXITCODE

if ($ExitCode -ne 0) {
    Write-Warning "FPSRValidateAnchoredData 실패 (exit $ExitCode). 로그: $AbsLog"
}
else {
    Write-Host "FPSRValidateAnchoredData 통과 (exit 0)."
}

exit $ExitCode
