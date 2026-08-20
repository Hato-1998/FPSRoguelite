<#
.SYNOPSIS
  FPSRoguelite BP 인라인 Text 감사(M0 EC 2, Roadmap.md 7-6) — GatherTextFromAssets로 BP 위젯 등 에셋의
  인라인 Text를 매니페스트에 모으고, 얼라우리스트와 대조해 신규(미검토) 항목을 표로 보여준다.
.DESCRIPTION
  UnrealEditor-Cmd로 -run=GatherText를 Config/Localization/Game_AuditBPText.ini 한 개로 무인 실행해
  Saved/Localization/BPTextAudit/BPTextAudit.manifest를 생성한 뒤, 그 매니페스트(UTF-16 LE + BOM)를
  파싱해 네임스페이스·Subnamespaces를 재귀적으로 평탄화한다. 각 발견 레코드는 Namespace/Key/Path에
  "WBP_"가 포함되면 게이트 대상(BP 위젯)으로, 아니면 참고(비-위젯 프로젝트 에셋)로 분류한다.
  게이트 대상 중 Config/Localization/BPTextAudit_Allowlist.json에 없는(면제되지 않은) 신규 항목이
  1건 이상이면 exit 1로 끝내 EC 2 회귀를 표시한다.
  이 스크립트는 감사 전용이며 Content/Localization/ 아래 출하 산출물은 전혀 건드리지 않는다(읽지도
  않는다) — 모든 산출물은 Saved/ 아래에 남는다.
  엔진 경로 해석·UnrealEditor-Cmd 인보크·로그 경로·exit 코드 패턴은 Scripts\localization-gather.ps1을
  그대로 미러한다(Windows PowerShell 5.1 호환, pwsh 전용 문법 없음).
.PARAMETER EnginePath
  UE 5.7 엔진 루트. 기본값은 이 저장소가 개발 중인 머신의 고정 경로.
.PARAMETER SkipGather
  지정 시 커맨드렛을 돌리지 않고 이미 있는 매니페스트를 그대로 파싱한다(빠른 반복용). 매니페스트가
  없으면 에러 후 exit 1.
.PARAMETER ReportOnly
  지정 시 신규(미검토) 게이트 대상이 있어도 항상 exit 0으로 끝난다(조사용, 실패 게이트로 쓰지 않음).
.PARAMETER AllowlistPath
  얼라우리스트 JSON 경로. 비어 있으면 Config/Localization/BPTextAudit_Allowlist.json을 쓴다.
.PARAMETER LogName
  GatherText abslog 파일명. 기본값은 타임스탬프가 붙은 자동 생성 이름.
.EXAMPLE
  powershell -File Scripts\localization-audit-bptext.ps1
.EXAMPLE
  powershell -File Scripts\localization-audit-bptext.ps1 -SkipGather -ReportOnly
#>
[CmdletBinding()]
param(
    [string]$EnginePath = 'D:\UnrealEngine\UE_5.7',
    [switch]$SkipGather,
    [switch]$ReportOnly,
    [string]$AllowlistPath,
    [string]$LogName = ("localization-audit-bptext_{0}.log" -f (Get-Date -Format 'yyyyMMdd_HHmmss'))
)

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrEmpty($AllowlistPath)) {
    $AllowlistPath = Join-Path $RepoRoot 'Config\Localization\BPTextAudit_Allowlist.json'
}

function Get-AuditRecords {
    # 매니페스트 네임스페이스 노드를 재귀적으로 평탄화해 발견 레코드 배열을 만든다.
    # PS 5.1에서 ConvertFrom-Json 결과의 단일 항목은 배열이 아닌 단일 객체로 오므로 순회 지점마다 @( )로 감싼다.
    param(
        [Parameter(Mandatory = $true)]
        $Node,
        [string]$ParentNamespace = ''
    )

    $Records = @()

    $CurrentNamespace = $ParentNamespace
    if ($null -ne $Node.Namespace) {
        $CurrentNamespace = $Node.Namespace
    }

    if ($null -ne $Node.Children) {
        foreach ($Child in @($Node.Children)) {
            $SourceText = ''
            if ($null -ne $Child.Source -and $null -ne $Child.Source.Text) {
                $SourceText = $Child.Source.Text
            }
            if ($null -ne $Child.Keys) {
                foreach ($K in @($Child.Keys)) {
                    $KeyValue = ''
                    if ($null -ne $K.Key) {
                        $KeyValue = $K.Key
                    }
                    $PathValue = ''
                    if ($null -ne $K.Path) {
                        $PathValue = $K.Path
                    }
                    $Records += [pscustomobject]@{
                        Namespace = $CurrentNamespace
                        Source    = $SourceText
                        Key       = $KeyValue
                        Path      = $PathValue
                    }
                }
            }
        }
    }

    if ($null -ne $Node.Subnamespaces) {
        foreach ($Sub in @($Node.Subnamespaces)) {
            $Records += Get-AuditRecords -Node $Sub -ParentNamespace $CurrentNamespace
        }
    }

    return $Records
}

