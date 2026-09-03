# Enemy — 몬스터 / 발사체 / 네트워크 (FPSRoguelite SSOT 분할)

> `Game.md`(SSOT 허브)의 분할 문서. **섹션 번호(§x)는 원본 그대로 보존** — 소스 주석·교차참조 호환.
> 작업 시작 전 허브 `Game.md` + `PROGRESS.md`를 먼저 읽고, 적 스웜·스폰·발사체·데미지 브릿지·네트워크 토폴로지 관련 작업 시 본 파일을 연다. 성능/플로우필드 수치는 `Performance.md`(§5) 참조.
> 담는 섹션: §2-6 몬스터(스웜 적) / §2-10 발사체·네트워크.

---

### 2-6. 몬스터 (스웜 적)
- ~~공격 타입 **근거리 / 원거리 / 특수 중 정확히 1개 고정** (상황 따라 전환 안 함)~~
  🔴 **정정(2026-08-26, [ADR 0013](../Architecture/0013-enemy-tier-axis-and-elite-gas.md))**: **근거리는 폐지됐다.**
  커밋 `f5b0a78d`(2026-08-14 사용자 결정, *"전 몬스터 원거리화"*)로 적 BP 2개가 **전부 `AFPSRRangedEnemyBase` 자식**이 됐고
  (`BP_EnemyMeleeBase` 는 이름만 "Melee" — ⚠️ **이 이름은 아직 거짓말이다**, 리네임은 리다이렉터를 낳고 `DA_EnemyRoster` 를 건드리므로 별도 콘텐츠 행), ADR [0008](../Architecture/0008-hover-enemy-pursuit-reachability-modes.md)·[0009](../Architecture/0009-hover-swarm-local-3d-flow-window.md) 비목표가
  *"근접 적 재도입 없음 — 근접처럼 보이는 적도 초단사거리 원거리로 만든다"* 로 못박았다.
  → 현행 공격 방식은 **원거리 하나**이고, 그 위에서 **사거리가 아키타입 데이터로 갈린다**(초단사거리 = 옛 "근접").
  **특수는 존치** — 아래 「분류 축」의 티어와 **직교하는 별도 축**(공격 형태: 공중·방패 등, P4 항목 참조)이다.
  ✅ **정리 완료(2026-08-26, `c1e9dd7f`)** — ~~`AFPSREnemyBase::ServerTickAttack` 의 근접 접촉 판정과 짝인 `AttackTokenLimit=10` 근접 토큰 회계는 실행하는 콘텐츠가 0인 죽은 코드다. `Enemy.Archetype.*`/`Enemy.Attack.*` 태그 6개도 참조 0건.~~
  근접 축이 코드에서 **제거됐다**: 접촉 판정 · `EFPSRServerAttackResult`(열거 자체) · `bMeleeTokenAvailable` ·
  `AttackTokenLimit` · `AttackVerticalRange` · `ContactDamage` · `CanAttack`/`AttackInterval` · `AttackDamage`, 그리고 태그 6개.
  `ServerTickAttack` 은 반환값 소비자가 0이 되어 **`void`** 가 됐다.
  ⚠️ **이름이 근접처럼 보이지만 살아 있는 것들**(지우지 말 것): `AttackRange`(클라이언트 근접-tell 휴리스틱) ·
  `LastAttackTime`/`NotifyAttacked`(원거리 발사도 스탬프 — 다만 살아 있는 독자는 ADR 0009 가 퇴역시킨 추격 스톨 감지기뿐) ·
  `AttackAnimHoldSeconds` · 이동 스톱게이트가 쓰는 로컬 `AttackVertGap`.
