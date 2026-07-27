# U22a-A — PCG 도시 밀도 저작 · 실행(재개) 프롬프트

> **작성 2026-07-20.** 이 문서 하나로 **새 세션이 U22a-A(PCG로 `Map_CyberCity` 채우기)를 이어받는다.**
> 선행 필독: `Game.md`(SSOT 허브) + `PROGRESS.md` + `Docs/U22_AssetReplacement_Prompt.md`(§3 A). 이 문서는 그 위에 **A(환경 밀도)를 PCG로 한다**는 결정과 실행 절차를 얹은 것이다.
> 수치·경로·줄번호는 2026-07-20 실측. 줄번호는 커밋마다 밀리니 안 맞으면 grep으로 재확인.

---

## §0 세션 시작 체크리스트 (건너뛰면 세션 통째로 낭비)

1. **에디터를 먼저 켜고 세션을 시작한다.** VibeUE MCP는 **세션 시작 시점에만** 붙는다 — 세션을 켠 뒤 에디터를 켜도 안 붙어 **세션 재시작**이 필요하다.
2. **PCG는 이 세션 시작 전에 이미 활성화돼 있어야 한다**(§2). 그래야 VibeUE가 붙은 채로 PCG를 쓸 수 있다. 세션 안에서 PCG를 켜면 에디터 재시작→VibeUE 끊김→세션 재시작이 된다.
3. 이 작업 = **PCG 그래프 저작 = 에디터 필수.** 에디터 없으면 우회·추측 말고 사용자에게 요청·대기.
4. 브랜치 = `phase/u22a-environment`. **머지 완료**(§2)로 u22a 소스에 도시툴 C++ 포함 → **게임모듈 빌드/재빌드 안전**(프롬프트 뜨면 **Yes**). ✅ 이 세션 검증 빌드 성공 → 신선 바이너리라 **재빌드 없이 열림**.
5. 편집→PIE는 에디터 재시작 후(World Leak 회피).

---

## §1 확정 사항 (재논의 금지)

| 항목 | 결정 (2026-07-20 사용자) |
|---|---|
| **U22a 순서** | **환경(건물) 먼저 → 지형 확정 후 게임플레이 레이어 이식 → D.** 화이트박스 지형은 **최종이 아니다**(건물이 걷는 공간을 바꾼다) |
| **A 생성 방식** | **UE PCG.** (Python 스크립트/툴버튼 아님 — 검토 후 PCG 채택) |
| **건물 소스** | `Content/PolygonScifi/` (187M·883 에셋, 사용자 임포트). 사이버펑크 시티 킷 |

🔴 **알아둘 현재 상태**: `Map_CyberCity` = **리네임된 `L_GameFloor`(U21 화이트박스)**. **게임플레이 레이어(룸·미션 스폰·문·스폰존)가 0개**다(실측: FPSRSpawnRoom/MissionSpawnPoint/FPSRDoor/SpawnZone 전부 0, 적스폰28·플로우필드4·SRS20만 있음). → **지금 PIE 런은 보스까지 안 간다**(미션 스폰 지점 0 → `FPSRRunDirectorSubsystem.cpp:631`의 `TActorIterator<AFPSRMissionSpawnPoint>`가 0개를 순회). **이건 정상·미결**이고, 이식은 **A(환경) 완료 후**다(§7).

---

## §2 브랜치/기반 (step 0 — ✅ 완료됨, 2026-07-20)

**이미 처리됨 (이 세션, 에디터 닫힌 상태):**
- ✅ **`main`→`u22a` 머지** (`db31d6a9`) — 도시툴 C++ 10파일 + `M_BlockoutGhost.uasset`(LFS 복원됨) + config + director/SSOT 문서 반입. `PROGRESS.md` 충돌 1건은 u22a(현 핸드오프) 유지로 해소. **검증: `git diff main -- Source` = 주석 2줄만** → u22a 소스 = main + 주석 → **빌드/재빌드 안전**.
- ✅ **PCG 플러그인 활성화** (`312e5df9`) — `.uproject` Plugins에 `{ "Name": "PCG", "Enabled": true }` 추가. 엔진 프리컴파일(`UnrealEditor-PCG*.dll`)이라 **게임모듈 재빌드 불요·에디터 재시작만**. 의존 엔진플러그인(EditorScriptingUtilities·ComputeFramework·GeometryProcessing·MeshModelingToolset) 자동 활성.
- ✅ **검증 빌드 `Result: Succeeded`** — `FPSRogueliteEditor Win64 Development`(0 에러, ~18분). 머지 트리가 PCG 활성 상태로 정상 컴파일. 신선 바이너리 → 새 세션 에디터가 **재빌드 없이 열림**.

