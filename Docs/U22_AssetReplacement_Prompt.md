# U22 — Synty 셀/툰 에셋 전체교체 · 실행 프롬프트

> **작성 2026-07-19.** 구 `Docs/AssetReplacement_Synty_ResumePrompt.md`는 **폐기본**(SRS를 "최후 폴백 유료옵션"이라 하는 등 SSOT와 모순) — 읽지 말 것. 이 문서가 U22의 유일한 실행 프롬프트다.
>
> 여기 적힌 수치·경로·줄번호는 **2026-07-19 HEAD(`c3245e76` 이후) 기준 실측**이다. 7 에이전트 조사 + 적대 검증(주장 13건 반박·정정)을 거쳤다. 다만 **줄번호는 커밋마다 밀린다** — 인용 줄이 안 맞으면 그 줄을 믿지 말고 grep으로 재확인할 것.

---

## §0 세션 시작 체크리스트 (건너뛰면 세션이 통째로 낭비된다)

U22 작업의 대부분은 **에디터가 있어야** 한다(레벨 저작·머티리얼 실제값·VAT 베이크·위젯 배선).

1. **에디터를 먼저 켜고 세션을 시작한다.** VibeUE MCP는 **세션 시작 시점에만** 붙는다 — 세션을 켠 뒤에 에디터를 켜도 안 붙어서 **세션 재시작**이 필요하다.
2. **머지·브랜치 전환 전에는 에디터를 끈다.** 열린 에디터가 `.uasset`/`.umap`을 잠가 `git checkout`/`merge`가 `unable to unlink: Invalid argument`로 실패하고 half-switched dirty 상태가 된다. 복구 = `git checkout -f`.
3. **에디터에서 에셋을 편집한 뒤 같은 세션에서 PIE를 돌리지 않는다**(World Leak). 편집 → 에디터 재시작 → PIE.
4. **헤드리스에서 크래시가 확정된 조작 3종** — 리그드 GLB 임포트(Interchange), 배치 IK 리타겟, 스켈레탈 FBX 익스포트. 이건 에디터 GUI로 하거나 사용자에게 요청한다.
5. 에디터가 없으면 **우회 조사·추측하지 말고 사용자에게 켜달라고 요청하고 대기**한다.

빌드는 별개다 — **빌드 대상 클론 = `FPSRoguelite2`**, 에디터를 닫고 실행한다.

---

## §1 확정 사항 (다시 논의하지 말 것)

### 1-1. 사용자 결정 (2026-07-19)

| 항목 | 결정 |
|---|---|
| **유닛 분할** | **2개로 쪼갠다** — `U22a`(환경/맵) → 사용자 육안 게이트 → `U22b`(무기·캐릭터·적·UI·VFX) |
| **맵 범위** | **맵1 CyberCity만.** 맵2(숲)·맵3(우주)은 이번 범위 밖 |
| **레거시 팩 삭제** | **확실히 안 쓰는 것만 지금** = `ModularSciFiStation`(1.4G, 참조 0건 실측). Paragon·LPAMG는 **보류** |
| **UI 방식** | **스타일만 리스킨** — 기존 WBP 구조·`BindWidget` 이름 유지, 이미지/색/폰트만 Synty로 |

### 1-2. U21 파일럿에서 확정된 것

- **렌더러 = SRS(StylizedRenderingSystem)** 확정. 레벨 배치 액터 4종: `BP_StylizedRenderingSystem` · `BP_SRS_Fog` · `BP_SRS_CubemapReflectionVolume` · `BP_LightCalibrationActor`. 현재 `L_GameFloor` **1개 맵에만** 배치돼 있다.
- **무기 백본 = Synty Military 전환**(→ 후에 SF 리스킨).
- **캐릭터 애님 = 3인칭 Blu · 1인칭 PWAS**. 손저작 AnimBP 계획(U15/U19)은 이 파이프라인에 흡수된다.
- `r.CustomDepth=3` 이미 켜짐 (`Config/DefaultEngine.ini:33`).

### 1-3. ⚠️ U21이 남긴 미결 제약 — U22의 선결조건

**SRS stencil 규약이 아직 없다.** 실측:

