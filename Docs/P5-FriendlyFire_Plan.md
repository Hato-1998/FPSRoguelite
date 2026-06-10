# P5 친선사격(FF) + AOE 자기데미지 + 폭발 카드 — 구현 플랜 (세션 핸드오프)

> **상태**: 설계 확정·미구현. 브랜치 `phase/p5-friendly-fire`(코드 0줄, 이 문서만). D1 백로그(아군 오사)를 통째로 당겨오는 작업.
> **착수 전 필독**: `Game.md` 허브 + `PROGRESS.md` + 본 문서. 데미지 도메인이라 `Docs/SSOT/Enemy.md`(§2-6,2-10) · `Docs/SSOT/CombatWeaponCard.md`(§2-4) 참조.
> **모델 정책**: 서버권위 데미지·팀판정이라 **Opus 직접 구현**(메모리 `haiku-delegation-security-wiring`). 검증도 Opus 직접.

---

## 0. 목표 & 확정 결정 (사용자, 2026-06-11)

플레이어 무기가 **적/자기/아군**에 데미지를 줄 수 있게 통합:
- **적** → 항상 풀 데미지(현행 유지)
- **자기(instigator)** → 폭발 계열만, **항상 풀 데미지**(자폭). 직접탄/히트스캔/근접은 자기 명중 불가
- **아군(다른 플레이어)** → **FF 토글 ON일 때만**, `데미지 × 0.5`
- **FF 토글 범위 = 전체**(직접 탄/히트스캔/근접/폭발 전부 아군피해) — 즉 D1 풀 구현
- **카드 A**: 자기 데미지 제거(폭발 자폭 무효화)
- **카드 B**: 라이플 등 히트스캔에 **소형 AOE(폭발탄)** 부여

---

## 1. 아키텍처 (제1원리 3줄)

1. **제1원리**: "이 데미지를 적용할까/얼마나"를 모든 무기 경로에 흩어진 `if(EnemyHealthComponent)` 분기 대신 **단일 통합 판정 헬퍼**로 모은다(적/자기/아군 + FF 배수). 폭발은 그 위의 공유 함수. 서버권위.
2. **Lyra/표준**: Lyra는 GE+TargetData+팀 서브시스템으로 FF 처리. 우리 적은 경량 비-GAS라 **직접 라디얼 오버랩 + 비-GE 데미지** 유지(원칙1). 플레이어만 GAS(`ApplyContactDamage`). 팀 판정은 클래스 식별(EnemyHealthComponent=적, AFPSRCharacter=플레이어)로 경량화.
3. **프로젝트 정합**: 기존 `FFPSRProjectileParams`/`FFPSRFireContext`/Fragment 확장 + GameState 호스트 토글(`bRunPaused` 복제 패턴 재사용). 신규 인프라 = 헬퍼 1파일 + 카드 2종.

---

## 2. 구성 요소 (파일 단위)

### 2-1. 통합 데미지 헬퍼 (신규) — `Source/FPSRoguelite/{Public,Private}/Combat/FPSRCombatStatics.{h,cpp}`
> 폴더 `Combat/` 신규. 네임스페이스 또는 static-only 클래스. **서버권위 전용**(호출부에서 `HasAuthority()` 보장).

```cpp
namespace FPSRCombat
{
    struct FDamageResult { bool bApplied = false; bool bKilled = false; bool bWasEnemy = false; };

    // 적/자기/아군 규칙 + FF 배수를 적용해 "최종 적용 데미지"를 돌려줌(0 = 스킵).
    // Instigator=플레이어 가정(플레이어 무기). bAllowSelf=폭발만 true.
    float ResolveDamage(const AActor* Instigator, const AActor* Target, float BaseDamage,
                        bool bAllowSelf, const UWorld* World);

    // FinalDamage를 타겟 종류에 맞는 브릿지로 적용(적→UFPSREnemyHealthComponent::ApplyDamage,
    // 플레이어→AFPSRCharacter::ApplyContactDamage). 결과(적중/킬/적여부) 반환.
    FDamageResult ApplyDamage(AActor* Target, float FinalDamage, AActor* Instigator);

    // 라디얼 폭발: 반경 내 폰 오버랩 → ResolveDamage/ApplyDamage 일괄, 크릿 롤(per-target),
    // 적 명중 시에만 발사자 PC에 히트마커 1회(강한 결과). bAllowSelf=자폭 허용 여부.
    void ApplyExplosion(UWorld* World, const FVector& Center, float Radius, float Damage,
                        float CritChance, float CritMultiplier, AActor* Instigator, bool bAllowSelf);
}
```

**ResolveDamage 판정 로직**:
- `Target == Instigator` → `bAllowSelf ? BaseDamage : 0`
- `Target`가 `UFPSREnemyHealthComponent` 보유(적) → `BaseDamage`
- `Target`가 `AFPSRCharacter`(다른 플레이어=아군) → `World GameState IsFriendlyFireEnabled() ? BaseDamage × FFScale : 0`
- 그 외 → 0