**새 세션이 할 것:**
- 에디터를 열어 **PCG 로드 확인**: Place Actors에 **PCG Volume**, 콘텐츠 우클릭 > PCG 항목. 스모크 `ModuleLoads` 그린.
- 재빌드 프롬프트가 뜨면 **Yes**(소스에 툴 포함이라 안전).
- ⚠️ 미커밋 untracked: `Content/PolygonScifi/`(건물 소스, 커밋 여부는 A 진행하며 판단) · 이 문서.

---

## §3 실행 (PCG)

### 3-0. perf 베이스라인 (아직 없으면 여기서 먼저)
U21 S4가 정량수치를 안 남겨 **교체 전 기준선이 없다.** 건물 깔기 **전에** 현 `Map_CyberCity` CSV 5스탯을 뜬다.
`csvprofile frames=N` (`start`/`stop` 아님) · **PIE 1920×1080**(기본 640×480 무의미) · 밀도=`FPSR.SpawnEnemies N`(`FPSR.EnemyTarget`은 디렉터가 0.25초마다 덮어써 무용). 분석=`Scripts/s4-check-capture.py`. **수치를 PROGRESS에 기록.**

### 3-1. 맵 지오 파악 + 생성 설계
- `Map_CyberCity`: 264m, **중앙 광장 허브**, 2×2 섹터, 플로우필드 바운드볼륨 4, 적스폰 28, SRS 액터 20. 셀 132×132=17,424(상한 40,000).
- **2계층 스킴**:
  - **(A) 스카이라인/배경** — 플레이 영역 **밖** perimeter에 통짜 `SM_Bld_Background_*`/`Large` scatter. 순수 시각 밀도, 콜리전 최소/없음, ISM.
  - **(B) 아레나 구조** — 플레이 영역 **안** 블록에 건물 footprint. **플로우필드 계약 준수**(§4). 도로=걷는 길은 **비운다**(스트리트 마스크 = 플로우필드 바운드/도로 스플라인 기반 제외).
- 그리드 250cm 정합(§4). 시드/밀도 파라미터로 재생성.
- ⚠️ 실제 블록/도로 패턴은 **맵을 열고** 광장·섹터·바운드 배치를 보고 확정(헤드리스 추측 금지).

### 3-2. PCG 그래프 저작 + 배치
- **PCGGraph 에셋**: 샘플러(Surface/Grid Sampler, 또는 Spline로 도로) → 포인트 변환(그리드 정렬·회전·건물 타입 가중 선택) → **`Static Mesh Spawner`**(출력=ISM, **WorldStatic BlockAll 콜리전 설정**) + **스트리트 제외 마스크**(Difference/Density filter).
- **저작 경로**: VibeUE Python(**PCGPythonInterop** = `PCGPythonInteropEditor`+`PythonScriptPlugin` 존재)으로 그래프 노드 프로그래매틱 구성 **시도** → 안 되면 **사용자 GUI 협업**(내가 노드 레시피·파라미터를 제공하고 사용자가 그래프 배선). PCG는 비주얼 프레임워크라 GUI 저작이 자연스러움.
- `Map_CyberCity`에 **PCG Volume** 배치 + 그래프 할당 + **Generate**. (편집→저장→에디터 재시작→PIE 순)

### 3-3. 검증 (완료 판정)
1. **perf**: 인스턴싱 동작(드로우콜↓) + **3-0 베이스라인 대비 회귀 없음**(CSV 5스탯, 수치 기록).
2. 🔴 **플로우필드**: 런타임 다운트레이스가 **PCG-생성 ISM 콜리전을 인식**하는지 = **최대 리스크(R1)**. PIE 로그 `no-floor` 비율 · 45~60 함정 구간 0 · 적이 맵 전역 추격.
3. **블록아웃 검증기** `FFPSRBlockoutValidator::ValidateLevel` + **앵커 커맨드릿** `UFPSRValidateAnchoredDataCommandlet` 통과.
4. **사용자 육안 게이트** — 룩 승인.

---

## §4 콘텐츠 계약 (어기면 재작업 / 적이 못 지나감)

**플로우필드** (`Source/FPSRoguelite/Public/Enemy/FPSRFlowFieldComputer.h`):
- `DefaultCellSize` 200(2m) · `MaxTotalCells` **40,000(하드락)** · `AgentFootprintRadius` 40.
- `ObstacleProbeZ/HalfHeight` 120/60 → 장애물 판정이 **셀바닥+60cm부터**. `Climbable` ≤45 밟고넘음 / ≥60 돌아감 / **45~60 = 적이 끼는 함정**. **커버로 스웜 쪼개려면 ≥60cm 필수.**
- 🔴 **장애물 마스크 = 다운트레이스 기반** → **PCG-생성 메시에 반드시 WorldStatic BlockAll 콜리전**(없으면 플로우필드가 벽을 못 봄). Synty 임포트 메시 콜리전 확인 필수.
- 🔴 **바닥 앵커**: 첫 `PlayerStart` 아래 트레이스로 `GridOrigin.Z`. **스폰 지점 위 콜리전 장식물 = 지면 시드 전멸**(실사례 `Plaza_Emblem` 47cm). PCG가 스폰 위에 뭘 얹지 않게.

