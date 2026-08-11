# 재개 프롬프트 — 인게임 맵 교체 (Synthwave City Kit → `L_Map1_City`)

> 작성 2026-08-11. 선행 세션에서 **조사·브랜치 분기까지만** 하고 중단(에디터 MCP 미연결). 보드 클레임은 **아직 안 했다.**

---

## 0. VibeUE MCP — 연결 완료 (2026-08-11 18:23)

이 머신에 VibeUE가 설치되지 않은 상태였다 → **설치·기동·등록까지 끝났다.** 구성:

```
Claude  --http-->  프록시 127.0.0.1:8089/mcp  --http-->  VibeUE 플러그인 127.0.0.1:8088/mcp (에디터 프로세스 내부)
```

| 구성요소 | 실체 | 상태 |
|---|---|---|
| 플러그인 | **Fab 설치본, 엔진 레벨** — `C:\Program Files\Epic Games\UE_5.7\Engine\Plugins\Marketplace\Untitleddd272512e3f0V1\` | VibeUE **4.0** · `EngineVersion 5.7.0` · 바이너리 **빌드 포함**(컴파일 불요) |
| 프록시 | `Scripts/vibeue-proxy.py` (리포 커밋본) — `Scripts/start-vibeue-proxy.bat` 로 기동 | 8089 LISTENING (pythonw) · 매니페스트 10 툴 |
| MCP 등록 | `claude mcp add --scope user --transport http vibeue http://127.0.0.1:8089/mcp` → `~/.claude.json` | `claude mcp list` = **✓ Connected** |

> 🔎 플러그인 폴더명이 `Untitleddd272512e3f0V1`(Fab 난독화)이라 이름으로 검색하면 안 나온다. `Plugins/VibeUE/`는 `.gitignore` 대상이며 **프로젝트 레벨엔 설치돼 있지 않다**(엔진 레벨로 충분).

**세션 시작 전 체크리스트**
1. **언리얼 에디터를 먼저 켠다** — 플러그인이 에디터 프로세스 안에서 8088을 호스팅한다.
2. 프록시가 떠 있는지 확인. 없으면 기동(중복 실행 안전):
   ```
   .\Scripts\start-vibeue-proxy.bat
   ```
   확인: `Get-NetTCPConnection -State Listen | ? LocalPort -eq 8089`
   로그: `%LOCALAPPDATA%\VibeUE-Proxy\proxy.log`
3. 그 다음 Claude 세션을 시작한다(MCP 서버는 세션 시작 시점에 붙는다).

**⚠️ 운영 주의**
- **재부팅하면 프록시는 자동으로 안 뜬다** — 시작프로그램 등록이 아직 없다(주 작업 머신엔 등록돼 있었다, 커밋 `ae31121a`). 매번 2번 항목을 실행하거나 시작프로그램에 바로가기를 걸 것.
- 에디터가 꺼져 있어도 프록시가 캐시 매니페스트로 `tools/list`를 답하므로 **툴 목록은 유지**된다. 단 `tools/call`은 "에디터를 켜세요" 안내를 반환한다 → 실제 저작 전에 에디터가 살아 있는지 확인.
- `Docs/WorkLog.md:1516` 교훈은 **프록시 도입으로 해소됐다**(에디터 재기동해도 세션 연결 유지). 다만 C++ 빌드가 끼는 작업은 여전히 세션을 쪼개는 편이 안전하다.

---

## 1. 아래를 새 세션에 붙여넣기

