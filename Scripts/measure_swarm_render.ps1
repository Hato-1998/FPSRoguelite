# measure_swarm_render.ps1 — VAT-1 스웜 렌더 경로 대조 측정 러너 (Docs/Review/20260812-plan-vat1-swarm-render-path.md)
# 패키지 빌드를 고정 시나리오(L_Map1_City 리슨 호스트 + FPSR.SpawnEnemies N)로 기동해
# CSV 프로파일(프레임타임/RHI/GPU 스탯)과 시차 스크린샷 2장(VAT 재생 확인용)을 수집한다.
#
# ⚠️ 부트 캡처(-csvCaptureFrames) 금지: RHI 초기화 전에 BeginCapture가 돌아
#    IsRayTracingAllowed 어설션으로 즉사한다(UE5.7 실측, CsvProfiler.cpp:4697 경로).
#    캡처는 -ExecCmds의 `CsvProfile Frames=N`(엔진 초기화 후 실행)으로 시작한다.
#
# 사용:
#   Scripts\measure_swarm_render.ps1 -BuildDir "Packaged\26_8_13_BuildTest_1_B" -Label B_300 -EnemyCount 300
#   Scripts\measure_swarm_render.ps1 -BuildDir "Packaged\26_8_13_BuildTest_1_B" -Label N1_300_4p -EnemyCount 300 -ClientCount 3
#
# 판정 규약(리포트 §측정 프로토콜):
#   - 워밍업 = 캡처 앞 10초 절삭(분석 단계), GPU ms 단독 판정 금지 — RHI/DrawCalls 병행
#   - 카메라 = PlayerStart 스폰 그대로(입력 無), -novsync 고정
param(
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [Parameter(Mandatory=$true)][string]$Label,
    [int]$EnemyCount = 300,
    [int]$SpawnRadius = 6000,     # cm. 근접 링(600)은 수 초 만에 카메라 위치로 뭉쳐 화면 밖 — 원거리 링이 수렴 중 시야를 채운다

    [int]$CaptureFrames = 6000,   # 60fps≈100s / 120fps≈50s — 워밍업 절삭 후에도 유효구간 확보
    [int]$BootWaitSeconds = 600,  # 캡처 시작(=CSV 생성) 대기 상한 — 첫 부팅 셰이더/PSO 컴파일이 5분+ 걸린 실측
    [int]$MaxWaitSeconds = 300,   # 캡처 시작 후 완주(크기 안정화) 대기 상한
    [int]$ShotAtSeconds = 30,     # 캡처 시작 기준 스크린샷 시점(+1.5s 두 번째 장)
    [int]$ClientCount = 0         # N-1 복제 폴백 측정용: 리슨 호스트에 붙는 -nullrhi 클라 수(기본 0 = 기존 동작 완전 무변경)
)
$ErrorActionPreference = 'Stop'
# ⚠️ 아카이브 최상위 exe는 부트스트랩 — Start-Process 핸들을 죽여도 실제 게임 자식이 살아남아
#    인스턴스가 누적되고 GPU를 나눠 먹어 측정을 오염시킨다(실사고: 동시 4개).
#    → 실행 전 동명 프로세스 전부 정리 + 종료도 프로세스 '이름' 기준으로 한다.
Get-Process -Name "FPSRoguelite*" -ErrorAction SilentlyContinue | Stop-Process -Force -Confirm:$false
Start-Sleep -Seconds 2
$exe = Join-Path $BuildDir "Windows\FPSRoguelite.exe"
if (-not (Test-Path $exe)) { throw "exe not found: $exe" }
$outDir = Join-Path "Packaged\Measurements" $Label
New-Item -ItemType Directory -Force $outDir | Out-Null
$csvDir = Join-Path $BuildDir "Windows\FPSRoguelite\Saved\Profiling\CSV"
if (Test-Path $csvDir) { Get-ChildItem $csvDir -Filter *.csv | Remove-Item -Force -Confirm:$false }