- **분류 축 = 티어(일반 / 엘리트)** — 확정 2026-08-26, [ADR 0013](../Architecture/0013-enemy-tier-axis-and-elite-gas.md).
  티어는 **C++ 클래스로 표현**하되 **중앙 enum/switch 로 분기하지 않는다**(virtual·폴리모픽 시임만 — 아래 로스터 룰의 확장성-우선 원칙과 같은 규약).
  **보스는 티어에 포함되지 않는다** — `AFPSRBossBase` 는 `ACharacter` 로 별도 유지하며 스웜과의 공유는 `UFPSREnemyHealthComponent` 하나뿐이다.
  ⚠️ **`Tier` 라는 낱말을 적 티어에 단독으로 쓰지 않는다** — `TierS0/S1/S2RadiusSq`(거리 LOD)·`EFPSRArenaPropTier`·`FFPSRCardRarityTier` 로 이미 3중 점유돼 grep 이 뒤엉킨다.
  ✅ **골격 착지(2026-08-26)** — 클래스 = **일반 `AFPSREnemyBase`**(공통 베이스가 곧 일반 티어) · **엘리트 `AFPSREnemyEliteBase`**(`a9f48d11`).
  같은 슬라이스에서 **`AFPSRRangedEnemyBase` 를 은퇴**시키고 그 원거리 FSM 을 베이스로 **승격 합병**했다(`00ed2d9a`) —
  전 적이 원거리인 지금 "원거리 전용 서브클래스"는 티어 축과 어긋난 계층이라, 티어·형태 클래스 전부가 그 이름을 조상으로 물려받게 되기 때문.
  BP 재부모화는 **`Config/DefaultEngine.ini` 의 `ClassRedirects` 로 자동 처리**된다(수작업 재부모화 불필요):
  엔진 `FLinkerLoad::FixupImportMap()` 이 임포트 맵(BPGC 의 `SuperStruct` 포함)에 리다이렉트를 적용하고,
  저작값은 `UStruct::SerializeTaggedProperties` 의 **이름 기반 상속 체인 탐색**으로 보존된다.
  ⚠️ 단 **저작되지 않은 상속 기본값은 그 보호를 못 받는다** — `StopDistance` 의 실효 900 이 은퇴한 랭드 생성자에만 있었고
  `BP_EnemyMeleeBase` 는 그 값을 디스크에 갖고 있지 않아, 베이스 인라인 기본값을 120→**900** 으로 올려 보존했다.
  회귀 가드 = `FPSRoguelite.Enemy.BlueprintParent` — `Content/Character/Enemy` 를 **폴더 규약으로 스캔**해 각 BP 를 로드하고 ①로드 성공(=리다이렉트 발화) ②`AFPSREnemyBase` 자식 ③`StopDistance > 0` ④**`ProjectileClass` 非null**(리다이렉트가 성공해도 태그드 프로퍼티는 조용히 기본값으로 떨어질 수 있고, 그러면 전 적이 발사 불능이 되는데 로그 한 줄 외엔 아무 신호가 없다)을 검사한다. ⚠️ ③은 **바닥값 검사이지 "값이 옳다" 검사가 아니다**(두 BP 의 정답값이 서로 다르다) — 실제 값과 스캔 개수는 로그로 남겨 사람이 되읽는다. 스캔 0건이면 **실패**시킨다(공허한 통과 방지).
  ✅ **풀 클래스별 버킷화 완료**(ADR 0013 불변식 7, `61f40fe7`) — ~~현행 `AcquireEnemy` 는 `DormantPool` 단일 평면 배열을 `GetClass()` 정확 일치로 선형 스캔한다.~~
  `FFPSREnemyDormantPool`(`TMap<TSubclassOf<AFPSREnemyBase>, 버킷>`)로 교체돼 취득 비용이 **요청 클래스 버킷 크기에만** 비례한다.
  키는 **정확 일치**여야 한다(`IsChildOf` 금지 — 엘리트가 `AFPSREnemyBase` 의 자식이라 일반 휴면체를 집어간다). 성질 가드 = `FPSRoguelite.Enemy.DormantPool`.
  🔴 **미해결 기아 모드**: `TotalSpawned` 는 증가만 하고 하드캡은 클래스 무관 총량이라, 전반이 클래스 A 로 캡을 채우면
  후반 엘리트 요청이 영구 거부될 수 있다. 버킷화가 만든 게 아니라 드러낸 문제 — 해법은 **후속 행 3(엘리트 캡 회계)** 의 결정 대상으로 라우팅됐다.
- **GAS** — ⚠️ **정정(2026-08-26, ADR 0013)**: 종전 *"GAS 미사용"* 은 이제 **티어별로 갈린다.**
  - **일반 티어 = GAS 미사용**(무변) — 경량 `UHealthComponent`(`UFPSREnemyHealthComponent`) + 비-GE 데미지 적용
  - **엘리트 = ASC 부착** — `Game.md §1` 의 *"GAS는 플레이어(1~4)와 보스/엘리트에만"* 중 **엘리트 절반이 실현**된다.
    소유자는 **액터 자신**(적은 PlayerState 가 없어 플레이어와 소유 패턴이 다르다 — 플레이어는 `AFPSRPlayerState` 소유).
    **플레이어와 어트리뷰트 셋을 공유하지 않는다.** 동시 마릿수 = **스테이지·난이도 함수 + 하드캡**(캡 없이 증가만 두면 후반 ASC 복제 예산이 터진다).
    ✅ **착지(2026-08-27)** — `AFPSREnemyEliteBase` 가 `IAbilitySystemInterface` 구현 + `UFPSRAbilitySystemComponent` 를 **액터 소유**로 부착.
    복제 모드 = **`Minimal`**(엔진 `AbilitySystemComponent.h:82` — *"does not work for **Owned** ASC"*, 엘리트는 오너 클라가 없어 정확히 해당. 플레이어가 `Mixed` 인 이유도 같은 문장).
    동시 마릿수 = `min(DA_RunSchedule.StageDifficulty[].MaxEliteAlive, UFPSREnemySpawnSubsystem::EliteHardCap)`. 캡 초과 시 **스폰 보류**(일반으로 대체하지 않는다).
    **이월 엘리트가 새 스테이지 캡을 넘으면 = 일시 초과를 허용한다**(ADR 이 물은 양자택일의 판정) — 캡은 **신규 스폰만** 막고
    이월분은 예산을 점유한 채 사망으로 자연 수렴한다. `GlobalAliveCap` 이 이미 정확히 그 의미론이라 새 기제가 0이고,
    강제 반납은 이월의 의도("쌓아 온 압박을 가져간다")와 충돌한다. 총량 손잡이는 `StageCarryOverMaxFraction` 이 따로 제공한다.
    ⚠️ **어트리뷰트 셋은 만들지 않았다**(사용자 결정 2026-08-26, ADR 문구에서 의도적 이탈) — 체력은 컴포넌트에 남고(불변식 4),
    **범용 상태이상은 GAS 로 가면 안 되므로**(아래 「시간축·상태이상 계약」) 지금 넣을 어트리뷰트가 0이다. 첫 소비자가 생길 때 만든다.
    🔴 **수명주기 = 4중 폐쇄** — `EnterDyingState`(어빌리티 취소, Super 앞) · `Deactivate`(GE 제거, Super 의 `DORM_DormantAll` 앞) ·
    `Activate`(방어적 재클리어 + 어빌리티 재부여) · `ServerResetEliteForStageCarry`(이월 — 어빌리티만 취소, GE 는 보존).
    이월 적은 `Activate`/`Deactivate` 를 **둘 다 안 밟기** 때문에 네 번째가 필요하다. 원거리 홀드가 같은 이유로 이미 4중이다.
  - 🔴 **시간축·상태이상 계약(2026-08-27)** — **엘리트 ASC 에서 시간이 흐르는 GAS 기제 전부 금지**:
    duration GE · **periodic GE** · cooldown GE · 시간 기반 AbilityTask. 근거 = §2-2 전역 프리즈는 **상태 게이트**이지 `TimeDilation` 이 아닌데
    엔진은 이들을 월드 `FTimerManager` 로 돌린다(`GameplayEffect.cpp:4409` duration · **`:4431` period `bLoop=true`**).
    4인 협동에서 1인 레벨업 = 전원 프리즈이므로, 카드 고르는 동안 엘리트 쿨다운·디버프가 공짜로 흘러간다.
    → 대체 = **프리즈-멈춤 서버 누산기**(원거리 차징 관용구). 기획자 손잡이 = `UFPSREliteGameplayAbility::CooldownSeconds`(BP 에 숫자만 저작).
    ⚠️ **강제 범위 정정(G2)**: 런타임 가드(`GameplayEffectApplicationQueries`, 엔진 네이티브 훅)가 막는 것은 **엘리트 ASC 가 받는 GE 3종뿐**이다.
    **시간 기반 AbilityTask(`WaitDelay` 등)는 가드 밖**이고 월드 타이머로 도므로 프리즈를 뚫는다 — 그건 **문서 약속으로만** 금지된다.
    (전환은 어빌리티를 취소하지만 §2-2 프리즈는 취소하지 않는다.)
    🔴 **엘리트가 *발신*하는 GE 도 계약 대상이다** — 가드는 **수신**만 막는다. 엘리트 어빌리티가 **플레이어에게** duration/periodic
    디버프를 걸면 그건 플레이어 ASC(`Mixed`, 가드 없음)의 월드 타이머로 돌아 레벨업 프리즈 중에도 타들어 간다.
    이 브랜치가 최초의 적→플레이어 GE 벡터(엘리트 어빌리티 시임)를 만들었으므로 더는 이론이 아니다 —
    **엘리트 어빌리티는 플레이어에게도 시간형 GE 를 걸지 않는다**(즉발 데미지 + 서버 누산기로 표현할 것).
    ⚠️ **Health 를 건드리는 GE 는 엘리트 ASC 에서 조용히 무시된다**(엔진 `GameplayEffect.cpp:4541` — 없는 어트리뷰트 modifier skip). 체력은 컴포넌트 소관이다.
    ⚠️ **범용 상태이상(얼음→슬로우 등)을 여기 얹지 마라** — 보스는 ASC 가 없고 일반 티어도 GAS 를 안 쓰므로 **전 적에 안 걸린다**.
    그건 별도 행(`범용 상태이상 — 속성 피격 → 전 적·보스 공통 디버프`) 소관이고, 착지점은 `DamageType` 이 이미 도달해 있는 `UFPSREnemyHealthComponent::ApplyDamage` 다.
  - **보스 = 여전히 미실현** — `FPSRBossBase.h` 가 *"no ASC/GAS is attached"* 라고 명시. ADR 0013 의 비목표다(문서가 앞서간 상태, `Docs/ProjectStructure_Report.md:38` D1).
