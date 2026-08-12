<#
.SYNOPSIS
  구글 시트(저작 마스터) -> 리포 CSV(빌드 스냅샷) 동기화 (LOC0 §5).
.DESCRIPTION
  Config/AuthoringSheets.json에 나열된 각 시트에 대해 공개 export URL
  (https://docs.google.com/spreadsheets/d/<sheetId>/export?format=csv&gid=<gid>)을 무인증 GET한다.
  받은 CSV의 1행 헤더가 expectedHeader와 정확히 일치하지 않으면 그 시트는 스킵하고(기존 파일 무접촉)
  전체 종료 코드를 1로 예약한다. 일치하면 target 경로에 UTF-8(BOM 없음)로 저장하고
  Content/StringTables/.sync-manifest.json에 {name, utcTime, sha256}을 기록한다.
  Windows PowerShell 5.1 호환(pwsh 전용 문법 없음) — Out-File -Encoding utf8은 BOM을 붙이므로
  [System.IO.File]::WriteAllText + UTF8Encoding($false)로 직접 쓴다.
.PARAMETER SheetName
  지정 시 AuthoringSheets.json의 해당 name 항목 하나만 동기화한다. 생략 시 전체.
.EXAMPLE
  powershell -File Scripts\sync-authoring-csv.ps1
.EXAMPLE
  powershell -File Scripts\sync-authoring-csv.ps1 -SheetName ST_UI
#>
[CmdletBinding()]
param(
    [string]$SheetName
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$MappingPath = Join-Path $RepoRoot 'Config\AuthoringSheets.json'

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

$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$ManifestDir = Join-Path $RepoRoot 'Content\StringTables'
$ManifestPath = Join-Path $ManifestDir '.sync-manifest.json'

$ManifestEntries = @{}
if (Test-Path $ManifestPath) {
    try {
        $ExistingManifest = Get-Content -Path $ManifestPath -Raw | ConvertFrom-Json
        foreach ($Entry in $ExistingManifest) {
            $ManifestEntries[$Entry.name] = $Entry
        }
    }
    catch {
        Write-Warning "기존 .sync-manifest.json 파싱 실패 — 새로 씁니다: $($_.Exception.Message)"
    }
}

$bHadFailure = $false

foreach ($Sheet in $Sheets) {
    $Name = $Sheet.name
    $TargetRelative = $Sheet.target
    $TargetPath = Join-Path $RepoRoot $TargetRelative
    $ExpectedHeader = @($Sheet.expectedHeader)
    $ExportUrl = "https://docs.google.com/spreadsheets/d/$($Sheet.sheetId)/export?format=csv&gid=$($Sheet.gid)"

    Write-Host "[$Name] GET $ExportUrl"

    try {
        $Response = Invoke-WebRequest -Uri $ExportUrl -UseBasicParsing
    }
    catch {
        Write-Warning "[$Name] 다운로드 실패: $($_.Exception.Message) — 기존 파일 무접촉."
        $bHadFailure = $true
        continue
    }

    $Content = $Response.Content
    if ([string]::IsNullOrEmpty($Content)) {
        Write-Warning "[$Name] 응답이 비어 있습니다 — 기존 파일 무접촉."
        $bHadFailure = $true
        continue
    }

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

    # 헤더 검증 통과 — target에 UTF-8(BOM 없음)으로 저장.
    $TargetDir = Split-Path -Parent $TargetPath
    if (-not (Test-Path $TargetDir)) {
        New-Item -ItemType Directory -Force -Path $TargetDir | Out-Null
    }
    [System.IO.File]::WriteAllText($TargetPath, $Content, $Utf8NoBom)

    $Sha256 = (Get-FileHash -Path $TargetPath -Algorithm SHA256).Hash
    $ManifestEntries[$Name] = [PSCustomObject]@{
        name    = $Name
        utcTime = (Get-Date).ToUniversalTime().ToString('o')
        sha256  = $Sha256
    }

    Write-Host "[$Name] 동기화 완료 -> $TargetRelative (sha256=$Sha256)"
}

if (-not (Test-Path $ManifestDir)) {
    New-Item -ItemType Directory -Force -Path $ManifestDir | Out-Null
}
$ManifestJson = ($ManifestEntries.Values | Sort-Object name | ConvertTo-Json -Depth 4)
[System.IO.File]::WriteAllText($ManifestPath, $ManifestJson, $Utf8NoBom)

if ($bHadFailure) {
    Write-Warning "일부 시트 동기화 실패 — 위 로그 참조."
    exit 1
}

Write-Host "모든 시트 동기화 완료."
exit 0
