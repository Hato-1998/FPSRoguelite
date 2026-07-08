# CombatWeaponCard — 무기 / 카드 / 사격 (FPSRoguelite SSOT 분할)

> `Game.md`(SSOT 허브)의 분할 문서. **섹션 번호(§x)는 원본 그대로 보존** — 소스 주석·교차참조 호환.
> 작업 시작 전 허브 `Game.md` + `PROGRESS.md`를 먼저 읽고, 카드·무기·모디파이어·사격 감각 관련 작업 시 본 파일을 연다.
> 담는 섹션: §2-3 카드 시스템 / §2-4 무기 시스템(§2-4-1 모디파이어, §2-4-2 사격 메커니즘) / §2-5 시작 캐릭터.

---

### 2-3. 카드 시스템 — **v2 재설계 (U18, 2026-06-20)**

> **설계 상태**: 이 절은 **v2 목표 설계**(사용자 확정 사양 + 확장성/툴 directive). **구현 = U18a~d**(§B/`TaskPrompts_Master.md`). 현행 출시 코드는 **v1 단일효과**(카드 1=효과 1, `ECardScope` enum)이며 U18a 마이그레이션 대상이다. 설계-우선(SSOT 먼저) 원칙에 따라 본 절을 v2로 갱신하고 코드가 뒤따른다.
> **무회귀 절대조건**: 기존 캐릭터카드 7종·무기 stat 카드·Fragment 4종은 v2 전환 후에도 **현행과 동일 거동**(단일→멀티효과 = 1효과 배열로 마이그레이션).

- **데이터 방식**: **DataAsset + (효과별) GE/GAS/무기모디파이어/Fragment** — 스탯 하드코딩 금지. 에셋 경로 C++ 하드코딩 금지(§6-2).
- **카드 확장 비용 (확장성-우선 directive 2026-06-20)**:
  - **새 효과 타입 = `UFPSRCardEffect` 서브클래스 1개(~40줄) — 중앙 코드 0 수정**(아래 §2-3-1). 새 카드 = DataAsset 1개(코드 0).
  - **기존 Attribute 범위 내 새 카드 = GE 효과 + DataAsset (코드 0)** / **완전히 새 Attribute = C++ AttributeSet 확장 필요** → AttributeSet 스탯 축을 넉넉히 미리 확보(`UFPSRCombatSet`에 예약 슬롯 주석). 상세 = 컨벤션 쿡북(§2-3-8).

#### 2-3-1. 데이터 모델 — 폴리모픽 효과 레이어 (토대)
- `UFPSRCardDataAsset` 단일효과 필드(v1) → **`UPROPERTY(Instanced) TArray<TObjectPtr<UFPSRCardEffect>> Effects`** (멀티효과). + 카드레벨: `ECardGroup Group`(§2-3-2), `OfferRarities`(제안 등급 집합), `Weight`, `CardFamily`.
- **`UFPSRCardEffect`** : `UObject` (`Abstract, EditInlineNew, DefaultToInstanced`):
  - `TArray<FFPSRCardRarityTier> RarityTiers` — **효과별 per-rarity 수치**(같은 등급 roll에서 효과마다 다른 magnitude → "연사+ / 데미지-" 트레이드오프).
  - virtual: `Apply(const FFPSRCardEffectContext&)`(서버) / `GetDescription(ECardRarity)→FText`(UI 자동설명) / `#if WITH_EDITOR ValidateEffect(FDataValidationContext&)` / `GetDamageTypeTag()→FGameplayTag`(속성 시임, §2-3-7).
  - 서브클래스(5종, 거동 1:1): `UCardEffect_CharacterGE`(ASC에 GE, SetByCaller) · `UCardEffect_CharacterPassive`(패시브 GA grant, §2-3-5) · `UCardEffect_WeaponStat`(`EFPSRWeaponStat`+`EFPSRWeaponModOp`+**`bThisWeaponOnly`**=효과별 범위) · `UCardEffect_WeaponBehavior`(Fragment grant, §2-4-1) · `UCardEffect_GrantWeapon`(새 무기 해금, §2-3-4).
- **등급/수치 모델**: 추첨이 카드당 **등급 1회 roll**(`OfferRarities`, 등급 기본가중치×Luck) → 각 효과가 굴린 등급에서 자기 `RarityTiers`의 `Magnitude` 조회. **수치 주입=`SetByCaller`**(GE 효과; 태그 `SetByCaller.CardMagnitude`). **"모든 등급 1에셋"**(등급당 1티어) 유지 — 등급별 별도 에셋 불필요.
  - ⚠️ **rarity 커버리지 강제**(Codex 게이트 P1): magnitude를 갖는 모든 효과는 카드 `OfferRarities` **전부에 대해 티어 보유 필수**(`IsDataValid` 에러). 누락 시 부분/무음 적용 회귀 → 금지.