- 셀 `MI_SRS_BASE_CelShader` = stencil **1~255** 요구 → 그런데 **맵에 stencil ≥1인 프리미티브가 0개**(`L_GameFloor.umap`에 `CustomDepthStencilValue` 문자열 0회).
- 아웃라인 `M_SRS_Outline01` = stencil **0~0** → **적만** 받는다(적 메시가 `renderCustomDepth=True, stencil=0`).
- 즉 **셀과 아웃라인이 stencil 0에서 상호배타**다. 둘 다 받으려면 적 stencil을 1로 올리고 아웃라인 마스크도 1~255로 넓혀야 한다.

🚨 **이게 U22a 공수를 지배한다.** 규약을 먼저 확정하지 않고 환경 메시 수백 개에 `render_custom_depth`/stencil을 일괄 적용하면 **전량 재작업**이다.

**톤다운(노출/블룸) 값도 미결**이다. `PP_Synthwave_Grade`에 노브 22개가 모여 있고, 실제 과잉값은 `BloomIntensity 1.6`(엔진 기본 0.675의 2.37배) 하나로 좁혀져 있다. **룩 기준선 없이 맵을 저작하면 첫 맵부터 재작업 후보**다.

### 1-4. ⚠️ perf 기준선이 없다

U21 S4는 **사용자 정성 판정**("다음 유닛 넘어갈 만한 퍼포먼스")만 남았고 **정량 수치가 리포 어디에도 없다**. 남은 유일한 수치는 파일럿 이전의 "Custom Depth 패스 1.33ms"(2026-07-10).

→ **U22a 첫 작업 = 교체 전 현 상태 캡처.** 계측 인프라는 이미 있다(`UFPSREnemyMetricsSubsystem` + CSV 커스텀 스탯 5종). 기준선 없이 교체하면 나중 측정만으로 **회귀인지 원래 그런지 판별이 불가능**하다.

---

## §2 현황 실측 (2026-07-19)

### 2-1. 이미 임포트된 팩 — 추가 구매/임포트 불요

| 폴더 | 용량 | uasset | U22 역할 |
|---|---|---|---|
| `Content/PolygonCyberCity/` | 467M | 1,641 | **맵1 환경 소스**. Props 581·Buildings 166·Characters 78·Vehicles 62·Weapons 52·Base 35 |
| `Content/PolygonMilitary/` | 373M | 2,590 | **무기 백본**. Modular 286 = Weapon_A 69 · Weapon_B 70 · Attachments 78 · Presets 42 · Crosshairs 27 |
| `Content/Synty/UISciFiSoldierHUD/` | 325M | 1,826 | **UI 소스**. Widgets 254 · Textures 1,510 · Fonts 7 |
| `Content/StylizedRenderingSystem/` | 216M | 332 | **확정 렌더러** |
| `Content/ProceduralWeaponAnimationSystem/` | 165M | 147 | **1P 팔**. `ABP_FPChar` + 무기군별 포즈 6종 |
| `Content/PolygonParticleFX/` | 104M | 256 | **VFX 소스**. Niagara 120개 — **참조 0건(미사용)** |
| `Content/Assets/Anime_Girl_Character_-_Blu-6ccdbbe7/` | 92M | 31 | **3P 캐릭터** |

전체 `Content/` = **4.0G · LFS 추적 9,523 파일**.

### 2-2. 리포에 없는 것 (이번 범위 밖이거나 취득 필요)

Synty Nature/Biomes(맵2) · Synty Sci-Fi Space(맵3) · 적 스웜 전용 팩 · Epic Niagara Examples — 전부 **미임포트**. 맵1만 하기로 했으므로 U22 범위에서는 **적 스웜 소스만** 문제가 된다(§4-3).

### 2-3. 지금 실제로 쓰이는 것

**무기 8종 — Rifle 1종만 Synty 전환 완료**

- `DA_Weapon_Rifle` = Synty Military 9참조(`SK_Wep_Mod_A_Body_01` + Barrel_03/Grip_01/Handguard_03/Mag_01/Stock_03/Trigger_01 + Reddot_01/Scope_09) + PWAS 2(`ABP_FPChar`, `AM_FP_RifleReload`) + Synty 레티클 1. **→ 나머지 7정의 참조 템플릿이다.**
- 나머지 7종 = LPAMG. **SMG만 모듈 구조**(`SK_LPAMG_AG14W` + 파츠 3), **Bazooka/ChargeLaser/LMG/Shotgun/Sniper 5종은 `SM_LPAMG_*_Preview` 통짜 스태틱메시 = 사실상 플레이스홀더**. Knife는 `SM_LPAMG_Knife`.
- **총구화염 = 8종 중 6종**(Bazooka/LMG/Rifle/SMG/Shotgun/Sniper). ChargeLaser·Knife는 파티클 참조 0.
- **발사 사운드 = 8종 전부 LPAMG**(Knife만 `SC_LPAMG_WEP_Knife_Attack`, 나머지 `_Fire`).
- 8종 전부 자체 크로스헤어(`DA_XH_*` + `MI_XH_*`) 참조 — U12/U17 산출물, **유지**.

