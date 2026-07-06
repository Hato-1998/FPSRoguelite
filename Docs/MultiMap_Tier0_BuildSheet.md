# 다중맵 Tier 0 — 화이트박스 빌드 시트 (구체 좌표 · 사용자 수동 저작)

> [`MultiMap_Tier0_ContentChecklist.md`](MultiMap_Tier0_ContentChecklist.md)의 **실행용 상세판**. 코드 계약을 소스에서 재검증한 뒤 좌표·프로퍼티까지 확정했다. 값은 전부 cm. **하드 불변식(§5)만 지키면 좌표는 조정 가능.**
>
> **총 3개 맵**: `L_RunPersistent`(persistent 런레벨) + `L_MapA`(스트리밍, 시작 로드) + `L_MapB`(스트리밍, 문 파괴 시 로드).
>
> **⚠️ AS-BUILT 갱신(2026-07-06)**: 실제 저작은 **인접 배치**로 변경됨(설계 의도 "문 부수기→심리스 다음 맵"에 부합). 아래 §2·§3의 500m 복도 좌표는 **폐기**. 최종: **Map.A `X[-1500,1500]` — 갭 600cm(문+브리지) — Map.B `X[2100,5100]`**(L_MapB 중심 `+3600`). 복도/난간 없음. 문=`(1800,0,200)` leaf scale`(6,6,4)`로 갭 채움 · 브리지 바닥=`(1800,0,-50)` scale`(6,10,1)` `X[1500,2100]` · 경계벽=`(2000,0,200)`. 트레이드오프: 근접이라 NetCull(200m)이 옆 맵 미컬링(perf 헤드룸 미사용, 정확성 무영향). 정확 좌표표는 커밋 시 갱신 예정.
>
> **⚠️ 스폰포인트 Z 갓차(2026-07-06 수정)**: 스폰포인트는 **바닥 위 +100cm**에 둘 것(§2 표의 Z=0은 폐기). 코드가 디자이너 점 Z를 재투영 없이 그대로 쓰는데(`FPSREnemySpawnSubsystem.cpp` "keep exact Z"), 적 캡슐 half-height=90이라 Z=바닥면이면 발이 바닥 아래로 묻혀 **바닥 뚫고 낙하**. as-built = 8개 전부 Z=100. (체크리스트 §2-2에도 반영.)

## 0. 사전 (이미 완료 / 확인)
- ✅ **Map.A / Map.B GameplayTag 등록됨**(내가 `Config/DefaultGameplayTags.ini`에 추가) → 에디터 GameplayTag 드롭다운에 뜬다.
- ✅ **MapsToCook 등록됨**(내가 `Config/DefaultGame.ini`에 3맵 추가).
- ⚠️ **에디터가 응답 없으면 먼저 재시작**(직전 MCP 태그 호출이 게임스레드를 물었을 수 있음). 재시작하면 위 태그도 로드된다.
- 공통 규칙: **모든 바닥/벽 = StaticMesh Cube, Collision Preset = 기본(WorldStatic·BlockAll)**. 플로우필드 bake가 WorldStatic 다운트레이스로 바닥을 잡는다. Z는 전부 공통(수직 스택 금지).

---

## 1. L_MapA (스트리밍 서브레벨 — 시작 로드+가시) · 원점에 저작
| 액터 | Location (X,Y,Z) | Scale / 설정 |
|---|---|---|
| 바닥 Cube | (0, 0, −50) | Scale (30, 30, 1) = 3000×3000, 윗면 Z=0 |
| 벽 N (+Y) | (0, +1500, 200) | Scale (30, 1, 4) |
| 벽 S (−Y) | (0, −1500, 200) | Scale (30, 1, 4) |
| 벽 W (−X) | (−1500, 0, 200) | Scale (1, 30, 4) |
| 벽 E 상 (+X, 문 위쪽) | (+1500, +900, 200) | Scale (1, 12, 4) |
| 벽 E 하 (+X, 문 아래쪽) | (+1500, −900, 200) | Scale (1, 12, 4) → **Y∈[−300,+300] 600cm 문틈** |
| **AFPSRFlowFieldBoundsVolume** | (0, 0, 150) | BoundsBox Extent (1500, 1500, 300) · **MapId = `Map.A`** |
| AFPSREnemySpawnPoint ×4 | (±1000, ±1000, 0) | **MapId = `Map.A`** · ZoneTag=비움 · MinPlayerDistance=800 · bEnabled=✓ |

