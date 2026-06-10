# CombatWeaponCard — 무기 / 카드 / 사격 (FPSRoguelite SSOT 분할)

> `Game.md`(SSOT 허브)의 분할 문서. **섹션 번호(§x)는 원본 그대로 보존** — 소스 주석·교차참조 호환.
> 작업 시작 전 허브 `Game.md` + `PROGRESS.md`를 먼저 읽고, 카드·무기·모디파이어·사격 감각 관련 작업 시 본 파일을 연다.
> 담는 섹션: §2-3 카드 시스템 / §2-4 무기 시스템(§2-4-1 모디파이어, §2-4-2 사격 메커니즘) / §2-5 시작 캐릭터.

---

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
- **빌드 시너지 설계 (기획 추가 2026-06-10)**: 카드 *메커니즘*(Scope/Rarity/Fragment)과 별개로, **무엇이 빌드를 서로 다르게 느끼게 하는가**(시너지 축)를 별도로 설계한다 — 뱀서 장르의 핵심 리텐션. 예시 축: 원소/상태이상, 투사체 수↔단발 위력, 크리↔지속피해, AOE↔관통. **Fragment 상호작용(2연발+관통 등 §2-4-1)이 시너지의 1차 수단**. **시너지 설계 패스 = 재미 게이트(§7-5) 전후 1회** — 카드 콘텐츠 폭증 전에 축을 먼저 정의(콘텐츠 양산 후엔 재설계 비용 큼).

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
- 3종: **연사총 / 점사총 / 근접칼**
- `UHeroDataAsset`(PawnData 개념) Base 구조 — DefaultWeapon, BaseAttribute, PassiveAbilitySet, FP팔/3P바디 메시
- 데이터 드리븐. 고유 패시브가 무거워지면 그때만 GameFeature 플러그인으로 승격
