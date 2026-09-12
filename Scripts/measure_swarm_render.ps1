# measure_swarm_render.ps1 — VAT-1 스웜 렌더 경로 대조 측정 러너 (Docs/Review/20260812-plan-vat1-swarm-render-path.md)
# 패키지 빌드를 고정 시나리오(L_Arena 리슨 호스트 + FPSR.SpawnEnemies N)로 기동해
# CSV 프로파일(프레임타임/RHI/GPU 스탯)과 시차 스크린샷 2장(VAT 재생 확인용)을 수집한다.
#
# ⚠️ 부트 캡처(-csvCaptureFrames) 금지: RHI 초기화 전에 BeginCapture가 돌아
#    IsRayTracingAllowed 어설션으로 즉사한다(UE5.7 실측, CsvProfiler.cpp:4697 경로).
#    캡처는 -ExecCmds의 `CsvProfile Frames=N`(엔진 초기화 후 실행)으로 시작한다.
#
# 사용:
#   Scripts\measure_swarm_render.ps1 -BuildDir "Packaged\26_8_13_BuildTest_1_B" -Label B_300 -EnemyCount 300
#   Scripts\measure_swarm_render.ps1 -BuildDir "Packaged\26_8_13_BuildTest_1_B" -Label N1_300_4p -EnemyCount 300 -ClientCount 3
#   Scripts\measure_swarm_render.ps1 -BuildDir <dir> -Label G2_300_asc -EnemyCount 300 -ForceClass BP_EnemyRangedBase_C -AttachASC -MemReport   # GASM1 §12-A 러너 프로토콜 ②
#
# 판정 규약(리포트 §측정 프로토콜):
#   - 워밍업 = 캡처 앞 10초 절삭(분석 단계), GPU ms 단독 판정 금지 — RHI/DrawCalls 병행
#   - 카메라 = PlayerStart 스폰 그대로(입력 無), -novsync 고정
#
# GASM1 측정 인자(Docs/Specs/GASM1_SwarmASCCostMeasurement.md §5-A·§12-A) — 전부 미지정 시 기존 동작 완전 무변경:
#   -ForceClass <이름> : FPSR.Debug.ForceSpawnClass — 로스터 가중추첨 대신 이 클래스로 강제(BP는 _C 접미)
#   -AttachASC : FPSR.Debug.AttachASC 1 — 일반 적에 ASC만 부착
#   -MeasureLoadout : FPSR.Debug.MeasureLoadout 1 — AttributeSet·더미 어빌리티까지 부여·구동(AttachASC 함의, 코드에서 강제)
#   -MeasureTickable : FPSR.Debug.MeasureTickable 1 — 구성 ③ᵀ: 측정 AttributeSet 을 Tickable 로 만들어 ASC 틱이
#                      켜진 채 유지되게 한다(MeasureLoadout 함의, 코드에서 강제)
#   -Cadence <초> : FPSR.Debug.MeasureCadence — 더미 어빌리티 발동 주기(허용 1.0/0.2, 스냅은 CVar 쪽 책임)
#   -MemReport : 종료 시퀀스를 CsvProfile Stop(시간 기준) → ASCDump → obj list → memreport -full 순으로 예약하고,
#                kill 조건도 CSV 대신 MemReports 새 파일 기준으로 바꿔 그 파일을 Packaged\Measurements\<Label>\ 로 복사한다
#
# G2 머지게이트 후속수정(2026-09-12, §12-A) — 이번 세션이 추가한 것:
#   - AttachASC/MeasureLoadout/MeasureTickable 중 하나라도 켜지면 -MemReport 와 무관하게 ASCDump 를 예약하고,
#     game.log 의 "instances: ASC N x" 를 파싱해 N ≥ 실제 전달수(delivered)인지 게이트한다(P2-4) — 이게
#     없으면 오퍼레이터가 -AttachASC 를 빠뜨려도 게이트가 전부 통과한다(2026-08-28 "300→225"와 같은 종류).
#   - ClientCount>0 이면 클라 PID 도 기동 직후 스냅샷해 kill 직전 전원 생존을 게이트하고, 호스트 로그의
#     연결종료 흔적(UNetConnection::Close / Connection TIMED OUT)이 0건인지도 확인한다(P2-5) — 기존 접속
#     게이트(AddClientConnection)는 '접속 시점'만 증명하고 중간 이탈은 못 잡는다.
param(
    [Parameter(Mandatory=$true)][string]$BuildDir,
    [Parameter(Mandatory=$true)][string]$Label,
    [int]$EnemyCount = 300,
    [int]$SpawnRadius = 6000,     # cm. 근접 링(600)은 수 초 만에 카메라 위치로 뭉쳐 화면 밖 — 원거리 링이 수렴 중 시야를 채운다

    [int]$CaptureFrames = 6000,   # 60fps≈100s / 120fps≈50s — 워밍업 절삭 후에도 유효구간 확보
    [int]$BootWaitSeconds = 600,  # 캡처 시작(=CSV 생성) 대기 상한 — 첫 부팅 셰이더/PSO 컴파일이 5분+ 걸린 실측
    [int]$MaxWaitSeconds = 300,   # 캡처 시작 후 완주(크기 안정화) 대기 상한
    [int]$ShotAtSeconds = 30,     # 캡처 시작 기준 스크린샷 시점(+1.5s 두 번째 장)
    [int]$ClientCount = 0,        # N-1 복제 폴백 측정용: 리슨 호스트에 붙는 -nullrhi 클라 수(기본 0 = 기존 동작 완전 무변경)
    # 셀/아웃라인 축(M0 베이스라인): 켠 상태가 기본이고, 이 스위치가 **포스트프로세스 머티리얼만** 끈다.
    # r.PostProcessing.DisableMaterials 는 블렌더블만 차단하고 톤매핑·블룸·노출·그레이드는 그대로 둔다
    # (엔진 실측: PostProcessMaterial.cpp:54 정의, :1000-1009 IsPostProcessMaterialsEnabledForView 게이트).
    # 그래서 showflag.PostProcessing 0 처럼 PP 전체를 죽이지 않고 셀 스택 비용만 분리할 수 있다 —
    # 같은 패키지 빌드 하나로 on/off 교차가 성립하는 이유다(빌드 2개 불요).
    # ⚠️ 전제: 측정 씬의 블렌더블이 SRS 셀+아웃라인뿐이어야 한다. M_PP_StageFade 도 블렌더블이지만
    #    스테이지 전환 중에만 기여하므로 정적 스폰 시나리오에서는 0 이다(BP_SRS_Fog 는 배치하지 않는다).
    [switch]$DisablePostProcessMaterials,

    # --- GASM1 측정 인자(Docs/Specs/GASM1_SwarmASCCostMeasurement.md §5-A·§12-A) — 전부 미지정 시 기존 동작 완전 무변경 ---
    [string]$ForceClass = "",  # FPSR.Debug.ForceSpawnClass 값(예: BP_EnemyRangedBase_C). 빈 문자열 = 미지정(로스터 기본 가중추첨)
    [switch]$AttachASC,        # FPSR.Debug.AttachASC 1 — 일반 적에 ASC만 부착(구성 ①↔②)
    [switch]$MeasureLoadout,   # FPSR.Debug.MeasureLoadout 1 — AttachASC 함의(코드에서 강제) + AttributeSet·더미 어빌리티 구동(구성 ②↔③)
    [switch]$MeasureTickable,  # FPSR.Debug.MeasureTickable 1 — MeasureLoadout 함의(코드에서 강제) + 측정 세트를 Tickable 로 만들어 ASC 틱을 켠 채 유지(구성 ③↔③ᵀ)
    [double]$Cadence = 1.0,    # FPSR.Debug.MeasureCadence 값(허용 1.0/0.2, 스냅은 CVar 쪽 책임) — 미지정 시 명령 자체를 생략해 CVar 기본값(1.0) 유지
    [switch]$MemReport         # CsvProfile Stop(시간 기준) → ASCDump → obj list → memreport -full 순 예약 + kill 조건을 MemReports 새 파일 기준으로 전환
)
$ErrorActionPreference = 'Stop'
# 완주 대기 자동 산출(레드팀 P2-4 수용): 미지정 시 60fps 미만에서 CaptureFrames가 MaxWaitSeconds를 넘겨
# 캡처가 중간 킬 → 꼬리 헤더 없는 불완전 CSV가 되는 모순을 막는다. 명시 지정 시 그 값 그대로 쓴다.
if (-not $PSBoundParameters.ContainsKey('MaxWaitSeconds')) {
    $MaxWaitSeconds = [Math]::Max(300, [int]($CaptureFrames / 30) + 120)
}
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
$hostLog = $null   # 클라 기동 직전 스냅샷(§5-C(3) 레드팀 P2-2) — 미설정(ClientCount=0)이면 기존 최신-선택 폴백
$hostPids = @()    # 클라 기동 직전 호스트 PID 스냅샷(레드팀 P3-1) — 3단계에서 호스트 단독 크래시 판별에 사용
$clientPids = @()  # 클라 기동 직후 PID 스냅샷(G2 P2-5) — kill 직전 전원 생존 게이트에 사용. $hostPids 와 같은 관용구
$deliveredCount = $null  # 전달수/구성 게이트가 파싱하는 실제 전달수(G2 P2-4) — ASC 부착 게이트가 기대값으로 재사용
# GASM1 §12-A: MemReports 폴더도 CSV와 같은 이유로 실행 전 비운다 — 안 비우면 이전 런의 파일이 남아
# "새 파일 존재"를 즉시 거짓 충족시켜 3단계가 쓰다 만 CSV/미완성 memreport로 조기 종료한다.
$memReportsDir = Join-Path $BuildDir "Windows\FPSRoguelite\Saved\Profiling\MemReports"
$memReportFile = $null
if ($MemReport -and (Test-Path $memReportsDir)) { Get-ChildItem $memReportsDir -File | Remove-Item -Force -Confirm:$false }

