# Game.MD — FPSRoguelite 단일 진실 공급원 (SSOT)

> **이 문서는 프로젝트의 단일 진실 공급원(Single Source of Truth)이다.** 기획·설계·기술스택·코드구조·작업규칙·구현현황·로드맵을 모두 담는다.
> 작업 시작 전 **이 문서와 `PROGRESS.md`(라이브 진행현황) 2개**를 먼저 읽는다. 설계 변경은 **이 문서를 먼저 갱신**한 뒤 코드/에셋을 수정한다.
> 엔진: **UE 5.7** (`D:\UnrealEngine\UE_5.7`) / 최종 확정일 기준: 2026-05-30 / 설계 잠금 버전: v6(Locked)

---

## 0. 문서 체계 (어디에 무엇이 있는가)

| 문서 | 역할 | 갱신 주체 |
|---|---|---|
| **Game.MD** (이 문서) | 기획+설계+기술+코드구조+규칙+현황 = SSOT 본문 | 우리(세션) |
| **PROGRESS.md** | 휘발성 진행현황·핸드오프(완료/진행중/다음순서/검증법) | 우리(세션) |
| `CLAUDE.md` / `AGENTS.md` | 진입 포인터(≤10줄). "Game.MD·PROGRESS.md 읽기" + 절대금지 3줄 | 거의 불변 |
| `DESIGN.md` / `AI_DESIGN_HANDOFF.md` | **stub** (Game.MD로 이전됨) | — |
| `GameConfirm.MD` | **다른 AI가 작성**하는 리뷰/추가제안 문서. 우리는 만들지 않음 | 외부 AI |

→ **AI가 읽는 본문 = Game.MD + PROGRESS.md 2개.**

---

## 1. ⚠️ 장르 정체성 — 가장 먼저 읽을 것 (안티-편향)

이 프로젝트는 **1인칭 FPS × 뱀파이어 서바이벌 × 4인 협동 로그라이트**다.
레퍼런스: **The Spell Brigade (Steam)**.

**이것은 Hero Shooter(Overwatch/Valorant/Apex)가 아니다.** 적은 수백 마리(~300-500)의 싼 액터다.
과거 참조 PDF 커리큘럼(`HeroShooter_Portfolio_Curriculum.pdf`)은 PvP Hero Shooter 기준이라 그 기술 스택을 **그대로 가져오면 안 된다.**

| | Hero Shooter (가져오면 안 됨) | **이 게임 (Survivor Swarm)** |
|---|---|---|
| 액터 수 | ~12명 | **적 수백 (~300-500)** |
| 액터당 비용 | 높아도 OK | **반드시 최소화** |
| AI | 개별 스마트 (StateTree/BT) | **단순 추격 스티어링 + Flow-Field, 배치** |
| 길찾기 | 에이전트별 NavMesh | **Flow-Field (고정맵 사전계산)** |
| GAS | 모든 액터 | **플레이어 + 보스/엘리트 한정** |
| 적 애니메이션 | 풀 피델리티 | **인스턴싱/VAT + LOD** |
| 리플리케이션 | 플레이어별 고정밀 | **적은 최소상태/Push Model** |

**임의로 도입 금지** (Hero Shooter 편향):
- ❌ Lyra 풀 fork → 경량 커스텀 모듈 + 엔진 플러그인 체리픽
- ❌ 모든 적에 GAS/ASC → 적은 경량 `UHealthComponent` + 비-GE 데미지
- ❌ 적별 StateTree/BehaviorTree + NavMesh 길찾기 → Flow-Field + 스티어링, 배치 처리
- ❌ Iris 핵심 의존 → 디폴트는 Push Model, Iris는 P5 평가용·OFF
- ❌ MassEntity 선도입 → 목표 규모 ~500은 풀액터로 충분
- ❌ Server-Side Rewind, Motion Matching, Bhop/Wall-run, PCG, True First Person 풀바디

**GAS는 플레이어(1~4)와 보스/엘리트(소수)에만. 스웜 적에는 절대 붙이지 않는다.**

---

## 2. 게임 기획 (확정)

### 2-1. 핵심 게임 루프
```
로비 (메타 강화) → 런(생존 + 레벨업 프리즈 + 시간대별 미션) → 마지막 보스 처치/클리어 → 재화 획득 → 로비
```
- **고정 맵 / 고정 구조** (절차적 생성 PCG 사용 안 함)
- **런 구조 = 생존형 (확정 2026-06-04, P4-A 재설계)**: 라운드제 **폐지**. 런은 끊김 없이 진행되며, **레벨업 시에만 게임이 전역 프리즈**되어 카드를 고른다(§2-2). 시간이 지나면 **미션이 시간대별로 독립 출현**(§2-8)하고, **마지막에 보스 출현 → 이후 시간 무제한·미션 없음, 보스만 처치 = 런 클리어**.
  - **프로덕션 기본**: 보스까지 약 **20분**, 미션 시각·보스 시각은 데이터 드리븐(`UFPSRRunScheduleDataAsset`, 에디터 설정값). 데모/테스트용 압축 스케줄(예: 미션 60/120/180s·보스 300s)을 데이터 교체만으로 운용.
- 시간 경과 → 몬스터 수·종류·HP·공격력 증가 (**시간 스케일링상 이동속도 불변**; 단 스폰 시 개체별 ±10% 이속 편차는 허용 — §2-6)
- **1~4인 협동** (리슨서버 P2P)

### 2-2. XP / 레벨업 / 카드 소비 흐름 (재설계 2026-06-04 — 레벨업 전역 프리즈)
- XP는 몬스터 사망 시 **드롭 아이템(범용 Pickup)** → 근접 시 자석 흡수 줍기
- **파티 공유 경험치 풀** — 누가 줍든 하나의 통에 누적, 모두 같은 하단 XP바 UI 공유
- **레벨업 시 전역 프리즈 (확정 2026-06-04, "예전 방식" 복귀 — 기존 "교전 중 프리즈 폐지"를 되돌림)**: 공유 풀이 레벨업 임계 도달 시 **그 즉시 게임이 전역 프리즈**된다 — **적·플레이어 모두 정지**(적은 제자리 동결, 스폰 정지, 플레이어 이동·발사 차단). 연결된 **전원이 본인 카드를 모두 고르면 재개**된다.
  - 프리즈는 전역 `bRunPaused`(서버 권위, `AFPSRGameState`, Replicated)로 표현. **전역 `TimeDilation=0` 미사용** → 스폰·적 이동/공격·플레이어 입력을 **상태로 게이트**(타이머·AbilityTask·RPC·이펙트 부작용 회피). UI·RPC는 프리즈 중 정상 tick.