```
[인게임 맵 교체 — Synthwave City Kit 데모맵 기반 재구축] 이어서 진행한다.

■ 현재 상태 (선행 세션이 만든 것)
- 브랜치 `content/map1-synthwave` 로 이미 분기돼 있다(main 기준). 아래 변경이 전부 미커밋 상태로 얹혀 있다:
    M  Config/DefaultEngine.ini
    D  Content/Maps/Map_CyberCity.umap        ← 구 인게임 맵 삭제됨
    ?? Content/Maps/L_Map1_City.umap          ← 신규(데모맵 복제 + 대규모 정리 완료, 264KB)
    ?? Content/Synthwave_city/                ← Fab 키트 임포트(223 파일)
    ?? Docs/Map1Synthwave_ResumePrompt.md     ← 이 문서
- 보드 클레임은 **아직 안 했다.** 착수 전에 반드시 클레임할 것(하드 게이트).

■ 보드 (§6-9)
- 행: "인게임 맵 교체 — Synthwave City Kit 데모맵 기반 재구축"
  https://app.notion.com/3b83972ddd88819197bce3ee11664ab1
  상태=대기 · 우선순위=하이 · 마일스톤=M0 · 크기=L · 추천모델=Opus · 담당/브랜치 공란
- 선행조사 결과(2026-08-11): 크리티컬 0건 · 진행중 0건(병렬충돌 없음) · `선행작업` DB 필드 공란 ·
  M0는 최선두 마일스톤이라 개시규칙 문제 없음 · 행 본문의 프로즈 선행조건("Fab 에셋 임포트")은 로컬 파일로 충족 확인.
  → 절차상 착수 가능. `/board 클레임 ...` 으로 담당·브랜치(`content/map1-synthwave`)·상태(진행중) 기입 후 재조회 확인.
- 참고(내가 만든 것 아님): 후행 행 "적 그림자 LOD — 거리별 on/off PIE 실측"이 이미 상태=검증중인데
  그 행의 선행이 이 맵교체 행이다. 구 맵 기준 실측이었을 가능성 → 맵 교체 후 재실측 필요한지 사용자에게 확인할 것.

■ 🔴 지금 리포가 깨진 상태다 — P1에서 가장 먼저 고칠 것
`Config/DefaultGame.ini` 가 삭제된 맵을 계속 가리킨다:
  - 37행  RunMap=/Game/Maps/Map_CyberCity.Map_CyberCity
  - 54행  +MapsToCook=(FilePath="/Game/Maps/Map_CyberCity")
→ 둘 다 `/Game/Maps/L_Map1_City` 로 교체. (코드 참조는 주석뿐 — `Roadmap.md` §8 "갱신 지점"과 일치)

■ 🔴 배치 툴이 새 키트를 못 본다 — P1에서 같이 고칠 것
`Config/DefaultEditor.ini`:
  - 7행  +PaletteFolders=(Path="/Game/PolygonCyberCity")   ← Synthwave 키트가 없다
  - 9행  PlacementGridSize=250.000000                       ← Synty CyberCity Base 실측값(2026-07-20). 새 키트 미측정
→ 팔레트에 `/Game/Synthwave_city/Meshes` + `/Game/Synthwave_city/Blueprint` 추가.
  (PolygonCyberCity 제거는 별건 — 맵2·3 계획이 걸려 있어 이 작업 범위 밖. 제거하지 말 것)
→ PlacementGridSize 는 에디터에서 `SM_module_road_02/03/04`·`SM_floor`/`_02`/`_03` 의 GetBounds 를 실측해 교체.
  실측 전에는 250 을 그대로 믿지 말 것(Synty 키트 수치다).

■ ⚠️ 사용자 결정 대기 1건 — 진행 전에 물어볼 것
`Config/DefaultEngine.ini` diff 에 보드 행이 언급하지 않은 변경이 섞여 있다:
    +r.SkinCache.CompileShaders=True
    +r.RayTracing=True
Fab 데모 레벨을 열 때 UE 가 권장 프로젝트 세팅을 자동 반영했을 가능성이 높다.
레이트레이싱 전역 on 은 제1원리(적 200-300 을 싸게, 액터당 비용 최소화 우선)와 방향이 반대이고,
M0 의 바로 다음 단계가 성능 정량 베이스라인 실측(§7-6 M0 (b))이라 **확인 없이 넘기면 베이스라인 자체가 오염된다.**
→ 되돌릴지/유지할지 사용자에게 확인하고, 결정을 보드 행 로그에 남길 것.

■ 신규 맵 필수 배치물 5종 (보드 행 체크리스트 = 블록아웃 가드레일과 1:1)
  1. PlayerStart
  2. AFPSRFlowFieldBoundsVolume — 정확히 1개(0=원점 폴백, 2+=단일맵 모호). 볼륨 중심에 떠 있는 콜라이더 두지 말 것
     (중심 다운트레이스로 그리드 원점 바닥을 잡는다)
  3. AFPSREnemySpawnPoint — Z = 바닥 + 100 (캡슐 반높이 90. 바닥에 딱 붙이면 스폰된 적이 바닥을 뚫고 가라앉는다)
  4. WorldSettings 의 GameMode 확인
  5. MapId 게임플레이 태그 = 전부 Default (단일맵. MapId 붙은 슬롯 볼륨은 멀티맵 전용 경로를 깨운다)

■ 플로우필드 셀 예산 (실수치, `FPSRFlowFieldComputer.h:262-266`)
  - DefaultCellSize = 200cm · MaxGridDimPerAxis = 256 · MaxTotalCells = 40,000
  - 정사각 기준 상한 = 200×200셀 = **400m × 400m** (축당 256셀=512m 이지만 총 40,000셀에 먼저 걸린다)
  - 더 넓히려면 볼륨의 CellSizeOverride 를 키운다(라우팅 해상도 down). ClimbableStepHeight 기본 45cm(UE MaxStepHeight)
  → 볼륨 크기를 이 예산 안에서 먼저 정하고 나서 배치를 시작할 것. 나중에 넓히면 베이크가 무효가 된다.

■ ⚠️ 배치 시작 전 필수 검사 — 콜리전
플로우필드의 BuildObstacleMask 는 **ECC_WorldStatic 다운트레이스**다. 콜리전 없는 건물 메시는 배치해도 스웜이 그냥 통과한다.
Fab 키트는 임포트 직후라 콜리전 미확인 상태다(`Docs/SyntyArtPilot_ResumePrompt.md:18` 이 같은 함정을 경고).
→ 블록아웃 탭의 **"상태 검사"** 를 먼저 돌려 카드 배지(✓/✗)를 확인하고, ✗ 인 건물은 콜리전을 만들어 준 뒤 배치할 것.

■ 사용할 도구 (이미 구현돼 있다 — 새로 만들지 말 것)
`Source/FPSRogueliteEditor/.../Blockout/` — 에디터 탭 "FPSR 블록아웃 툴"
  - 팔레트(폴더별 카드) · "선택 배치"(카메라 앞) · **"뷰포트 배치"**(심시티식 고스트, 격자·바닥 스냅, 좌클릭 연속배치, `[`/`]` 회전, ESC 종료)
  - "상태 검사" — 팔레트 전 에셋 WorldStatic 콜리전 배지
  - **"레벨 검증"** — 가드레일 6종(콜리전·지면·스폰Z·볼륨·셀예산·중심) → Message Log "FPSRBlockout"
  - "선택→프리팹" — 선택 액터들을 경량 BP_* 로 묶어 팔레트에 즉시 등록(서브레벨 없음)
  - 팔레트 폴더/격자 설정 = Project Settings > FPSR > FPSR Blockout (= `Config/DefaultEditor.ini`)

■ 키트 인벤토리 (Content/Synthwave_city/, 223 파일)
  - 메시 57 — Buildings(SM_building_01~12, SM_dome_building×4) · Floor(SM_floor/_02/_03) · Road(SM_road, SM_module_road_02/03/04)
    · Bridge · Stairs(×2) · Arch · Column · Fence · Car(×5) · Advertisements(×10) · Hologram · Street_lamp · Light_base
    · Road_sign · Mountain(×2) · Tree(palm ×3) · Tree_base(×3) · Sun
  - 액터 BP 7 — BP_building / BP_building1 / BP_building2 / BP_fence_spline / BP_road_01 / BP_road_02 / BP_tree_decoration
  - Sky_system — BP_sky · SM_SkySphere · MI_sky_day / MI_sky_night / MI_stars · M_sky_sphere · T_cloud_01~05
  - FX — NS_cars (Niagara)
  - Material 71 · Textures 72
  - 원본 데모: Content/Synthwave_city/Level/L_showcase_synthwave_city.umap (10.4MB)
    → 복제·정리본 = Content/Maps/L_Map1_City.umap (264KB)

■ 작업 순서
  P1 (에디터 불필요 — 지금 바로)
     ① 보드 클레임  ② RayTracing 결정 확인  ③ DefaultGame.ini RunMap/MapsToCook 교체
     ④ DefaultEditor.ini 팔레트에 Synthwave 폴더 추가  ⑤ 커밋
  P2 (에디터 필요 — VibeUE MCP 연결 확인 후)
     ① "상태 검사"로 콜리전 감사  ② 모듈 그리드 GetBounds 실측 → PlacementGridSize 교체
     ③ FlowFieldBoundsVolume 크기를 셀 예산 안에서 확정  ④ 필수 배치물 5종
     ⑤ 세부 에셋 배치(← 사용자가 요청한 본 작업)  ⑥ "레벨 검증" 0건까지  ⑦ PIE 스모크
  P3 (마감)
     ① SSOT 전파 — `Docs/SSOT/Roadmap.md` §8 · `Game.md` §9 의 맵 목록에서 Map_CyberCity → L_Map1_City
     ② `Docs/WorkLog.md` 맨 위에 상세 경위  ③ 보드 행 마감(커밋 해시)  ④ `--no-ff` 머지

■ 🔑 순서에 대한 주의 (제1원리)
"세부 프롭 배치"는 단순 미화가 아니라 **M0 (b) 성능 베이스라인의 선행**이다.
프롭을 나중에 추가하면 ① 플로우필드 장애물 마스크 ② 드로우콜/프레임 예산이 **둘 다** 바뀌어 실측이 무효가 된다
(§7-6 M0 (a′) 가 맵 교체를 (b) 의 선행으로 강제한 것과 정확히 같은 논리).
→ 배치를 "일단 대충 깔고 나중에 다듬는다"로 쪼개지 말 것. 이번 작업 단위 안에서 밀도를 확정한다.

■ 읽을 문서
  - `Game.md` (허브) + `Docs/SSOT/Workflow.md` §6(전체) — 브랜치·검증·보드 프로토콜
  - `Docs/SSOT/Roadmap.md` §7-6 M0 (a′) · §8 인벤토리
  - `Docs/SSOT/Performance.md` §5-2 (플로우필드 2층 인지)
  - `Docs/SyntyArtPilot_Scoped_ResumePrompt.md` 75-79행 — 직전 CyberCity 파일럿의 동일 절차
  - `Source/FPSRogueliteEditor/Public/Blockout/FPSRBlockoutValidator.h` — 가드레일 6종 정의
```

---

## 2. 선행 세션이 실제로 한 것 / 안 한 것

**한 것**
- `git fetch` — 로컬 = origin 동기(c70377de)
- `main` → **`content/map1-synthwave` 분기**(미커밋 변경 전부 이관)
- 보드 클레임 **조사**(`/board`, SQL 1회) — 크리티컬 0 · 진행중 0 · 선행 공란 확인
- 로컬 실물 감사 — 깨진 config 2건 + 팔레트 누락 + 그리드 수치 불일치 + RayTracing 혼입 발견
- 셀 예산 실수치 도출(400m × 400m 상한)

**안 한 것 (새 세션이 할 것)**
- 보드 클레임 실행(상태·담당·브랜치 기입) — **미실행**
- 코드/config 수정 — **0건** (working tree 는 사용자가 만든 상태 그대로)
- 커밋 — **0건**
