# Resume Prompt — U 연속필드 멀티맵 ④ **U-레이아웃 콘텐츠 저작** (VibeUE 세션)

> 새 Claude 세션(VibeUE MCP 연결됨)에 이 파일 내용을 붙여 시작하라. 목표 = **U(고정 3×3 단일 그리드) 연속필드를 in-world로 활성화하는 레벨 콘텐츠 저작**. 코드는 이미 커밋됨 — 콘텐츠가 없어 U가 아직 PIE 미검증.

---

## 0. 먼저 읽기
- `Game.md`(SSOT 허브) + `PROGRESS.md` 최상단 핸드오프(2026-07-08) + `Docs/Review/20260707-plan-continuous-field-arch.md`(완성 플랜).
- 프로젝트 = FPSRoguelite2, 브랜치 `phase/p8-multimap-tier0`. 엔진 UE 5.7 `D:\UnrealEngine\UE_5.7`.

## 1. 지금 상태 (코드는 됨, 콘텐츠가 없음)
커밋됨: `f99666a`(P-0~P-C 헤드리스 + 서브시스템 통합 배선) + `413bf85`(③ P-C 전투게이트) + `49e6ee7`(PROGRESS). 헤드리스 스모크 8/8, Codex R1~R17 CLEAN.
- **U 서브시스템 배선 완료**: 레벨에 `bUnifiedExtent` 볼륨이 있으면 `UFPSRFlowFieldSubsystem`이 단일 그리드를 프리사이즈하고 각 슬롯을 bake, 스웜 flow가 그 단일 필드를 샘플. **볼륨이 없으면 완전 dormant = 기존 per-map registry(무회귀)**.
- **③ 전투게이트 활성**: unified 활성 시 `CanAffectTarget`이 origin↔target open-grid 연결성으로 데미지/AOE 게이트(닫힌 벽 너머 = 0). 콘텐츠만 있으면 **지금 PIE로 검증 가능**.
- **미착수 = ② FPSRDoor 배선** (문 파괴→door셀 스탬프). 그래서 이번 콘텐츠로 **크로스도어 이동은 아직 검증 안 됨**(문 부숴도 그리드 안 열림). 문은 배치해두되 크로스 증명은 ② 후속.

## 2. 저작할 것 (U-레이아웃 레벨)
새 레벨(예 `L_U_Whitebox`) 또는 기존 정비. 기존 `L_MapA/L_MapB/L_RunPersistent`는 **다른 레이아웃**(per-map 인접, Tier-0) — U는 **고정 3×3 단일 extent**라 재사용 말고 새로 배치 권장.
1. **`AFPSRFlowFieldBoundsVolume` 1개, `bUnifiedExtent=true`** — 박스가 **3×3 전체**를 덮음. 이 박스 AABB의 Min corner = 그리드 원점.
2. **슬롯 볼륨 N개** (`AFPSRFlowFieldBoundsVolume`, `bUnifiedExtent=false`, 각기 다른 `MapId`) — 3×3 중 저작할 슬롯마다 1개(최소 2개 = 크로스 증명용 인접쌍). 중앙 스타트 + 인접 1~2개로 시작해도 됨.
3. **각 슬롯 whitebox 바닥** = walkable `WorldStatic` 지오(평면). 없으면 그 슬롯 bake 거부(no-floor).
4. **슬롯 사이 벽** = `WorldStatic` — bake 시 blocked 셀(seam 벽)이 됨.
5. **문** = `AFPSRDoor`를 벽 gap에 배치(파괴가능). ②가 이걸 door셀 스탬프에 연결할 예정. 이번엔 배치만.
6. **스폰포인트** = 각 슬롯에 `AFPSREnemySpawnPoint`(그 슬롯 MapId), **Z = 바닥 + 100**([[enemy-spawnpoint-z-floor-offset]]).
7. **PlayerStart** 중앙 슬롯.

## 3. ⚠️ 계약 (어기면 서브시스템이 fail-closed 거부 — 로그 Warning)
`CommitSubregion`/`BuildUnifiedField`가 검증. 어기면 그 슬롯은 sealed(적 라우팅 불가) 또는 unified 미빌드(registry 폴백):
1. **동일 CellSize** — 통합+모든 슬롯 볼륨의 `CellSizeOverride`를 **전부 0**(=200cm) 또는 **전부 같은 값**. 불일치 → 슬롯 거부(`CellSize mismatch`).
2. **동일 ClimbableStepHeight** — 전부 0(=45) 또는 전부 같은 값. 불일치 → 거부(`step ... != unified`).
3. **셀 정렬** — 각 슬롯 박스 `Min.X`/`Min.Y` = 통합 `Min.X`/`Min.Y` + **정수×CellSize** (1cm 이내). CellSize=200이면 슬롯 Min을 통합 Min에서 **200cm 배수** 위치에. `CellOffset = round((SlotMin−Origin)/CellSize)`로 계산되고, 어긋나면 `author bounds drift` 거부.
4. **슬롯이 통합 extent 안에 들어감** — `CellOffset + 슬롯셀 ≤ 통합 dims`.
5. **예산** — 통합 extent ≤ **40000셀·≤256/축**. 200cm 셀 = **≤400m/축 총**(3슬롯), 즉 **≤133m/슬롯**. D1 확정 100~132m/슬롯 → OK. 초과 시 unified 미빌드 + Error 로그(축소 또는 CellSizeOverride↑).
6. **통합 볼륨 = 레벨당 정확히 1개**(bUnifiedExtent=true). 여러 개면 첫 번째만.
7. **ProbeApexAboveOriginOverride**(볼륨) = 최상단 walkable 바닥 위로(2층/플랫폼 있으면). 평지면 0(=기본).