- **확장성/보안**: 효과는 always-loaded 카드 asset의 inline 서브오브젝트 → **와이어 미통과**. 서버권위 인덱스-선택(클라=`Index`+`OfferId`만, `ServerSelectCard`) 불변. `ApplyCard`는 서버에서 `Effects[i]->Apply()` 루프(효과타입-무지, 새 타입에 무수정).
- **Instanced 결정 + 폴백**: `EditInlineNew`/`Instanced` 서브오브젝트 on DataAsset = UE5.7/Lyra 표준 데이터드리븐 확장(인라인 저작 = 최선의 기획자 UX, 에셋 폭증 없음). ⚠️ 단 *cook/load/network 스모크를 U18a 첫 커밋의 통과 조건으로 고정*(기존 `UFPSRWeaponFragment`는 *공유 asset ref*지 instanced 서브오브젝트가 아니므로 별도 검증). **스모크 실패 시 폴백 = 효과를 공유 asset ref(`UPrimaryDataAsset`)로**(Fragment와 동일 모델, 에셋 수↑ 대가).
- **제1원리 3줄**: ① directive=OCP(확장 개방/수정 폐쇄) 그 자체 — enum+switch는 효과당 5레이어 결합(~58줄·5파일), 폴리모픽은 서브클래스 1파일·중앙 0. ② Instanced UObject on DataAsset = UE/Lyra 표준(컨텍스트 이펙트·코스트/쿨다운) — 단 cook/load는 스모크로 증명(Fragment "공유 ref"를 근거로 쓰지 말 것). ③ SetByCaller·서버권위 인덱스선택·`FFPSRCardRarityTier`·Luck 보존.

#### 2-3-2. 3 카드군 — `ECardGroup` ⟂ 효과별 범위
- **`ECardGroup`{Character, Weapon, WeaponUnlock}** = 추첨 **풀 + 트리거 + UI 필터**(= 사양의 "카드군"). 효과 적용 **범위**는 **효과별**(`UCardEffect_WeaponStat.bThisWeaponOnly`) — 카드 전역 `ECardScope` 폐지(효과별이 더 표현적).
  - **캐릭터 카드** = 캐릭터 + 든 모든 무기. 효과 = 캐릭터 속성(GE)·캐릭터 행동(패시브)·**전체무기 소폭 stat**(`WeaponStat bThisWeaponOnly=false`, 사양7).
  - **무기 카드** = 각 무기 귀속. 효과 = 무기 stat(`bThisWeaponOnly=true`, **레벨업 풀** `WeaponCards`)·행동 트리거 Fragment(`UCardEffect_WeaponBehavior` → **미션/마일스톤 해금 풀** `UnlockableFeatures`, §2-3-4 라우팅 — 레벨업/오프닝시드 누수 방지; U6 재확정, §2-3-5).
  - **무기 해금 카드** = §2-3-4.
- **구 `ECardScope` 매핑(무회귀)**: `Character`→군=Character·`CharacterGE` / `AllWeapons`→**군=Character·`WeaponStat(false)`** / `ThisWeapon`→군=Weapon·`WeaponStat(true)` 또는 `WeaponBehavior`.
- **같은 주제 양군 공존**(사양7): 연사 Up = 캐릭터 카드(전체무기 소폭) ‖ 무기 카드(해당무기 크게) — "넓고 얕게 vs 좁고 깊게" 빌드 선택.
- **family-key 교정(무회귀 위험, Codex 게이트)**: `GetCardFamilyKey`의 `AppliedEffect` GE클래스 폴백은 멀티효과에서 카드레벨 Scope/AppliedEffect 소멸로 붕괴 → **멀티효과 카드 `CardFamily` 필수**(`IsDataValid` 에러) + 폴백 삭제. 같은 family = 한 추첨 1장(상호배타) 유지.

