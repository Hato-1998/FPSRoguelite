# SessionStart 훅 (.claude/settings.json 참조). 새 세션이 설명 없이 이어받도록
# 현재 브랜치·최근 커밋·미커밋 상태와 PROGRESS.md 포인터(= PM 보드 하드 게이트)를 출력한다.
$ErrorActionPreference = 'SilentlyContinue'

Write-Output '=== SESSION RESUME - git state ==='
Write-Output ("Branch: " + (git rev-parse --abbrev-ref HEAD))
Write-Output '--- recent commits ---'
git log --oneline -6
Write-Output '--- uncommitted ---'
git status --short
Write-Output ''
Write-Output '=== PROGRESS.md (top) - PM board hard gate: claim via /board before any commit-producing work ==='
if (Test-Path PROGRESS.md) { Get-Content PROGRESS.md -TotalCount 24 } else { Write-Output '(PROGRESS.md not found)' }