**적 — 교체 대상이 좁다**

- `BP_EnemyMeleeBase` · `BP_EnemyRangedBase` 2개만. 둘 다 `SM_BroBot_VAT` + `MI_BroBot_VAT_Enemy`. `DA_EnemyRoster`에도 이 2클래스뿐.
- **VAT에 실제 베이크된 애님 = `BCC_01_BroBot_Walk` 1개뿐.** BroBot 원본에는 Idle·Walk·Run·Jump·Jump_Start·Loop가 있으나 **Attack/Death 애님은 프로젝트 전체에 없다.**
- 🔴 **VAT 파라미터 이름 불일치(실증)**: 코드(`FPSREnemyAnimProfile.cpp`)는 `AnimationIndex`/`Phase`를 쓰는데 `M_BroBot_VAT`에는 **그 이름이 없다**. 실제 보유 = `Playrate`/`StartFrame`/`EndFrame`/`TimeOffset`/`AutoPlay`/`NumFrames`/`SampleRate`. **U22b에서 반드시 정리**.

**기타 실사용**

- 문/스폰게이트 = ZerinLabs(`SM_DoubleDoor`·`SM_SM_WallDoor`·`SM_SpawnGate`, `L_Lobby`·`L_Sandbox`에서 사용).
- 보스 = Paragon `Prime_Helix`. 로비 표시 폰 = BroBot.
- UI 25개(HUD 10·Menu 8·Widget 5·`WBP_Settings`·`DA_Crosshair_ColorPreset`). **Synty 참조는 `WBP_RunHUD` 1건뿐**.

### 2-4. 사문·댕글링 (U22에서 정리해야 쿡이 안 터진다)

- `Config/DefaultGame.ini:51-57` `MapsToCook`에 **`L_RunPersistent`·`L_MapA`·`L_MapB` 3개가 등재돼 있는데 디스크에 없다**(맵은 6개뿐).
- `BP_FPSRPlayer` → `/Game/Fab/Anime_Girl_Character_-_Blu/…/Blu_-_Animated` = **댕글링**. `Content/Fab/`는 **파일 0개**다. 실제 Blu는 `Content/Assets/Anime_Girl_Character_-_Blu-6ccdbbe7/`.
- `Content/_SyntyPilot/` 8파일(`IK_Blu`·`IK_Manny`·`RTG_Manny_to_Blu` 등)은 **gitignore**라 커밋되지 않는다. **Blu 리타겟 자산이 여기 갇혀 있다** — 정식 경로로 승격할지 결정 필요(§6).

---

## §3 U22a — 환경(맵1 CyberCity)

```
Game.md + PROGRESS.md 먼저 읽어. Docs/U22_AssetReplacement_Prompt.md의 U22a를 진행한다.
읽을 SSOT: Docs/SSOT/Roadmap.md §8, Docs/SSOT/Concept.md §1-C-9, Docs/SSOT/Performance.md §5,
Docs/SSOT/Architecture.md §3-4, Docs/SSOT/Workflow.md §6.
브랜치: main에서 phase/u22a-environment 분기.
```

### 순서 (이 순서를 바꾸면 재작업이다)

**A-0. 교체 전 perf 베이스라인 캡처** ← 반드시 첫 단계
`csvprofile frames=N`(`start`/`stop` 아님 — 엔진이 스스로 EndCapture하므로 0바이트가 구조적으로 불가)으로 현 `L_GameFloor` 기준 CSV 5스탯을 뜬다. **PIE 창을 1920×1080으로**(기본 640×480은 화면 크기가 곧 지표라 무의미). 밀도는 `FPSR.SpawnEnemies N`(캡 우회)로 만들고, `FPSR.EnemyTarget`은 0.25초마다 디렉터가 덮어써서 **무용**이다. 분석 = `Scripts/s4-check-capture.py`.