- 이동: **Flow-Field 샘플링(고정맵 사전계산, 높이/유계 2층 인지) + 분리(separation)**, 배치 업데이트 (P2). 적 Z로 레이어(서피스 rank) 선택 → 겹친 2층(메자닌) 플레이어를 계단/램프로 추격(U7, 상세 `Performance §5-2`). ⚠️층간 중첩은 **storey급(≥~1층)**으로 저작(storey 미만 근접 2면은 단일 계단면으로 — 레이어 진동 회피)
- 렌더: 거리 LOD — ⚠️ **정정(2026-08-11)**: `USignificanceManager`는 **쓰지 않는다**(플러그인만 켜져 있고 코드 참조 0). 실제 티어링은 **손수 짠 거리밴드**이고 이동 패스와 융합돼 있다. **인스턴싱 컴포넌트(ISM/HISM)는 쓰지 않는다** — 적마다 개별 `UStaticMeshComponent`. 🔁 **결론 갱신 2026-08-13(M0 EC ④ 재대조)**: 이건 *미상환 부채*가 아니라 **채택된 설계**다 — ADR [`0007`](../Architecture/0007-enemy-swarm-render-path-cpd.md)이 대조 실험으로 ISM을 **기각**하고 **MID 폐기 + CustomPrimitiveData**를 채택했다(엔진 동적 인스턴싱이 드로우 병합을 이미 보존하므로, 필요했던 것은 인스턴싱 컴포넌트가 아니라 per-actor MID 제거였다). 실측 = 스웜 렌더 합 **2.05ms@300**(예산 4ms, 여유 2배) · 3.03ms@500 — `Performance.md §5`. ~~(인스턴싱/VAT = `Roadmap.md §8` 별도 트랙)~~ → 트랙은 §7-6 M0 (a″)로 이동해 VAT-1에서 종결됐다. 티어 정의 자체(S0~S3)는 `Performance.md §5-1` 그대로 유효.
- 풀링 필수 — ⚠️ **정정(2026-08-11)**: `UActorPool`이라는 클래스는 **없다**. 풀링은 `UFPSREnemySpawnSubsystem` **안에 인라인**돼 있다(기능은 있고 이름만 없던 것). 계약("적은 Destroy 하지 말고 반납한다")은 그대로.
- 시간 스케일링: `UEnemyScalingProfile` DataAsset — HP/공격력 커브 (이속 불변, 스탯별 슬롯 확장 가능). ⚠️ **미구현 — P5 이연**(`RunFlow.md` 사용자 결정과 동일). 현재 스케일링은 런스케줄 값 주도.
- 🔴 **적 생존치 = 실드 + 체력 2층, 저작처는 프로파일 DataAsset (확정 2026-09-01, VIT1 — 명세 `Docs/Specs/VIT1_ShieldHealthTwoLayer.md`)**
  - 🔁 **정정** — 종전 규약("스웜 적은 `MaxHealth` 를 **에디터 기본값**으로 저작한다", `FPSREnemyHealthComponent.h` 주석)이 **`UFPSRVitalsProfileDataAsset` 로 전면 이관**된다(사용자 결정). 프로파일은 **아키타입 단위 공유**이고, 담는 것 = `MaxHealth` · `MaxShield` · 실드 재생속도 · 부분/완파 이원 지연 · **데미지 타입별 층 계수** · 완화 상한. 프로파일 미할당 = 현행 BP `MaxHealth` 폴백(**이관 중 회귀 0**, 미할당은 검증기가 경고로 리포트).
  - **덱(ADR 0014) 층이 하나 더 있다** — 로스터 DA 의 `VitalsModifier` 가 체력·실드·재생을 **배수**로 민다. ⚠️ **덱은 양(量)만 스케일하고 계수는 안 건드린다** — 계수까지 덱에 두면 같은 몬스터가 덱에 따라 다른 속성 저항을 갖게 되어 플레이어가 학습할 수 없다(뱀서류 리텐션의 핵심은 학습 가능성). 합성 = `프로파일 × 덱배수`, 스폰 시 1회 접어 컴포넌트에 굽는다.
  - **신규 컴포넌트 0** — 실드는 기존 `UFPSREnemyHealthComponent` 의 필드다(제1원리: 적 200~300 을 싸게 = 액터당 UObject 를 늘리지 않는다). 액터당 +약 40바이트, **틱 0**.
  - **재생 = 지연계산(무틱·무복제)** — 맞는 순간에만 `f(경과시간)` 을 한 번 계산한다. 비용이 **피격 횟수**에 비례하고 서 있기만 하는 적은 0 이다. 대가 = 클라가 재생 과정을 못 본다(사용자가 감수하기로 결정). 시계는 플레이어와 **같은** 프리즈-멈춤 전투시계다.
  - **복제 프로퍼티 3 → 5** (`Health`/`MaxHealth`/`bDead` + `Shield`/`MaxShield`). `MaxShield = 0` 인 적은 Push Model 이라 **영원히 dirty 가 안 되므로 대역 0**. ⚠️ 패키지 빌드는 Push Model 이 컴파일 아웃되지만 값이 안 변하면 shadow 비교로 걸러진다(비교 비용만, 대역 0) — **복제 실측은 에디터에서만 유효**.
  - **`AFPSRDestructible`(문·프롭)과 `AFPSRBossBase` 는 같은 컴포넌트를 쓰므로 실드 필드가 자동으로 딸려온다** → 기존 `InitializeMaxHealth` 가 **`MaxShield = 0` 을 명시**해 현행 거동을 그대로 유지한다. 보스에 실드를 주고 싶으면 그건 데이터 플립 한 번이다(이번 유닛의 비목표 — 요구가 "보스는 실드 없이 체력만 많거나"였다).