- **레벨업 스택 = 플레이어별 보류 픽**: `SharedXP`/`PartyLevel`은 파티 공유지만, **파티가 1레벨 오를 때마다 연결된 모든 플레이어에게 카드 픽 1개 부여**(`AFPSRPlayerState::CardPicksPending`, Replicated). 다중 레벨업 = 픽 누적 → 프리즈 동안 본인 픽 수만큼 연속 선택(플레이어별 독립, 한 명의 선택이 남의 픽을 소모하지 않음). 늦게 합류한 플레이어는 합류 후 레벨업부터 픽 부여.
- **재개 = 전원 보류 픽 0**(서버 `RefreshPauseState`): 모든 연결 플레이어의 보류 픽(레벨업+미션보상)이 0이 되면 `bRunPaused=false` → 즉시 재개. (별도 준비 입력 없음. AFK 시 전원이 길게 멈추는 점은 감수 — 상태표시는 P7 UX)
- **오프닝 카드 시드**: 런 시작 시 **2장을 즉시 선택**(초반 빌드 시드). 이것도 동일 프리즈 모델(시작 시 프리즈 → 선택 → 재개).
- **미션 보상 프리즈**: 시간대별 미션(§2-8)을 **클리어하면 즉시 프리즈**되어 **무기 모디파이어 보상 카드**(§2-4-1)를 전원이 선택 → 재개. (레벨업 프리즈와 동일 메커니즘, 다른 카드 풀)
- **보스전 중에도 프리즈 동작**: 보스 등장 후 시간제한·미션은 없지만, XP 획득으로 레벨업하면 동일하게 전역 프리즈 후 카드 선택(`bRunPaused`는 페이즈와 독립).

### 2-3. 카드 시스템
- 데이터 방식: **DataAsset + GameplayEffect(GE)** — 스탯 하드코딩 금지
- 카드 확장 비용:
  - **기존 Attribute 범위 내 새 카드 = GE + DataAsset 추가 (코드 변경 0)**
  - **완전히 새로운 Attribute = C++ AttributeSet 확장 + GE + DataAsset (코드 변경 필요)**
  - → AttributeSet 설계 시 스탯 축을 넉넉히 미리 확보할 것
- `UCardDataAsset`: `Scope`(Character / ThisWeapon / AllWeapons), `AppliedEffect`(GE), `Weight`, **`RarityTiers[]`**, **`CardFamily`** (P3-C 확정 2026-06-02)
  - **`RarityTiers[]`(`FFPSRCardRarityTier`{Rarity, Magnitude})**: 카드 1개가 **여러 등급에서 각기 다른 수치로** 등장. 추첨이 등급을 굴려(등급 기본가중치×Luck) 해당 티어의 `Magnitude`를 적용. **"모든 등급에서 나오는 카드 = 1 에셋"**(등급당 1티어). 1티어만 두면 단일 등급 카드. → 등급별 별도 에셋 불필요(콘텐츠 폭증 방지)
  - **수치 주입=`SetByCaller`**: GE 모디파이어를 `SetByCaller`(태그 `SetByCaller.CardMagnitude`)로 작성 → `ApplyCard`가 굴린 티어의 `Magnitude`를 주입. 고정 수치 GE(SetByCaller 미사용)에도 무해(무시)
  - **`CardFamily`(GameplayTag, 선택)**: 같은 family 카드는 **한 추첨에 1장만 제안**(상호배타). 미설정 시 `AppliedEffect` GE 클래스를 family 키로 사용 → 단일 GE 방식이면 자동 그룹핑(같은 카드의 다른 티어도 한 번에 1개만)
  - **데이터 검증**: `UFPSRCardDataAsset::IsDataValid`(WITH_EDITOR) — RarityTiers 비면 에러(추첨 누락 방지), AppliedEffect 없으면 경고. 런타임에도 빈 티어 카드는 경고 로그(무음 실패 금지)
- **`Scope` 의미 (확정 2026-06-04)**:
  - **`Character`** — 캐릭터 ASC 글로벌 속성(MaxHealth/Luck/이속/Crit 등). Character-scope GE를 ASC에 적용(현재 구현).
  - **`AllWeapons`** — **들고 있는 모든 무기**에 적용되는 무기 스탯/모디파이어("캐릭터 종속" 느낌). 수치 **작게**.
  - **`ThisWeapon`** — **현재(대상) 무기 1정**에만 적용. **같은 효과를 더 큰 수치**로(집중 강화).
  - → 같은 효과(예: 연사속도↑)를 **AllWeapons(소·전체)** 와 **ThisWeapon(대·단일)** 두 카드로 제공 = "넓고 얕게 vs 좁고 깊게" 빌드 선택. (구현은 무기 모디파이어 시스템 §2-4-1, P4-B)
- **등급 4단계** (Common/Rare/Epic/Legendary) — **Luck** 스탯이 추첨 가중치(상위 등급 확률)에 작용. (※ RarityBonus는 Luck으로 통합·폐지 — 2026-06-02. 광역 행운 1개 축으로 단순화; 향후 드랍품질·희귀스폰 등도 Luck이 담당)
- **무기별 전용 카드**: 무기 보유 시 해당 무기 카드(ThisWeapon)가 레벨업 카드 풀에 동적 합류 (Gunfire Reborn식)
- **리롤**: 캐릭터 메타로 해금, **게임당 3회 제한** (`RunRerollCharges`, 서버 권위 차감)
- **카드 소비 시점**(§2-2): 오프닝 시드(런 시작 2장) + **레벨업 전역 프리즈**(레벨업 스택 소비) + **미션 클리어 프리즈**(무기 모디파이어 보상). 모두 게임이 멈춘 프리즈 중 선택

### 2-4. 무기 시스템
- 최대 **3개 동시 보유** (기본 1 + 추가 2). 5렙/20렙 등에 무기 카드 등장
- **무기 버림·교체 시스템 없음**(확정 2026-05-30) — 기존 무기로 시작해 슬롯 확장으로만 추가(최대 3). 환불/카드치환 보완 불필요
- **아키타입 7종**: 연사총(FullAuto) / 점사총(Burst) / AOE(바주카·유탄) / 근접(Melee) / 차징 관통 레이저(ChargeLaser) / 단발 스나이퍼(Sniper) / 샷건(Shotgun)
- **무기별 스탯 = WeaponInstance 스탯 블록(리플리케이트 struct, `FFPSRWeaponStatBlock`)**, 캐릭터 ASC와 분리
  - 스탯 예: Damage, FireMode, FireRate, BurstCount, Range, MagSize, Spread/Bloom, Recoil, MeleeRadius (+ 후속: ReloadTime, ProjectileSpeed, Pierce, AOERadius, ChargeTime, PelletCount). **예비 탄약은 무한**(ReserveAmmo 미사용 — §2-4-2)
- **ADS**: 무기 DA에 `bHasADS`+FOV (스나이퍼/차징=정밀, 연사/샷건=약함, 근접=없음)
- **무기 교체 입력**: 숫자키 **1/2/3** 직접 슬롯 선택 (`IA_EquipSlot1/2/3`, 마우스휠 미사용)

#### 2-4-1. 무기 모디파이어 = 게임의 핵심 재미
무기 모디파이어 = **① 스탯 모디파이어(수치 조정)** + **② 행동 Fragment(동작 변경)** 두 종류. 둘 다 런타임 `UWeaponInstance`(무기별 모디파이어 컨테이너, P4-B 신설)에 누적되며, 스탯 해석 = `FFPSRWeaponStatBlock` 베이스 × 누적 모디파이어(ThisWeapon + 캐릭터의 AllWeapons).

