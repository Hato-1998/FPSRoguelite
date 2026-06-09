# Printed automatically at Claude Code SessionStart (see .claude/settings.json) so a NEW session resumes the
# code-only-per-branch workflow without re-explaining: shows the in-progress branch, recent commits, and what's
# uncommitted (content is intentionally left for the user). Full PROGRESS.md + Game.MD must still be read per CLAUDE.md.
$ErrorActionPreference = 'SilentlyContinue'

Write-Output '=== SESSION RESUME - git state ==='
Write-Output ("Branch: " + (git rev-parse --abbrev-ref HEAD))
Write-Output '--- recent commits ---'
git log --oneline -6
Write-Output '--- uncommitted (Content/* = user asset work; code should already be committed+verified) ---'
git status --short
Write-Output ''
Write-Output '=== PROGRESS.md (top) - open the FULL file + Game.MD before working (CLAUDE.md rule) ==='
if (Test-Path PROGRESS.md) { Get-Content PROGRESS.md -TotalCount 24 } else { Write-Output '(PROGRESS.md not found)' }