- **개체별 이속 편차**(확정 2026-05-30): 아키타입 기본속도는 고정, **스폰 시 개체마다 ±10% 무작위 편차** 부여 → 단일 Blob 밀착 방지, 스웜을 입체적·유기적으로 분산해 카이팅 재미. 비용 0(스폰 시 1회 곱셈)
- **원거리 공격 규격**(확정 2026-05-30): 기본 **투사체(Projectile)** 방식(눈으로 보고 회피 가능)으로 강제. 히트스캔 사용 시 **차징 유예 + 사전경고 인디케이터 필수**(부조리 탄막 금지)
- **공격 토큰(Attack Token)**(확정 2026-05-30): 플레이어당 **동시에 공격을 시도할 수 있는 적 개체 수 상한**(서버 권위). 수백 마리 동시 사격/특수공격의 불합리 방지 + §5 "적 공격 판정 서버 배치"의 구현 수단. (토큰 개수 등 수치 튜닝은 밸런스 후속 — 기전=U5 구현 완료, 아래 참조; FF 배율은 §2-10)
  - **구현(U5, 2026-06-30)**: ~~근접/원거리 **이원 토큰**. ① 근접 = `UFPSREnemySpawnSubsystem` per-pass 순간 게이트(`AttackTokenLimit=10`, 매 패스 데미지 적용 적 수 상한).~~ 🔴 **정정(2026-08-26, [ADR 0013](../Architecture/0013-enemy-tier-axis-and-elite-gas.md) 후속 C0, `c1e9dd7f`)**: **①근접 토큰(`AttackTokenLimit`) 회계는 제거됐다** — `f5b0a78d` 이후 실행하는 콘텐츠가 0이던 죽은 코드(위 §2-6 서두 정정 참조). **②원거리 held 토큰은 무변** — 이하 그대로 유효하다. ② 원거리 = **held 토큰**(`RangedAttackTokenLimit=3`, 플레이어당 동시 **차징 중** 적 수 상한; 차징 시작에 획득→발사/중단/teardown에 반납, `TryAcquireRangedToken`/`ReleaseRangedToken`/`IsRangedTokenAvailable`, 키=타겟 PC). held 모델이라야 여러 패스에 걸친 차징의 동시 위협을 제한 + 동시 적탄(복제 투사체)을 자연 상한. 수치는 밸런스 후속.