# 무적 시간은 ExecCmds 시점부터 기산 — 부팅(첫 부팅 PSO 컴파일 5분+ 실측)·캡처·여유를 전부 덮어야
# grace 만료→다운→run-end→빈 씬이 평균을 오염시키는 무효 측정을 막는다(레드팀 P3 지적).
$invulnSeconds = $BootWaitSeconds + $MaxWaitSeconds + 300
# 프리즈 게이트(§5-C(3) 레드팀 P2-1): late-join 클라의 오프닝 시드가 전역 프리즈를 재유발하므로(§5-A ⑦),
# ClientCount>0이면 SkipCards를 반복 모드(§5-C(2-b))로 걸어 5s 내 해소한다. 0이면 기존 1회 호출 그대로
# (VAT 솔로 시나리오 무변경).
$skipCardsCmd = if ($ClientCount -gt 0) { "FPSR.SkipCards $invulnSeconds" } else { "FPSR.SkipCards" }
# 셀 축 off 실행분: ExecCmds 맨 앞에 붙여 캡처 시작(CsvProfile) 전에 확정되게 한다.
$ppPrefix = if ($DisablePostProcessMaterials) { "r.PostProcessing.DisableMaterials 1, " } else { "" }
# GASM1 측정 CVar(§5-A) — ppPrefix와 같은 방식: CsvProfile Frames= 앞에서 확정돼야 하므로 선행 결합한다.
# 전부 미지정이면 네 변수가 전부 빈 문자열이라 ExecCmds가 기존 호출과 완전히 같아진다.
$forceClassCmd = if ($ForceClass) { "FPSR.Debug.ForceSpawnClass $ForceClass, " } else { "" }
$attachAscCmd = if ($AttachASC) { "FPSR.Debug.AttachASC 1, " } else { "" }
$measureLoadoutCmd = if ($MeasureLoadout) { "FPSR.Debug.MeasureLoadout 1, " } else { "" }
$measureTickableCmd = if ($MeasureTickable) { "FPSR.Debug.MeasureTickable 1, " } else { "" }
$cadenceCmd = if ($PSBoundParameters.ContainsKey('Cadence')) { "FPSR.Debug.MeasureCadence $Cadence, " } else { "" }
$measurePrefix = "$forceClassCmd$attachAscCmd$measureLoadoutCmd$measureTickableCmd$cadenceCmd"
# -MemReport 종료 시퀀스(§12-A, G1 P2-3): 시간 기준 Stop을 먼저 예약해 캡처를 닫고, 그 뒤로 ASCDump → obj list →
# memreport -full을 순서대로 예약한다. Frames= 자동종료가 이 Stop보다 먼저 오면 안 되므로 MemReport 시엔 Frames
# 값을 크게 부풀린다(원본 $CaptureFrames는 시간 환산에만 쓰고 그대로 둔다).
# ⚠️ 예약 명령에 쉼표를 넣지 않는다 — -ExecCmds 자체가 쉼표로 명령을 나눈다(§12-B).
$captureFramesForExec = $CaptureFrames
$memReportCmds = ""
if ($MemReport) {
    # 60fps 가정(파일 상단 사용법 주석의 "60fps≈100s"와 동일 환산) — 실제 fps가 이보다 낮으면(무거운 구성) 표본이
    # 그만큼 줄 뿐 캡처가 중간에 잘리지는 않는다. 시간 상자가 고정이라 ①②③이 같은 wall-clock으로 비교된다.
    $captureFramesForExec = $CaptureFrames * 100
    $captureSeconds = [int]($CaptureFrames / 60)
    $execAfterCmds = @(
        "FPSR.Debug.ExecAfter $captureSeconds CsvProfile Stop",
        "FPSR.Debug.ExecAfter $($captureSeconds + 2) FPSR.Debug.ASCDump",
        "FPSR.Debug.ExecAfter $($captureSeconds + 4) obj list class=FPSRAbilitySystemComponent",
        "FPSR.Debug.ExecAfter $($captureSeconds + 6) memreport -full"
    )
    $memReportCmds = ", " + ($execAfterCmds -join ", ")
}
# ASC 부착 실증(G2 P2-4) — 측정 CVar가 하나라도 켜지면 -MemReport 여부와 무관하게 항상 ASCDump 를 예약한다.
# -MemReport 가 이미 켜져 있으면 위 $memReportCmds 체인이 같은 시각(+2)에 ASCDump 를 이미 포함하므로
# 여기서는 중복 예약하지 않는다(중복돼도 해는 없지만 — 로그에 같은 줄이 두 번 찍히는 것을 피한다).
# ⚠️ 판단(스펙 갭, §12-A "<t+2>" 표기의 해석) — 이 t 를 위 $captureSeconds(60fps 가정 캡처 총시간)로 그대로
# 재사용하면 안 된다: -MemReport 가 꺼진 경로는 Frames=N 자동종료를 부풀리지 않으므로, 이 리포의 실측 fps
# (Performance.md:47 — 적300 기준선 222fps)에서는 캡처가 그보다 훨씬 일찍 끝나 3단계가 먼저 kill해버려
# 이 ExecAfter가 영원히 발화하지 않는다(매 non-MemReport 측정 런에서 게이트가 상시 거짓 경고를 내는 회귀).
# 그래서 캡처추정시간이 아니라 "스폰 직후" 개념의 고정 지연을 쓴다 — SpawnEnemies 는 같은 ExecCmds 배치
# 안에서 동기 처리되므로(§6-A Activate 도 동기) 캡처 시작 시점에 이미 N 마리가 서 있고, 러너 3단계는
# $ShotAtSeconds(기본 30초) 만큼 무조건 먼저 자므로 kill 최소시각이 ~40초대다.
#
# 🔴 지연값 = 5초, **워밍업 절삭 구간(앞 10초) 안**이어야 한다. ASCDump 는 300 액터를 순회하며 로그를
#    300줄 찍는다 — 그 히치가 판정 창 안에서 터지면 P95 를 오염시킨다(= -MemReport 경로가 CsvProfile Stop
#    을 먼저 예약하는 것과 같은 이유, §12-A G1 P2-3). 절삭 구간에 넣으면 분석기가 통째로 버리므로 무해하다.
#    ⚠️ 이 값을 워밍업 절삭(analyze_swarm_csv.py 의 --skip-seconds 기본 10) 이상으로 올리지 말 것.
$ascDumpCmds = ""
if (($AttachASC -or $MeasureLoadout -or $MeasureTickable) -and -not $MemReport) {
    $ascDumpCmds = ", FPSR.Debug.ExecAfter 5 FPSR.Debug.ASCDump"
}
# 런맵 = 아레나 지속 레벨(ADR 0012). 실제 전투 공간은 그 스트리밍 서브레벨(L_Map_1 등)이고
# UFPSRArenaStreamSubsystem 이 가시화한다 — 즉 여기서 지속 레벨만 띄우면 되고 서브레벨은 런 진행이 붙인다.
# ⚠️ 종전 값 "L_Map1_City?listen" 은 **삭제된 맵**이었다(ADR 0010 에서 플레이 감각 기각 → 아레나로 전환).
#    그대로 두면 존재하지 않는 맵을 참조해 측정 자체가 무효가 된다.
$gameArgs = @(
    "L_Arena?listen",
    "-windowed","-resx=1920","-resy=1080","-novsync","-log",
    "-ExecCmds=`"$measurePrefix$ppPrefix$skipCardsCmd, FPSR.Invuln $invulnSeconds, FPSR.SpawnEnemies $EnemyCount $SpawnRadius, CsvProfile Frames=$captureFramesForExec$memReportCmds$ascDumpCmds`"",
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
    # 호스트 로그 고정(레드팀 P2-2) + 호스트 PID 스냅샷(레드팀 P3-1): 이 시점이 신규 로그/프로세스 = 호스트뿐인
    # 마지막 순간 — 클라가 뜨면 "최신 로그/이름 기준" 선택이 클라를 집을 수 있다(비결정).
    $hostLog = Get-ChildItem (Join-Path $BuildDir "Windows\FPSRoguelite\Saved\Logs") -Filter "FPSRoguelite*.log" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
    $hostPids = (Get-Process -Name "FPSRoguelite*" -ErrorAction SilentlyContinue).Id
    for ($i = 0; $i -lt $ClientCount; $i++) {
        Write-Host "[measure] launching client $($i + 1)/$ClientCount"
        # 클라 PID 스냅샷(G2 P2-5) — Start-Process -PassThru 로 그 클라만의 PID 를 직접 받는다(이름 필터로는
        # 이 시점부터 호스트도 같은 이름이라 못 가른다). kill 직전 전원 생존 게이트(3단계 아래)가 이 배열을 쓴다.
        $clientProc = Start-Process -FilePath $exe -ArgumentList @("127.0.0.1", "-nullrhi", "-nosound", "-windowed", "-resx=640", "-resy=360", "-log", "-nosteam", "-NetDriverOverrides=/Script/OnlineSubsystemUtils.IpNetDriver") -PassThru
        $clientPids += $clientProc.Id
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

# 3단계: 완주 대기 — 크기 안정화(10초 무변화)로 판정. -MemReport 시엔 대상이 CSV가 아니라 MemReports 새 파일이다
#   (memreport -full은 CsvProfile Stop보다 한참 뒤, ASCDump·obj list 이후에야 실행되므로 CSV 안정만 보면
#    300 액터 월드의 memreport -full이 kill에 중간에 잘린다 — §12-A 러너 종료 순서).
$waited = 0
$lastSize = -1; $stableFor = 0
while ($csv -and $waited -lt $MaxWaitSeconds) {
    # 호스트 단독 크래시 감시(레드팀 P3-1): ClientCount>0이면 클라 생존이 이름 기준 검사를 속여 호스트 크래시를
    # 가릴 수 있다 — 클라 기동 직전 스냅샷한 호스트 PID들이 전멸했는지로 판별한다. ClientCount=0(스냅샷 없음)이면
    # 기존 이름 기준 검사 그대로.
    if ($ClientCount -gt 0 -and $hostPids) {
        $hostAlive = $hostPids | Where-Object { Get-Process -Id $_ -ErrorAction SilentlyContinue }
        if (-not $hostAlive) { Write-Warning "[measure] host exited (crash?)"; break }
    } elseif (-not (Get-Process -Name "FPSRoguelite*" -ErrorAction SilentlyContinue)) {
        Write-Warning "[measure] game exited early (crash?)"; break
    }
    if ($MemReport) {
        # 확장자 = .memreport 확정(엔진 실물: UnrealEngine.cpp:8213 `CreateProfileFilename(InFileName, TEXT(".memreport"), true)`).
        # 폴더 안 아무 파일이나 집으면 엔진이 같은 폴더에 남기는 다른 산출물을 memreport 로 오인해 3단계가 조기 종료한다.
        $memReportFile = Get-ChildItem $memReportsDir -Filter *.memreport -File -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
        if ($memReportFile) {
            if ($memReportFile.Length -eq $lastSize -and $memReportFile.Length -gt 0) { $stableFor += 5; if ($stableFor -ge 10) { break } }
            else { $stableFor = 0 }
            $lastSize = $memReportFile.Length
        }
    } else {
        $csv = Get-ChildItem $csvDir -Filter *.csv -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
        if ($csv) {
            if ($csv.Length -eq $lastSize -and $csv.Length -gt 0) { $stableFor += 5; if ($stableFor -ge 10) { break } }
            else { $stableFor = 0 }
            $lastSize = $csv.Length
        }
    }
    Start-Sleep -Seconds 5; $waited += 5
}
# 클라 생존 게이트(G2 P2-5) — 접속 게이트(AddClientConnection, 아래)는 '접속 시점' 증거일 뿐이라 중간에 죽은
# 클라를 못 잡는다. 구성 ⑤ 는 클라 3개가 300 액터의 동적 ASC 서브오브젝트를 처음 받는 실행이라(리포에서
# 처음 밟는 경로) 캡처 도중 클라가 죽을 현실적 위험이 있고, 그러면 캡처의 상당 부분이 실제로는 3인
# 복제인데 "4인"으로 기록된다. $hostPids 스냅샷과 같은 관용구 — kill 직전 1회 확인(연속 감시는 위
# 호스트 크래시 감시가 이미 한다).
if ($ClientCount -gt 0 -and $clientPids) {
    $clientAlive = @($clientPids | Where-Object { Get-Process -Id $_ -ErrorAction SilentlyContinue })
    if ($clientAlive.Count -lt $clientPids.Count) {
        Write-Warning "[measure] ⚠️ 클라 생존 게이트 미달 — kill 시점까지 $($clientAlive.Count)/$($clientPids.Count) 클라만 생존(요청 ClientCount=$ClientCount). 캡처 구간 일부가 그보다 적은 인원의 복제로 남았을 수 있음 — 무효 의심"
    } else {
        Write-Host "[measure] validity: all $($clientPids.Count) clients alive at kill time"
    }
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
# MemReports 수집(§12-A 검증기준 8) — 완주 대기(3단계)가 이미 이 파일의 안정화를 기다렸으므로 여기서는 복사만 한다.
if ($MemReport) {
    if ($memReportFile) {
        $memCopied = $false
        for ($i = 0; $i -lt 5 -and -not $memCopied; $i++) {
            try { Copy-Item $memReportFile.FullName (Join-Path $outDir $memReportFile.Name) -ErrorAction Stop; $memCopied = $true }
            catch { Start-Sleep -Seconds 3 }
        }
        if ($memCopied) { Write-Host "[measure] MemReport: $($memReportFile.Name) -> $outDir\$($memReportFile.Name)" }
        else { Write-Warning "[measure] MemReport copy failed after retries: $($memReportFile.FullName)" }
    } else {
        Write-Warning "[measure] -MemReport requested but no file found under $memReportsDir"
    }
}
# 호스트 로그 고정(레드팀 P2-2): 클라 기동 직전 스냅샷($hostLog)을 우선 사용 — 4인스턴스가 같은 Saved\Logs를
# 공유해 "최신 로그 1개" 선택은 클라 로그를 집을 수 있다(게이트 전부가 서버측 로그 전제). 스냅샷 없으면(ClientCount=0)
# 기존 최신-선택 폴백.
$gameLog = if ($hostLog) { $hostLog } else { Get-ChildItem (Join-Path $BuildDir "Windows\FPSRoguelite\Saved\Logs") -Filter "FPSRoguelite*.log" -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1 }
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
# 프리즈 게이트(레드팀 P2-1 수용): FREEZE 발생 수 > RESUME 발생 수면 카드선택 프리즈가 캡처에 미해소로
# 남았다는 뜻(원문 "[Run] FREEZE (card selection)" / "[Run] RESUME" — FPSRGameState.cpp:226) — 그 구간은
# "멈춘 월드"라 복제 비용을 과소 측정한다.
if ($gameLog) {
    $freezeCount = (Select-String -Path (Join-Path $outDir "game.log") -Pattern "\[Run\] FREEZE" -SimpleMatch:$false | Measure-Object).Count
    $resumeCount = (Select-String -Path (Join-Path $outDir "game.log") -Pattern "\[Run\] RESUME" -SimpleMatch:$false | Measure-Object).Count
    if ($freezeCount -gt $resumeCount) {
        Write-Warning "[measure] ⚠️ 캡처에 프리즈 구간 잔존 — 측정 무효 의심 (FREEZE=$freezeCount RESUME=$resumeCount)"
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
    else { Write-Warning "[measure] ⚠️ client joins $joins/$ClientCount — capture may not represent a $(1 + $ClientCount)-player listen server (초과 = 재접속[타임아웃 후 재시도] 의심)" }
    # 연결 종료 흔적 게이트(G2 P2-5, 보조) — AddClientConnection 은 '접속'만 증명하고 그 뒤 끊긴 클라는 못
    # 잡는다. 엔진 실물 확인(UE 5.8 NetConnection.cpp:1123 "UNetConnection::Close: " · :4928 "…Connection
    # TIMED OUT. Closing connection.") 두 문구를 호스트 로그에서 찾는다 — 정상은 0건. Select-String 은
    # 기본 대소문자 무시라 "TIMED OUT" 표기도 그대로 잡힌다.
    # 로그가 없으면(총체적 실패 등) 판정을 내지 않는다 — "0건"을 "깨끗하다"로 오인시키지 않는다. 위
    # AddClientConnection 게이트가 그 경우를 이미 개수불일치로 잡는다.
    if (Test-Path $joinLog) {
        $closeEvents = (Select-String -Path $joinLog -Pattern "UNetConnection::Close|Connection TIMED OUT" -ErrorAction SilentlyContinue | Measure-Object).Count
        if ($closeEvents -gt 0) { Write-Warning "[measure] ⚠️ 호스트 로그에 연결 종료 흔적 $closeEvents 건 — 캡처 도중 클라 접속이 끊겼을 가능성(무효 의심)" }
        else { Write-Host "[measure] validity: no connection-close traces in host log" }
    }
}
# 전달수/구성 게이트(§12-A 검증기준 7) — 2026-08-28 "300 요청 → 225" 무음 사고 재발 방지
# (FPSREnemySpawnSubsystem.cpp:2317-2328의 "delivered %d/%d ... Composition: %s" 자체 리포트를 확인한다).
if ($ForceClass) {
    $spawnLog = Join-Path $outDir "game.log"
    $delivered = $false
    $compositionOk = $false
    if (Test-Path $spawnLog) {
        $delivered = [bool](Select-String -Path $spawnLog -Pattern "delivered $EnemyCount/$EnemyCount" -SimpleMatch)
        $compositionOk = [bool](Select-String -Path $spawnLog -Pattern "Composition: $ForceClass x$EnemyCount" -SimpleMatch)
        # 실제 전달수(G2 P2-4, 아래 ASC 부착 게이트가 기대값으로 쓴다) — "delivered %d/%d…"(성공)와
        # "delivered %d in %d attempts — SHORT BY…"(부족) 두 로그 포맷 모두 "delivered <숫자>"까지는 같으므로
        # 이 패턴 하나로 둘 다 잡는다(FPSREnemySpawnSubsystem.cpp 의 두 UE_LOG 분기 대조 확인).
        $deliveredMatch = Select-String -Path $spawnLog -Pattern "delivered (\d+)" | Select-Object -Last 1
        if ($deliveredMatch) { $deliveredCount = [int]$deliveredMatch.Matches[0].Groups[1].Value }
    }
    if ($delivered -and $compositionOk) {
        Write-Host "[measure] validity: delivered $EnemyCount/$EnemyCount, Composition: $ForceClass x$EnemyCount"
    } else {
        Write-Warning "[measure] ⚠️ 전달수/구성 게이트 미달 — delivered=$delivered composition=$compositionOk (기대: delivered $EnemyCount/$EnemyCount, Composition: $ForceClass x$EnemyCount) — 2026-08-28 무음 사고 재발 의심"
        if (Test-Path $spawnLog) {
            Select-String -Path $spawnLog -Pattern "FPSR.SpawnEnemies:" -SimpleMatch | Select-Object -Last 3 | ForEach-Object { Write-Warning "  $($_.Line.Trim())" }
        }
    }
}
# ASC 부착 실증 게이트(G2 P2-4, 이번 후속수정의 핵심) — 위 전달수/구성 게이트는 "몇 마리가 스폰됐나"만
# 본다. ASC 가 실제로 붙었다는 증거는 지금까지 어디서도 확인되지 않았다 — -MemReport 를 줬을 때만
# ASCDump 가 로그에 찍혔고, 그마저 러너가 파싱하지 않았다. 오퍼레이터가 ②를 돌리며 -AttachASC 를
# 빠뜨리면 캡처는 ①과 동일한데 라벨은 "..._asc"로 저장되고 위 게이트는 전부 통과한다
# (2026-08-28 "300 요청 → 225"와 같은 종류의 무음 사고).
if ($AttachASC -or $MeasureLoadout -or $MeasureTickable) {
    $ascLog = Join-Path $outDir "game.log"
    $ascCount = $null
    if (Test-Path $ascLog) {
        # 느슨한 패턴 — DumpMeasureASCState() 가 찍는 정확한 문구가 바뀌어도(다른 세션이 그 함수를 동시
        # 수정 중일 수 있다) "instances: ASC" 뒤 첫 정수만 잡으면 깨지지 않는다. 현재 실물
        # (FPSREnemySpawnSubsystem.cpp, DumpMeasureASCState): "  instances: ASC %d x %dB = …".
        $ascMatch = Select-String -Path $ascLog -Pattern "instances:\s*ASC\s+(\d+)\s*x" | Select-Object -Last 1
        if ($ascMatch) { $ascCount = [int]$ascMatch.Matches[0].Groups[1].Value }
    }
    # 기대값 = 실제 전달수 — 요청수($EnemyCount)가 아니다. 위 게이트가 이미 부족을 경고한 상황이면 ASC 도
    # 그만큼만 붙을 수 있으므로, 요청수와 비교하면 전달수 부족과 ASC 부착 실패를 혼동한다.
    $ascExpected = if ($null -ne $deliveredCount) { $deliveredCount } else { $EnemyCount }
    if ($null -eq $ascCount) {
        Write-Warning "[measure] ⚠️ ASC 부착 게이트 미확인 — game.log 에 'instances: ASC N x' 줄이 없다(ASCDump 미실행 의심). 측정 CVar를 줬는데 이 줄이 없으면 캡처가 무효다."
    } elseif ($ascCount -lt $ascExpected) {
        Write-Warning "[measure] ⚠️ ASC 부착 게이트 미달 — instances: ASC $ascCount x (기대 ≥ $ascExpected) — -AttachASC 를 빠뜨렸거나 부착이 실패했을 가능성(2026-08-28 유형 무음 사고 재발 의심)"
    } else {
        Write-Host "[measure] validity: ASC attach $ascCount x (>= delivered $ascExpected)"
    }
}
Write-Host "[measure] done: $outDir"