**ApplyExplosion = 현행 `AFPSRProjectile::HandleImpact`의 AOE 인라인 로직 + 방금 추가한 크릿/히트마커를 추출**해 여기로 통합. (`OverlapMultiByObjectType(ECC_Pawn)`, instigator를 더 이상 ignore하지 않음 — 자기/아군 판정을 ResolveDamage가 함.)

### 2-2. FF 호스트 토글 — `AFPSRGameState` (`Core/FPSRGameState.{h,cpp}`)
- `UPROPERTY(ReplicatedUsing=OnRep_RunState 또는 별도)` `bool bFriendlyFireEnabled = false;` (호스트 설정, 복제)
- `static constexpr float FriendlyFireDamageScale = 0.5f;` (또는 EditDefaultsOnly 튜닝값)
- 게터 `bool IsFriendlyFireEnabled() const`, `float GetFriendlyFireDamageScale() const`
- `SetFriendlyFireEnabled(bool)` (서버, 호스트 설정 진입점) + 디버그 `FPSR.SetFriendlyFire 0/1`
- 복제 패턴은 `bRunPaused`와 동일(`DOREPLIFETIME_WITH_PARAMS_FAST` + Push Model). UI 호스트 토글은 후속(지금 디버그 cmd로 검증).

### 2-3. 자기데미지 플래그
- `FFPSRFireContext`(`Weapon/FPSRWeaponFragment.h`)에 `bool bSuppressSelfDamage = false;` 추가.
- `FFPSRProjectileParams`(`Weapon/FPSRProjectileTypes.h`)에 `bool bSelfDamage = true;` 추가.
- **직접탄/히트스캔/근접**: `bAllowSelf=false`로 ResolveDamage 호출(자기 명중 시 0).
- **AOE 투사체**: Projectile GA가 `Params.bSelfDamage = !FireCtx.bSuppressSelfDamage`로 베이크 → `HandleImpact`가 `ApplyExplosion(..., bAllowSelf=Params.bSelfDamage)`.
- **히트스캔 폭발탄**: ExplosiveRounds fragment의 폭발이 `bAllowSelf = !Context.bSuppressSelfDamage`.

### 2-4. 데미지 경로 4곳 치환 (인라인 "적만" → 통합 헬퍼)
각 경로: ① 타겟 찾기(현행 트레이스/오버랩) ② 크릿 롤로 baseDmg 산출 ③ `float dmg = ResolveDamage(instigator, target, baseDmg, bAllowSelf, world)` ④ `dmg>0`이면 `ApplyDamage(target, dmg, instigator)` ⑤ 히트마커는 **적 명중(result.bWasEnemy)일 때만**.
- **Hitscan GA** `FPSRGA_WeaponFire_Hitscan.cpp` — `ApplyDamageToActor` 람다(현 169-208) 교체. 단일트레이스/관통 둘 다. 트레이스는 이미 `ECC_Visibility`/`ECC_Pawn`라 아군 플레이어 캡슐도 잡힘(플레이어 캡슐이 Visibility 블록인지 확인 — 아니면 Pawn 멀티트레이스 경로 점검).
- **ChargeLaser GA** `FPSRGA_WeaponFire_ChargeLaser.cpp` — `ApplyDamageToActor` 람다(현 173-208) 교체.
- **Projectile** `AFPSRProjectile.cpp` — 단일타격(`OnSphereOverlap`/`TryDamageActor`)은 `bAllowSelf=false`로 ResolveDamage; AOE(`HandleImpact`)는 `ApplyExplosion`으로 위임. `IsHostileTarget`는 ResolveDamage로 흡수(투사체 비행 중 친화 통과는 ResolveDamage가 0 반환으로 자연 처리, 단 **자기 투사체가 자기 캡슐에 발사 즉시 오버랩**하는 점블랭크는 기존 가드 유지).
- **Melee GA** `FPSRGA_WeaponMelee.cpp` — 오버랩 데미지 루프(현 94-130) 교체.
> ⚠️ **적 투사체(Team=Enemy, B1 원거리적 후속)**: ResolveDamage가 instigator=적일 때 플레이어만 때리도록 일반화하거나, 당분간 `Params.Team`으로 분기. 본 작업은 **플레이어 instigator** 우선, Team=Enemy 경로는 현행 유지/주석.

### 2-5. 카드 A — 자기 데미지 제거
- `UFPSRFragment_NoSelfDamage`(`Weapon/FPSRWeaponFragment.h`): `PreFire`에서 `Context.bSuppressSelfDamage = true;`
- 콘텐츠: `UFPSRCardDataAsset`(Scope=ThisWeapon, GrantedFragment=NoSelfDamage). Bazooka/Grenade `AvailableModifiers`에 등록.