- **원거리 아키타입(U5, 2026-06-30 — B1)**: ~~`AFPSRRangedEnemyBase : AFPSREnemyBase`~~ → 🔴 **정정(2026-08-26, ADR 0013 C1 `00ed2d9a`)**: **그 클래스는 은퇴했고 아래 사이클은 `AFPSREnemyBase` 자신의 것이다**(전 적이 원거리이므로 "원거리 전용 서브클래스"가 축과 어긋났다 — 위 「분류 축」 참조). 경량 Pawn, **GAS 無**(일반 티어). **사거리 정지 → 차징(예고) → 발사 → 쿨다운** 사이클을 서버 배치 패스에서 구동. ~~베이스에 가상 `ServerTickAttack(FFPSRServerAttackContext)` 시임 추가(근접 거동을 이 override로 이전 — 거동 동일), 원거리는 차징/발사로 override.~~ → `ServerTickAttack` 은 **베이스 본체가 곧 차징/발사**이며 `virtual` 시임은 엘리트·형태 클래스를 위해 유지된다(반환형은 `void` — C0 에서 소비자 소멸). **차징·쿨다운=프리즈-멈춤 누산기**(패스가 §2-2 프리즈에 early-return하므로 `DeltaSeconds` 미가산=자동 정지). 차징 시작/종료에 대상 PC `ClientNotifyRangedTarget`(기존 Client/Reliable RPC)로 사전경고 생산(소비자=§2-14 `ReceiveRangedTarget`, 신규 RPC 0). LOS 차단 시 차징 안 함(트레이스=`ECC_WorldStatic`+`ECC_FPSRPlayerPawn`이라 벽·**닫힌 문 leaf** 너머 헛발사/관통 방지; 문은 단방향 파괴라 발사 후 재차단 없음 → LOS 게이트만으로 관통 완전 차단, Codex 머지게이트 P2 교정). DBNO/!Alive 타겟은 배치 alive 필터로 자동 제외 + 차징 중 타겟 다운 시 Abort+경고해제. 투사체=기존 `AFPSRProjectile` Team=Enemy 재사용(신규 데미지 코드 0). 약점=투사체 임팩트 자동. **콘텐츠**: BP 자식이 `ProjectileClass`·차징/쿨다운/사거리/데미지 등 EditDefaultsOnly 값 저작.
- **데이터 주도 아키타입 혼합(U5, 2026-06-30)**: 신규 `UFPSREnemyRosterDataAsset`(`DA_RunSchedule.EnemyRoster`가 참조, 디렉터 StartRun이 스폰 서브시스템에 push). 폴리모픽 `UFPSREnemySpawnRule`(EditInlineNew, 확장성-우선=중앙 enum/switch 無; MVP=`_Static` class+weight, `FFPSREnemySpawnContext{RunClock,PartyLevel}` 전달로 시간/레벨 스케일 룰 시임 예약). 스폰 서브시스템이 가중랜덤으로 클래스 선택 + **동질 풀을 클래스별 매칭 재사용**(⚠️ 2026-08-26 `61f40fe7` 이후 그 매칭은 선형 스캔이 아니라 **클래스별 버킷 TMap** 이다 — 위 「분류 축」의 버킷화 항목 참조). 빈 로스터=기존 단일 `EnemyClass` 폴백(무회귀).
  🔴 **개정 예고(2026-08-27, [ADR 0014](../Architecture/0014-enemy-deck-difficulty-tier.md)) — 로스터가 「런당 1개 고정」에서 「난이도 등급별 덱」으로 바뀐다.**
  종전은 `StartRun` 이 `DA_RunSchedule.EnemyRoster` 를 한 번 밀어 넣고 런 내내 같은 비율이었다(그래서 **엘리트가 1스테이지부터 같은 확률로 나온다**).
  → **덱 = 이 로스터 자산 그대로**(신규 자료구조 0). `DA_RunSchedule` 이 **등급(1~10)→덱 목록**과 점수 가중치 5개를 들고,
  디렉터가 **스테이지·시간·인원수**를 종합한 점수로 등급을 정해 `덱 = 목록[등급][Hash(런시드, 등급) % N]` 로 고른 뒤 `SetEnemyRoster` 한다.
  ⚠️ **덱은 「비율」만 정한다 — 적 「수」의 소유자는 계속 디렉터**(`ComputeTargetAliveCount`)다. 팀 레벨은 수 축 전용이라 점수에 넣지 않는다.
  ⚠️ **덱 교체가 이미 나온 적을 안 건드리는 것은 `AcquireEnemy` 가 로스터를 매 스폰마다 읽기 때문이다** — 푸시 시점 캐시를 넣으면 그 성질이 조용히 깨진다(ADR 0014 불변식 4).
  ⚠️ **덱 회전이 풀 기아를 부른다** — 등급마다 새 아키타입을 넣으면 `TotalSpawned` 가 캡에 닿아 전환마다 축출이 상시화된다.
  대응 = 감수 + **인접 등급 덱은 클래스 집합을 공유하게 저작**(검증기 경고). 상세·기각안·미해소 반론 = ADR 0014.
- 🔴 **룸 기반 점진 개방 스폰 = 폐기 (사용자 확정 2026-08-16 — [ADR 0010](../Architecture/0010-arena-topology-and-stage-transition.md))**
  아레나가 고정 크기 단일 공간이 되면서 방·개방 개념이 사라졌다. 현행 스폰 = **적격(MinPlayerDistance) 균등 랜덤만**
  (`AFPSRSpawnRoom`·존 활성/비활성 미사용). `AFPSRDoor`는 제거되지 않고 **`AFPSRDestructible`(파괴물 + 폴리모픽 리워드)로
  일반화**되며, 그중 리워드가 스테이지 전환인 것이 **억제기**다(0010 D6·D7).
  ⚠️ **함께 사라진 것 = "60s+ 전후방 2섹터 압력"**(아래 '초반 협동 비트' 불릿). 특수 AI 없이 **스폰 위치만으로**
  back-to-back을 강제하던 USP 부품이다. **160×160 m**(ADR 0011 E1, 80×80 에서 갱신) 아레나에서 균등 랜덤이 자연히 사방을 커버하는지는 **측정하지 않았다** — 면적이 4배가 되며 이 미측정 항목의 위험도 함께 커졌다
  (`판단`) — 성립하지 않으면 스폰 위치 편향(폐루프 디렉터 P1 공간선택)을 후속으로 다시 연다.
  아래 항목은 **경위 보존용**이며 현행 설계가 아니다.