#### 2-3-3. 추첨·적용 (서버권위)
- **레벨업 프리즈(§2-2)**: 캐릭터군 + 보유 무기군 풀 전체에서 **3장 랜덤**, **리롤 3회**(`RunRerollCharges`, 서버 차감) 또는 선택, **런 종료까지 영구**. 무기 stat 카드는 무기 보유 시 동적 합류(Gunfire Reborn식). 등급 4단계(Common/Rare/Epic/Legendary), **Luck**이 상위등급 가중치(※ RarityBonus는 Luck 통합·폐지 2026-06-02).
- **무기 해금**(§2-3-4) = 별도 풀·트리거.
- **카드 소비 시점**(§2-2): 오프닝 시드(런 시작) + 레벨업 프리즈 + 미션/마일스톤(해금). 모두 전역 프리즈 중 선택.
- **`FFPSRCardDraw` 변경**: 단일 `Magnitude` 제거(클라가 효과별 `GetDescription`/magnitude를 로컬 asset에서 조회). ⚠️ **블라스트 라디우스**(Codex 게이트): UI(`FPSRCardEntryWidget`)뿐 아니라 **debug 캐시(`FDebugCardOffer`)·`BuildSingleDraw`·`ClientPresentCards` RPC payload·`ServerSelectCard` apply 계약** 전부 점검 — U18a 검증 항목.
- **보안 불변(테스트 항목)**: 클라는 `Index`+`OfferId`만 전송, 카드/효과/수치 포인터 미전송(`FPSRCardSubsystem.cpp` 서버 빌드 오퍼 인덱싱). family 상호배제·SetByCaller·`AllWeaponsStatExclusions`(§2-4-1) 보존.

#### 2-3-4. 무기 해금 시스템
- **오퍼타입 신설** `EFPSROfferType::WeaponUnlock`(MissionReward 오버로드 금지 — reroll 차단·`DrawWeaponModifierOffer` 특수처리). `ECardGroup::WeaponUnlock`은 직교 grouping/routing 태그.
- **새 무기 해금** = `UCardEffect_GrantWeapon`(효과 서브클래스) → `Inventory->AddWeapon`(3슬롯 캡, full=INDEX_NONE). 해금 무기의 WeaponCards/UnlockableFeatures는 이후 풀에 자동 합류.
- **잠긴 기능 해금** = 신규 효과타입 불요 — 기존 `WeaponBehavior`/`WeaponStat` 효과를 **해금 전용 풀**(무기 DA `UnlockableFeatures[]`)에 둠. 후보생성 = `DrawWeaponModifierOffer`처럼 보유무기 순회·소속무기 태깅. 예: "탄도 2배"(MultiShot 류)·"차징 후 연사"(Fragment).
- **새 무기 풀** = `UFPSRCardPoolDataAsset.WeaponUnlockCards[]`.
- **트리거**: 미션 클리어(기존 `GrantMissionReward` 분리) + **레벨 20/30/40**(신규 마일스톤 훅 = `FPSRGameState::AddSharedXP` 레벨업 루프). `PresentNextOfferIfNeeded`에 unlock 슬롯. 마일스톤 레벨엔 레벨업+해금 **순차 2프리즈**(데드락 없음).
- **3정 차단 = 새 무기 후보만**(사용자 결정 2026-06-20). 기능 해금은 3정 후 계속.
- **U3 시임**: 보스 킬은 `ApplyDamage`(EnemyHealthComponent) 미경유 GAS 경로 → 무기 OnKill 시임이 보스엔 미발화 → **보스 OnKill = U3가 별도 배선**.

#### 2-3-5. 행동 트리거 (무기 훅 + 캐릭터 행동)
- **무기 행동 훅**(`UFPSRWeaponFragment`에 default-empty virtual 추가, §2-4-1): 기존(PreFire/ModifyShotCount/OnHitActor/PostFire/OnProjectileSpawn/ModifyChargeTime/OnImpact) + **OnAim**(`ServerSetAiming`서 현 무기 Fragment 순회; 지속버프=활성효과 슬롯 필요→U18c) · **OnFire**(탄약 커밋 직후, PostFire 아님 — "on-fire 적립/on-hit 소비" 레이스 방지) · **OnMiss**(`PostFire`서 `bServerHit` false 1회) · **OnKill** · **OnStatusKill**(default-empty 시임, **D3 후 배선**).
  - ⚠️ **공통 훅 브릿지 필수**(Codex 게이트 P1): 데미지 경로가 **Hitscan/ChargeLaser/Melee/Projectile/Explosion** 5종 분산 → OnKill/OnFire/OnMiss는 **공유 헬퍼로 5경로 전부에서 호출**(히트스캔 한정 금지, 무기군별 미발화 방지). 각 경로의 라이브 `FFPSRFireContext` + per-hit kill 결과 사용.
  - ⚠️ **OnKill = "이번 타격 alive→dead 전이"**(Codex 게이트 P1, U18c 구현): `Result.bKilled`(`FPSRCombatStatics.cpp:86`)는 현재 *타격 후 dead*(코프스 재타격 시 true) → `ApplyDamage`가 **pre-state 캡처로 alive→dead 전이만 true**가 되도록 교정(`bJustKilled`). `FDamageResult`에 **`DamageDealt`(HealthBefore-After 실측)** 신설 → 코프스/오버킬은 `DamageDealt=0` → 히트마커·GAS 이벤트 **완전 inert**(마커 게이트 `bApplied`→`DamageDealt>0`, 관통/임팩트 지오메트리는 `bApplied` 유지). 기존 Kill 히트마커 코프스-재타격 더블엣지 동시 교정. ❌적 `OnDeath` 델리게이트 per-enemy 바인딩 금지(적500 dispatch 예산).
  - **ChargeLaser warm-up 틱 OnKill 미발화(의도 사양, U18c)**: warm-up 틱은 fragment 훅 전체를 스킵(payoff-only)하므로 OnKill도 payoff 빔에서만 발화. warm-up 칩뎀이 적을 처치해도 OnKill 미발화(기능 소비자 없음 — ChargeLaser는 처치-트리거 무기 아님, 마커도 현재 payoff-only).