**① 스탯 모디파이어 카드 (확정 2026-06-04 — 레벨업 카드 풀)**
- 예: **탄창 용량↑ / 연사 속도↑ / 반동↓** (그 외 데미지/확산/재장전속도 등 `FFPSRWeaponStatBlock` 축 확장 가능).
- **두 스코프, 같은 효과 다른 수치**(§2-3): **`AllWeapons`**(들고 있는 모든 무기, 수치 **작게**) vs **`ThisWeapon`**(현재 무기 1정, **큰 수치**). 둘 다 레벨업 프리즈(§2-2) 카드 풀에 합류(무기 보유 시).
- 데이터: 카드 `Scope`로 적용 범위, `RarityTiers[].Magnitude`로 수치(SetByCaller 동일 패턴). 무기 스탯 모디파이어는 GE가 아닌 **WeaponInstance 모디파이어로 적용**(무기 스탯은 ASC 밖이므로) — `ApplyCard`에서 weapon-scope 분기로 처리(P4-B).

**② 행동 Fragment (미션 보상 = 동작 근본 변경)**
- 미션 보상으로 무기 동작을 **근본 변경** (2연발, 차징 무효, 아군 힐 빔 등)
- 적용 방식: **Weapon Behavior Fragment (합성형 훅) + 누적 가능**
  - `UWeaponInstance.ActiveModifiers[]`에 누적, 서로 상호작용 (예: 2연발+관통)
  - `GA_WeaponFire`(아키타입별 베이스)에 훅: `PreFire → ModifyShotCount → ModifyChargeTime → OnProjectileSpawn → OnHitActor → PostFire`
  - 각 무기 DA가 `AvailableModifiers`(약 4종) 정의 → **미션 클리어 시 즉시 프리즈(§2-2)에 모디파이어 해금/변경 카드로 1종 선택**
- **GA 교체 방식 금지**(조합 폭발), **거대 태그 분기 금지**(유지보수 지옥)
- ⚠️ **성능**: `OnHitActor`가 500마리 타격 시 과도한 virtual dispatch/heap alloc 금지 → 훅은 경량(데이터 기반·히트당 무할당)
- **구현 확정 (P4-B-2, 2026-06-08)**:
  - **Fragment = 무상태 `UFPSRWeaponFragment : UPrimaryDataAsset`** — 동작은 C++ 서브클래스(virtual 훅), 수치는 authored DataAsset. (Instanced/runtime-struct 대신 공유 무상태 asset → 복제·메모리 비용 최소). 레퍼런스: `UFPSRFragment_MultiShot{ExtraShots}`(ModifyShotCount), `UFPSRFragment_OnHitBonusDamage{BonusDamage}`(OnHitActor).
  - **누적·중첩**: `UFPSRWeaponInstance.ActiveFragments[]`(복제 참조)에 누적. **`UFPSRWeaponFragment.MaxStacks`**(기본 1)만큼 같은 fragment 중복 누적 → 훅이 스택마다 적용(예: MultiShot 2스택=+2발). 미션보상 오퍼는 `StackCount < MaxStacks`인 동안 계속 제시(서버 권위 dedup).
  - **MultiShot 탄약**: 멀티샷은 **발사 펠릿 수만큼 탄창 소모**(`ConsumeAmmo(NumShots)`), 잔량 부족 시 **잔량까지 클램프 후 발사**(최소 1발 보장). NumShots는 fragment 훅 계산 후 소모.
  - **미션보상 카드 UI**(`UFPSRCardEntryWidget`): fragment 카드(=`Scope==ThisWeapon && GrantedFragment`)는 등급 대신 **카테고리 라벨**(위젯 기본값 `FragmentCategoryText`, WBP override) 표시 + **수치 슬롯 빈칸**(behavior 해금이라 magnitude 무의미).
  - 적용 위치=서버 권위(`ApplyCard` ThisWeapon 분기 `AddFragment`), 클라 예측=GA `ModifyShotCount` 루프(복제된 ActiveFragments로 N샷 예측). 런 종료 소멸=WeaponInstance 수명. **후속**: ModifyChargeTime/OnProjectileSpawn 훅(차징/투사체 아키타입), Melee fragment, fragment 제거/교체.

#### 2-4-2. 사격 메커니즘 / 슈팅 감각 (데이터 드리븐, FPS 핵심)
> 슈팅 감각은 FPS의 핵심. 모든 값은 `FFPSRWeaponStatBlock`(또는 분리된 `FWeaponFireProfile`)에 두어 **무기별·카드별 조정 가능**하게.

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
- 3종: **연사총 / 점사총 / 근접칼**
- `UHeroDataAsset`(PawnData 개념) Base 구조 — DefaultWeapon, BaseAttribute, PassiveAbilitySet, FP팔/3P바디 메시
- 데이터 드리븐. 고유 패시브가 무거워지면 그때만 GameFeature 플러그인으로 승격

### 2-6. 몬스터 (스웜 적)
- 공격 타입 **근거리 / 원거리 / 특수 중 정확히 1개 고정** (상황 따라 전환 안 함)
- **GAS 미사용** — 경량 `UHealthComponent`(`UFPSREnemyHealthComponent`) + 비-GE 데미지 적용
- 이동: **Flow-Field 샘플링(고정맵 사전계산) + 분리(separation)**, 배치 업데이트 (P2)
- 렌더: 인스턴싱/VAT + 거리 LOD (Significance Manager)
- 풀링 필수 (`UActorPool`)
- 시간 스케일링: `UEnemyScalingProfile` DataAsset — HP/공격력 커브 (이속 불변, 스탯별 슬롯 확장 가능)
- **개체별 이속 편차**(확정 2026-05-30): 아키타입 기본속도는 고정, **스폰 시 개체마다 ±10% 무작위 편차** 부여 → 단일 Blob 밀착 방지, 스웜을 입체적·유기적으로 분산해 카이팅 재미. 비용 0(스폰 시 1회 곱셈)
- **원거리 공격 규격**(확정 2026-05-30): 기본 **투사체(Projectile)** 방식(눈으로 보고 회피 가능)으로 강제. 히트스캔 사용 시 **차징 유예 + 사전경고 인디케이터 필수**(부조리 탄막 금지)
- **공격 토큰(Attack Token)**(확정 2026-05-30): 플레이어당 **동시에 공격을 시도할 수 있는 적 개체 수 상한**(서버 권위). 수백 마리 동시 사격/특수공격의 불합리 방지 + §5 "적 공격 판정 서버 배치"의 구현 수단. (토큰 개수·FF 기본 10% 등 수치는 밸런스 후속)

### 2-7. 보스
- **Base 구조**: `ABossBase` + `UBossDefinitionDataAsset` + StateTree (소수라 GAS/StateTree OK)
- **출현 = `BossTime` 도달 시**(프로덕션 ~20분, 스케줄 DA, §2-1·§2-8). 보스전부터 **시간 무제한·미션 없음**, 처치 = 클리어. 2페이즈. 스케줄에 보스 시각·정의 추가로 확장. (단 보스 중에도 레벨업 프리즈는 동작 §2-2)
- 구현 상태: 보스 실물은 P6. P4-A는 보스 게이트(`ERunPhase::Boss` 전환 + 훅)만 스텁