**A-1. SRS stencil 규약 확정** ← 물량 작업 전
§1-3의 상호배타를 어떻게 풀지 정하고 **문서에 적는다**. 결정 후에야 메시에 stencil을 먹인다. 규약 없이 수백 개를 건드리면 전량 재작업.

**A-2. 톤다운 값 확정**
`showflag.VisualizeHDR 1`로 클램프 방향을 10초 확인한 뒤 조정한다. min에 붙었으면 1.0 고정=어두워짐(해결), max면 **더 밝아짐(악화)**. 권고 = bias 1.0→0 · min/max→1.0 · bloom 1.6→0.7 · `AutoExposureMethod`는 **Histogram 유지**(Manual로 바꾸면 GreyMult가 0.18→1.0으로 5.56배 튄다).

**A-3. 맵 저작** — 아래 콘텐츠 계약을 지킬 것

**A-4. SRS 액터 4종 배치** (현재 `L_GameFloor`에만 있음)

**A-5. 검증 → 사용자 육안 게이트**

### 맵 저작 콘텐츠 계약 (전부 코드 실측)

**플로우필드 — 어기면 적이 못 지나가거나 낀다** (`Source/FPSRoguelite/Public/Enemy/FPSRFlowFieldComputer.h:262-278`)

| 상수 | 값 | 의미 |
|---|---|---|
| `DefaultCellSize` | 200 | 셀 한 변 2m |
| `MaxGridDimPerAxis` | 256 | 축당 셀 상한 |
| `MaxTotalCells` | **40,000** | **하드 잠금** |
| `ObstacleProbeZ` / `ObstacleProbeHalfHeight` | 120 / 60 | 장애물 판정이 **셀바닥+60cm부터** 시작 |
| `DefaultClimbableStepHeight` | 45 | 이하는 밟고 넘음 |
| `MaxClimbableStepHeight` | 60 | |
| `AgentFootprintRadius` | 40 | |
| `WalkableNormalZ` | 0.573 | |

🔴 **높이 45/60 규칙**: **≤45 = 밟고 넘음 / ≥60 = 돌아감 / 45~60 = 적이 끼는 함정 구간**. 커버로 스웜을 쪼개려면 **≥60cm 필수** — 40cm 커버는 발목이라 기능 0이다.

🔴 **셀 예산**: 264m 맵 = 132×132 = 17,424셀(여유 있음). 자동화 테스트 실측으로 198×198 = 39,204는 통과, **202×202 = 40,804는 실패**. 맵을 키울 거면 이 상한을 먼저 계산할 것.

🔴 **바닥 앵커**: 첫 `PlayerStart` 아래로 트레이스해서 `GridOrigin.Z`를 잡는다. **스폰 지점 위에 충돌 있는 장식물을 놓으면 지면 시드가 전멸**한다(실사례: `Plaza_Emblem` 47cm BlockAll이 앵커를 오염시킴). 판정 = PIE 로그의 `no-floor` 비율.

🔴 **장애물 마스크는 다운트레이스 기반**이다 → **Synty 임포트 메시의 콜리전을 반드시 확인**할 것. 콜리전이 없으면 플로우필드가 벽을 못 본다.

**기타**
- **Nanite OFF.**
- 스폰: `AFPSREnemySpawnPoint` 배치 + `AFPSRSpawnRoom`/`AFPSRDoor`(룸 점진 개방) + 스폰존 비활성화 볼륨.
- 문 콜리전 이중 계약 유지 — `DoorMesh` = `ECC_FPSRPlayerPawn`, `FrameMesh` = `ECC_WorldStatic`(`FPSRDoor.cpp:28-43`).
- 블록아웃 툴 팔레트에 새 폴더를 추가하려면 `Config/DefaultEditor.ini`의 `PaletteFolders`(재빌드 불요). 현재 = `/Game/PolygonCyberCity` 1개. ⚠️ C++ 기본 시드는 `/Game/PolygonCyberCity/**Meshes**`로 ini 값과 **다르다**.

### U22a 완료 판정 (전부 충족해야 "완료")