- **캐릭터 행동 = GAS-native**(무상태 Fragment 미러 아님 — 플레이어는 ASC 보유·4인뿐, 적500 perf 이유 없음): ① **흡혈** "데미지 줄 때 회복" = **단일 브릿지 `FPSRCombat::ApplyDamage`**에서 instigator ASC에 `GameplayEvent`(`GameplayEvent.Player.DealtDamage`, 페이로드=**실제 입힌 데미지 `DamageDealt`**, 오버킬/코프스 제외) → 트리거 패시브 GA가 회복 GE(힐=DamageDealt×비율, **모든 무기경로 균일** — 브릿지가 공통이라 캐릭터 효과엔 충분, 무기 Fragment 훅과 달리 FireCtx 불요). ⚠️적500: 이벤트송신은 플레이어-instigator·`DamageDealt>0`·**리스너 카운트>0**(흡혈 grant 시 캐릭터에 set)일 때만. ② **초당 체력 재생** = **기존 `UCardEffect_CharacterGE` + periodic infinite GE**(패시브 GA 불요, 코드 0). ~~"N초 유휴 회복"(reset 루프)~~ 폐기(사용자 2026-06-20). `UCardEffect_CharacterPassive`(흡혈용)가 **신규 `UFPSRPassiveAbility` 베이스**(auto-activate-on-grant·`OnRemoveAbility` 정리) 서브클래스를 grant, 핸들은 PS 추적·런종료 ClearAbility.
- **서버권위**: 무기 해금·캐릭터 행동·행동 훅·이동속도 전부 서버권위(클라 보고 금지). [[freeze-gate-client-server-symmetry]]
- **협동 카드 시임 (컨설트 2026-07-01 F7 — `Docs/Review/20260701-concept-conclusions.md`)**: FF 기본 ON·치사(Enemy §2-10)와 짝 = **FF 데미지를 아군 회복으로 전환하는 카드**(사선을 벌이 아니라 지원으로). 캐릭터 행동(GAS-native, 흡혈과 동일 `ApplyDamage` 브릿지 경로 재사용 가능 — 아군 타겟 분기). **제약**: 오버힐 없음·틱/초당 힐 상한·다운 직전 세이브 용도(지속 '힐건'化 금지 = "아군을 쏴서 힐하는 게임" 붕괴 방지). 협동 유도 가성비 = 협동유도 스페셜 적(§2-6) > 어그로 룰/카드 > FF→회복 카드.

#### 2-3-6. 이동속도 속성
- `UFPSRCombatSet`에 **`MoveSpeedMultiplier`(base 1.0)** + **`PostAttributeChange` 오버라이드 신설**(현재 없음; `UFPSRHealthSet` 패턴 차용) → 소유 Pawn `MaxWalkSpeed = BaseWalkSpeed × Mult`(서버 PostAttributeChange + 클라 OnRep, `COND_None`이라 시뮬프록시 도달). `FPSRCharacter.cpp:60` 하드코딩 600 → `BaseWalkSpeed` 상수. 4인 협동 desync 없음(속성만 복제, MaxWalkSpeed 로컬 재계산·CMC 예측 처리). 적용 순서(서버/클라 OnRep) = U18 검증 항목.