### 2-6. 카드 B — 폭발탄(히트스캔 → 소형 AOE)
- 신규 훅: `UFPSRWeaponFragment`에 `virtual void OnImpact(const FFPSRFireContext& Context, const FVector& ImpactPoint, bool bAllowSelf) const {}`
- Hitscan GA: 명중 지점마다(단일트레이스 `Hit.ImpactPoint`, 관통은 각 PawnHit 지점 또는 벽 지점) fragment `OnImpact` 호출.
- `UFPSRFragment_ExplosiveRounds{ float AOERadius=150, float AOEDamage=20 }`: `OnImpact`에서 `FPSRCombat::ApplyExplosion(World, ImpactPoint, AOERadius, AOEDamage, 0,1, Avatar, bAllowSelf)`.
- 콘텐츠: 카드 DA(ThisWeapon, GrantedFragment=ExplosiveRounds). Rifle `AvailableModifiers`에 등록.

---

## 3. 구현 순서 (권장)

1. **GameState FF 토글**(2-2) + `FPSR.SetFriendlyFire` 디버그 → 빌드 통과.
2. **FPSRCombatStatics 헬퍼**(2-1): ResolveDamage/ApplyDamage/ApplyExplosion. `AFPSRProjectile::HandleImpact`에서 AOE 로직 추출 이전.
3. **Projectile 치환**(2-4 일부): HandleImpact→ApplyExplosion, 단일타격→ResolveDamage. `bSelfDamage` 베이크(2-3).
4. **Hitscan/ChargeLaser/Melee GA 치환**(2-4). 히트마커=적 명중만.
5. **FireContext.bSuppressSelfDamage + NoSelfDamage 카드**(2-3,2-5).
6. **OnImpact 훅 + ExplosiveRounds 카드 + Hitscan 배선**(2-6).
7. **콘텐츠(사용자)**: 카드 DA 2종 + 풀/AvailableModifiers 등록.
8. 각 단계 빌드+스모크. 최종 **2-client PIE**(FF on/off, 자폭, 아군 50%, 카드 2종).

---

## 4. 검증

- 빌드(§6-6) + 헤드리스 스모크(`FPSRoguelite.Smoke.ModuleLoads`).
- **PIE(데미지=서버권위라 2-client 필수)**:
  - FF OFF: 아군 무피해(폭발/탄/근접/레이저), 적 정상.
  - FF ON: 아군 50% 피해(전 경로). 자기 폭발=풀 데미지(자폭).
  - NoSelfDamage 카드: 자폭 0, 적/아군은 그대로.
  - ExplosiveRounds 카드: 라이플 탄착 소폭발(적/아군[FF]/자기[bAllowSelf] 규칙 동일).
- 회귀: 적만 쏠 때 데미지/크릿/히트마커 현행과 동일.

---

## 5. 미결 구현 세부 (착수 시 결정)

- **자폭과 i-frame**: 플레이어 데미지는 `ApplyContactDamage`(대시 i-frame 게이트). 자폭이 i-frame에 막혀도 OK로 둘지, 자폭은 i-frame 무시할지. **권장: 일단 i-frame 존중**(단순), 어색하면 ApplyContactDamage에 bIgnoreIFrame 인자.
- **히트마커/피드백**: 적 명중만 발사자 히트마커. 아군/자기 피해는 마커 없음(피격자는 기존 피격방향 인디케이터). FF 아군 명중 시 별도 "아군 명중" 피드백은 후속.
- **플레이어 캡슐 Visibility**: 히트스캔 단일트레이스가 아군을 맞히려면 플레이어 캡슐이 `ECC_Visibility` 블록이어야. 아니면 Pawn 멀티트레이스로 보강(스나이퍼 관통 경로 참고).
- **점블랭크 자기 오버랩**: 투사체 스폰 즉시 자기 캡슐 오버랩 → 자폭 의도와 무관한 즉발 폭발 방지(기존 머즐 클램프/초기 오버랩 가드 점검).
- **FFScale 위치**: GameState 상수 0.5 vs EditDefaultsOnly. 자폭 배수(현 1.0)도 별도 상수화 여지.

---

## 6. 새 세션 재개 프롬프트 (복붙)

```
Game.md + PROGRESS.md + Docs/P5-FriendlyFire_Plan.md 먼저 읽어.
브랜치 phase/p5-friendly-fire에서 P5 친선사격(FF) 구현을 진행한다.
- 플랜 §3 구현 순서대로. 서버권위 데미지/팀판정이라 Opus 직접 구현/검증(Haiku 위임 금지, 메모리 haiku-delegation-security-wiring).
- 단계마다 빌드+헤드리스 스모크. 최종 2-client PIE(FF on/off·자폭·아군50%·카드2종)는 사용자.
- 콘텐츠(카드 DA 2종)는 사용자 작성 — 코드 베이스만 완성.
- 확정값: 아군 50%, FF 전체범위, 자기 항상 풀, NoSelfDamage/ExplosiveRounds 카드.
```