1. 빌드 `Result: Succeeded` + 헤드리스 스모크 `ModuleLoads Result={Success}`
2. **에디터 블록아웃 검증기** `FFPSRBlockoutValidator::ValidateLevel` 통과
3. **앵커 데이터 커맨드릿** `UFPSRValidateAnchoredDataCommandlet` 통과
4. PIE에서 적이 맵 전역을 추격 — `no-floor` 비율 확인, 45~60 함정 구간 0
5. **A-0 대비 perf 회귀 없음**(CSV 5스탯 비교, 수치를 PROGRESS에 기록)
6. **사용자 육안 게이트** — 룩 승인
7. **패키지 쿡 1회 성공** (§5-4)

---

## §4 U22b — 무기 · 캐릭터 · 적 · UI · VFX

```
Game.md + PROGRESS.md + Docs/U22_AssetReplacement_Prompt.md 먼저 읽어. U22b를 진행한다.
선행: U22a 사용자 게이트 통과.
브랜치: main에서 phase/u22b-assets 분기.
```

### 4-1. 무기 7정 (Rifle 제외)

**⚠️ 소켓 명명 규약을 먼저 확정한다.** 현재 Rifle은 `SOCKET_Mount_*`, SMG는 `SOCKET_Forestock`/`Magazine`/`Scope`로 **규약이 갈려 있다**. 규약 확정 → 그 다음 7정 조립. 순서를 뒤집으면 7정을 두 번 만든다.

- `DA_Weapon_Rifle`을 템플릿으로 복제한다(§2-3에 9참조 전부 적어둠).
- 무기 DA의 `IsDataValid`가 참조 소켓 존재를 검증한다 — ⚠️ 단 **`IsDataValid`는 `WITH_EDITOR` 전용이라 빌드/스모크가 무효 DA를 못 잡는다.** 헤드리스 `is_object_valid`로 별도 검증할 것.
- **`_Preview` 통짜 메시 5종(Bazooka/ChargeLaser/LMG/Shotgun/Sniper)이 최우선**이다 — 사실상 플레이스홀더다.
- 크로스헤어(`DA_XH_*`)는 **유지**(U12/U17 산출물).

### 4-2. 캐릭터

- **댕글링 먼저 고친다**: `BP_FPSRPlayer` → `/Game/Fab/…/Blu_-_Animated`. `Content/Fab/`는 빈 폴더다. 실경로 = `Content/Assets/Anime_Girl_Character_-_Blu-6ccdbbe7/`.
- **Blu 리타겟 파이프라인을 먼저 확정**한다(`_SyntyPilot`의 `IK_Blu`·`RTG_Manny_to_Blu` 승격 여부). 확정 후 3P 애님 저작 — 순서를 뒤집으면 애님셋 전체를 다시 만든다.
- 1P = PWAS. ⚠️ `ABP_FPChar`은 `Demo/FPManny/S_Mannequin` 스켈레톤 위에 있다(**Blu 스켈레톤이 아니다**) — 1P 팔을 Blu로 바꾸려면 스켈레톤 호환 처리가 필요하다. PWAS는 거대 자기완결 시스템이라 **가볍게 보지 말 것**.
- 무기 백본이 Synty로 넘어가면 손저작 애님 16개(`ABP_Arms_*`·`AM_Arms_*`·`BS_Arms_*`·`ABP_Wep_*`·`AM_Wep_*`, 전부 LPAMG 스켈)가 **폐기 대상**이다.

### 4-3. 적 (⚠️ 순서 고정)

**적 메시 교체 → 그 다음 VAT 베이크.** 지금 BroBot 기준으로 베이크하면 교체 후 **재베이크**다.