- ~~**룸 기반 점진 개방 스폰**~~(구현 2026-06-25 · 🔴 폐기 2026-08-16): 맵=방(룸) 구성. 벽의 `AFPSRDoor`(파괴 장벽)를 사격해 부수면 통로 개방 → 플레이어가 `AFPSRSpawnRoom`(박스 트리거) 진입 시 그 방의 스폰존이 활성. 활성 존은 **누적**(지나온 방 계속 스폰; 적 총량은 레벨기반(§위)으로 불변, 방 개방은 스폰 **위치**만 추가). 스폰포인트는 방 박스가 BeginPlay에 자동 태깅(`AFPSREnemySpawnPoint.ZoneTag=RoomTag`, 수동 태그는 존중), 선택은 적격(MinPlayerDistance + 존활성) **균등 랜덤**(가중치·거리폴오프 폐지 2026-06-25, **out-of-view(시야 밖) 게이트 폐지 2026-06-29** — 스폰포인트가 플레이어 시야 안에 있어도 스폰 허용. 적이 눈앞에 등장할 수 있으므로 배치는 디자이너가 등 뒤/측면으로 의도; 단일 정면 포인트가 스폰을 굶기던 문제 해소). 시작방=`bActiveAtStart`. 서버 권위(`UFPSREnemySpawnSubsystem.ActiveSpawnZones`/`ActivateSpawnZone`/`ResetSpawnZones`/`DeactivateSpawnZone`; 리셋=OnWorldBeginPlay + StartRun). **룸 비활성화 볼륨**(2026-06-25 추가): 누적이 **기본**이지만, 디자이너가 `AFPSRSpawnRoom.TriggerMode=Deactivate`로 둔 볼륨에 플레이어가 진입하면 대상 존(같은 `RoomTag`)이 꺼진다(`DeactivateSpawnZone`=`ActiveSpawnZones.RemoveTag`, `ActivateSpawnZone`의 대칭; 플랫 RoomTag exact 제거). 즉 누적=기본 동작, 비활성화=레벨 디자이너가 특정 방 스폰을 **명시적으로** 정리(페이싱/슬라이딩)하려 할 때만 작동(Deactivate룸은 자동태깅 안 함=대상 존만 참조, `ResetSpawnZones`는 Activate 시작방만 재활성). 존 상태는 전역 서버권위(활성화와 동일)라 4인 협동에서 1인 진입=전역 토글(분리 파티 주의). 같은태그 대칭(1볼륨=1존). 설계상세 `Docs/Archive/guides/RoomSpawnSystem_Handoff.md`(아카이브).
- **구조형 스폰 + 탈출 경로 (C1, 2026-06-29)**: 오목한 구조물(파이프/박스) 안에서 스폰된 적이 플로우필드로 못 빠져나와 갇히던 문제 해소. `AFPSREnemySpawnPoint`에 ① **`SpawnAnchor`**(자식 Scene, 기본=액터원점) = **실제 스폰 위치**(구조물 BP에선 메시 공동 안으로 이동, 배치 기즈모와 분리) ② **`ExitPathRoot` 자식 웨이포인트**(attach 순서=경로) = 스폰 직후 따라갈 탈출 경로. 적은 웨이포인트를 순서대로 따라 밖으로 나온 뒤 **마지막 점에서 플로우필드 추적 인계**(`ConsumeExitPathSteering`, 도중엔 플로우필드·분리 무시; 스톨 타임아웃 폴백). 콘텐츠 = `BP_StructuredSpawner`(메시+SpawnAnchor+웨이포인트). 단순 스폰포인트는 SpawnAnchor 기본=원점이라 무회귀. **맵 배치별 MovePoint 추가(2026-09-03)**: 위 웨이포인트는 컴포넌트라 BP 안에서만 저작되어 그 BP의 모든 복사본이 같은 경로를 쓴다 — 이를 보완해 `ExitPathPoints`(`TArray<FVector>`, 액터 로컬, `meta=(MakeEditWidget=true)`) 추가. 디테일 패널 `+` 로 늘리고 뷰포트 드래그 핸들로 위치를 잡으며, **레벨 인스턴스 오버라이드로 저장**되므로 BP 기본 1개 + 맵에 놓은 개체마다 추가가 성립한다. 컴포넌트 웨이포인트 **뒤에 append**(대체 아님) → 기존 `BP_StructuredSpawner` 무회귀. 런타임(`SetExitPath`·`ConsumeExitPathSteering`·통과·스톨 타임아웃) 무변경.
- **초반 협동 비트 / 양방향 스폰 압력 (컨설트 2026-07-01 F4 — `Docs/Review/20260701-concept-conclusions.md`)**: 1인칭 협동 대상은 "스페셜/사각 위협"이라(Concept §1-C-3), 스페셜이 없는 초반은 "4솔로"로 회귀 → 방지: 0~60s 개인 손맛, **60s+ 전후방 2섹터 압력**(특수 AI 없이 **스폰 위치만으로** back-to-back 강제), **75~180s 내 첫 원거리/스페셜-lite 1마리**(초반 held ranged token 1/player·전체 ≤2), 5분 전까지 DBNO·사각·원거리 각 1회 경험. **팀 위협 비트 하한**(스케줄 §RunFlow 2-8): 1분 후 30~45s마다, 5분 후 15~25s마다 "팀적으로 봐야 하는 사건" 최소 1회. (밸런스 후속 수치)
- **협동유도 스페셜 적 (컨설트 2026-07-01 F5)** — ℹ️ **정합 확인(2026-08-11)**: 아래 `IsolationScore` 기반 스폰 편향은 **센서만 구현돼 있고 액추에이터(스폰/타겟에 실제로 반영하는 쪽)는 아직 없다**. `RunFlow.md`의 디렉터 구현 상태와 일치하며, 이 줄은 어긋난 게 아니라 **읽는 사람이 "이미 편향이 걸려 있다"고 오해하지 않도록** 남기는 기록이다. 원칙1(적별 StateTree/개별 AI/NavMesh 금지) 준수 = **개별 지능 아니라 데이터드리븐 타겟팅 룰 + 스폰 위치 + 강한 연출**로 "1인 격리·플랭킹" 체감. `IsolationScore`(최근접 아군거리 + 시야밖 + 저최근구조)로 **고립 플레이어 측후방 스폰 가중 + 타겟 편향**(0.5~1.0s 저빈도 갱신). 공격=기존 원거리 차징/토큰/사전경고(`ClientNotifyRangedTarget`) 재사용, 연출=고유 사운드·색 투사체·피해자 HUD 마커·구출 마커. **MVP = 기존 원거리 베이스 재색칠 1종**(신규 아트 0에서 검증). 후보: Leasher(둔화/끌림)·Pinger(고립자 차징)·Splitter(파티 사이 스폰)·Screamer(주변 aggro 편향). 적 다양성 공백(평가)도 동시 해소.
- **폐루프 디렉터 연동 (설계 채택 2026-07-20 · 미구현 · 상세 `Docs/Review/20260720-plan-closed-loop-director.md`)**: 스폰포인트 선택에 **MAX거리 게이트 + 고립 소프트가중**(⚠️회귀규약: 넓은 적격집합 유지 = 단일포인트 스폰굶김 방지, 하드게이트 금지) · **스폰포인트 per-biome 로스터**(문개방/맵확장에 디렉터 무변경으로 대응) · 구성 = 로스터룰 컨텍스트 확장(비미션 캠핑/좁은초크 사수 → 원거리↑[기존]·방패/공중↑[신규]). `UEnemyScalingProfile`(적 진폭)·성격프로파일 = P5 이연. `IsolationScore`(F5) = 센서 P0b 구현.
- **공중 아키타입 = 엘리베이티드 히트박스(비행 아님, P4)** — ⚠️ **미구현(설계만, 2026-08-11 확인)**. ~~실재하는 적 아키타입은 **근접·원거리 2종뿐**이다~~ → ⚠️ **정정(2026-08-26, ADR 0013)**: **근거리는 폐지됐다**(위 「공격 타입」 항목). 실재하는 아키타입은 **원거리 2종**(쌍뿔·원자큐브)이며, 분류 축은 이제 **티어(일반/엘리트)**다. 이하 설계 내용은 무변: 일반 몹처럼 **지상 이동**(루트=지상, 이동코드 0) + 시각메시 상방 오프셋 + **올린 콜라이더 필수**(`UFPSRWeakpointComponent` mult=1.0; 시각메시는 `NoCollision`이라 "메시만 올리면" 안 맞음). 효과 = 상방 조준 강제. (Track2 감사 = **설계 정합성** 확인이지 구현 확인이 아니다)
- **방패 아키타입 = 방향성 아머(P4)** — ⚠️ **미구현(설계만, 2026-08-11 확인 — `UFPSRShieldComponent` 코드 참조 0)**: `FPSRCombat::ResolveDamage` 1지점 + 신규 `UFPSRShieldComponent`(bearing = Origin/Instigator, 배선0, 전 5경로 자동커버). **정면 = 90~95% 강DR(잔여>0)** — ⚠️하드블록(0뎀) 금지(hitscan 관통통과 + DamageDealt=0으로 히트마커/라이프스틸/킬크레딧 침묵). 측/후면 무방비. 정면 아크에 weakpoint 금지(상쇄). AOE near-zero bearing 가드. (Track2 감사 = **설계 정합성** 확인이지 구현 확인이 아니다)
- **파괴 장벽(`AFPSRDoor`) = 비-적 데미지 대상**: `UFPSREnemyHealthComponent`를 가져 **데미지 브릿지로 전 무기 경로 자동 피격**(신규 데미지 코드 0). 콜리전 오브젝트타입 = `ECC_FPSRPlayerPawn`(플레이어·적 모두 차단 + 대시로 통과 불가 + 모든 무기 오브젝트쿼리에 포함). **`bCountsAsKill=false`** → 부숴도 킬 크레딧·on-kill 프래그먼트·흡혈(on-damage GAS 이벤트) 미발동(데미지/`DamageDealt`/파괴는 정상; `FPSRCombat::ApplyDamage` 게이트). 메시는 BP 지정(C++ 하드코딩 금지), 파괴 연출=`OnDoorBroken`(BlueprintImplementableEvent). 문틀은 `FrameMesh`(WorldStatic=무기쿼리 비대상=무반응 벽). XP는 `AFPSREnemyBase::HandleDeath` 전용이라 문은 자동 0.