### 2-8. 런 스케줄 / 미션 (재설계 2026-06-04, P4-A — 시간 기반)
- **런 스케줄 = `UFPSRRunScheduleDataAsset`**(서버 권위, **전부 에디터 설정값**): **시간 기반 미션 이벤트** `TArray<FFPSRMissionEvent>{ float TriggerTime, UFPSRMissionDataAsset* Mission }` + `float BossTime` + 스폰 강도 스케일링(시간경과 `UCurveFloat` 또는 base+rate). 라운드 개념 없음.
- **런 디렉터 = `UFPSRRunDirectorSubsystem`**(서버 권위): 런클럭 누적(프리즈·보스 중 정지) → 미션 이벤트 시각 도달 시 미션 스폰, `BossTime` 도달 시 보스 진입. 스폰 강도를 시간에 따라 구동.
- **미션 = 시간대별 독립 출현**(확정 2026-06-04):
  - 스케줄의 각 이벤트 시각에 **1개** 출현, **선택적**(미클리어 무방). 이전 미션 종료 후 다음(동시 다중 없음).
  - **클리어 시 즉시 전역 프리즈**(§2-2) → 무기 모디파이어 **보상 카드를 전원 선택** → 재개.
  - **확장형 프레임워크**: `AFPSRMissionActor`(서버권위 리플리케이트 Actor 베이스) 서브클래스 + `UFPSRMissionDataAsset`(메타+로직클래스+보상카드) + **디자이너 배치 `AFPSRMissionSpawnPoint`**(태그매칭+가중랜덤). 디렉터가 스폰·생명주기 관리.
  - **미션 카탈로그**(기획, 프레임워크로 수용): 좁은구역 N초 버티기 / 이동 구역 점령 / 도망치는 고체력 몹 처치 / 점프맵 오브 획득 / 피격 없이 구체 N초 소지 / 시야 극제한 버티기 / 전원 제자리 정지 등. 각 종류 = `AFPSRMission_*` 서브클래스(필요 인프라는 서브클래스+콘텐츠로 격리, 코어 불변).
- 난이도 곡선은 `UCurveFloat`로 분리(후속)
- **구현 상태(P4-A)**: 디렉터(시간 미션+보스타임) + 미션 프레임워크 + 레퍼런스 미션 1종(`AFPSRMission_HoldZone`) + 스폰포인트 + 레벨업/미션보상 전역 프리즈 + 오프닝시드. 미션 보상의 **무기 모디파이어 실적용(weapon-scope `ApplyCard`)은 P4-B**(P4-A는 프리즈+선택 흐름까지). 나머지 6종 미션·보스 실물은 후속.
- **테스트 스케줄**: DataAsset 교체로 압축(예: 미션 60/120/180s·보스 300s). ⚠️ 압축값은 임시(프로덕션 전환 시 원복).

### 2-9. 카메라 / 메시
- **Separated Arms**: 본인 = FP팔(`OnlyOwnerSee`) + 무기 / 타인 = 3P 캐릭터(`OwnerNoSee`)
- True First Person 풀바디 렌더링은 사용 안 함 (가독성/속도감 우선)

### 2-10. 발사체 / 네트워크
- 발사체: **클라 예측 + 서버 검증**
  - 스나이퍼/레이저 = 히트스캔, 바주카/유탄 = 실제 발사체(소수), 샷건 = 다중 히트스캔, 연사 = 히트스캔/경량
- 데미지: **플레이어 GAS 계산 → 적 HealthComponent.ApplyDamage 브릿지** (적 ASC 없음)
- **아군 오사(Friendly Fire)**(확정 2026-05-30): 판정은 존재하나 아군 적중 시 **몬스터 데미지의 10%만 적용**(기본값, 불쾌감 완화). 호스트가 세션 설정에서 **FF 적용 토글** 가능. 데미지 브릿지에 팀/FF 배율 경로를 둠(구현 P5)
- 토폴로지: **리슨서버 P2P** (Steam Sockets/EOS 세션, P5에서 구축)
- **개발 방법론: P1부터 Net-aware (서버 권위 + Push Model), PIE 2-client 상시 검증.** 솔로로 만들고 나중에 리플리케이션 retrofit 금지
- GAS ASC Replication Mode: 솔로 = Minimal, 협동 = Mixed
- Iris: **OFF (디폴트는 Push Model)**. P5에서 평가용으로만 검토 (Beta 리스크)

### 2-11. 메타 프로그레션
- `URogueliteSaveGame`(USaveGame) — 누적 재화, 업그레이드 트리 상태, 캐릭터/무기 해금
- `UGameplayStatics::AsyncSaveGameToSlot`
- Run 시작 시 영구 스탯을 GE로 캐릭터 ASC에 적용
- 통화 1종, Steam 클라우드 세이브 고려
- **저장 정책(P6)**: SaveData 버전 필드+마이그레이션, 슬롯명 규칙, Steam Cloud 대상, 저장 실패 처리, 해금 데이터 삭제/리네임 fallback, 런중 vs 로비 저장 구분. **`UGameInstanceSubsystem` SaveManager 경유**(UI/Actor가 SaveGame 직접 접근 금지)

### 2-12. 난이도 압박 수단 (이속 불변 유지)
스폰 밀도↑ / 원거리 적 비율↑ / 특수 적 패턴 / 미션 목표 압박 / 보스 phase pressure.

### 2-13. 플레이어 생존 / 이동 (협동, 확정 2026-05-30)
- **수동 부활(DBNO: Down But Not Out)**: 체력 0 시 즉시 사망하지 않고 쓰러진 상태로 전환. 생존 아군이 쓰러진 캐릭터 근처 일정 반경에 머물면 **부활 게이지**가 차오르고, 가득 차면 부활(서버 권위). 플레이어 상태기계 Alive / DBNO / Dead. (블리드아웃 타이머·전원 다운 시 처리는 밸런스 후속) — 구현 P5
- **충돌무시 대시(Dash)**: 적·아군 충돌을 무시하고 통과하는 짧은 거리 회피기(모든 캐릭터 기본 제공). 적에게 완전 포위돼 카메라가 막히는 상황 탈출용. (쿨다운/차지는 밸런스 후속) — §3 무브먼트, 구현 P2
- **최대체력 증가 = 즉시 회복(확정 2026-06-02)**: `MaxHealth`가 증가하면(체력 카드 등) **증가분만큼 현재 `Health`도 함께 증가**(서버 권위, `UFPSRHealthSet::PostAttributeChange`에서 처리·새 최대치로 clamp). +체력 업그레이드 선택의 즉각적 체감. 감소 시는 다음 Health 변경 때 clamp로 캡.