**정렬 팁**: 통합 원점을 (0,0)에 두고 슬롯을 200cm 그리드에 스냅. 예) 슬롯 한 변 = 60m(=6000cm=30셀), 통합 3×3 = 180m(=90셀/축, 8100셀). 슬롯 Min = (0,0)/(6000,0)/(12000,0)/(0,6000)/... 전부 200 배수 ✓.

## 4. 검증 (PIE + 서버 로그)
1. **그리드 빌드**: `[FlowField] U unified grid NxN cell=200 ... built` + `U: M slot(s) baked into the unified grid`. M = 저작 슬롯 수. 슬롯 거부 시 Warning(`misaligned / no floor / step mismatch`) → 그 슬롯 재배치.
2. **in-slot 적 추격**: 적이 같은 슬롯 플레이어를 단일 필드로 추격(정상). 다른 슬롯 플레이어는 **벽에 막혀 안 쫓음**(문 닫힘 = 슬롯 격리 = 정상, ② 전까지).
3. **③ 전투게이트(핵심, 지금 검증 가능)**: **닫힌 벽/문 너머로 폭발(로켓)·사격 → 데미지 0, 넉백 0**. 같은 슬롯/열린 통로 = 정상 타격. (③ 커밋됨 `413bf85`.)
4. **⚠️ 크로스도어 이동은 이번 스코프 아님** — 문 부숴도 그리드 안 열림(② 미착수). 슬롯 격리 상태까지만.

## 5. 툴 / 환경 / 함정
- **VibeUE MCP** `execute_python` 저작. 연결 확인 먼저(툴 부재 시 에디터 열고 재시작).
- **FGameplayTag** = `unreal.GameplayTag().import_text("Map.Center")` — `GameplayTagsManager.get()`은 게임스레드 행([[vibeue-gameplaytag-import-text]]). 3×3 MapId 태그를 `Config/DefaultGameplayTags.ini`에 등록(현재 `Map.A`/`Map.B`만).
- **MCP로 레벨/BP 편집 후 PIE 전 에디터 재시작** — Undo 버퍼 REINST World Leak 크래시([[vibeue-buildgraph-pie-worldleak]]).
- **코드 빌드 불필요**(콘텐츠만). 만약 코드 빌드해야 하면 다른 클론 `E:\Git_Project\FPSRoguelite` 에디터 Live Coding 해제 필요([[build-livecoding-cross-clone-block]]) — 이번 세션 그걸로 빌드 hang 겪음.
- **커밋**: 콘텐츠(맵/볼륨)는 사용자 자산이라 저작 후 커밋 여부 확인. 소스 변경 없음.

## 6. 코드 참조 (계약 근거, 읽기만)
- `Source/FPSRoguelite/Public/Enemy/FPSRFlowFieldBoundsVolume.h` — `bUnifiedExtent`, `MapId`, `CellSizeOverride`, `ClimbableStepHeightOverride`, `ProbeApexAboveOriginOverride`.
- `Source/FPSRoguelite/Private/Enemy/FPSRFlowFieldSubsystem.cpp` — `BuildUnifiedField`(:152, extent→dims→예산게이트→BuildEmptyGrid+슬롯 bake), `BakeSlotIntoUnified`(:201, CellOffset 계산).
- `Source/FPSRoguelite/Private/Enemy/FPSRFlowFieldComputer.cpp` — `CommitSubregion`(정렬/CellSize/step/floor fail-closed 검증), `AreWorldLocationsConnected`(③ 전투게이트 코어).

## 7. 이후 순서 (이 콘텐츠 완료 후)
④ 콘텐츠 + PIE(그리드빌드 + ③ 벽너머 무피해) 검증 → **② FPSRDoor 배선**(문 파괴→door셀 매핑→`StampDoorEdgesOpen`+`StampCellBlocked`+즉시 recompute) → PIE 크로스도어 증명 → **P-G**(per-map registry·전환추적자 제거) → P-D~P-H(FrontId allocator/drain/generation/NetCull). 플랜 = `Docs/Review/20260707-plan-continuous-field-arch.md` §2-4.