#### 2-3-7. 속성(elemental) 데미지 forward-compat 시임 — 시임만, 거동은 D3
- **데미지타입 태그 배선**(U18a): `FPSRCombat::ApplyDamage`/`ApplyExplosion`/`AFPSRCharacter::ApplyContactDamage`/`EnemyHealthComponent`에 **`FGameplayTag DamageType = FGameplayTag()`(빈=Physical) 디폴트 인자** 추가(기존 콜러 무수정 컴파일). `DefaultGameplayTags.ini`에 `DamageType.Physical/Fire/Ice/Poison/Lightning` 선언. **~7파일 ~15줄, 거동 변화 0.**
- **효과 시임**: `UFPSRCardEffect::GetDamageTypeTag()`(빈 default). 향후 `UCardEffect_ElementalDamage`가 override → 태그를 무기 인스턴스에 기록 → 발사 GA가 `ApplyDamage`에 전달. **U18은 elemental 서브클래스 미출시**.
- **D3 이연**: `Event.DamageTaken`(태그 페이로드) 듣는 패시브 GA→상태이상 / 적 저항·배수 태그 룩업(U3 보스 약점 공유). 시임=지금(싸다), 거동=D3(비싸다) 분리.
- **기획자가 새 속성 추가(향후)**: ①`.ini`에 `DamageType.<New>` 1줄 → ②카드에 elemental 효과 + 보너스 → ③풀에 추가. 파이프라인/어빌리티/서브시스템 수정 0.

#### 2-3-8. 검증·명명·기획자 툴 (directive ②)
- **`IsDataValid` 가드레일**(load-bearing): `Effects[]`→`ValidateEffect()`(서브클래스별 자기 필드) + 빈 Effects 에러 + 멀티효과 `CardFamily` 필수 + **rarity 커버리지**(§2-3-1) + 빈 DisplayName 경고 + 명명 prefix 린트. 메시지=자산+필드 명시(무음 실패 금지).
- **자동 설명 `GetDescription`**: 카드 표시텍스트를 효과에서 **생성**(기획자 Description 수기 불요, 멀티효과="primary (+N more)" 집계). `FPSRCardEntryWidget`가 `Effect->GetDescription(Rarity)` 소비.
- **서브클래스별 깔끔 에디터 UI** = `EditInlineNew` 폴리모픽의 공짜 부산물(각 효과 자기 필드만 노출, `EditConditionHides` 캐스케이드 제거). [[dataasset-conditional-field-visibility]]
- **카드 카탈로그 에디터 유틸**(U18d, 비차단): Blutility + `BlueprintCallable` C++ 로더 = 전 `DA_Card_*`의 군/효과/등급 magnitude/family/풀소속 한눈에 + 필터 → 밸런스 이상치 점검. [[vibeue-mcp-capabilities]]
- **명명 규약**: `DA_Card_<Group>_<Theme>` / `DA_Frag_<Weapon>_<Behavior>` / `GE_Card_<Attr>`.
- **잡정비**: `BP_Card_RarityBouns.uasset`(삭제된 속성 `RarityBonus` 참조로 이미 깨짐) = **개명 아니라 삭제**(가리키는 GE 포함). `CardId`(U10 세이브 대비)=보류(`GetPrimaryAssetId()` 존재, U10 키 확정 후 결정).
- **컨벤션 쿡북**: 새 카드=5분(복제) / 새 효과 타입=~40줄 1파일 / 새 무기 Fragment=15분(서브클래스+DA) / 새 무기 stat mod(기존축)=5분 / 새 무기 stat **축**=enum+`RecomputeResolved` switch case(**컴파일체크 유지**, 데이터화 금지) / 새 캐릭터 속성=C++ AttributeSet(예약 슬롯 활용) / 새 캐릭터 패시브=`UFPSRPassiveAbility` 서브클래스 / 새 카드 스코프/오퍼타입=v2 후 동결.

#### 2-3-9. 빌드 시너지 · 경계
- **빌드 시너지 설계 (기획 2026-06-10)**: 카드 *메커니즘*과 별개로 **무엇이 빌드를 다르게 느끼게 하는가**(시너지 축: 원소/상태이상, 투사체수↔단발위력, 크리↔지속피해, AOE↔관통)를 별도 설계 — 뱀서 핵심 리텐션. **Fragment 상호작용(§2-4-1) + 멀티효과 트레이드오프가 1차 수단**. 시너지 패스 = 재미 게이트(§7-5) 전후.
- **경계/시임**: 상태창 UI(사양9)=후속(데이터 노출 시임만). 상태이상 본체·OnStatusKill 배선·elemental 거동=**D3**. 보스 OnKill=**U3**. AllWeapons 복제=**U11b**. CardId=**U10**.