### 2-14. 게임필 / 피드백 / 공간 지각 (확정 2026-05-30)
1인칭 스웜 특성상 사각지대(등 뒤·측면) 무방비를 해소하고 대량학살 쾌감을 극대화한다.
- **공간 지각**: 사각지대(특히 등 뒤) 접근 시 괴성/경고 사운드 + **화면 테두리 방향성 위협 인디케이터(Threat Indicator) UI**(시야각 밖에서 적 접근/공격 판정 거리 진입 시)
- **오디오**: 몬스터 발소리 구체화, 위협/경고 사운드(Significance 티어 연동 — §5-1)
- **타격감(Juice)**: 크로스헤어 **히트마커** + 피격 사운드 / 적 처치 시 **경량 파편(Gibs)·팝 연출**(과도한 연산 금지) / 크리티컬·처치 시 **가볍고 맑은 핑(Ping)** 사운드
- 구현: 핵심(히트마커·핑·위협 인디케이터·기본 오디오) **P4**, 폴리시 **P7**
- **구현 상태(P4-D)**: `UFPSRPlayerFeedbackComponent`(로컬·비복제·이벤트형) + PC Client RPC. ① **히트마커**(서버권위 Hit/Crit/Kill, 활성화당 1회, Unreliable) ② **피격 방향 인디케이터**(CoD식: `ApplyContactDamage`→오너클라, 카메라 기준 각도) ③ **원거리 타겟 사전경고**(다수소스 id별 추적·각도배열·추적Tick·Reliable; 생산자=원거리 적 AI는 후속, 디버그 `FPSR.TestDamageDir`/`FPSR.TestRangedWarn`). WBP(GameHUD 컨테이너+RunHUD+HitMarker+ThreatIndicator). **설계 정제(2026-06-09)**: 근접/사각지대 위협의 *상시 시각 표시*는 번잡으로 제외 → **사운드 등 타 방식으로 이전**(오디오 단계). 핑/Gibs/사각오디오는 후속. **히트마커 최종 연출**은 크로스헤어/발사체 작업 후 재확인.

---

## 3. 확정 기술 스택

| 레이어 | 채택 | 비채택(편향) |
|---|---|---|
| 베이스 | 경량 커스텀 C++ 모듈 + 엔진 플러그인 체리픽 | ❌ Lyra 풀 fork |
| 플레이어 | GAS · EnhancedInput · CommonUI · Push Model | |
| 적 스웜 | 경량 풀액터 + Flow-Field + Significance + 인스턴싱 | ❌ GAS, ❌ MassEntity, ❌ 적별 StateTree/NavMesh |
| 보스/엘리트 | 일반 Actor + StateTree (+GAS) | |
| 메시징 | GameplayMessageSubsystem (경량 재구현/Lyra 복사) | |
| UI | CommonUI + Activatable Widget Stack | |
| 저장 | SaveGame | |
| 네트워크 | 리슨서버 P2P, Push Model | ❌ Iris 핵심 의존, ❌ Server-Side Rewind |
| 무브먼트 | 표준 CMC + **충돌무시 대시(회피기)** | ❌ Bhop/Wall-run/Motion Matching |
| 레벨 | 고정 맵 | ❌ PCG |

- **엔진 포함 플러그인(바로 enable)**: GameplayAbilities, EnhancedInput, ModularGameplay, GameFeatures, CommonUI, StateTree, GameplayStateTree, SignificanceManager, Iris(off)
- **엔진에 없는 Lyra 출신 플러그인(P3+ 필요 시 경량 재구현/복사)**: CommonUser, CommonGame, ModularGameplayActors, GameplayMessageRouter
- **Build.cs 의존성**: Phase별 실제 사용 시점에 추가(CommonUI/CommonInput/StateTreeModule/GameplayStateTreeModule/SignificanceManager, 필요 시 ReplicationGraph). 미사용 모듈 선등록 지양

---

## 4. 프로그래밍 구조

### 4-1. 목표 폴더 / 모듈 구조 (설계 전체)
```
Source/FPSRoguelite/Public/
├── Core/             GameMode, GameState, PlayerController, PlayerState, 로그/Assert 매크로
├── Hero/             UHeroDataAsset, AFPSRCharacter (Separated Arms)
├── AbilitySystem/    ASC, AttributeSets(글로벌), GA, GE, Cue, DamageCalc(브릿지)
├── Weapon/           Archetype 7종, WeaponInstance, StatBlock, ModifierFragment
├── Card/             CardDataAsset(Scope), CardPool, DrawSystem, Reroll
├── Enemy/            경량 풀액터, HealthComponent, AttackType, FlowField, ScalingProfile
├── Boss/             ABossBase, BossDefinition, StateTree
├── Run/              SpawnDirector, **RunDirector(`UFPSRRunDirectorSubsystem`=런클럭/스폰강도/시간 미션 스케줄/보스타임, 서버 전용)**, RunSchedule(`UFPSRRunScheduleDataAsset`), **Mission/**(`AFPSRMissionActor` 베이스+`UFPSRMissionDataAsset`+`AFPSRMissionSpawnPoint`+서브클래스, P4-A)  ※ 공유XP/레벨/RunPhase/**bRunPaused** = **복제 필요 → `AFPSRGameState` 호스팅**(Push Model, 서버 권위). WorldSubsystem은 복제 불가라 런 상태는 GameState에 둔다(P3-A 확정 2026-06-01). **레벨업 보류 픽은 플레이어별이라 `AFPSRPlayerState::CardPicksPending`(+미션보상 `MissionRewardPicksPending`)에 둔다**(§2-2). 레벨업/미션클리어 시 `bRunPaused`로 전역 프리즈(§2-2, 재설계 2026-06-04).
├── Pickup/           범용 PickupActor + PickupDefinition(자석)
├── MetaProgression/  SaveGame, UpgradeTree, Subsystem
├── Performance/      ActorPool, SignificanceConfig, 인스턴싱
├── Messages/         GMS Payload structs
└── UI/               HUD(공유XP바/하단 무기바), CardSelect, MissionUI, MetaUI
```
- 글로벌 스탯(Luck, GlobalCrit, CritMult, MoveSpeed, MaxHealth, HealthRegen, PickupRadius, XPGain) → Character ASC AttributeSet
  - ※ Luck = 광역 행운(카드 등급 가중 + 향후 드랍품질·희귀스폰 등). RarityBonus는 Luck으로 통합·폐지(2026-06-02). PickupRadius·XPGain·MoveSpeed·HealthRegen은 미구현(필요 단계에서 추가)
- 무기별 스탯 → WeaponInstance 스탯 블록 (ASC 아님)
- 하단 무기바 HUD: 가시성을 HUD State(GMS/Tag)에 바인딩 → ADS/카드UI/미션UI 시 숨김

