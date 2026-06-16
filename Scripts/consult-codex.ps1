<#
.SYNOPSIS
  ConsultLoop용 Codex(클라이언트/콘텐츠 렌즈) 비대화 토론 래퍼 — `codex exec`.
.DESCRIPTION
  백엔드(Claude)×클라이언트(Codex) 컨설팅 루프에서 Codex 측 입장·반론을 받는다(Docs/ConsultLoop.md).
  코드 diff 게이트인 codex-review.ps1(`codex review`)과 다르다 — 이쪽은 임의 프롬프트 토론(`codex exec`).
  Codex는 리포의 AGENTS.md(장르 정체성·핵심 3원칙)를 자동 로드한다. 프롬프트는 stdin으로 전달.
  read-only 샌드박스 · 비대화. 결과는 stdout 반환 + (옵션) Docs/Review/_raw/ 에 타임스탬프 저장.
  주의: workspace 쓰기 없는 read-only지만 신뢰 로컬 리포 전용으로 쓸 것.
.EXAMPLE
  Scripts\consult-codex.ps1 -PromptFile .\round1.txt
  Scripts\consult-codex.ps1 -Prompt "스폰 디렉터 복제 모델 한 줄 의견" -DryRun
  $env:CODEX_EXE = 'D:\tools\codex.exe'; Scripts\consult-codex.ps1 -PromptFile r.txt   # 경로 강제
#>
[CmdletBinding(DefaultParameterSetName = 'File')]
param(
    [Parameter(ParameterSetName = 'File', Mandatory)]
    [string]$PromptFile,
    [Parameter(ParameterSetName = 'Inline', Mandatory)]
    [string]$Prompt,
    [string]$Model,
    [string]$Title,
    [switch]$DryRun,
    [switch]$NoSave
)

$ErrorActionPreference = 'Stop'
$RepoRoot = Split-Path -Parent $PSScriptRoot

# --- Codex 자동탐지 (이식성): 환경변수 → PATH → 알려진 네이티브 설치경로 ---
function Resolve-Codex {
    if ($env:CODEX_EXE -and (Test-Path $env:CODEX_EXE)) { return $env:CODEX_EXE }
    $cmd = Get-Command codex -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $known = Join-Path $env:LOCALAPPDATA 'OpenAI\Codex\bin\codex.exe'
    if (Test-Path $known) { return $known }
    return $null
}

$Codex = Resolve-Codex
if (-not $Codex) {
    Write-Error ("Codex CLI를 찾을 수 없습니다. 설치/인증 후 다시 실행하세요: " +
        "'npm install -g @openai/codex' 그리고 'codex login'(브라우저 ChatGPT 인증 1회). " +
        "경로를 직접 지정하려면 환경변수 CODEX_EXE 에 codex 실행파일 경로를 설정하세요.")
    exit 1
}

# --- 프롬프트 확보 ---
if ($PSCmdlet.ParameterSetName -eq 'File') {
    if (-not (Test-Path $PromptFile)) { Write-Error "PromptFile 없음: $PromptFile"; exit 1 }
    $PromptText = Get-Content -Path $PromptFile -Raw -Encoding UTF8
}
else {
    $PromptText = $Prompt
}

# --- codex exec 인자 (전역 -C 루트 + read-only, 프롬프트는 stdin '-') ---
# ⚠️ 인코딩: npm codex.ps1 셸로 한글을 파이프하면 $input 재인코딩으로 mojibake가 된다(스모크로 확인).
# 따라서 프롬프트를 UTF-8(no BOM) 파일에 쓰고 .cmd 런처에 Start-Process -RedirectStandardInput 로 raw 바이트 전달한다.
$argStr = "-C `"$RepoRoot`" exec -s read-only"
if ($Model) { $argStr += " -m $Model" }
$argStr += " -"

# .ps1 셸 대신 .cmd 런처 사용 (Start-Process 가 stdin 파일을 child node 로 그대로 상속 → 인코딩 안전)
$Launcher = if ($Codex -match '\.ps1$') { [IO.Path]::ChangeExtension($Codex, 'cmd') } else { $Codex }
if (-not (Test-Path $Launcher)) { $Launcher = $Codex }

if ($DryRun) {
    Write-Host "▶ [DryRun] 런처: $Launcher" -ForegroundColor Cyan
    Write-Host "▶ [DryRun] 명령: codex $argStr" -ForegroundColor Cyan
    Write-Host "▶ [DryRun] stdin 프롬프트 (호출 안 함):" -ForegroundColor Cyan
    Write-Host "----------------------------------------"
    Write-Host $PromptText
    Write-Host "----------------------------------------"
    exit 0
}

$env:NO_COLOR = '1'
$utf8 = New-Object System.Text.UTF8Encoding $false
$tmpPrompt = [IO.Path]::GetTempFileName()
$tmpOut = [IO.Path]::GetTempFileName()
$tmpErr = [IO.Path]::GetTempFileName()
[IO.File]::WriteAllText($tmpPrompt, $PromptText, $utf8)

Write-Host "▶ codex $argStr  (런처: $Launcher)" -ForegroundColor Cyan

try {
    Start-Process -FilePath $Launcher -ArgumentList $argStr `
        -RedirectStandardInput $tmpPrompt -RedirectStandardOutput $tmpOut -RedirectStandardError $tmpErr `
        -NoNewWindow -Wait | Out-Null
    $output = [IO.File]::ReadAllText($tmpOut, [System.Text.Encoding]::UTF8)
}
finally {
    Remove-Item $tmpPrompt, $tmpOut, $tmpErr -ErrorAction SilentlyContinue
}

# 샌드박스 정리 잡음("SUCCESS: The process with PID ...") 제거
$clean = ($output -split "`r?`n" | Where-Object { $_ -notmatch '^SUCCESS: The process with PID \d+' }) -join "`n"
Write-Host $clean

if (-not $NoSave) {
    $rawDir = Join-Path $RepoRoot 'Docs\Review\_raw'
    if (-not (Test-Path $rawDir)) { New-Item -ItemType Directory -Path $rawDir -Force | Out-Null }
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $slug = if ($Title) { ($Title -replace '[^\w\-]+', '-').Trim('-') } else { 'round' }
    $outFile = Join-Path $rawDir "$stamp-$slug.md"
    $clean | Out-File -FilePath $outFile -Encoding utf8
    Write-Host "💾 저장: $outFile" -ForegroundColor DarkGray
}