function Test-AllowlistMatch {
    # entry의 namespace/key/path/source 중 값이 있는 필드만 레코드와 대소문자 구분(-ceq) 비교한다.
    # 값이 하나도 없는 entry(전체 와일드카드)는 이 함수 호출 전에 얼라우리스트 로딩부에서 이미 걸러지므로
    # 여기 도달하는 entry는 항상 필드가 1개 이상 있다고 가정한다.
    param(
        [Parameter(Mandatory = $true)]
        $Record,
        [Parameter(Mandatory = $true)]
        $Entry
    )

    $FieldMap = @{
        namespace = $Record.Namespace
        key       = $Record.Key
        path      = $Record.Path
        source    = $Record.Source
    }

    foreach ($FieldName in @('namespace', 'key', 'path', 'source')) {
        $EntryValue = $Entry.$FieldName
        if (-not [string]::IsNullOrEmpty($EntryValue)) {
            if (-not ($FieldMap[$FieldName] -ceq $EntryValue)) {
                return $false
            }
        }
    }

    return $true
}

# --- 1. 엔진/uproject 경로 확인 ---
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

# --- 2. 매니페스트 경로 ---
$ManifestPath = Join-Path $RepoRoot 'Saved\Localization\BPTextAudit\BPTextAudit.manifest'

# --- 3. gather (SkipGather 미지정 시) ---
if (-not $SkipGather) {
    $LogDir = Join-Path $RepoRoot 'Saved\Logs'
    if (-not (Test-Path $LogDir)) {
        New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
    }
    $AbsLog = Join-Path $LogDir $LogName

    Write-Host "GatherText 실행: $EditorCmd"
    Write-Host "Config: Config/Localization/Game_AuditBPText.ini"
    Write-Host "로그: $AbsLog"

    & $EditorCmd $UProject "-run=GatherText" "-config=Config/Localization/Game_AuditBPText.ini" -log "-abslog=$AbsLog"
    $ExitCode = $LASTEXITCODE

    if ($ExitCode -ne 0) {
        Write-Warning "GatherText 실패 (exit $ExitCode). 로그: $AbsLog"
        exit $ExitCode
    }

    Write-Host "GatherText 통과 (exit 0)."
}

if (-not (Test-Path $ManifestPath)) {
    Write-Error "매니페스트를 찾을 수 없습니다: $ManifestPath (-SkipGather 없이 다시 실행하세요)"
    exit 1
}

# --- 4. 매니페스트 파싱 ---
# 매니페스트는 UTF-16 LE + BOM으로 기록된다(GenerateGatherManifest 커맨드렛 기본 출력 인코딩).
# Get-Content 기본 인코딩(코드페이지 추정)으로 읽으면 한글 등 비-ASCII 문자가 깨지므로
# .NET File.ReadAllText로 직접 읽어 BOM 기반 인코딩 자동 감지를 그대로 활용한다(BOM은 자동으로 제거된다).
$ManifestText = [System.IO.File]::ReadAllText($ManifestPath)
$Manifest = $ManifestText | ConvertFrom-Json

# --- 5. 평탄화 ---
$AllRecords = @(Get-AuditRecords -Node $Manifest -ParentNamespace '')

# --- 6. 분류 (게이트 대상 vs 참고) ---
# 게이트 = EC 2가 정의한 "BP 위젯의 인라인 Text". 판정 규칙(실측 2026-08-19 기준):
#  · 위젯 판별 = 경로에 'WBP_'(이 리포의 위젯 명명규약) 또는 ':WidgetTree.'(엔진이 붙이는 위젯 트리 경로).
#    후자를 함께 보는 이유 = 앞으로 WBP_ 접두사를 안 쓰는 위젯이 생겨도 게이트가 뚫리지 않게.
#  · EUW_ 제외 = 에디터 유틸리티 위젯은 개발 도구이고 패키지에 실리지 않는다(EUW_CityGen).
#  · 네임스페이스 UObjectDisplayNames 제외 = 위젯 애니메이션 트랙의 DisplayName('Render Opacity' 등)은
#    엔진이 자동 생성하는 에디터 메타데이터이며 플레이어에게 표시되지 않는다. 경로로 얼라우리스트에
#    넣으면 애니메이션 트랙을 가진 위젯이 새로 생길 때마다 오탐이 나므로 구조적으로 제외한다.
foreach ($Record in $AllRecords) {
    $Combined = '{0}{1}{2}' -f $Record.Namespace, $Record.Key, $Record.Path
    $IsWidget = $Combined.Contains('WBP_') -or $Record.Path.Contains(':WidgetTree.')
    $IsEditorUtilityWidget = $Record.Path.Contains('EUW_')
    $IsEngineMetaNamespace = ($Record.Namespace -ceq 'UObjectDisplayNames')
    $IsGate = $IsWidget -and (-not $IsEditorUtilityWidget) -and (-not $IsEngineMetaNamespace)
    Add-Member -InputObject $Record -NotePropertyName 'IsGate' -NotePropertyValue $IsGate
}