### 4-2. 실제 구현된 클래스맵 (현재 코드, P0~P1.5-A)
```
Source/FPSRoguelite/
├── FPSRoguelite.Build.cs / *.Target.cs (UE5.7, V6, Unreal5_7)
├── Core/
│   ├── AFPSRGameMode / AFPSRGameState / AFPSRPlayerController / AFPSRPlayerState
│   └── FPSRLogChannels (로그 카테고리)
├── AbilitySystem/
│   ├── UFPSRAbilitySystemComponent
│   ├── Attributes/ UFPSRHealthSet, UFPSRCombatSet  (글로벌 속성)
│   └── Abilities/
│       ├── UFPSRGameplayAbility           (GA 베이스)
│       ├── UFPSRGA_WeaponFire_Hitscan     (카메라 히트스캔 + 크리티컬 + 적 데미지 브릿지)
│       └── UFPSRGA_WeaponMelee            (전방 구체 오버랩 다중 타격)
├── Weapon/
│   ├── FPSRWeaponTypes.h  (EFPSRWeaponArchetype, EFPSRFireMode, FFPSRWeaponStatBlock)
│   ├── UFPSRWeaponDataAsset               (무기 콘텐츠 바인딩)
│   ├── UFPSRWeaponInventoryComponent      (3슬롯 서버권위, Push Model, 장착 시 발사 GA 부여)
│   └── UFPSRWeaponFireComponent           (오너클라 연사 cadence/반동/블룸, 샷마다 발사 GA 활성)
├── Enemy/
│   ├── AFPSREnemyBase                     (경량 Pawn, 최근접 추격 스티어링, placeholder 큐브)
│   └── UFPSREnemyHealthComponent          (비-GAS 체력/데미지)
├── Hero/
│   └── AFPSRCharacter                     (1P 카메라 + Separated Arms + EnhancedInput + 인벤토리/발사)
└── Tests/
    └── FPSRSmokeTest                      (모듈 로드 자동화 테스트)
```
- ASC는 `AFPSRPlayerState`가 소유, `AFPSRCharacter`가 `PossessedBy`/`OnRep_PlayerState`에서 ActorInfo 초기화
- 데미지 브릿지: 플레이어 GAS 계산 → `UFPSREnemyHealthComponent::ApplyDamage` (적 ASC 없음)

---

## 5. 성능 / 네트워크 예산 (⚠️ P2 착수 전 수치 확정·검증 — 최우선 보완)

> 이 프로젝트 최대 리스크는 Hero Shooter 과설계가 아니라, **적 500마리 협동의 성능/복제 예산이 미수치화된 점**이다. 아래는 잠정값이며 P2에서 Unreal Insights + NetProfiler로 검증·조정한다.

| 항목 | 잠정 목표 | 비고 |
|---|---|---|
| 최대 활성 적 수(서버) | 하드캡 500, 통상 200~350 | 풀 고갈 시 스폰 보류 |
| 클라이언트별 관련(relevant) 적 | 상한 ~150 | relevancy cull |
| 적 NetCullDistance | 잠정 ~40m (조정 대상) | 화면 밖 컬링 |
| 적 NetUpdateFrequency | 위협도별 S0 30Hz / S1 10Hz / S2 5Hz / S3 2Hz | Significance 연동 |
| 적 Dormancy | 원거리·비활성 DORMANT, 접근 시 wake | |
| 적 복제 상태 | Transform(위치/Yaw)만 최소 복제, 체력=서버 권위 | 히트/사망 코스메틱은 GameplayMessage/Cue (복제 액터 상태 아님) |
| XP/픽업 | 개수 cap + 인접 병합, 자석=클라 코스메틱·서버 권위 수령 | |
| 복제 발사체 액터 | 실제 발사체(바주카/유탄)만 ≤64 동시 | 히트스캔/연사=비복제 코스메틱 |
| 적 공격 판정 | 서버 배치 처리(거리 체크 배치) + **공격 토큰 상한** | 플레이어당 동시 공격 시도 적 수 제한(§2-6) |

**리플리케이션 도구 평가 순서**: Push Model(기본) → 부하 시 **Replication Graph**(spatial grid relevancy, 검증된 도구) → 그래도 부족 시 Iris(Beta) 평가. **Iris를 1순위로 두지 않음**(RepGraph가 다수 액터·연결별 relevancy 병목에 더 직접적).

### 5-1. Significance 티어 (플러그인 enable ≠ 최적화)
적/VFX/SFX/anim tick/mesh/healthbar를 단계별로 다운. **AI update budget에도 연동.**
- **S0** 근접 위협: full update
- **S1** 근거리: 저빈도 update
- **S2** 중거리 군집: anim·VFX 축소
- **S3** 원거리: coarse movement·no cosmetic

### 5-2. Flow-Field 적 타겟 (P2)
**고정맵 grid + 단일 목표점 field(가장 가까운 플레이어) + local separation steering**으로 시작. 타겟 규칙(가까운/위협도/파티중심/미션목표)은 데이터로 전환 가능하게. Flow-Field를 플레이어별로 둘지/파티중심·목표점별로 둘지 결정 필요. 동적 장애물은 비용 대비 나중에. separation batch update 주기·충돌 처리 범위 정의.
- **구현 보강(P4, 2026-06-09 main 머지 — `phase/p4-enemyspawnpoints` 코드분, Codex 5R 하드닝)**: ① **디자이너 배치 스폰포인트** `AFPSREnemySpawnPoint`(Weight/ZoneTag/MinPlayerDistance/bEnabled) + `UFPSREnemySpawnSubsystem`이 전 플레이어 비가시(FOV)+거리 게이트로 가중랜덤 선택(후보 0 시 링 폴백 → 미배치 맵도 동작). 디자이너 지점은 권위적 위치라 ground-snap 생략(실내/지붕 천장 스냅 방지). `SetActiveSpawnZone` 훅은 TimeGate(§2-8) 후속. ② **플로우필드 장애물 마스크 + BFS 라우팅 + 적 중력/지면추종**(`AFPSREnemyBase`, 경사/계단 소폭 보정). 하드닝: 월드 밖 추락 적 **KillZ 회수**(슬롯 영구점유 방지), 접촉 데미지 **수직 게이트**(바닥 관통 타격 방지), 동일 스폰지점 중첩은 **분리(separation)의 동일위치 결정적 푸시**로 근본 해소(스폰 위치 비이동). **후속(C1, `phase/p2-flowfield-height`, PIE 의존)**: 멀티레벨/높이 인지 BFS + 플로우필드 셀 **클리어런스 인지 프로브**(현 전셀 오버랩은 경계 벽 양쪽 셀 차단 → 좁은 통로 과차단 트레이드오프). 콘텐츠 스폰포인트 배치(L_Sandbox)는 사용자 PIE 후 별도 머지.

---

## 6. 작업 방식 / 규칙

### 6-1. 환경 / 경로
- 엔진: **UE 5.7** — `D:\UnrealEngine\UE_5.7`
  - UBT: `D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat`
  - GenerateProjectFiles: `D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\GenerateProjectFiles.bat`
  - 엔진 소스/플러그인: `D:\UnrealEngine\UE_5.7\Engine\Source`, `D:\UnrealEngine\UE_5.7\Engine\Plugins`
- 프로젝트 루트: `E:\Git_Project\FPSRoguelite`
- 참조 세팅 템플릿: `E:\Git_Project\언리얼세팅`
- 모듈명: `FPSRoguelite`(Runtime), 게임 타깃 `FPSRoguelite`, 에디터 타깃 `FPSRogueliteEditor`
- VS 2022 (`.vsconfig` 워크로드 기준)