- 교체 대상은 좁다: BP 2개 + MI 1개 + VAT 메시 1개.
- **소스 후보(리포 내)**: `PolygonCyberCity/CharactersUE4Mannequin` 18종 · `PolygonMilitary` 캐릭터 33종. **스켈레톤을 공유하면 VAT를 1회만 베이크해 재사용**할 수 있다 — 이게 "적 저코스트"의 핵심이다.
- 🔴 **VAT 파라미터 이름 불일치를 이때 정리**한다(§2-3). 코드 `AnimationIndex`/`Phase` vs 머티리얼 `StartFrame`/`EndFrame`/`TimeOffset`. **코드 변경이 필요한 지점**이다.
- ⚠️ **VAT 머티리얼 함정**: `bUseMaterialAttributes=True`인데 MA 핀이 비면 **WPO가 조용히 상수 0**이 되어 A포즈로 고착된다(컴파일 에러 0). 검증 = `get_statistics().num_vertex_shader_instructions`가 뛰는지 확인(148→572 실사례). **PIE 없이 확정 가능**하다.
- 애님 상한: 현재 Attack/Death 애님이 프로젝트에 없다. 새 소스에 있으면 상태를 늘릴 수 있다.
- 🔴 **메시 비율 정합**: 적 캡슐 halfheight **90**, 메시 `RelativeLocation(0,0,-90)`·`RelativeScale(0.8,0.8,1.8)`·NoCollision, `AttackRange=150`, `GroundSnapTolerance=60`, `EnemyStandOffset=95`. 새 메시가 비율이 다르면 **`UFPSRWeakpointComponent`(헤드샷 구) 위치가 어긋난다** — 약점 판정은 서버권위 히트라 시각과 어긋나면 곧 히트박스 버그다.

### 4-4. UI — **스타일 리스킨** (사용자 결정)

- 기존 WBP 구조와 **`BindWidget` 이름을 반드시 보존**한다(CardSelect 3 · PrimaryGameLayout 4 · MainMenu 2 · Result 1 · Settings 1). 이름이 바뀌면 C++이 위젯을 못 찾는다.
- 이미지·색·폰트만 Synty 것으로. ⚠️ Synty 위젯 접두사는 `WBP_`가 아니라 **`HUD_SciFiSoldier_*`** — 혼동 주의.
- 🔴 **컨테이너 위젯(자식 WBP를 품은 것)을 프로그래매틱으로 compile/save 하지 말 것.** 모달 행 + 크래시 + 저장 중단 시 **`.uasset` 손상** 전례가 있다(`WBP_GameHUD`). 리스킨이므로 **append-only, 재구조화 금지**. 복구 = `git checkout HEAD -- <asset>` + LFS smudge.
- ⚠️ **Synty 폰트(Orbitron/exo-2 계열)의 한글 글리프 보유 여부 미확인** — UI에 한국어가 들어가면 먼저 확인할 것.

### 4-5. VFX — ⚠️ U13과 경계를 지킬 것

**U13(VFX/오디오 배선)이 별도 라이브 유닛으로 존재한다.** 같은 파일(무기 DA·Niagara)을 양쪽이 건드리면 중복 저작 또는 상호 파괴다.

- U22b = **메시/머티리얼 룩 통일까지**. 이펙트·사운드 **배선**은 U13.
- 🔴 **LPAMG를 지우지 말 것** — 무기 8종 전부의 발사 사운드와 6종의 총구화염이 아직 LPAMG 참조다. U13 전에 지우면 **소리 없는 총**이 된다.
- `MuzzleFlash` 필드 타입 = `TSoftObjectPtr<UParticleSystem>`(Cascade). **Niagara로 가려면 C++ 필드 타입 변경 = DA 8종 재저작**이다. 이 결정은 U13과 함께 내릴 것.

### 4-6. 레거시 삭제 (사용자 결정 = 확실한 것만)

- ✅ **`Content/Assets/Environment/ModularSciFiStation/` 삭제** — 1.4G, 저작 폴더 13개 전수 스캔에서 **참조 0건** 확인됨.
- ❌ **Paragon(311M)·LPAMG(556M)는 보류** — Paragon은 보스가, LPAMG는 무기 8종 전부의 사운드가 쓰고 있다.
- ⚠️ **지워도 git/LFS 히스토리 용량은 안 줄어든다.** 클론 크기를 줄이려는 목적이면 삭제로는 달성되지 않는다.
- 삭제 **직전에 커밋/태그로 롤백 포인트**를 박을 것.

### U22b 완료 판정

1. 빌드 + 스모크 통과
2. 무기 DA 8종 헤드리스 유효성 검증(`is_object_valid`)
3. VAT 셰이더 명령어 수로 WPO 정상 확인
4. **4인 PIE**(§5-3 멀티 점검) + perf 회귀 없음
5. 사용자 육안 게이트
6. 패키지 쿡 성공

---

## §5 공통

### 5-1. 코드 변경이 필요한 지점 (나머지는 콘텐츠 저작만)

콘텐츠 유닛이라 "빌드 통과하니 괜찮다"는 착각이 쉽다. **아래는 C++/config 변경이고, 구현=Sonnet 위임 / 검증=Opus 직접이다.**