# --- 7. 얼라우리스트 대조 ---
$AllowlistEntries = @()
if (Test-Path $AllowlistPath) {
    $AllowlistText = Get-Content -Path $AllowlistPath -Raw -Encoding UTF8
    $AllowlistJson = $AllowlistText | ConvertFrom-Json
    if ($null -ne $AllowlistJson.entries) {
        $AllowlistEntries = @($AllowlistJson.entries)
    }
}
else {
    Write-Warning "얼라우리스트 파일을 찾을 수 없습니다: $AllowlistPath (빈 목록으로 진행)"
}

$ValidatedEntries = @()
foreach ($Entry in $AllowlistEntries) {
    $HasAnyField = $false
    foreach ($FieldName in @('namespace', 'key', 'path', 'source')) {
        $EntryValue = $Entry.$FieldName
        if (-not [string]::IsNullOrEmpty($EntryValue)) {
            $HasAnyField = $true
        }
    }
    if (-not $HasAnyField) {
        Write-Warning "얼라우리스트 entry에 유효한 필드가 하나도 없어 무시합니다(전체 와일드카드 금지): $($Entry | ConvertTo-Json -Compress)"
        continue
    }
    $ValidatedEntries += $Entry
}

foreach ($Record in $AllRecords) {
    $IsExempt = $false
    foreach ($Entry in $ValidatedEntries) {
        if (Test-AllowlistMatch -Record $Record -Entry $Entry) {
            $IsExempt = $true
            break
        }
    }
    Add-Member -InputObject $Record -NotePropertyName 'IsExempt' -NotePropertyValue $IsExempt
}

# --- 8. 출력 ---
$GateRows = @()
$InfoRows = @()
foreach ($Record in $AllRecords) {
    $StatusLabel = '신규'
    if ($Record.IsExempt) {
        $StatusLabel = '면제'
    }

    $Row = [pscustomobject]@{
        상태      = $StatusLabel
        Namespace = $Record.Namespace
        Key       = $Record.Key
        Path      = $Record.Path
        Source    = $Record.Source
    }

    if ($Record.IsGate) {
        $GateRows += $Row
    }
    else {
        $InfoRows += $Row
    }
}

# Format-Table -AutoSize만으로는 좁은 콘솔·파이프에서 열이 잘린다. Source 원문은 얼라우리스트 시딩의
# 근거라 절단되면 안 되므로 Out-String -Width로 렌더 폭을 콘솔과 무관하게 고정한다.
Write-Host ''
Write-Host '=== 게이트 대상 (BP 위젯) ==='
$GateRows | Format-Table -AutoSize -Wrap -Property '상태', 'Namespace', 'Key', 'Path', 'Source' | Out-String -Width 4096 | Write-Host

Write-Host '=== 참고 (비-위젯 프로젝트 에셋) ==='
$InfoRows | Format-Table -AutoSize -Wrap -Property '상태', 'Namespace', 'Key', 'Path', 'Source' | Out-String -Width 4096 | Write-Host

$TotalCount = @($AllRecords).Count
$GateExemptCount = @($AllRecords | Where-Object { $_.IsGate -and $_.IsExempt }).Count
$GateNewCount = @($AllRecords | Where-Object { $_.IsGate -and (-not $_.IsExempt) }).Count
$InfoCount = @($AllRecords | Where-Object { -not $_.IsGate }).Count

Write-Host "총 발견: $TotalCount 건"
Write-Host "게이트 대상: 면제 $GateExemptCount 건 / 신규 $GateNewCount 건"
Write-Host "참고: $InfoCount 건"

# --- 9. 종료 코드 ---
if ($GateNewCount -gt 0) {
    if ($ReportOnly) {
        Write-Host "판정: 신규 BP 인라인 Text $GateNewCount 건 — EC 2 회귀 (ReportOnly 지정 — exit 0)"
        exit 0
    }
    Write-Host "판정: 신규 BP 인라인 Text $GateNewCount 건 — EC 2 회귀"
    exit 1
}

Write-Host "판정: 신규 BP 인라인 Text 없음 — EC 2 통과"
exit 0