### 2-4. 무기 시스템
- 최대 **3개 동시 보유** (기본 1 + 추가 2). 5렙/20렙 등에 무기 카드 등장
- **무기 버림·교체 시스템 없음**(확정 2026-05-30) — 기존 무기로 시작해 슬롯 확장으로만 추가(최대 3). 환불/카드치환 보완 불필요
- **아키타입**: 연사총(FullAuto) / AOE(바주카) / 근접(Melee) / 차징 관통 레이저(ChargeLaser) / 단발 스나이퍼(Sniper) / 샷건(Shotgun). `EFPSRWeaponArchetype`의 `Burst`는 **직렬화 보존용으로만 잔존**(전용 무기 없음 — 아래 개편 참조).
- **⚠️ 무기 개편 (2026-07-08)**: ① **점사총(Burst) 전용 무기 제거** → **점사는 라이플의 언락 프래그먼트**(`UFPSRFragment_BurstFire`, `Rifle.UnlockableFeatures`, 미션 언락)로 이동. 라이플 기본은 연사. ② **유탄 발사기 제거**(바주카는 유지). ③ **기관단총(SMG) 추가**(FullAuto·연사↑↑·데미지↓·투사체). ④ **전달 방식 = 발사체 통일**: **차지레이저(히트스캔)·근접 외 전 무기가 발사체**(연사총 포함). 발사체는 히트스캔과 데미지/약점/크리/처치/프래그먼트(OnHitActor·OnImpact) **패리티**. 연사 무기는 고속·단수명(§5 발사체 예산). **전달 방식 = 무기 DA `FireAbility`(GA)가 결정**(아키타입과 직교): Projectile GA / ChargeLaser GA / Melee GA.
- **무기별 스탯 = WeaponInstance 스탯 블록(리플리케이트 struct, `FFPSRWeaponStatBlock`)**, 캐릭터 ASC와 분리
  - 스탯 예: Damage, FireMode, FireRate, BurstCount, Range, MagSize, Spread/Bloom, Recoil, MeleeRadius (+ 후속: ReloadTime, ProjectileSpeed, Pierce, AOERadius, ChargeTime, PelletCount). **예비 탄약은 무한**(ReserveAmmo 미사용 — §2-4-2)
- **ADS**: 무기 DA에 `bHasADS`+FOV (스나이퍼/차징=정밀, 연사/샷건=약함, 근접=없음)
- **무기 교체 입력**: 숫자키 **1/2/3** 직접 슬롯 선택 (`IA_EquipSlot1/2/3`, 마우스휠 미사용)

#### 2-4-1. 무기 모디파이어 = 게임의 핵심 재미
무기 모디파이어 = **① 스탯 모디파이어(수치 조정)** + **② 행동 Fragment(동작 변경)** 두 종류. 둘 다 런타임 `UWeaponInstance`(무기별 모디파이어 컨테이너, P4-B 신설)에 누적되며, 스탯 해석 = `FFPSRWeaponStatBlock` 베이스 × 누적 모디파이어(ThisWeapon + 캐릭터의 AllWeapons).

**① 스탯 모디파이어 카드 (확정 2026-06-04 — 레벨업 카드 풀)**
- 예: **탄창 용량↑ / 연사 속도↑ / 반동↓** (그 외 데미지/확산/재장전속도 등 `FFPSRWeaponStatBlock` 축 확장 가능).
- **두 스코프, 같은 효과 다른 수치**(§2-3): **`AllWeapons`**(들고 있는 모든 무기, 수치 **작게**) vs **`ThisWeapon`**(현재 무기 1정, **큰 수치**). 둘 다 레벨업 프리즈(§2-2) 카드 풀에 합류(무기 보유 시).
- **무기별 AllWeapons 제외 (확정 2026-06-11)**: 무기 DA `AllWeaponsStatExclusions`(`TArray<EFPSRWeaponStat>`)에 나열된 축은 **AllWeapons 스코프 모디파이어가 적용되지 않는다**(per-weapon·per-axis). 해석은 `UFPSRWeaponInstance::RecomputeResolved`가 AllWeapons 스택을 합산할 때 필터(ThisWeapon 스택은 플레이어가 의도 타겟이라 항상 적용·미필터). 예: ChargeLaser 반동은 차징 램프라 일반 "반동↓" 광역 카드가 무의미 → DA에서 `RecoilVertical` 제외. DA는 정적이라 복제 불필요·클라/서버 결정적.
- 데이터: 카드 `Scope`로 적용 범위(**v2: 효과 `UCardEffect_WeaponStat.bThisWeaponOnly`로 이동, §2-3-2**), `RarityTiers[].Magnitude`로 수치(SetByCaller 동일 패턴). 무기 스탯 모디파이어는 GE가 아닌 **WeaponInstance 모디파이어로 적용**(무기 스탯은 ASC 밖이므로) — `ApplyCard`에서 효과 루프로 처리(v2; v1 weapon-scope 분기 P4-B).