# 무적 시간은 ExecCmds 시점부터 기산 — 부팅(첫 부팅 PSO 컴파일 5분+ 실측)·캡처·여유를 전부 덮어야
# grace 만료→다운→run-end→빈 씬이 평균을 오염시키는 무효 측정을 막는다(레드팀 P3 지적).
$invulnSeconds = $BootWaitSeconds + $MaxWaitSeconds + 300
$gameArgs = @(
    "L_Map1_City?listen",
    "-windowed","-resx=1920","-resy=1080","-novsync","-log",
    "-ExecCmds=`"FPSR.SkipCards, FPSR.Invuln $invulnSeconds, FPSR.SpawnEnemies $EnemyCount $SpawnRadius, CsvProfile Frames=$CaptureFrames`"",
    "-csvGpuStats"
)
# N-1 멀티클라 시(§5-A ⑤): 패키지는 GameNetDriver=SteamSockets 라우팅(DefaultEngine.ini)이라 127.0.0.1 다이얼과
# 프로토콜이 어긋나고(스모크 1차: 클라 핸드셰이크 타임아웃), NetDriverOverrides만으론 소켓 서브시스템이 Steam이라
# raw UDP 바인드 실패(스모크 2차: SO_BROADCAST failed → NetDriverListenFailure → 메인메뉴 폴백) →
# host·클라 모두 -nosteam(SteamSockets 소켓 서브시스템 미등록) + IpNetDriver 강제. ClientCount 0이면 기존 인자 그대로.
if ($ClientCount -gt 0) { $gameArgs += @("-nosteam", "-NetDriverOverrides=/Script/OnlineSubsystemUtils.IpNetDriver") }
Write-Host "[measure] launching $Label : $exe $($gameArgs -join ' ')"
$proc = Start-Process -FilePath $exe -ArgumentList $gameArgs -PassThru

Add-Type -AssemblyName System.Drawing
function Take-Shot($path) {
    $b = New-Object System.Drawing.Bitmap(1920, 1080)
    $g = [System.Drawing.Graphics]::FromImage($b)
    $g.CopyFromScreen(0, 0, 0, 0, $b.Size)
    $b.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $b.Dispose()
}
# 1단계: 캡처 시작 대기 — CSV 파일은 캡처 '시작' 시 생성돼 스트리밍된다(실측). 부팅(첫 부팅 셰이더 컴파일 5분+)의
#        기준점이 불명확하므로 CSV 생성을 "런 시작" 신호로 쓴다.
$csv = $null; $waited = 0
while ($waited -lt $BootWaitSeconds) {
    if (-not (Get-Process -Name "FPSRoguelite*" -ErrorAction SilentlyContinue)) { Write-Warning "[measure] game exited during boot (crash?)"; break }
    $csv = Get-ChildItem $csvDir -Filter *.csv -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
    if ($csv) { Write-Host "[measure] capture started after ${waited}s boot"; break }
    Start-Sleep -Seconds 5; $waited += 5
}

# N-1 클라 기동(§5-C(3)): CSV 생성 = 엔진 초기화 후 ExecCmds 실행 완료 = 맵 로드·리슨 확립의 실측 가능한 신호 —
# 그 전 조인은 접속 실패 리스크라 캡처 시작 감지 '직후'에만 기동한다. 2초 간격 순차 기동으로 동시 접속 폭주를 피한다.
if ($csv -and $ClientCount -gt 0) {
    for ($i = 0; $i -lt $ClientCount; $i++) {
        Write-Host "[measure] launching client $($i + 1)/$ClientCount"
        Start-Process -FilePath $exe -ArgumentList @("127.0.0.1", "-nullrhi", "-nosound", "-windowed", "-resx=640", "-resy=360", "-log", "-nosteam", "-NetDriverOverrides=/Script/OnlineSubsystemUtils.IpNetDriver")
        Start-Sleep -Seconds 2
    }
}

# 2단계: 캡처 시작 +ShotAt초에 시차 스크린샷(스웜이 화면에 있는 시점)
if ($csv) {
    Start-Sleep -Seconds $ShotAtSeconds
    Take-Shot (Join-Path $outDir "shot_t0.png")
    Start-Sleep -Milliseconds 1500
    Take-Shot (Join-Path $outDir "shot_t1.png")
}

# 3단계: 완주 대기 — 크기 안정화(10초 무변화)로 판정
$waited = 0
$lastSize = -1; $stableFor = 0
while ($csv -and $waited -lt $MaxWaitSeconds) {
    if (-not (Get-Process -Name "FPSRoguelite*" -ErrorAction SilentlyContinue)) { Write-Warning "[measure] game exited early (crash?)"; break }
    $csv = Get-ChildItem $csvDir -Filter *.csv -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
    if ($csv) {
        if ($csv.Length -eq $lastSize -and $csv.Length -gt 0) { $stableFor += 5; if ($stableFor -ge 10) { break } }
        else { $stableFor = 0 }
        $lastSize = $csv.Length
    }
    Start-Sleep -Seconds 5; $waited += 5
}
Get-Process -Name "FPSRoguelite*" -ErrorAction SilentlyContinue | Stop-Process -Force -Confirm:$false
Start-Sleep -Seconds 3

if ($csv) {
    # 킬 직후 핸들 해제 지연 대비 재시도
    $copied = $false
    for ($i = 0; $i -lt 5 -and -not $copied; $i++) {
        try { Copy-Item $csv.FullName (Join-Path $outDir "capture.csv") -ErrorAction Stop; $copied = $true }
        catch { Start-Sleep -Seconds 3 }
    }
    if ($copied) { Write-Host "[measure] CSV: $($csv.Name) -> $outDir\capture.csv" }
    else { Write-Warning "[measure] CSV copy failed after retries: $($csv.FullName)" }
} else {
    Write-Warning "[measure] no CSV captured"
}
$gameLog = Get-ChildItem (Join-Path $BuildDir "Windows\FPSRoguelite\Saved\Logs") -Filter "FPSRoguelite*.log" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
if ($gameLog) { Copy-Item $gameLog.FullName (Join-Path $outDir "game.log") }

# 유효성 게이트 — 캡처 중 런이 끝났으면(플레이어 다운 → 빈 씬) 그 CSV는 무효다. 조용히 통과 금지.
if ($gameLog) {
    $endEvents = Select-String -Path (Join-Path $outDir "game.log") -Pattern "\[Run\] END|EndRun outcome|-> DBNO" -SimpleMatch:$false
    if ($endEvents) {
        Write-Warning "[measure] ⚠️ 측정 무효 의심 — 캡처 세션에서 런 종료/다운 이벤트 감지:"
        $endEvents | Select-Object -First 3 | ForEach-Object { Write-Warning "  $($_.Line.Trim())" }
    } else {
        Write-Host "[measure] validity: run survived (no END/DBNO events)"
    }
}
# 접속 유효성 게이트(§5-A): CSV의 Replication/NumConnections는 Game 빌드에서 갱신되지 않는다
# (USE_SERVER_PERF_COUNTERS = (UE_SERVER||UE_EDITOR)&&WITH_PERFCOUNTERS, Build.h:115 — 컬럼만 등록, 영원히 0).
# 호스트 로그의 AddClientConnection 카운트가 유일한 접속 증거다.
if ($ClientCount -gt 0) {
    $joinLog = Join-Path $outDir "game.log"
    $joins = 0
    if (Test-Path $joinLog) { $joins = (Select-String -Path $joinLog -Pattern "AddClientConnection" -SimpleMatch | Measure-Object).Count }
    if ($joins -eq $ClientCount) { Write-Host "[measure] validity: client joins $joins/$ClientCount (AddClientConnection in host log)" }
    else { Write-Warning "[measure] ⚠️ client joins $joins/$ClientCount — capture may not represent a $(1 + $ClientCount)-player listen server" }
}
Write-Host "[measure] done: $outDir"
