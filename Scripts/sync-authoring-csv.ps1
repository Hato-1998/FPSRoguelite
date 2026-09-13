<#
.SYNOPSIS
  구글 시트 -> 리포 CSV 동기화 (LOC0 §5). ** 사람이 시트에서 편집한 것을 리포로 당겨오는 경로 ** 다.
.DESCRIPTION
  구글 시트 -> 리포 CSV 동기화 (LOC0 §5). 정본 = 구글 시트, 리포 CSV = 스냅샷 (2026-09-13 재개정, SHEET1).
  사람은 시트에서 직접 편집하고, 자동화는 Scripts/authoring_sheet.py apply 로 시트에 쓴다(apply 가 끝에 이 스크립트를 부른다).
  로컬 CSV가 매니페스트와 다르면(리포 CSV 를 직접 고쳤으면) 덮어쓰기를 거부한다(-Force 로만 강제).
  매니페스트 기록을 못 찾으면(파일 없음·파싱 실패·항목 없음) target 이 있는 시트는 덮지 않는다(fail-closed).

  Config/AuthoringSheets.json에 나열된 각 시트에 대해 공개 export URL
  (https://docs.google.com/spreadsheets/d/<sheetId>/export?format=csv&gid=<gid>)을 무인증 GET한다.
  받은 CSV의 1행 헤더가 expectedHeader와 정확히 일치하지 않으면 그 시트는 스킵하고(기존 파일 무접촉)
  전체 종료 코드를 1로 예약한다. 일치하면 target 경로에 UTF-8(BOM 없음)로 저장하고
  매핑 파일 옆 <매핑 파일명>.manifest.json(기본 Config/AuthoringSheets.manifest.json)에 {name, utcTime, sha256}을 기록한다.
  Windows PowerShell 5.1 호환(pwsh 전용 문법 없음) — Out-File -Encoding utf8은 BOM을 붙이므로
  [System.IO.File]::WriteAllText + UTF8Encoding($false)로 직접 쓴다.
.PARAMETER SheetName
  지정 시 AuthoringSheets.json의 해당 name 항목 하나만 동기화한다. 생략 시 전체.
.PARAMETER MappingPath
  매핑 파일 경로. 기본 = <리포>\Config\AuthoringSheets.json. manifest 는 같은 폴더의
  "<매핑 파일명(확장자 제외)>.manifest.json"(authoring_sheet.py manifest_path_for 와 같은 규칙).
.EXAMPLE
  powershell -File Scripts\sync-authoring-csv.ps1
.EXAMPLE
  powershell -File Scripts\sync-authoring-csv.ps1 -SheetName ST_UI
#>
[CmdletBinding()]
param(
    [string]$SheetName,

    # 로컬 CSV가 매니페스트와 다르면(=자동화나 사람이 리포에서 먼저 고쳤으면) 기본적으로 덮어쓰기를 거부한다.
    # 그 변경을 버리고 시트 내용으로 되돌릴 의도가 확실할 때만 이 스위치를 쓴다.
    [switch]$Force,

    # 매핑 파일 경로. 기본 = <리포>\Config\AuthoringSheets.json.
    # manifest = 같은 폴더의 "<매핑 파일명(확장자 제외)>.manifest.json" — authoring_sheet.py manifest_path_for 와 같은 규칙.
    [string]$MappingPath
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot

if (-not $MappingPath) {
    $MappingPath = Join-Path $RepoRoot 'Config\AuthoringSheets.json'
}
else {
    $MappingPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($MappingPath)
}

if (-not (Test-Path $MappingPath)) {
    Write-Error "시트 매핑 파일을 찾을 수 없습니다: $MappingPath"
    exit 1
}

$Mapping = Get-Content -Path $MappingPath -Raw | ConvertFrom-Json
$Sheets = $Mapping.sheets

if ($SheetName) {
    $Sheets = $Sheets | Where-Object { $_.name -eq $SheetName }
    if (-not $Sheets) {
        Write-Error "AuthoringSheets.json에 '$SheetName' 항목이 없습니다."
        exit 1
    }
}

# manifest 경로 파생 — authoring_sheet.py manifest_path_for 와 같은 규칙(매핑 파일 옆, 확장자만 교체).
# ⛔ [IO.Path]::ChangeExtension($p, $null) 금지 — PS 5.1이 $null을 빈 문자열로 넘겨 점 두 개짜리 파일명이 된다(SHEET1 G1 P1-1).
$ManifestPath = Join-Path (Split-Path -Parent $MappingPath) ([System.IO.Path]::GetFileNameWithoutExtension($MappingPath) + '.manifest.json')
$ManifestDir = Split-Path -Parent $ManifestPath

Write-Host "[manifest] $ManifestPath"

# 잠금 존중 — authoring_sheet.py apply/seed 가 시트를 쓰는 중(신선한 잠금 + 다른 세션)이면 아무것도 안 쓰고 종료한다(SHEET1 §5-5, G1 2회차 P3-B).
$LockPath = Join-Path $env:USERPROFILE '.fpsr\authoring-sheet.lock'
if (Test-Path -LiteralPath $LockPath) {
    $LockAgeSec = ([DateTime]::UtcNow - (Get-Item -LiteralPath $LockPath).LastWriteTimeUtc).TotalSeconds
    if ($LockAgeSec -le 900) {
        $LockParsedOk = $true
        $LockInfo = $null
        try {
            $LockJsonText = [System.IO.File]::ReadAllText($LockPath, [System.Text.Encoding]::UTF8)
            $LockInfo = $LockJsonText | ConvertFrom-Json
        }
        catch {
            # 잠금 JSON 파싱 실패는 신선한 잠금으로 본다(거부) — 다른 세션의 토큰인지 확인할 방법이 없다.
            $LockParsedOk = $false
        }
        # 내용이 비었거나 token 이 없어도 파싱 실패와 같게 본다(파일 생성 직후 기록 전의 순간 · 손으로 만든 잠금).
        # PS 5.1 은 빈 문자열·'{}' 를 예외 없이 받아 $null token 을 내고, 환경변수 미설정($null)과 비교하면 "같다"로 통과한다(SHEET1 부록 I-15).
        if ($LockParsedOk -and ($null -eq $LockInfo -or [string]::IsNullOrEmpty([string]$LockInfo.token))) {
            $LockParsedOk = $false
        }
        if (-not $LockParsedOk -or $LockInfo.token -ne $env:FPSR_AUTHORING_LOCK_TOKEN) {
            $LockCommand = if ($LockParsedOk) { $LockInfo.command } else { '(잠금 파일 파싱 실패)' }
            $LockPid = if ($LockParsedOk) { $LockInfo.pid } else { '?' }
            Write-Warning "authoring_sheet.py 가 시트를 쓰는 중입니다($LockCommand, pid $LockPid) — 끝난 뒤 다시 실행하세요"
            exit 1
        }
    }
    else {
        Write-Warning "[lock] 오래된 authoring-sheet.lock 발견(마지막 갱신 $([int]$LockAgeSec)초 전, 900초 초과) — 진행합니다: $LockPath"
    }
}

$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

$ManifestEntries = @{}
$bManifestParseFailed = $false
if (Test-Path $ManifestPath) {
    try {
        $ExistingManifest = Get-Content -Path $ManifestPath -Raw | ConvertFrom-Json
        foreach ($Entry in $ExistingManifest) {
            $ManifestEntries[$Entry.name] = $Entry
        }
    }
    catch {
        Write-Warning "manifest 파싱 실패 — 이번 실행은 target 이 있는 시트를 덮지 않습니다(fail-closed): $($_.Exception.Message)"
        $bManifestParseFailed = $true
    }
}

$bHadFailure = $false
$SavedCount = 0

foreach ($Sheet in $Sheets) {
    $Name = $Sheet.name
    $TargetRelative = $Sheet.target
    $TargetPath = [System.IO.Path]::Combine($RepoRoot, $TargetRelative)
    $ExpectedHeader = @($Sheet.expectedHeader)
    # gid 공란/누락 = 첫 번째(유일) 탭 export. "파일>가져오기>현재 시트 교체"가 탭을 갈아끼우며 gid를 바꾸는
    # 갓차가 있어(C3 실측 2026-08-13, 구 gid로는 HTTP 400) 우리 시트는 스프레드시트당 탭 1개 원칙 + gid 생략이 기본.
    $ExportUrl = "https://docs.google.com/spreadsheets/d/$($Sheet.sheetId)/export?format=csv"
    if ($Sheet.PSObject.Properties['gid'] -and $Sheet.gid) {
        $ExportUrl += "&gid=$($Sheet.gid)"
    }

    Write-Host "[$Name] GET $ExportUrl"

    try {
        $Response = Invoke-WebRequest -Uri $ExportUrl -UseBasicParsing -TimeoutSec 30
    }
    catch {
        Write-Warning "[$Name] 다운로드 실패: $($_.Exception.Message) — 기존 파일 무접촉."
        $bHadFailure = $true
        continue
    }

    # PS 5.1의 $Response.Content(문자열)는 charset 미지정 응답을 Latin-1로 디코드해 UTF-8 본문을 이중 인코딩으로
    # 파손시킨다(C3에서 실측). 원시 바이트를 받아 UTF-8로만 해석하고, 저장도 그 바이트를 그대로 쓴다.
    $RawBytes = $null
    if ($Response.RawContentStream) {
        $RawBytes = $Response.RawContentStream.ToArray()
    }
    if (-not $RawBytes -or $RawBytes.Length -eq 0) {
        Write-Warning "[$Name] 응답이 비어 있습니다 — 기존 파일 무접촉."
        $bHadFailure = $true
        continue
    }
    $Content = [System.Text.Encoding]::UTF8.GetString($RawBytes)

    # 헤더 행만 파싱해서 비교 (본문은 검증 통과 후 그대로 저장 — 우리가 재직렬화하지 않는다).
    $FirstLineBreak = $Content.IndexOfAny([char[]]@("`r", "`n"))
    $HeaderLine = if ($FirstLineBreak -ge 0) { $Content.Substring(0, $FirstLineBreak) } else { $Content }
    $ActualHeader = $HeaderLine.Split(',') | ForEach-Object { $_.Trim('"') }

    $HeaderMatches = ($ActualHeader.Count -eq $ExpectedHeader.Count)
    if ($HeaderMatches) {
        for ($i = 0; $i -lt $ExpectedHeader.Count; $i++) {
            if ($ActualHeader[$i] -ne $ExpectedHeader[$i]) {
                $HeaderMatches = $false
                break
            }
        }
    }

    if (-not $HeaderMatches) {
        Write-Warning "[$Name] 헤더 불일치 (기대: $($ExpectedHeader -join ','), 실제: $($ActualHeader -join ',')) — 이 파일 스킵, 기존 파일 무접촉."
        $bHadFailure = $true
        continue
    }

    # target 은 있는데 이 시트의 manifest 기록을 못 찾으면(파일 없음·파싱 실패·항목 없음) 가드가 판정할 근거가 없다 → 덮지 않는다.
    # (종전엔 이 경우 가드를 통과해 덮어썼다 = fail-open. manifest 경로가 틀려도 조용히 리포 CSV 를 잃던 계급을 막는 유일한 지점. SHEET1 G1 P1-1)
    if (-not $Force -and (Test-Path $TargetPath) -and -not $ManifestEntries.ContainsKey($Name)) {
        Write-Warning "[$Name] manifest 기록이 없어 로컬 CSV 가 앞서 있는지 판정할 수 없습니다 — 덮어쓰기를 거부합니다(기존 파일 무접촉). manifest: $ManifestPath"
        Write-Warning "         처음 받는 테이블이면 target CSV 가 없어야 합니다. 로컬 CSV 를 버리고 시트로 덮으려면: -Force"
        $bHadFailure = $true
        continue
    }

    # 로컬-앞섬 가드: 리포 CSV가 마지막으로 당겨온 내용(매니페스트 sha256)과 다르면, 누군가 리포 CSV 를
    # 직접 고쳤다는 뜻이다. 그대로 덮으면 그 변경이 흔적 없이 사라진다.
    # (정본 = 시트. 리포 CSV 직접 편집은 정본 위반이라 조용히 덮지 않고 막는다. Localization.md L-5)
    if (-not $Force -and (Test-Path $TargetPath) -and $ManifestEntries.ContainsKey($Name)) {
        $LocalSha = (Get-FileHash -Path $TargetPath -Algorithm SHA256).Hash
        if ($LocalSha -ne $ManifestEntries[$Name].sha256) {
            Write-Warning "[$Name] 로컬 CSV가 마지막 pull보다 앞서 있습니다 — 덮어쓰기를 거부합니다(기존 파일 무접촉)."
            Write-Warning "         무엇이 다른지: git diff -- $TargetRelative"
            Write-Warning "         정본은 시트다: 그 변경을 변경셋으로 만들어 python Scripts/authoring_sheet.py apply 로 시트에 쓰고, 리포 파일은 git checkout 으로 되돌린다"
            Write-Warning "         로컬 변경을 버리고 시트로 되돌리려면: -Force"
            $bHadFailure = $true
            continue
        }
    }

    # 헤더 검증 통과 — 받은 원시 바이트를 그대로 저장(재직렬화·재인코딩 없음. Sheets export는 BOM 없는 UTF-8).
    $TargetDir = Split-Path -Parent $TargetPath
    if (-not (Test-Path $TargetDir)) {
        New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null
    }
    [System.IO.File]::WriteAllBytes($TargetPath, $RawBytes)
    $SavedCount++

    $Sha256 = (Get-FileHash -Path $TargetPath -Algorithm SHA256).Hash
    $ManifestEntries[$Name] = [PSCustomObject]@{
        name    = $Name
        utcTime = (Get-Date).ToUniversalTime().ToString('o')
        sha256  = $Sha256
    }

    Write-Host "[$Name] 동기화 완료 -> $TargetRelative (sha256=$Sha256)"
}

if ($bManifestParseFailed -or $SavedCount -eq 0) {
    Write-Host "[manifest] 기록 안 함(파싱 실패 또는 저장 0건) — $ManifestPath"
}
else {
    if (-not (Test-Path $ManifestDir)) {
        New-Item -ItemType Directory -Force -Path $ManifestDir | Out-Null
    }
    # @()로 강제 배열화 — 항목 1개일 때 ConvertTo-Json이 단일 오브젝트를 내보내 스키마가 요동하는 것 방지(레드팀 P3-6b).
    $ManifestJson = ConvertTo-Json -InputObject @($ManifestEntries.Values | Sort-Object name) -Depth 4
    [System.IO.File]::WriteAllText($ManifestPath, $ManifestJson, $Utf8NoBom)
}

if ($bHadFailure) {
    Write-Warning "일부 시트 동기화 실패 — 위 로그 참조."
    exit 1
}

Write-Host "모든 시트 동기화 완료."
exit 0
