# Scripts/board-snapshot.ps1
#
# 무엇을 하는지: Notion "작업 보드" 데이터소스를 REST API로 직접 조회해 진행중·차단·
#   크리티컬 행만 로컬 파일 .claude/board-snapshot.md 로 떨군다. Claude Code
#   SessionStart 훅에서 이 스크립트를 실행해 두면, 세션은 네트워크를 타지 않고 이
#   파일만 읽어 보드 상태를 안다. (보드 스키마·워크플로 전체 = Docs/SSOT/Workflow.md §6-9)
#
# 토큰을 어떻게 넣는가: ① 환경변수 NOTION_TOKEN, 또는 ② 파일 .claude/notion-token.txt
#   의 첫 줄(trim). 둘 다 없으면 에러가 아니라 정상 종료(exit 0)하고 안내 한 줄만
#   출력한다 — 이 스크립트 때문에 SessionStart 훅이 깨지는 일은 없어야 한다.
#   ⚠️ 토큰 파일은 커밋 대상이 아니다 — .gitignore 가 `.claude/*` 와
#   `.claude/notion-token.txt` 로 이중으로 이미 막는다(2026-09-09 git check-ignore 확인).
#
# 왜 페이지네이션이 필수인가: 이 스크립트가 존재하는 이유의 절반이 이 사고다 — 보드
#   조회가 기본 페이지 크기에서 조용히 잘려 "진행중" 행을 놓친 적이 있다. has_more 가
#   true 인 동안 next_cursor 로 계속 받아야 하고, 절대 첫 페이지만 받고 끝내면 안 된다
#   (안전장치로 최대 20페이지 상한을 둔다 — 상한에 걸리면 출력에 경고를 남긴다).
#
# 왜 바디를 바이트 배열로 보내는가: PS 5.1 은 -Body 문자열의 비-ASCII 를 '?' 로 뭉갠다.
#   한글 속성명('상태'·'우선순위')이 그대로 죽어 조회가 전부 실패한다 — 아래 4번 주석 참조.
#
# 왜 BOM 이 필요한가: PowerShell 5.1 은 BOM 없는 .ps1 을 시스템 ANSI(한국어 Windows =
#   cp949)로 읽어서 아래 한글 리터럴(속성명·상태값)을 통째로 깨뜨린다. 그래서 이
#   파일은 반드시 UTF-8 BOM 으로 저장한다 — 같은 함정을 이미 겪은
#   Scripts/pm-board-guard.ps1, Scripts/session-resume.ps1 참조.

$ErrorActionPreference = 'Stop'

$TokenFile     = '.claude/notion-token.txt'
$OutputFile    = '.claude/board-snapshot.md'
$TempFile      = "$OutputFile.tmp"
$DataSourceId  = '063de2a5-482d-46be-9cbb-6a6610b7f141'
$ApiUrl        = "https://api.notion.com/v1/data_sources/$DataSourceId/query"
$NotionVersion = '2026-03-11'
$MaxPages      = 20
$PageSize      = 100

function Get-TitleText {
    param($Prop)
    if (-not $Prop -or -not $Prop.title) { return '' }
    (($Prop.title | ForEach-Object { $_.plain_text }) -join '').Trim()
}

function Get-RichTextValue {
    param($Prop)
    if (-not $Prop -or -not $Prop.rich_text) { return '' }
    (($Prop.rich_text | ForEach-Object { $_.plain_text }) -join '').Trim()
}

function Get-SelectValue {
    param($Prop)
    if (-not $Prop -or -not $Prop.select) { return '' }
    $Prop.select.name
}