**② 행동 Fragment (동작 변경)**
> **라우팅 확정(U6, 2026-07-01 — U18 v2 경유값 교정)**: 행동 트리거 Fragment(`UCardEffect_WeaponBehavior`) 전부는 **미션/마일스톤 해금 풀**(무기 DA `UnlockableFeatures[]`, §2-3-4)로 라우팅. **레벨업 무기 카드 = 순수 stat만**(FireRate/MagSize/Damage/Crit…) — Fragment의 레벨업·오프닝시드 누수 방지. (U18은 일시적으로 Fragment→레벨업 카드였으나 U6가 미션 풀로 재확정. 구 `AvailableModifiers` 필드 폐지→`UnlockableFeatures`.) 훅 면 확장 = **OnAim/OnFire/OnMiss/OnKill/OnStatusKill**(OnStatusKill=D3 시임). OnKill·OnMiss·OnFire는 5 데미지경로(Hitscan/ChargeLaser/Melee/Projectile/Explosion) **공통 헬퍼**로 호출, OnKill=`bJustKilled` 전이.
- (v1) 미션 보상으로 무기 동작을 **근본 변경** (2연발, 차징 무효, 아군 힐 빔 등) — U6 라우팅: 모든 행동 Fragment(근본 변경·점진 트리거 무관)=미션/마일스톤 해금 풀(§2-3-4)
- 적용 방식: **Weapon Behavior Fragment (합성형 훅) + 누적 가능**
  - `UWeaponInstance.ActiveModifiers[]`에 누적, 서로 상호작용 (예: 2연발+관통)
  - `GA_WeaponFire`(아키타입별 베이스)에 훅: `PreFire → ModifyShotCount → ModifyChargeTime → OnProjectileSpawn → OnHitActor → PostFire` (+ v2 트리거 훅 `OnAim/OnFire/OnMiss/OnKill/OnStatusKill`, §2-3-5)
  - 각 무기 DA가 `UnlockableFeatures`(구 `AvailableModifiers`, 폐지) 정의 → **미션 클리어/레벨 마일스톤(20/30/40) 시 즉시 프리즈(§2-2)에 모디파이어 해금/변경 카드로 1종 선택**
- **GA 교체 방식 금지**(조합 폭발), **거대 태그 분기 금지**(유지보수 지옥)
- ⚠️ **성능**: `OnHitActor`가 500마리 타격 시 과도한 virtual dispatch/heap alloc 금지 → 훅은 경량(데이터 기반·히트당 무할당)
- **구현 확정 (P4-B-2, 2026-06-08)**:
  - **Fragment = 무상태 `UFPSRWeaponFragment : UPrimaryDataAsset`** — 동작은 C++ 서브클래스(virtual 훅), 수치는 authored DataAsset. (Instanced/runtime-struct 대신 공유 무상태 asset → 복제·메모리 비용 최소). 레퍼런스: `UFPSRFragment_MultiShot{ExtraShots}`(ModifyShotCount), `UFPSRFragment_OnHitBonusDamage{BonusDamage}`(OnHitActor).
  - **누적·중첩**: `UFPSRWeaponInstance.ActiveFragments[]`(복제 참조)에 누적. **`UFPSRWeaponFragment.MaxStacks`**(기본 1)만큼 같은 fragment 중복 누적 → 훅이 스택마다 적용(예: MultiShot 2스택=+2발). 미션보상 오퍼는 `StackCount < MaxStacks`인 동안 계속 제시(서버 권위 dedup).
  - **MultiShot 탄약**: 멀티샷은 **발사 펠릿 수만큼 탄창 소모**(`ConsumeAmmo(NumShots)`), 잔량 부족 시 **잔량까지 클램프 후 발사**(최소 1발 보장). NumShots는 fragment 훅 계산 후 소모.
  - **미션보상 카드 UI**(`UFPSRCardEntryWidget`): fragment 카드(=`Scope==ThisWeapon && GrantedFragment`)는 등급 대신 **카테고리 라벨**(위젯 기본값 `FragmentCategoryText`, WBP override) 표시 + **수치 슬롯 빈칸**(behavior 해금이라 magnitude 무의미).
  - **무기 귀속 표시 (계획, P4 card-weapon-pools 후속 UI)**: 무기에 귀속되는 카드(ThisWeapon 스탯 카드 + 무기 fragment)는 카드 선택 UI에 **소속 무기 아이콘 + 이름**을 표시한다(어느 무기 강화인지 식별). 데이터 출처 = `FFPSRCardDraw.TargetWeapon`(이미 추첨 시 서버가 세팅, P4 card-weapon-pools). 필요: ① `UFPSRWeaponDataAsset`에 **`Icon`(UTexture2D/소프트참조) 필드 신설**(콘텐츠 채움), 이름은 기존 `DisplayName`. ② `UFPSRCardEntryWidget`이 `TargetWeapon` 게터 바인딩 → 아이콘/이름 슬롯 노출(`TargetWeapon==null`=캐릭터/AllWeapons 카드면 숨김). **코드(추첨)는 완료, UI 배선 + Icon 필드 + 무기 아이콘 콘텐츠 = 후속 유닛.**
  - 적용 위치=서버 권위(`ApplyCard` ThisWeapon 분기 `AddFragment`), 클라 예측=GA `ModifyShotCount` 루프(복제된 ActiveFragments로 N샷 예측). 런 종료 소멸=WeaponInstance 수명. **후속**: ModifyChargeTime/OnProjectileSpawn 훅(차징/투사체 아키타입), Melee fragment, fragment 제거/교체.

