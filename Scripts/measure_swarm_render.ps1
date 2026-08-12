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
#
# 판정 규약(리포트 §측정 프로토콜):
#   - 워밍업 = 캡처 앞 10초 절삭(분석 단계), GPU ms 단독 판정 금지 — RHI/DrawCalls 병행
#   - 카메라 = PlayerStart 스폰 그대로(입력 無), -novsync 고정
param(
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [Parameter(Mandatory=$true)][string]$Label,
    [int]$EnemyCount = 300,
    [int]$CaptureFrames = 6000,   # 60fps≈100s / 120fps≈50s — 워밍업 절삭 후에도 유효구간 확보
    [int]$MaxWaitSeconds = 300,   # CSV 플러시 대기 상한
    [int]$ShotAtSeconds = 45      # VAT 재생 확인 스크린샷 시점(+1.5s 두 번째 장)
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

$gameArgs = @(
    "L_Map1_City?listen",
    "-windowed","-resx=1920","-resy=1080","-novsync","-log",
    "-ExecCmds=`"FPSR.SkipCards, FPSR.Invuln 600, FPSR.SpawnEnemies $EnemyCount, CsvProfile Frames=$CaptureFrames`"",
    "-csvGpuStats"
)
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
Start-Sleep -Seconds $ShotAtSeconds
Take-Shot (Join-Path $outDir "shot_t0.png")
Start-Sleep -Milliseconds 1500
Take-Shot (Join-Path $outDir "shot_t1.png")

# CSV 완주 대기 — 파일은 캡처 '시작' 시 생겨 스트리밍되므로(실측), 존재가 아니라 '크기 안정화'를 기다린다.
$waited = $ShotAtSeconds + 2
$csv = $null
$lastSize = -1; $stableFor = 0
while ($waited -lt $MaxWaitSeconds) {
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
Write-Host "[measure] done: $outDir"