## 2. L_MapB (스트리밍 서브레벨 — 시작 로드 OFF) · **X를 +50000 이동해 저작**
> Map.A와 동일 구조, 모든 X에 +50000. 문틈은 **−X 방향(복도 쪽)**을 향한다.

| 액터 | Location (X,Y,Z) | Scale / 설정 |
|---|---|---|
| 바닥 Cube | (50000, 0, −50) | Scale (30, 30, 1) |
| 벽 N (+Y) | (50000, +1500, 200) | Scale (30, 1, 4) |
| 벽 S (−Y) | (50000, −1500, 200) | Scale (30, 1, 4) |
| 벽 E (+X) | (+51500, 0, 200) | Scale (1, 30, 4) |
| 벽 W 상 (−X, 문 위쪽) | (48500, +900, 200) | Scale (1, 12, 4) |
| 벽 W 하 (−X, 문 아래쪽) | (48500, −900, 200) | Scale (1, 12, 4) → 600cm 문틈(복도 쪽) |
| **AFPSRFlowFieldBoundsVolume** | (50000, 0, 150) | Extent (1500, 1500, 300) · **MapId = `Map.B`** |
| AFPSREnemySpawnPoint ×4 | (50000±1000, ±1000, 0) | **MapId = `Map.B`** · ZoneTag=비움 · MinPlayerDistance=800 |

> ⚠️ **Map.B의 BoundsVolume과 SpawnPoint는 반드시 L_MapB 서브레벨 안에** 둔다(스트림-in 시 bake가 여기서 찾음. 없으면 "봉인 유지"+에러 로그).

## 3. L_RunPersistent (persistent 런레벨) — 복도 + 게임플레이 액터
| 액터 | Location (X,Y,Z) | 설정 |
|---|---|---|
| 복도 바닥 Cube | (25000, 0, −50) | Scale (470, 8, 1) = X∈[1500,48500] 연결, 폭 800 |
| (선택) 복도 난간 ×2 | (25000, ±400, 100) | Scale (470, 0.4, 2) — 낙하 방지 |
| **PlayerStart** | (0, 0, 100) | Map.A 방 안(= Map.A 볼륨 안). 여러 개 두면 코옵 스폰 분산 |
| **AFPSRDoor** | (1500, 0, 200) | ▼ 아래 프로퍼티 |
| **AFPSRBoundaryBlocker** | (1800, 0, 200) | BlockBox Extent (100, 400, 300) · **TargetMapId = `Map.B`** |

**AFPSRDoor 프로퍼티 (인스턴스 Details에서):**
- **DoorMesh 컴포넌트 → Static Mesh = `Cube`** (기본 클래스엔 메시 없음 — 안 넣으면 안 보이고 못 쏨). 컴포넌트 Scale ≈ (0.4, 6, 4) → 문틈 600w×400h 채움. **Collision 프리셋은 건드리지 말 것**(C++가 ECC_FPSRPlayerPawn로 세팅 — 이걸로 모든 무기가 데미지+플레이어 차단).
- **TargetMapId = `Map.B`** (EditAnywhere, 인스턴스 세팅 가능)
- **TargetLevelName = `L_MapB`** (FName, 서브레벨 short name)
- Durability = 150(기본, EditDefaultsOnly라 인스턴스 변경 불가 — Tier 0 기능검증엔 충분. "혼자 부수기 어렵게"는 후속 BP 자식에서 상향).