#### 2-4-2. 사격 메커니즘 / 슈팅 감각 (데이터 드리븐, FPS 핵심)
> 슈팅 감각은 FPS의 핵심. 모든 값은 `FFPSRWeaponStatBlock`(또는 분리된 `FWeaponFireProfile`)에 두어 **무기별·카드별 조정 가능**하게.
> **정체성 (확정 2026-06-10)**: 기본 손맛은 *정밀 FPS*(반동 패턴·ADS·블룸 실재)로 간다 — **의도된 선택**(스웜-서바이벌이라고 조준을 느슨하게 두지 않음). 단 **카드/모디파이어로 캐주얼화 가능**해야 한다 — 플레이어가 빌드로 *정밀↔학살 뿌리기* 스펙트럼을 고른다(예: 반동↓·확산↓·조준 보조·오토에임 보정 카드). 아키타입별로도 정밀도 차등(스나이퍼/차징=정밀, 연사/샷건=뿌리는 맛). 카드 축 확장 시 이 **'캐주얼화 레버'(반동/확산/조준보조)를 `FFPSRWeaponStatBlock`에 미리 확보**.

- **발사 모드** `EFPSRFireMode`: Single / Burst(N발) / FullAuto
- **연사속도** FireRate (shots/sec) — FullAuto는 hold 시 이 주기로 반복 발사
- **반동(Recoil)**: 발당 VerticalKick(상하)·HorizontalKick(좌우), 회복 속도/곡선
- **반동 패턴**: 샷 인덱스별 정형 스프레이 패턴 (UCurveVector 또는 오프셋 배열)
- **확산(Spread/Bloom)**: 기본 확산각 + 발당 블룸 증가 + 최대 블룸 + 회복. 트레이스를 확산 콘 내 랜덤화
- **탄약**: MagSize / ReloadTime / 재장전 어빌리티(R), 빈 탄창 시 발사 차단. **예비 탄약 무한**(ReserveAmmo 미사용, 확정 2026-05-30) — 스웜 상대 탄약고갈 스트레스 제거, 장전 타이밍 긴장만 유지
- **ADS(조준)**: 우클릭, FOV/확산/반동 배율 (무기별 `bHasADS`)
- (선택) 거리 데미지 감쇠
- 절차적 반동/스웨이(스프링)는 Tier3 / P4 "게임필" 연계
- **권장 시점**: P1.5(전투 슬라이스 직후, P2 전). 슈팅 감각 튜닝 반복이 많아 일찍 확립 권장
- **구현 상태**: P1.5-A에서 FullAuto hold-to-fire 루프 + 반동(카메라 킥) + 확산/블룸을 `UFPSRWeaponFireComponent`에 구현. 탄약(MagSize/재장전, 예비 무한)/ADS는 P1.5-B 예정

### 2-5. 시작 캐릭터
- 시작 무기 = **로드아웃 풀(`DA_LoadoutPool.SelectableWeapons`)에서 1택**(로비 선택). **현재 = 연사총(라이플) / 기관단총(SMG)**(2026-07-08 개편: 점사총 전용 무기 제거→점사는 라이플 언락 프래그먼트로 대체, SMG 추가). 근접칼·기타 무기는 언락 카드(`UCardEffect_GrantWeapon`) 경로(현 풀 미배선=휴면).
- ~~3종: 연사총 / 점사총 / 근접칼~~ (2026-07-08 개편으로 대체)
- `UHeroDataAsset`(PawnData 개념) Base 구조 — DefaultWeapon, BaseAttribute, PassiveAbilitySet, FP팔/3P바디 메시
- 데이터 드리븐. 고유 패시브가 무거워지면 그때만 GameFeature 플러그인으로 승격
