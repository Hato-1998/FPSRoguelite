# 룸 기반 점진 개방 스폰 시스템 — 핸드오프 (다음 세션)

> 생성 2026-06-25. 밸런스 2차 패스에서 **설계 확정**(사용자 4결정 + 문 확장성 확인). 기반(points-only 스폰 + 레벨기반 밀도)은 `balance/pass2`에 커밋 완료(`93e8a28` 카드·`79e95b3` 스폰, **미머지**). 이 문서 = 다음 세션 착수용 **복붙 프롬프트 + 설계 상세**.

---

## ▶ 복붙 프롬프트 (새 세션 첫 메시지로 그대로 붙여넣기)

```
룸 기반 점진 개방 스폰 시스템 구현 — 브랜치 balance/pass2 (밸런스 2차 기반 2커밋 위, 미머지).

[먼저 읽기] Game.md(SSOT 허브) + PROGRESS.md(맨 위 "밸런스 2차 패스" 섹션) + Docs/SSOT/Enemy.md(§2-6 스폰) + Docs/SSOT/Workflow.md(§6 빌드/모델정책) + 이 문서 Docs/RoomSpawnSystem_Handoff.md "설계 상세" 전체.
VibeUE 연결 확인(127.0.0.1:8088, 에디터 열림+플러그인). C++ 빌드=에디터 닫고 UBT(Workflow.md §6-6). 콘텐츠(문/룸/포인트 BP·배치)는 사용자가 에디터에서 저작.

[목표] 맵이 방(룸)들로 구성. 시작 방에서 시작 → 벽의 문을 사격해 부수면 통로 개방 → 플레이어가 그 방에 진입하면 그때부터 그 방의 스폰포인트가 활성(몹 등장). 지나온 방들은 계속 스폰(누적, 방 개방될수록 액티브 포인트 증가). 적 총량은 레벨기반(불변), 방 개방=스폰 "위치"만 누적.

[확정 설계 — 본 문서 "설계 상세" 따름. 사용자 4결정: ①위치만 누적(총량 불변) ②문파괴=처치후타 미발동 ③방영역 자동태깅 ④진입=방전체볼륨]
1. AFPSRDoor (신규, 순수 장벽): StaticMeshComponent(BP에서 SM_Doors 지정—교체=리빌드0) + UFPSREnemyHealthComponent(데미지브릿지로 자동 피격, MaxHealth=내구도) + 콜리전(ECC_Pawn·Visibility 차단). HealthComponent->OnDeath→HandleBroken(콜리전 off+메시 숨김, bBroken 복제). 연출은 BlueprintImplementableEvent OnDoorBroken으로 BP 위임(애니/Chaos/파티클 확장—베이스 무수정). 방 참조 없음(물리 장벽이라 부수기 전엔 진입 불가→진입트리거가 자동으로 파괴後만 발동).
2. AFPSRSpawnRoom (신규, 진입=활성): RoomTag(FGameplayTag) + UBoxComponent EntryTrigger(방 전체) + bool bActiveAtStart. BeginPlay(서버): 박스 내부 AFPSREnemySpawnPoint들에 ZoneTag=RoomTag 자동 부여(이미 태그 있으면 존중=수동 오버라이드). 플레이어 진입 BeginOverlap(서버)→SpawnSub->ActivateSpawnZone(RoomTag).
3. UFPSREnemyHealthComponent: bool bCountsAsKill=true(UPROPERTY) 추가. FPSRCombatStatics::ApplyDamage가 Result.bKilled·bWasEnemy를 bCountsAsKill일 때만 세팅(데미지/DamageDealt/파괴는 항상 작동) → 문(false)=부숴져도 on-kill 프래그먼트(reload-on-kill 등)·킬크레딧 미발동. XP는 이미 AFPSREnemyBase::HandleDeath에만 묶여 문은 자동으로 XP 0.
4. UFPSREnemySpawnSubsystem: ActiveSpawnZone(단일 FGameplayTag)→ActiveSpawnZones(FGameplayTagContainer 누적) + ActivateSpawnZone(FGameplayTag)/ResetSpawnZones(). 방 캐시(CacheSpawnRooms, CacheSpawnPoints 미러) + StartRun 푸시 때 ResetSpawnZones()+bActiveAtStart 방 활성(재런 안전). TrySelectSpawnPoint: 존필터=ZoneTag 없으면 항상 적격·있으면 ActiveSpawnZones에 매칭될 때만 + Weight·거리폴오프 제거 → 적격(out-of-view·MinDist·존활성) 포인트 균등 랜덤.
5. AFPSREnemySpawnPoint: Weight 필드+GetWeight 제거(사용자 "가중치 빼기"). ZoneTag/MinPlayerDistance/bEnabled 유지.

[콘텐츠/저작 = 사용자] BP_Door(부모 AFPSRDoor, SM_Doors 메시+내구도) 도어웨이마다 / BP_SpawnRoom(부모 AFPSRSpawnRoom, 박스=방 전체, RoomTag 고유, 시작방만 bActiveAtStart=true) / BP_EnemySpawnPoint 방 안에 배치(방 박스가 자동 태깅—포인트엔 손 안 대도 됨) / DefaultGameplayTags.ini에 SpawnZone.Room.* 선언.

[검증] 빌드 Succeeded + 헤드리스 스모크 + bCountsAsKill 헤드리스 확인. 룸 플로우(문파괴→진입→스폰, 누적, 이전방 지속)=사용자 PIE.
[모델/워크플로] 복잡 서버권위·스폰 배선이라 구현=Opus 직접(haiku-delegation-security-wiring). 플랜 우선(HotL)→Codex 플랜게이트(plan-codex-comparison-gate, 5분 워치독)→구현→빌드/검증. 기반 2커밋은 머지 전(사용자 PIE 후 머지 결정).
[주의] VibeUE 컨테이너 위젯 프로그래매틱 compile/save 금지(이 작업은 무관). DataAsset/BP 편집 후 is_object_valid 헤드리스 검증.
```