**World Settings:**
- **GameMode Override = None(비움)** → GlobalDefaultGameMode(`BP_FPSRGameMode`) 자동 적용 → BeginPlay가 EnemyClass/RunSchedule/CardPool 세팅 + `StartRun()` → 적 스폰 시작. (BP_FPSRGameMode에 RunSchedule/EnemyClass가 세팅돼 있어야 함 — L_Sandbox가 도니 이미 세팅됨.)
- KillZ가 바닥(Z=0)보다 충분히 아래인지 확인(기본이면 OK).

## 4. 스트리밍 등록 (L_RunPersistent에서 · Window→Levels)
1. `L_RunPersistent` 열고 **Levels 패널**에서 **Add Existing → L_MapA**, **Add Existing → L_MapB**.
2. 각 서브레벨 **Streaming Method = Blueprint**(ULevelStreamingDynamic), **Transform offset = (0,0,0)**(콘텐츠를 이미 최종 위치에 저작했으므로).
3. 초기 상태:
   - **L_MapA**: Initially Loaded ✓ · Initially Visible ✓ (시작부터 존재 → world-begin에 Map.A 필드 bake).
   - **L_MapB**: Initially Loaded ✗ · Initially Visible ✗ (문 파괴 시 `LoadStreamLevel("L_MapB")`이 이름으로 로드).
   > ⚠️ 서브레벨이 persistent의 **StreamingLevels 배열에 등록돼 있어야** LoadStreamLevel이 이름으로 찾는다(Levels 패널에 추가하면 등록됨).

---

## 5. ⛔ 하드 불변식 (이거 틀리면 동작 안 함)
1. **두 BoundsVolume AABB가 XY로 겹치면 안 됨** — 점유가 이 박스로 판정된다. Map.A X∈[−1500,1500], Map.B X∈[48500,51500] → 겹침 0. (분리 거리는 조정 가능하나 **중심 간 ≥ ~250m**: NetCull 200m 초과. 500m 권장.)
2. **공통 Z** — 모든 바닥 Z=0. 수직 스택 금지(WorldKillZ/바닥트레이스/DistSquaredXY가 단일 바닥 가정).
3. **바닥/벽 = WorldStatic** (Cube 기본 프리셋). 플로우필드가 WorldStatic 다운트레이스로 바닥 잡음.
4. **Map.B의 BoundsVolume+SpawnPoint = L_MapB 안**. **문/blocker/PlayerStart/복도 = persistent 안**. **각 방 지오/볼륨/스폰 = 해당 서브레벨 안**.
5. **PlayerStart는 Map.A 볼륨 안**(X∈[−1500,1500]) — 시작 시 플레이어가 Map.A 점유가 되게.
6. MapId/TargetMapId는 **드롭다운에서 Map.A/Map.B 선택**(오타 금지). Door.TargetLevelName은 **정확히 `L_MapB`**.

## 6. 저장 후 넘겨주기
- **Save All**. 3맵(`Content/Maps/L_RunPersistent.umap`, `L_MapA.umap`, `L_MapB.umap`) 생성 확인.
- 넘겨주면 내가: (1) 프로그램 검증(볼륨 비겹침·PlayerStart⊂Map.A·door/blocker 프로퍼티·스트리밍 플래그) (2) PIE 스텝·감시 로그 안내 (3) 통과+승인 시 콘텐츠 커밋 + `--no-ff` main 머지.

## 7. PIE 검증 요약 (§3 — 코옵 2인, `net.AllowPIESeamlessTravel=1`)
문파괴→스트림(로그 `[MapStream] map 'Map.B' READY`) · 새맵 적 0~3s · 추격 연속 · 빈맵 드레인 · 전역~200(`FPSR.FlowField.Debug 1` per-map) · late-join. 상세는 체크리스트 §3.
