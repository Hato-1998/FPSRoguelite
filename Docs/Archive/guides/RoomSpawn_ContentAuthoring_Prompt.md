# 룸 스폰 시스템 — 콘텐츠 저작 핸드오프 (새 세션 프롬프트)

> 생성 2026-06-25. 룸 스폰 **C++ 구현+검증 완료**(`balance/pass2`, 빌드/스모크/`bCountsAsKill` 헤드리스 PASS). 이제 **에디터 콘텐츠 저작**(BP·배치)만 남음. 이 문서 = 새 세션 첫 메시지로 붙여넣는 복붙 프롬프트 + 저작 가이드.

---

## ▶ 복붙 프롬프트 (새 세션 첫 메시지)

```
룸 기반 점진 개방 스폰 — 콘텐츠 저작 (C++는 balance/pass2에 구현+검증 완료, 에디터 작업만 남음).

[먼저 읽기] Game.md(SSOT 허브) + PROGRESS.md(맨 위 "밸런스 2차 패스 / 룸 스폰" 섹션) + Docs/SSOT/Enemy.md(§2-6 룸 스폰·파괴 장벽) + Docs/RoomSpawnSystem_Handoff.md(설계 상세) + 이 문서 Docs/RoomSpawn_ContentAuthoring_Prompt.md 전체.

[MCP] VibeUE 연결 확인: 에디터 열림 + VibeUE 플러그인 활성, 127.0.0.1:8088, 등록명 VibeUE-Claude. (mcp__unreal_editor__* 는 죽은 Aura 잔상 — 쓰지 말 것.) 연결 안 되면 사용자에게 에디터/플러그인 확인 요청. 메모리 [[unreal-editor-mcp-vibeue]] [[vibeue-mcp-capabilities]] [[vat-bake-inherited-component-wiring]] 참고.

[전제] balance/pass2에 룸 스폰 C++(AFPSRDoor·AFPSRSpawnRoom·UFPSREnemyHealthComponent.bCountsAsKill·서브시스템 누적존·SpawnZone.Room.* 태그)가 이미 빌드돼 에디터에 클래스가 떠 있음. 새 빌드 불필요(콘텐츠만).

[목표] 맵을 방(룸)으로 구성: 시작방 시작 → 벽의 문을 사격해 부수면 통로 개방 → 그 방 진입 시 그 방 스폰포인트 활성(누적). 아래 저작 작업 수행.

[저작 작업 — 가이드는 이 문서 §저작 체크리스트]
1. BP_Door (부모 AFPSRDoor): DoorMesh=도어 상하단 메시(SM_Doors), FrameMesh=문틀 메시(있으면), Durability 설정. bCountsAsKill은 C++가 이미 false(건드리지 말 것).
2. BP_SpawnRoom (부모 AFPSRSpawnRoom): 박스(EntryTrigger)를 방 전체로, RoomTag 고유(SpawnZone.Room.*), 시작방만 bActiveAtStart=true.
3. BP_EnemySpawnPoint (부모 AFPSREnemySpawnPoint, 이미 있으면 재사용): 각 방 박스 안에 배치 — 방 박스가 BeginPlay에 자동 태깅하므로 포인트엔 손 안 대도 됨.
4. 도어웨이마다 BP_Door 배치, 방마다 BP_SpawnRoom 배치(박스가 방 덮게), L_Sandbox에 룸 레이아웃 구성.
5. SpawnZone.Room.* 태그: Start/NE/NW/SE/SW 이미 선언됨. 레이아웃에 맞게 추가/개명(DefaultGameplayTags.ini 또는 에디터 Project Settings > GameplayTags).

[저작 후 검증] (저=Opus 직접)
- 헤드리스 is_object_valid: BP_Door·BP_SpawnRoom 무효 없음. (IsDataValid는 WITH_EDITOR-only라 빌드/스모크가 못 잡음 → is_object_valid로 확인, 메모리 [[vibeue-render-target-gpu-hazard]].)
- BP_Door CDO bCountsAsKill=false 재확인(BP가 실수로 덮지 않았는지).
- 콘텐츠 커밋: balance/pass2에 BP/맵 커밋(메모리 [[phase-end-commit-user-content]] — git status로 Content/* untracked 확인 후 동반 커밋 질문). LFS+BuiltData gitignore 주의.

[검증=사용자 PIE] 문 사격 파괴(전 무기) → 통로 개방 → 방 진입 시 스폰 시작 → 이전 방도 계속 스폰(누적) → 문틀 사격 무반응 → 대시로 닫힌 문 통과 불가.

[워크플로] 플랜 우선(HotL). 구현(BP 저작)=직접 또는 Haiku 위임, 검증=Opus 직접. 콘텐츠 커밋은 사용자 승인 후.
```

---

## 저작 체크리스트 (상세 — 새 세션이 참조)

### 신규 C++ 클래스 (이미 빌드됨 — 에디터에서 부모로 선택 가능)

| 클래스 | 경로 | 디자이너가 세팅할 것 |
|---|---|---|
| `AFPSRDoor` | `/Script/FPSRoguelite.FPSRDoor` | `DoorMesh`(상하단 메시)·`FrameMesh`(문틀 메시, 옵션)·`Durability`(float, 기본 150) |
| `AFPSRSpawnRoom` | `/Script/FPSRoguelite.FPSRSpawnRoom` | `EntryTrigger`(Box) extent·`RoomTag`(FGameplayTag)·`bActiveAtStart`(bool) |
| `AFPSREnemySpawnPoint` | `/Script/FPSRoguelite.FPSREnemySpawnPoint` | (방 안에 배치만 — `ZoneTag` 자동) `MinPlayerDistance`·`bEnabled` 옵션 |

