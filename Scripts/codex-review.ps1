<#
.SYNOPSIS
  FPSRoguelite 코드 검증용 Codex(gpt-5.5) 비대화형 리뷰 래퍼.
.DESCRIPTION
  Claude 검증 단계의 최종 게이트. 비대화(approval=never)로 자동 실행한다.
  프로젝트 원칙은 리포의 AGENTS.md(핵심 3원칙) + Game.md/PROGRESS.md를 Codex가 자동 로드해 적용한다.
  결과는 stdout 출력 + Docs/codex-reviews/ 에 타임스탬프 md(UTF-8) 저장(gitignore됨; Docs/Review 컨설팅 폴더와 분리).
  주의:
    - codex review 는 scope 플래그(--base/--uncommitted/--commit)와 커스텀 프롬프트를 동시에 쓸 수 없다.
    - Windows에서 codex review 는 자체적으로 workspace-write 샌드박스로 동작한다(-s read-only 미반영 관찰됨).
      리뷰 에이전트는 읽기 분석 전용이나 워크스페이스 쓰기 권한이 있으므로 신뢰하는 로컬 리포에서만 사용할 것.
.EXAMPLE
  powershell -File Scripts\codex-review.ps1                # main 대비 현재 브랜치 diff 리뷰 (기본)
  powershell -File Scripts\codex-review.ps1 -Uncommitted   # 커밋 안 한 작업트리(staged+unstaged+untracked)
  powershell -File Scripts\codex-review.ps1 -Commit <sha>  # 특정 커밋 리뷰
  powershell -File Scripts\codex-review.ps1 -Base develop  # 다른 base 브랜치 대비
  powershell -File Scripts\codex-review.ps1 -NoSave        # 파일 저장 없이 stdout만
#>
[CmdletBinding(DefaultParameterSetName = 'Base')]
param(
    [Parameter(ParameterSetName = 'Base')]
    [string]$Base = 'main',
    [Parameter(ParameterSetName = 'Uncommitted')]
    [switch]$Uncommitted,
    [Parameter(ParameterSetName = 'Commit')]
    [string]$Commit,
    [string]$Title,
    [switch]$NoSave
)

$ErrorActionPreference = 'Stop'

# Codex 자동탐지 (이식성): 환경변수 → PATH → 알려진 네이티브 설치경로
function Resolve-Codex {
    if ($env:CODEX_EXE -and (Test-Path $env:CODEX_EXE)) { return $env:CODEX_EXE }
    $cmd = Get-Command codex -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $known = Join-Path $env:LOCALAPPDATA 'OpenAI\Codex\bin\codex.exe'
    if (Test-Path $known) { return $known }
    return $null
}
$CodexExe = Resolve-Codex
if (-not $CodexExe) {
    Write-Error "Codex CLI를 찾을 수 없습니다. 'npm install -g @openai/codex' + 'codex login' 후 다시 실행하세요(또는 `$env:CODEX_EXE 로 경로 지정)."
    exit 1
}

$RepoRoot = Split-Path -Parent $PSScriptRoot

# review 서브커맨드 인자 (scope 플래그만 — 커스텀 프롬프트와 양립 불가)
$reviewArgs = @('review')
switch ($PSCmdlet.ParameterSetName) {
    'Uncommitted' { $reviewArgs += '--uncommitted' }
    'Commit' { $reviewArgs += @('--commit', $Commit) }
    'Base' { $reviewArgs += @('--base', $Base) }
}
if ($Title) { $reviewArgs += @('--title', $Title) }

# 전역 옵션: 작업루트 지정 + read-only + 비대화 (review 서브커맨드 앞)
$codexArgs = @('-C', $RepoRoot, '-s', 'read-only', '-a', 'never') + $reviewArgs

# ANSI 색상 제거(저장 파일 가독성) + UTF-8 콘솔
$env:NO_COLOR = '1'
$prevOut = [Console]::OutputEncoding
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

Write-Host "▶ codex $($codexArgs -join ' ')" -ForegroundColor Cyan

# 네이티브 호출: codex는 시작 배너를 stderr로 출력 → PS5.1에서 Stop이면 종료성 에러가 되므로 Continue로 격리.
# 리뷰 본문은 stdout으로 나오며, stderr(배너/진행)는 콘솔로 흘려보낸다.
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $output = & $CodexExe @codexArgs | Out-String
}
finally {
    $ErrorActionPreference = $prevEAP
    [Console]::OutputEncoding = $prevOut
}

# 샌드박스 정리용 프로세스 종료 메시지("SUCCESS: The process with PID ...") 잡음 제거
$clean = ($output -split "`r?`n" | Where-Object { $_ -notmatch '^SUCCESS: The process with PID \d+' }) -join "`n"

Write-Host $clean

if (-not $NoSave) {
    # Docs\codex-reviews (Docs\Review 컨설팅 폴더와 Windows 대소문자 충돌 회피). gitignore됨.
    $reviewDir = Join-Path $RepoRoot 'Docs\codex-reviews'
    if (-not (Test-Path $reviewDir)) { New-Item -ItemType Directory -Path $reviewDir -Force | Out-Null }
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $outFile = Join-Path $reviewDir "codex-review-$stamp.md"
    $clean | Out-File -FilePath $outFile -Encoding utf8
    Write-Host "💾 저장: $outFile" -ForegroundColor Green
}