### 6-2. 프로덕션 방식 원칙 (필수)
- 코드/개발은 **프로덕션 품질**. 엔진 템플릿 편의 단축은 지양
- **C++ = 로직/베이스 클래스 / 콘텐츠 바인딩 = BP 서브클래스·DataAsset·config** (데이터 드리븐). **에셋 경로를 C++에 하드코딩 금지**(`ConstructorHelpers` 지양)
- 네트워크 = 서버 권위 + Push Model. 엔진 API는 소스 대조 후 사용
- 디버그/테스트 스캐폴딩(DrawDebug, 콘솔 커맨드, 임시 로그, 플레이스홀더 메시)은 **검증용으로만 허용** — 프로덕션 전환 대상임을 명시·추적(§8)하고 `#if !UE_BUILD_SHIPPING` 등으로 게이트하거나 제거
- 잠깐의 동작 테스트는 단축 가능하나, **남는 코드는 프로덕션 기준**

### 6-3. 코딩 / 빌드 규칙
- UE5 코딩 컨벤션 준수(Epic Coding Standard). `.editorconfig` 적용: C++ 탭 들여쓰기, eol=crlf
- 빌드 타깃 버전: `BuildSettingsVersion.V6`, `EngineIncludeOrderVersion.Unreal5_7`
- 엔진 API/매크로/플래그는 **추론 금지 — 엔진 소스에서 실제 사용례를 grep**해 대조 후 작성
- 네트워크: **P1부터 서버 권위 + Push Model**. 솔로 후 retrofit 금지
- 검증 없이 "완료" 보고 금지 — 빌드/스모크/`git diff` 중 하나로 자체 검증

### 6-4. 진행 핸드오프 규칙 (다른 세션/AI 인수인계)
- 작업 시작 전 **이 문서(Game.MD) + `PROGRESS.md`** 를 먼저 읽는다
- 큰 작업은 단계 완료 시점과 **세션 중단 전 반드시 `PROGRESS.md`를 갱신**(완료/진행중/다음순서/빌드·검증법/확정사항)하고 커밋
- 커밋 메시지에 단계·검증 결과를 명확히. 상세 이력은 `git log --oneline`
- 설계 변경은 **이 문서(Game.MD)를 먼저 갱신**
- **각 P 단계는 전용 `phase/` 브랜치에서 작업**(§6-7). 세션 중단 전 `PROGRESS.md`에 **현재 활성 브랜치명**을 명시

### 6-5. 모델 / 작업 흐름 정책 (전역 HotL 준수)
- 새 작업은 **플랜 우선**. HIGH_RISK(파일 생성/수정/삭제, 빌드, 커밋)는 승인 후 실행
- **구현(코드 작성·파일 수정) → Haiku 위임 / 검증(빌드·diff·스모크·UI) → 메인(Opus) 직접**. 검증은 Haiku에 위임 금지
- 단순 한 줄 수정·읽기 전용 조회는 모델 분리 없이 즉시 처리

### 6-6. 빌드 / 검증 방법
- 빌드(에디터 닫고):
  `"D:\UnrealEngine\UE_5.7\Engine\Build\BatchFiles\Build.bat" FPSRogueliteEditor Win64 Development -Project="E:\Git_Project\FPSRoguelite\FPSRoguelite.uproject" -WaitMutex`
- 헤드리스 검증:
  `UnrealEditor-Cmd.exe <uproject> -unattended -nopause -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests FPSRoguelite.Smoke.ModuleLoads" -TestExit="Automation Test Queue Empty" -abslog=...`
- 새 UCLASS 다수면 Live Coding 불가 → 풀빌드(에디터 닫아야 함). 입력 IA 생성은 `Scripts/gen_input_assets.py`
- **MCP(unreal) 인증 실패로 미사용** → UBT 빌드 + 헤드리스 자동화로 검증
- **Codex 코드리뷰 게이트**: 단계 완료·커밋 직전 `Scripts/codex-review.ps1` 실행 → 외부 AI(Codex gpt-5.5)가 Game.MD 원칙 기준으로 diff 리뷰. 기본 `-Base main`(브랜치 diff) / `-Uncommitted`(작업트리). 비대화(approval never; Windows codex review는 workspace-write 샌드박스 → 신뢰 로컬 리포 전용). 결과 `Docs/reviews/`(gitignore). 호출·판독은 Opus가 직접

### 6-7. 브랜치 전략 (Phase 단위 워크플로)
로드맵(§7-3)의 각 **P 단계**는 `main`에 직접 커밋하지 않고 **전용 브랜치에서 작업 → 검증 통과 후 `main`에 머지**한다. (도입 2026-05-30. 이전 main 히스토리는 소급 적용하지 않음.)

- **브랜치명**: `phase/<단계소문자>-<핵심키워드>` (예: `phase/p2-spawndirector`, `phase/p1.5-b-ammo-reload`)
- **머지**: `--no-ff` 머지 커밋(phase 경계가 main 히스토리에 보이도록)
- **원격**: phase 브랜치도 `origin`에 push(백업/리뷰 협업)

라이프사이클:
1. **분기** — `git checkout main` → `git checkout -b phase/<단계>-<키워드>` → `git push -u origin phase/<단계>-<키워드>`
2. **작업** — 해당 phase 구현·문서를 이 브랜치에서만 커밋(구현=Haiku 위임 / 검증=Opus 직접, §6-5)
3. **검증(머지 전 필수)** — 빌드(§6-6) → 헤드리스 스모크 → `Scripts/codex-review.ps1 -Base main`(브랜치 diff 리뷰). 검증 없이 머지 금지(§6-3)
4. **핸드오프** — phase 완료/세션 중단 전 `PROGRESS.md` 갱신·커밋(활성 브랜치명 명시, §6-4)
5. **머지** — `git checkout main` → `git merge --no-ff phase/<단계>-<키워드> -m "merge(phase): <단계> <요약> — 검증 통과"` → `git push origin main`
6. **정리** — `git push origin --delete phase/<단계>-<키워드>` + `git branch -d phase/<단계>-<키워드>`

> 전역 `claude/` 브랜치 클린업 정책과 prefix(`phase/`)로 분리되어 충돌 없음.

---

## 7. 현재 구현 상태 & 로드맵

### 7-1. 완료 (커밋·빌드 검증됨)
- **P0** 경량 C++ 스캐폴드 (UE5.7, 플러그인 enable, GameplayTags 초안, 빌드 OK, 스모크 테스트)
- **P1-0** 코어 프레임워크 / **P1-1** 플레이어 GAS 글로벌 속성(`UFPSRHealthSet`/`UFPSRCombatSet`)
- **P1-2** EnhancedInput(이동·시점·점프 PIE 확인) / **P1-3** 1인칭 카메라 + Separated Arms
- **P1-4** 무기 기반 — `Weapon/`: 3슬롯 서버권위, Push Model, 장착 시 발사 GA 부여
- **P1-5** 발사 GA — `UFPSRGA_WeaponFire_Hitscan`: 카메라 히트스캔 + 디버그 라인 + 크리티컬 + 적 데미지
- **P1-6** 근접 GA — `UFPSRGA_WeaponMelee`: 전방 구체 오버랩 다중 타격
- **P1-7** 적 — `AFPSREnemyBase`(경량 Pawn) + `UFPSREnemyHealthComponent`: 최근접 추격, 엔진 큐브 placeholder, 데미지 브릿지, 콘솔 `FPSR.SpawnEnemies [N]`
- **통합**: Character에 인벤토리 부착 + 기본무기 지급(서버) + IA_Fire/IA_EquipSlot1~3 배선
- **P1.5-A** 사격 코어 — `UFPSRWeaponFireComponent`: FullAuto 연사 루프 + 반동(카메라 킥) + 확산/블룸. 하드코딩 경로 제거(BP 참조 패턴) 리팩터 완료
- **빌드 성공 + 헤드리스 부팅·스모크 통과**(Fatal 0)