### 2-10. 발사체 / 네트워크
- 발사체: **서버권위 spawn·복제**(결정적 PMC 이동; 클라 cosmetic 예측 = 후속·미구현, A3)
  - **차지레이저 = 히트스캔, 근접 = 구체 오버랩, 그 외 전 플레이어 무기 = 복제 발사체**(무기 전면 투사체화 2026-07-08 `3adc945`; 샷건=펠릿 다발, 스나이퍼=관통 탄환, 바주카=AOE, 연사총 포함. 유탄런처 제거). 발사체 예산·팀별 캡·회수 = `Performance.md §5`.
- 데미지: **플레이어 GAS 계산 → 적 HealthComponent.ApplyDamage 브릿지** ~~(적 ASC 없음)~~
  ⚠️ **정정(2026-08-26, ADR 0013)**: **엘리트는 ASC 를 갖는다.** 다만 **엘리트의 체력이 어디에 사는지는 미결정**이다 —
  (가) 엘리트도 `HealthComponent` 유지 + ASC 는 어빌리티/GE 전용(이 브릿지 무개정) / (나) ASC 어트리뷰트 체력(**전 무기 데미지 경로 재배선**).
  ADR 0013 은 (가)를 기본값으로 권하되 강제하지 않으며, 후속 행 「엘리트 ASC 부착」의 첫 결정이다. **일반 티어는 무변**(ASC 없음).
  - 🔴 **2층화 (2026-09-01, VIT1)** — 이 브릿지가 이제 **실드 → 체력 2층**을 통과한다. 배분·계수·이월·파손 판정은 **상태 없는 순수 함수 `FPSRVitals::ApplyDamage` 하나**가 하고, 플레이어(GAS 어트리뷰트)와 적(컴포넌트 필드)이 **같은 함수를 공유**한다 — 저장소만 둘이고 규칙은 하나다. 브릿지 구조·경로 수는 무변.
  - 🔴 **`FDamageResult::DamageDealt` 재정의** — 종전 `HealthBefore - HealthAfter` → **`실드소진 + 체력소진`**. 안 고치면 실드가 흡수한 타격이 `DamageDealt = 0` 이 되어 **히트마커·흡혈·관통 판정이 조용히 침묵**한다. 같은 이유로 `AFPSRCharacter::ApplyContactDamage` 도 결과를 반환하게 된다(종전에는 플레이어 분기가 `DamageDealt` 를 0 으로 남겼다).
  - **데미지 시그니처** — trailing `FGameplayTag DamageType`(U18a 시임)이 **`FFPSRDamageSpec` 구조체**로 바뀐다(현재 = `DamageType` + 무기별 대실드 계수). 기본값이 현행 거동이라 호출자 파급은 약 5줄이고, **자매 행(경량 적 상태이상 · 범용 상태이상)이 시그니처를 다시 안 건드리고 필드만 추가**하면 되도록 연 자리다.
  - **상태이상과의 상호작용 규칙(여기서 정하고 그쪽이 따른다)**: ①"Shock 가 실드를 2배로 깎는다" = 프로파일의 `DefenseByDamageType[Lightning].ShieldDefense = 2.0` — **신규 기제 0** ②"실드 있으면 상태이상 저항" = `GetShield()`/`IsShieldBroken()` 질의 ③부여 판정은 `FPSRVitals::FResult` 를 본다(실드에만 막힌 타격 vs 체력까지 들어간 타격을 구분할 수 있다) ④**저장소는 합치지 않는다**(실드 = 복제되는 float 2개 / 상태이상 = 비트+타이머, 복제 정책이 다르다).
  - ⚠️ **M4 「방패 아키타피」(방향성 아머)와는 직교 축**이다. 예약어 `UFPSRShieldComponent` 는 **그쪽 것**이고 VIT1 은 쓰지 않는다. 결합 규칙 = 아머 DR 과 층 계수는 곱해지되 **완화 상한(저작 상한 0.99)으로 클램프**되어, `실드 계수 × 아머 DR = 무적` 이 산술적으로 불가능하다(= 방패 아키타입이 요구한 "하드블록 금지"의 이행).