**그리드/룩**:
- **그리드 250cm** (Synty Base Floor/Ceiling 250×250 · Block 1500=6×250). ⚠️ 200은 플로우필드 셀 단위이지 건물 단위 아님.
- **Nanite OFF.**
- 🚨 **SRS stencil 규약 미확정** — 셀 `MI_SRS_BASE_CelShader`=stencil 1~255 요구 / 아웃라인=stencil 0(적만). **셀↔아웃라인이 stencil 0에서 상호배타.** 환경 메시 수백 개에 `render_custom_depth`/stencil **일괄 적용 전에 규약부터 확정**(안 그러면 전량 재작업). PCG StaticMeshSpawner의 stencil 설정도 이 규약을 따를 것.
- 문 콜리전 이중 계약(이식 단계): `DoorMesh`=`ECC_FPSRPlayerPawn` / `FrameMesh`=`ECC_WorldStatic`(`FPSRDoor.cpp:28-43`).

---

## §5 리스크 (에디터 실측 검증 대상)
- **R1 (최대)**: PCG-생성 ISM 콜리전 ↔ 런타임 플로우필드 다운트레이스 정합. ISM/HISM은 LineTrace에 잡히나(WorldStatic BlockAll 시), PCG 생성물이 이걸 만족하는지 **PIE 실측 필수**.
- **R2**: PCG 생성/베이크 타이밍. 정적 도시는 **에디터 Generate → 레벨에 베이크/저장 → 런타임 존재**여야(런타임 생성이면 플로우필드 BeginPlay와 순서 경쟁). PCG 컴포넌트의 generation trigger 확인.
- **R3**: PCG 그래프 헤드리스/VibeUE 저작 미검증. VibeUE는 BP/위젯/머티리얼 그래프엔 강하나 **PCG 그래프는 별개 에디터** → 안 되면 사용자 GUI 협업으로 즉시 전환.

---

## §6 참조 (실측, 재조사 불요)

- **미션 스폰 의존**: `FPSRRunDirectorSubsystem.cpp:631` `TActorIterator<AFPSRMissionSpawnPoint>`. 미션 DA `SpawnPointTag`(`FPSRMissionDataAsset.h:40`)로 매칭. → 이식 전엔 미션 스폰 0.
- **도시 툴**(main): `FFPSRBlockoutSpawn::SpawnPiece(World, AssetData, Transform, bTransientGhost)` = 조각당 `AStaticMeshActor`+WorldStatic BlockAll(**인스턴싱 아님**). 팔레트=`UFPSRBlockoutSettings::PaletteFolders`(config `DefaultEditor.ini`, **재빌드 불요**). 현재=`/Game/PolygonCyberCity`. 그리드 250. 검증기=`FFPSRBlockoutValidator`. → **PCG와 별개**지만 수동 터치업에 병용 가능(머지 시).
- **PolygonScifi 건물**(`Content/PolygonScifi/Meshes/`): 통짜=`Background_Med`(9)/`Small`(4)/`Lrg`(3)·`Large`(6)·`Bank`/`Warehouse`/`StripClub`/`Chopshop`/`Shop`/`Power`(4)/`City_Wall`/`LandingPad`. 모듈섹션=`Section_Wall`(5)/`Window`(4)/`Door`(7)/`Corner`(3)/`Industrial`(3). 환경=`Env_Ground_Tile`/`Sidewalk`/`Road_Lines`/greeble. (Buildings 99·Environments 다수)
- **PolygonCyberCity**(기존): 건물 166·Props 581 — 병용 가능한 2번째 건물 소스.
- **PCG**: 엔진 프리컴파일. `PCGPythonInterop`(`PCGPythonInteropEditor`+`PythonScriptPlugin`) 존재 → Python 저작 경로 가능성.

---

## §7 A 완료 후 (이 세션 범위 밖, 순서만 명시)

1. **게임플레이 레이어 이식** — `L_Sandbox`의 룸 25·미션 스폰 8·문·스폰존 볼륨을 **확정된** `Map_CyberCity` 지형에 맞게 **재배치(재저작)**. 좌표 복사가 아니라 새 지형에 맞는 저작.
2. **D** — PIE 런 1회 완주(미션 스폰·룸 개방·문·보스) → **통과 후** `L_Sandbox` 삭제(⚠️먼저 `L_MainMenu.umap`의 L_Sandbox 참조 1건 정리) → 패키지 쿡 1회 → **u22a→main `--no-ff` 머지**(이때 §2의 PROGRESS 충돌 자명 해소).
3. **perf 수치·채택 스택을 문서에 남긴다**(U21이 정성판정만 남겨 기준선이 없었던 실수 반복 금지).