| # | 위치 | 내용 |
|---|---|---|
| a | `FPSREnemyAnimProfile.cpp` + `FPSRVATAnimParams.h` | VAT 파라미터명 불일치 정리 (U22b) |
| b | `FPSRWeaponDataAsset.h` `MuzzleFlash` | Cascade→Niagara 시 타입 변경 (U13과 공동 결정) |
| c | `Config/DefaultEditor.ini` `PaletteFolders` | 신규 팩 추가 시 |
| d | `Config/DefaultGame.ini` `RunMap` / `MapsToCook` | 맵 전환·사문 3건 정리 |
| e | `Config/DefaultGame.ini` `PlaceholderVisualSettings` | 실메시 전환 시(현재 Sphere/Cube/Cube) |
| f | `Config/DefaultGame.ini:73` `DirectoriesToAlwaysCook` | `/Engine/BasicShapes` — 실메시 넣으면 불요 |

**런타임 모듈에 에셋 경로 하드코딩은 0건**이다(에디터 모듈 2건만). 이 상태를 **유지**할 것 — 새 경로는 config/DataAsset로.

### 5-2. Roadmap §8 플레이스홀더 — 소관 분리

9행 중 **U22 소관 = ③적 큐브 메시 · ④XP 픽업 스피어 · ⑤FP팔/3P 바디 · ⑦환경/레벨 지오메트리**.
**U14 소관 = ①DrawDebug · ②`FPSR.SpawnEnemies` · ⑧Input Warning · ⑨CommonUI 로그.**
(⑥적 추격=단순 스티어링은 U7로 이미 해소.)

### 5-3. 멀티플레이 점검 — 에셋 교체도 MP를 건드린다

**솔로 육안으로는 안 드러난다. 4인 협동 + 리슨서버 기준으로 볼 것.**

1. **3P 메시 교체** → `SetOwnerNoSee`/`SetOnlyOwnerSee` 계약과 캡슐 34/88 대비 Blu 실측 크기·오프셋 정합. 어긋나면 **남의 화면에서만** 발이 뜨거나 머리가 잠긴다.
2. **적 메시 교체** → §4-3의 캡슐/오프셋/약점 구 정합. 약점은 **서버권위 히트**라 시각과 어긋나면 히트박스 버그.
3. **문 메시 교체** → 이중 콜리전 계약 유지.
4. **애님 상태는 복제 안 되는 코스메틱**이다(각자 로컬 계산). 교체 후에도 이 경계를 넘지 말 것 — 복제 페이로드를 늘리면 적 500 예산이 깨진다.
5. 데디서버 빌드에서 새 스켈레탈 메시의 로드/피직스 애셋 비용.

### 5-4. 검증 절차

```
빌드   : "D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat" FPSRogueliteEditor Win64 Development
         -Project="<빌드 클론>\FPSRoguelite.uproject" -WaitMutex     (에디터 닫고 / 클론 = FPSRoguelite2)
스모크 : UnrealEditor-Cmd.exe <uproject> -unattended -nullrhi -ExecCmds="Automation RunTests FPSRoguelite.Smoke.ModuleLoads"
Codex  : powershell -File Scripts\codex-review.ps1 -Base main
```

- XGE가 간헐적으로 `C1076`(힙 고갈)로 실패한다 → **`-NoXGE`로 재빌드**. 판정은 래퍼 exit 코드가 아니라 **로그의 `Result:`** 로 한다.
- 다른 클론 에디터의 **Live Coding이 이 트리 빌드까지 막는다**(엔진 공유). 그 에디터를 끌 것.
- **패키지 쿡을 단계마다 1회씩 돌린다.** 에셋 대량 교체·삭제는 쿡에서 처음 터지는데, U14까지 미루면 원인 커밋이 수백 개 뒤에 있다. §2-4의 사문 맵 3건 + 댕글링 1건을 먼저 정리해야 한다.

### 5-5. 롤백 — U22는 되돌리기 어려운 조작을 다수 포함한다

- **대량 이동/삭제 직전에 커밋 또는 태그로 롤백 포인트를 박는다.** `rename_directory`/`rename_asset` 대량 실행은 **부분실패 + 레지스트리 손상** 전례가 있다(복구 불가). 고아가 생기면 디스크에서 직접 rm 후 에디터 재시작.
- 손상 `.uasset` = `git checkout HEAD -- <경로>`(+ LFS smudge).
- 브랜치 abort 시 4GB 콘텐츠 체크아웃 되돌리기는 **에디터 종료가 선행**이다.