function Format-BoardRow {
    param($Row)
    $props    = $Row.properties
    $status   = Get-SelectValue   $props.'상태'
    $priority = Get-SelectValue   $props.'우선순위'
    $title    = Get-TitleText     $props.'작업명'
    $owner    = Get-RichTextValue $props.'담당'
    $path     = Get-RichTextValue $props.'주요경로'
    $url      = $Row.url

    if ([string]::IsNullOrWhiteSpace($title)) { $title = '(제목 없음)' }
    if ([string]::IsNullOrWhiteSpace($owner)) { $owner = '(미지정)' }
    if ([string]::IsNullOrWhiteSpace($path))  { $path  = '(미지정)' }

    "- [$status] $priority · $title · 담당=$owner · 주요경로=$path · $url"
}

try {
    # --- 1. 토큰 취득 (env > 파일) ------------------------------------------
    $token = $env:NOTION_TOKEN
    if ([string]::IsNullOrWhiteSpace($token) -and (Test-Path -LiteralPath $TokenFile)) {
        $firstLine = Get-Content -LiteralPath $TokenFile -Encoding UTF8 -TotalCount 1 -ErrorAction SilentlyContinue
        if ($firstLine) { $token = ([string]$firstLine).Trim() }
    }

    if ([string]::IsNullOrWhiteSpace($token)) {
        Write-Output '[board-snapshot] 스냅샷 건너뜀 — Notion 토큰 없음. 설정: 환경변수 NOTION_TOKEN 또는 .claude/notion-token.txt 첫 줄에 토큰 저장.'
        exit 0
    }

    # --- 2. TLS 1.2 보장 (PS5.1 기본값이 낮을 수 있음) ------------------------
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

    # --- 3. 필터: 상태=진행중 OR 상태=차단 OR 우선순위=크리티컬 --------------
    $filter = @{
        or = @(
            @{ property = '상태';     select = @{ equals = '진행중' } }
            @{ property = '상태';     select = @{ equals = '차단' } }
            @{ property = '우선순위'; select = @{ equals = '크리티컬' } }
        )
    }

    $headers = @{
        'Authorization'  = "Bearer $token"
        'Notion-Version' = $NotionVersion
    }

    # --- 4. 페이지네이션 조회 (has_more 를 끝까지 따라간다, 최대 $MaxPages 페이지) ---
    $allResults = New-Object System.Collections.Generic.List[object]
    $cursor     = $null
    $page       = 0
    $hasMore    = $true
    $hitPageCap = $false

    while ($hasMore -and $page -lt $MaxPages) {
        $page++

        $bodyObj = @{ filter = $filter; page_size = $PageSize }
        if ($cursor) { $bodyObj['start_cursor'] = $cursor }
        # -Depth 를 낮게 두면(기본값 2) 중첩된 filter 해시테이블이 조용히
        # "System.Collections.Hashtable" 문자열로 뭉개진다 — 반드시 넉넉히 준다.
        $bodyJson = $bodyObj | ConvertTo-Json -Depth 10 -Compress

        # 🧨 PS 5.1 은 -Body 에 **문자열**을 주면 비-ASCII 를 '?' 로 뭉갠다(실측 2026-09-09:
        #    로컬 HttpListener 로 받아 보니 '상태'->'??', '진행중'->'???' 로 도착했다 = 79바이트).
        #    그러면 Notion 은 "property '??' 없음" 으로 떨어지고, 이 스크립트는 토큰이 있어도
        #    100% 실패한다. 바디를 **UTF-8 바이트 배열**로 넘겨 PS 의 문자열 인코딩을 우회하고,
        #    charset 도 명시한다(둘 중 하나만으로도 고쳐지지만, 실패가 조용해서 양쪽 다 건다 = 89바이트).
        #    ⚠️ 여기를 '단순화'해서 문자열로 되돌리지 말 것 — 한글 속성명이 전부 죽는다.
        $bodyBytes = [System.Text.Encoding]::UTF8.GetBytes($bodyJson)

        $reqParams = @{
            Uri             = $ApiUrl
            Method          = 'Post'
            Headers         = $headers
            ContentType     = 'application/json; charset=utf-8'
            Body            = $bodyBytes
            UseBasicParsing = $true
        }
        $response = Invoke-WebRequest @reqParams

        # Invoke-WebRequest/.Content 는 PS5.1에서 charset 을 제대로 안 따를 수 있다 —
        # RawContentStream 을 직접 UTF-8 로 디코드한 뒤 ConvertFrom-Json 한다.
        $stream = $response.RawContentStream
        $stream.Position = 0
        $reader = New-Object System.IO.StreamReader($stream, [System.Text.Encoding]::UTF8)
        $rawText = $reader.ReadToEnd()
        $reader.Dispose()

        $parsed = $rawText | ConvertFrom-Json

        if ($parsed.results) {
            foreach ($r in $parsed.results) { $allResults.Add($r) }
        }

        $cursor  = $parsed.next_cursor
        $hasMore = [bool]$parsed.has_more
    }

    if ($hasMore -and $page -ge $MaxPages) { $hitPageCap = $true }

    # --- 5. 마크다운 조립 -----------------------------------------------------
    $criticalRows = @($allResults | Where-Object { (Get-SelectValue $_.properties.'우선순위') -eq '크리티컬' })
    $statusRows   = @($allResults | Where-Object {
        $st = Get-SelectValue $_.properties.'상태'
        $pr = Get-SelectValue $_.properties.'우선순위'
        ($st -eq '진행중' -or $st -eq '차단') -and ($pr -ne '크리티컬')
    })

    $now   = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
    $total = $allResults.Count

    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("생성: $now · 총 ${total}건")
    $lines.Add('')

    if ($criticalRows.Count -gt 0) {
        $lines.Add('## 크리티컬')
        foreach ($r in $criticalRows) { $lines.Add((Format-BoardRow $r)) }
        $lines.Add('')
    }

    $lines.Add('## 진행중·차단')
    if ($statusRows.Count -gt 0) {
        foreach ($r in $statusRows) { $lines.Add((Format-BoardRow $r)) }
    } else {
        $lines.Add('(해당 행 없음)')
    }

    if ($hitPageCap) {
        $lines.Add('')
        $lines.Add("경고: 페이지네이션이 최대 ${MaxPages}페이지 상한에 도달했습니다 — 일부 행이 누락되었을 수 있습니다.")
    }

    # --- 6. 원자적 쓰기 (임시 파일 → 교체) — 중간에 실패해도 기존 스냅샷은 그대로 ---
    $lines | Out-File -LiteralPath $TempFile -Encoding utf8 -Force
    Move-Item -LiteralPath $TempFile -Destination $OutputFile -Force

    Write-Output "[board-snapshot] 스냅샷 갱신 완료 — 총 ${total}건 ($OutputFile)."
}
catch {
    # 네트워크/HTTP 오류 등 무엇이 실패하든: 기존 스냅샷 파일은 건드리지 않고,
    # 경고 한 줄만 출력하고 exit 0 — SessionStart 훅을 절대 깨뜨리지 않는다.
    # 응답 본문은 진단에 도움이 되면 일부 포함하되, Authorization 헤더(토큰)는 절대 포함하지 않는다.
    if (Test-Path -LiteralPath $TempFile) {
        Remove-Item -LiteralPath $TempFile -Force -ErrorAction SilentlyContinue
    }

    $detail = $_.Exception.Message
    if ($_.ErrorDetails -and $_.ErrorDetails.Message) {
        $bodyText = ($_.ErrorDetails.Message -replace '[\r\n]+', ' ')
        if ($bodyText.Length -gt 300) { $bodyText = $bodyText.Substring(0, 300) + '...' }
        $detail = "$detail | 응답: $bodyText"
    }
    $detail = ($detail -replace '[\r\n]+', ' ')
    Write-Output "[board-snapshot] 스냅샷 갱신 실패 — 기존 파일 유지. 사유: $detail"
}

exit 0