- **아군 오사(Friendly Fire)** — 🔄 **기본 OFF 확정(사용자 결정 2026-08-07)**. 이 결정은 **2026-07-01의 "기본 ON = 코어 협동 정체성" 설계를 뒤집은 것**이다(Concept §1-C-6 동반 정정). 코드 기본값 `AFPSRGameState::bFriendlyFireEnabled = false`가 **이제 설계와 일치**하며, 종전 여기 적혀 있던 "⚠️ 코드 후속 = 기본값 flip 필요"는 **철회**한다.
  - **기능 자체는 그대로 있다** — 호스트가 세션 설정에서 **ON 토글** 가능(`SetFriendlyFireEnabled` / 디버그 `FPSR.SetFriendlyFire`). 켰을 때의 규격은 종전 그대로: **치사(lethal)** · 아군 적중 = **몬스터 데미지의 50%**(`FriendlyFireDamageScale=0.5f`, 크리 없음, **아군 다운(HP 0)까지 가능·HP 클램프 없음**).
  - 데미지 브릿지 팀/FF 배율 = `FPSRCombatStatics`. 자폭(폭발)·넉백은 FF 플래그와 **독립**(플래그를 꺼도 그대로).
  - **협동 카드 시임**: FF 데미지를 아군 회복으로 전환하는 카드 등(CombatWeaponCard, 콘텐츠) — FF를 켠 세션에서만 의미가 있다.
  - (원 설계 근거 보존) 기본 ON 안의 논거는 "사선 관리가 코어 협동 긴장·실력 축 / 공개 매칭 미고려(솔로+친구 협동)라 트롤 안전장치 불요"였다. 뒤집혔지만 **ON 토글의 설계 의도는 여전히 이것**이다.
- 토폴로지: **리슨서버 P2P** (Steam Sockets/EOS 세션, P5에서 구축)
- **개발 방법론: P1부터 Net-aware (서버 권위 + Push Model), PIE 2-client 상시 검증.** 솔로로 만들고 나중에 리플리케이션 retrofit 금지
- GAS ASC Replication Mode — ⚠️ **정정(2026-08-27)**: 이 줄은 **플레이어 ASC** 이야기이고, 코드는 솔로/협동 구분 없이 **무조건 `Mixed`** 다(`FPSRPlayerState.cpp:22`).
  **엘리트 ASC 는 별개로 `Minimal`**(오너 클라가 없다 — 위 §2-6 「GAS」 참조).
- Iris: **OFF (디폴트는 Push Model)**. P5에서 평가용으로만 검토 (Beta 리스크)