### 5-6. 커밋·브랜치 규약

- `phase/u22a-environment` / `phase/u22b-assets` 분기 → 검증 → `--no-ff` 머지 → 브랜치 삭제.
- 커밋 타입: 에셋/맵/DataAsset = `content` · `DefaultEngine.ini`/`Build.cs` = `chore` · `DefaultGame.ini` 밸런스 = `content`.
- 중간 커밋 단위 = **맵 1개 / 무기 1정 / 적 1종**. 통짜 커밋 금지(되돌리기 불가).
- ⚠️ **이 프로젝트는 아트 커밋 정책을 이미 두 번 깨뜨렸다** — 파일럿 산출물이 격리 폴더 밖 main에 커밋됐고(`b25db2ab`), 팩 6,823파일이 게이트 통과 **8일 전에** 커밋됐다(`ff191fec` 07-10 vs `5aa69c5d` 07-18). 세 번째를 내지 말 것.
- LFS는 `*.uasset`/`*.umap`만 잡는다. 원본 아트(`.fbx`/`.glb`/`.rar`/`.blend`)는 gitignore라 자동 제외된다.

### 5-7. 신규 팩 배치 경로 규약

현재 리포는 최상위(`PolygonCyberCity`/`PolygonMilitary`/`PolygonParticleFX`)와 `Content/Assets/` 하위와 `Content/Synty/`가 **뒤섞여 있다**. Fab "Add to Project"는 **항상 Content 루트**에 떨어지는데, 사후 이동은 §5-5의 최고위험 조작이다.

→ **임포트 전에 최종 위치를 정하고, 임포트 직후 한 번에 옮긴다.** 이게 유일한 안전 경로다.

---

## §6 착수 시 결정할 것 (세션이 임의로 정하지 말 것)

§1-1에서 4건은 확정됐다. 남은 것:

1. **적 스웜 소스** — 리포 내 후보(CyberCity 마네킹 18종 / Military 33종, 스켈 공유 시 VAT 1회 베이크 재사용) vs 신규 팩. **U20 VAT 베이크의 선행이라 병목이다.**
2. **`L_GameFloor`(파일럿 264m 아레나)를 맵1 베이스로 승격할지 폐기할지**, 그리고 `RunMap`을 `L_Sandbox`에서 언제 전환할지(전환하면 `L_Sandbox` 기준 회귀 테스트 기반이 사라진다).
3. **맵 크기** — 264m 단일맵 유지 vs U 3×3 다중맵(슬롯당 코드 상한 132m). 셀 상한 40,000이 하드 잠금이라 되돌리기 어렵다.
4. **맵 이름 규약** — `MapsToCook`의 `L_RunPersistent`/`L_MapA`/`L_MapB`를 실제 규약으로 살릴지, 사문으로 보고 지울지.
5. **보스(Paragon Prime_Helix)를 셀/툰 통일 범위에 넣을지** — 과제 서술의 "캐릭터"에 보스가 명시돼 있지 않다.
6. **`MuzzleFlash` Cascade 유지 vs Niagara 전환**(후자는 C++ 타입 변경 + DA 8종 재저작). U13과 공동 결정.
7. **`Content/_SyntyPilot/`의 리타겟 자산(`IK_Blu`·`IK_Manny`·`RTG_Manny_to_Blu`)을 정식 경로로 승격할지** — 지금은 gitignore라 커밋되지 않는다.
8. **자체 절차적 크로스헤어(U12/U17 완료분)와 Synty 레티클의 중복 정리 방향** — 현재 무기 8종 전부가 자체 크로스헤어를 쓰는데 Rifle만 Synty 레티클도 참조한다.

---

## §7 완료 시

파일럿/채택 목록을 PM에 보고 → PM이 `Docs/SSOT/Roadmap.md` §8에 확정 스택 기록 + `TaskPrompts_Master.md` §B 라이브 표 갱신 + `PROGRESS.md` 갱신.

**perf 수치를 반드시 문서에 남길 것** — U21이 정성 판정만 남겨서 이번에 기준선이 없었다. 같은 일을 반복하지 말 것.