### 7-2. PIE 테스트 대기 / 사용자 BP 셋업 (블로킹)
PROGRESS.md '사용자 대기 작업' 참조. 요약: **BP 3종 생성 + 참조 할당** 필요
- `BP_FPSRGameMode`(**반드시 `/Game/Core/`**, 부모 `FPSRGameMode`): DefaultPawnClass / PlayerControllerClass
- `BP_FPSRCharacter`(부모 `FPSRCharacter`): IA 8개 + DefaultPrimary/SecondaryWeapon(DA_Weapon_Rifle/Knife)
- `BP_FPSRPlayerController`(부모 `FPSRPlayerController`): DefaultMappingContext=`IMC_Default`
- FireMode: Rifle=FullAuto / Knife=Single·무반동
- → 이후 full-auto PIE 테스트 → 통과 시 P1 완료, P1.5-B(탄약/재장전/ADS)

### 7-3. Phase 로드맵 (총 ~23주)
| Phase | 기간 | 산출물 |
|---|---|---|
| **P0** ✅ | 2주 | 경량 C++ 스캐폴드 + Git/LFS + 플러그인 + GameplayTags + 빌드 OK + 스모크 |
| **P1** (마무리중) | 3주 | Net-aware 1P 캐릭터(Separated Arms) + 무기 2종 + 적 1종 + 데미지 브릿지 |
| **P1.5** | — | 사격/이동 감각 (A: 연사/반동/확산 ✅, B: **MagSize+재장전(예비 무한)**/ADS) |
| P2 | 3주 | SpawnDirector + Flow-Field + Pooling + Significance (적 300+ 안정) + **적 이속 ±10% 편차** + **적 근접 데미지·공격토큰 baseline** + **충돌무시 대시** |
| P3 | 4주 | 공유XP + 파티레벨 + **레벨업 스택(프리즈 폐지)** + **정비시간 RunPhase** + **오프닝 카드 시드** + Card UI + 동적 카드풀 + Rarity + 리롤 |
| P4 | 4주 | **P4-A**(재설계) 런 디렉터(시간 미션 스케줄+보스타임) + 확장형 미션 프레임워크+스폰포인트 + **레벨업/미션클리어 전역 프리즈**(라운드제 폐지)+레퍼런스 미션 1종+오프닝시드 / **P4-B** Weapon Modifier Fragment+weapon-scope 카드+미션 보상 실적용 / **P4-C** 무기 7종 / **P4-D** 게임필(히트마커·핑·위협 인디케이터·사각 오디오)+PickupRadius/XPGain+HUD위젯. (+원거리 적 규격·공격토큰 확장) |
| P5 | 3주 | 4인 협동 + 세션(Steam) + Iris 평가 + NetProfiler + **아군 오사(10%+호스트 토글)** + **수동 부활(DBNO)** |
| P6 | 2주 | 메타 프로그레션 + 보스 + 클리어 플로우 |
| P7 | 2주 | CommonUI 폴리시 + **오디오/이펙트(게임필) 폴리시** + Insights + README + 빌드 |

### 7-4. P0/P1 잔여 연결 항목 (착수 전 확인)
- 기본 맵/GameMode config 연결(OpenWorld 템플릿 기본값 제거), `GlobalDefaultGameMode` 지정 검토
- CommonUI viewport/input data 설정 (P1 후반~P3): ViewportClient, InputData·Back/Click·ControllerData, Activatable Widget Stack 레이어(Game/GameMenu/Menu/Modal)

---

## 8. 디버그 / 플레이스홀더 인벤토리 (프로덕션 전환 대상)

| 항목 | 현재 | 전환 계획 |
|---|---|---|
| 발사/근접 DrawDebug 라인·구체 | 검증용 시각화 | `#if !UE_BUILD_SHIPPING` 게이트 / VFX 교체 |
| `FPSR.SpawnEnemies [N]` 콘솔 커맨드 | 적 스폰 테스트 | P2 SpawnDirector로 대체, shipping 제외 |
| 적 큐브 placeholder 메시 | 엔진 기본 큐브 | 실제 적 메시 + 인스턴싱/VAT |
| XP 픽업 placeholder 스피어(`AFPSRXPPickup`, ConstructorHelpers) | 엔진 기본 스피어 | 실제 XP 오브 VFX + 풀링/배칭(P3-B 후속) |
| FP팔/3P 바디 메시 미할당 | 빈 SkeletalMesh | HeroDataAsset 메시 바인딩 |
| 적 추격 = 단순 스티어링 | 최근접 추격 | P2 Flow-Field + separation + 배치 교체 |
| PlayerController `[Input] Added DefaultMappingContext` Warning | 1회성 로그 | 다음 빌드 시 Verbose 다운그레이드 |
| CommonUI `LogUIActionRouter` 에러 | 무해 | P3 `CommonGameViewportClient` 설정 시 해결 |

---

## 9. 확정 사항 / 주의점 (빠른 참조)
- 무기 교체 = 숫자키 **1/2/3**(`IA_EquipSlot1~3`) / 사격 = 좌클릭
- **UE5.7 IMC 매핑은 Python `set_editor_property` 미반영 → 에디터 수동**(IA 에셋 생성은 Python OK)
- 카드선택 = **레벨업/미션클리어 시 전역 프리즈**(적·플레이어 정지)에 전원 선택 → 재개(§2-2). 오프닝 시드 2장은 런 시작 시. (라운드제·정비시간 폐지 2026-06-04)
- git: 사용자 콘텐츠(L_Sandbox 맵, DA_Weapon_Rifle/Knife @ `Content/Weapons/DataTable/`)는 디스크 존재·**미커밋**(untracked)

---

## 10. 리뷰 루프 (외부 AI 협업)
- 사용자 + 다른 AI가 이 문서를 읽고 추가/수정점을 **`GameConfirm.MD`**(다른 AI 작성, **우리는 만들지 않음**)에 정리
- 이후 세션이 `GameConfirm.MD`를 불러와 현재 프로젝트와 비교 → (a) 타당한 추가/보완은 문서 갱신 + 작업계획 반영, (b) 사용자 판단·결정 필요한 것은 사용자에게 정리 보고
- **코드 리뷰(Codex CLI)**: 문서 리뷰와 별개로, 구현 검증 단계에서 `Scripts/codex-review.ps1`로 Codex(gpt-5.5)에 diff 코드리뷰를 받는다(§6-6). 지적은 Opus가 판독해 수정 여부 결정. **분리 원칙: 문서 제안=`GameConfirm.MD`(외부 AI 작성) / 코드 리뷰=`Docs/reviews/`(Codex 산출).**