### 1. BP_Door (부모 `AFPSRDoor`)
- `Content/Actors/` (또는 기존 액터 폴더)에 부모=AFPSRDoor로 BP 생성.
- **`DoorMesh`**(상속 네이티브 컴포넌트) Static Mesh = **도어 상하단 메시**. 이게 파괴 대상(`ECC_FPSRPlayerPawn`=전 무기 자동 피격). 상/하단을 따로 보여주려면 DoorMesh 자식으로 메시 컴포넌트 붙임(같이 파괴, `OnDoorBroken`에서 각각 연출 가능).
- **`FrameMesh`**(상속) Static Mesh = **문틀 메시**(있으면). 무반응 벽(WorldStatic, 무기쿼리 비대상). 콜리전은 테두리 트림만(통로 가운데 안 막게). 문틀 없으면 비워둠.
- **`Durability`** = 내구도(라이플 N발). 기본 150, PIE 보고 튜닝.
- **`bCountsAsKill`은 C++가 이미 false** — 디테일에서 보이지만 건드리지 말 것(false 유지).
- ⚠️ 상속 네이티브 컴포넌트 메시 지정은 VibeUE에서 **CDO `set_editor_property`** 사용(인스턴스 set_component_property로 영속 안 될 수 있음 — 메모리 [[vat-bake-inherited-component-wiring]]).
- (옵션) `OnDoorBroken`(BlueprintImplementableEvent)에 파괴 연출(애니/Chaos/파티클/사운드) 구현. 미구현이어도 게임플레이는 정상(콜리전 off+메시 숨김은 C++).

### 2. BP_SpawnRoom (부모 `AFPSRSpawnRoom`)
- 부모=AFPSRSpawnRoom로 BP 생성(또는 AFPSRSpawnRoom 직접 배치도 가능).
- **`EntryTrigger`(Box)** extent를 **방 전체**를 덮게 — 이 박스 안에 든 스폰포인트가 자동 태깅되고, 플레이어가 박스 진입 시 방 활성. 박스 사이징은 **배치 인스턴스에서 스폰포인트보다 약간 크게**.
- **`RoomTag`** = 방마다 고유 `SpawnZone.Room.*`(예 Start·NE·NW…). 중복 금지.
- **`bActiveAtStart`** = **시작 방만 true**(나머지 false). 시작방은 런 시작부터 스폰.
- 박스는 플레이어만 트리거(적 무시)·아무도 안 막음(오버랩) — C++ 기본 세팅.

### 3. 스폰포인트 배치
- 각 방 박스 안에 `BP_EnemySpawnPoint`(또는 AFPSREnemySpawnPoint) 배치. **방 박스가 BeginPlay에 `ZoneTag=RoomTag` 자동 부여** → 포인트엔 손 안 대도 됨.
- 수동으로 `ZoneTag`를 미리 세팅하면 그게 우선(오버라이드). `bEnabled`/`MinPlayerDistance`만 필요 시 조정. (가중치 필드는 제거됨 — 적격 포인트 균등 추첨.)

### 4. 레이아웃 (L_Sandbox)
- 도어웨이(방↔방 통로)마다 BP_Door 배치(통로를 메시가 막게).
- 방마다 BP_SpawnRoom 배치(박스가 방 덮게) + 스폰포인트 몇 개.
- 시작방 1개(bActiveAtStart) → 인접방으로 문이 연결되게.

### 5. 태그
- `SpawnZone.Room.Start`·`NE`·`NW`·`SE`·`SW` 이미 선언됨(`Config/DefaultGameplayTags.ini`). 방이 더 많으면 같은 패턴으로 추가(에디터 Project Settings > Project > GameplayTags 또는 ini 직접).

---

## 동작 원리 요약 (디버깅용)
- **문 사격 파괴**: DoorMesh 오브젝트타입 `ECC_FPSRPlayerPawn` → 모든 무기 오브젝트쿼리(히트스캔/레이저/근접/투사체/폭발)가 수집 → `UFPSREnemyHealthComponent`로 데미지 → HP 0 시 `OnDeath`→`HandleBroken`(콜리전 off + DoorMesh 숨김 + `bBroken` 복제). `bCountsAsKill=false`라 킬크레딧/흡혈/on-kill 미발동.
- **문틀 무반응**: FrameMesh `WorldStatic` → 무기 오브젝트쿼리 비대상 → 데미지 0, 총알만 벽처럼 막힘.
- **방 진입=활성**: 플레이어가 EntryTrigger 진입 → 서버 `ActivateSpawnZone(RoomTag)` → `ActiveSpawnZones`에 누적 → 그 방 스폰포인트 적격. 누적이라 이전 방 계속 스폰.
- **리셋**: OnWorldBeginPlay + RunDirector::StartRun에서 `ResetSpawnZones()` → 시작방(bActiveAtStart)만 재활성(재런 안전).
- **적 총량 불변**: 레벨기반 밀도(`DA_RunSchedule.AliveCountByLevel`). 방 개방=스폰 위치만 추가, 총량 그대로.