---

## 설계 상세 (위 프롬프트의 근거 — 새 세션이 참조)

### 재사용 (신규 아님 — DESIGN-FIRST)
- **`AFPSREnemySpawnPoint.ZoneTag` + `UFPSREnemySpawnSubsystem`의 존 필터**(`TrySelectSpawnPoint` 내 `ActiveSpawnZone`/`MatchesTag`)는 **이미 존재하지만 현재 호출자 0**(미사용). "방=존" 게이팅이 이 인프라의 직접 소비. 단일 존→누적 `FGameplayTagContainer`로 확장만.
- **데미지 브릿지(`FPSRCombatStatics::ResolveDamage`/`ApplyDamage`)가 타깃을 클래스가 아닌 `UFPSREnemyHealthComponent`로 식별** → 보스 스캐폴드(U3)가 증명. 문이 이 컴포넌트만 가지면 **히트스캔/투사체/레이저/근접/폭발 전 무기 경로 데미지 자동** → "사격해 부수기"가 신규 데미지 코드 0.
- **XP 안전**: `AFPSREnemyBase::HandleDeath`(HealthComponent->OnDeath 바인딩)에서만 `SpawnXPPickup` → 문(Enemy 아님)은 부숴도 XP 0(자동).

### 데미지 브릿지 정확 위치 (bCountsAsKill 적용점)
- `Source/.../Combat/FPSRCombatStatics.cpp` `ApplyDamage`: `bWasDeadBefore`/`HealthBefore` 캡처 후 `HealthComp->ApplyDamage(...)`, `Result.bWasEnemy=true`, `Result.DamageDealt=max(0,Before-After)`, `Result.bKilled=(!bWasDeadBefore && IsDead())`. → **`bWasEnemy`/`bKilled`를 `HealthComp->bCountsAsKill`로 게이트**(DamageDealt/파괴는 무게이트). `ApplyExplosion`의 `KilledEnemies.Add`도 동일 게이트(폭발로 문 부숴도 on-kill 미발동).

### 문 확장성 (사용자 확인됨 — 메시 나중에 바뀔 수 있음)
- 메시 **C++ 하드코딩 금지**(Game.md 원칙2): `AFPSRDoor`=로직만, 메시는 `BP_Door` 디테일에서 지정 → 교체=콘텐츠 작업, **리빌드 0**.
- `OnDoorBroken`(BlueprintImplementableEvent)로 **파괴 연출을 BP에 위임** → StaticMesh→Skeletal(애니 열림)/Chaos(조각 파괴)/파티클로 바뀌어도 베이스 무수정. C++는 게임플레이(콜리전 off·broken 복제)만.
- (옵션, 문 종류 많아지면) `UFPSRDoorDataAsset`(메시 soft-ref+내구도+연출 프리셋) 또는 BP 자식 카탈로그화 — 지금은 BP 메시 지정으로 충분.

### 라이프사이클/엣지
- **시작 방**: `bActiveAtStart` 방은 StartRun에 활성(플레이어가 트리거 안에서 스폰돼 BeginOverlap 타이밍 불확실 → 명시 플래그가 안전). 서브시스템이 방 캐시 후 활성(재런 안전).
- **누적 리셋**: 런 종료/새 런 시 `ResetSpawnZones()`(서브시스템) → 룸 재런 시 시작방만 재활성. 레벨 리로드형 런이면 월드 신규라 자연 리셋.
- **문 콜리전**: 닫힘=ECC_Pawn(플레이어 차단)·ECC_Visibility(히트스캔 피격+관통 차단). 부서지면 둘 다 off → 적도 통과(이전 방 적이 추격). 닫힌 문 뒤 방은 비활성이라 적 없음 → 정합.
- **플로우필드/적 패스**: 닫힌 문이 통로 막으면 적도 막힘(닫힌 문 뒤엔 적 없으니 무해). 열린 후 추격 정합. PIE 점검 포인트.

### 사용자 측(이 세션 기준 미커밋, 본인 작업)
`Content/Actors/SM_Doors.uasset`·`SM_SpawnGate.uasset`(문 메시 임포트), `Content/Maps/L_Sandbox.umap`(스폰게이트/룸 배치 중), `Config/DefaultEditor.ini`(스푸리어스). → 사용자 커밋.

### 사용자가 정해줄 튜닝값
- **문 내구도**(라이플 N발). 기본값 제안 필요.
- 방 레이아웃/RoomTag 명명(예: `SpawnZone.Room.Start`·`SpawnZone.Room.NE`…).
