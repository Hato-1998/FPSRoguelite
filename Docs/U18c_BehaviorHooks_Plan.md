# U18c — 행동 트리거 확장 구현 플랜 (무기 훅 + GAS-native 패시브 + 미룬 기능해금)

> 설계 SSOT: `Docs/SSOT/CombatWeaponCard.md` §2-3-5(행동 트리거)·§2-4-1(Fragment 훅)·§2-3-7(속성 시임).
> 실행 프롬프트: `Docs/TaskPrompts_Master.md` §C(U18c). 선행: U18a·U18b(카드 v2 스키마·무기해금) main 머지 완료.
> 브랜치: `phase/u18c-behavior-hooks` (main 분기, --no-ff 머지). 구현=Haiku 위임 / 보안배선=Opus 직접.

## 0. 목표 (4)
1. 무기 행동 훅 신설: OnAim/OnFire/OnMiss/OnKill (+OnStatusKill 빈 시임=D3 발화) — `UFPSRWeaponFragment` virtual, 5데미지경로 공통 브릿지로 호출.
2. OnKill = "이번 타격 alive→dead 전이"(bJustKilled) — 코프스 재타격 중복 처치/킬마커 차단(동시 교정).
3. 캐릭터 행동 = GAS-native: ApplyDamage→GameplayEvent→패시브 GA. UFPSRPassiveAbility 베이스 + UCardEffect_CharacterPassive(5번째 효과).
4. 미룬 기능해금(U18b서 이연): LMG 빗나감→탄 리필(B) / 샷건·바주카 처치→재장전(C). **Feature A(ChargeLaser 연사)=전면 연기**(후속 유닛).

## 0-1. 사용자 확정(2026-06-20)
- **Feature A 전면 연기**: ChargeLaser 차징→연사는 DoFinalShot 1빔후 EndAbility라 시퀀스 수술 필요(40~80줄, load-bearing) → U18c 제외, 후속 유닛 등재.
- **c2 회복 2종만**: ① 흡혈(데미지 입힌 양의 일정 비율 회복) ② 체력 재생(초당 회복). "유휴 회복"(reset 루프) 폐기.

## 0-2. 코드 매핑 검증 결과 (실측, 프롬프트 file:line 대조)
- ✅ bJustKilled 코어: `FPSRCombatStatics.cpp:83`(ApplyDamage)·`:86`(bKilled=IsDead). 6 리더: Hitscan.cpp:225·ChargeLaser.cpp:275·WeaponMelee.cpp:164·Projectile.cpp:363·CombatStatics.cpp:189(폭발집계)·:194(넉백제외).
- ✅ FFPSRFireContext = **`FPSRWeaponFragment.h:21-37`**(프롬프트 "FireComponent.h"는 오기, 라인 일치). 7훅=`:67-81`. FDamageResult=`FPSRCombatStatics.h:30-35`.
- ⚠️ **정정**: Hitscan "OnFire/OnMiss @:317-333"은 실제로 히트마커전달(:317-326)+PostFire(:329-335). **OnFire/OnMiss 훅 현존 X — 전부 신규**.
- ✅ ChargeLaser: OnKill `:271-275`(payoff-only), OnFire/OnMiss payoff블록 `:324-344`, 탄약 `:103`(활성화당 1, warm-up tick bIsPayoffShot=false은 fragment 스킵=의도).
- ✅ Melee: **FireCtx 부재**(`:41-43` Avatar/Controller/World, `:78` Instance, `:102` HasAuthority 게이트), OnKill `:164`, 히트마커 `:172`, 쿨다운 `:105`. fragment 훅 0건(신규 구성).
- ✅ Projectile: Params 채움 `FPSRGA_WeaponFire_Projectile.cpp:178-214`(OnProjectileSpawn 이미 배선), TryDamageActor `FPSRProjectile.cpp:335-380`(bKilled `:362`), HandleImpact `:247-303`(ApplyExplosion `:265`). **FFPSRProjectileParams에 Instance 백레프 없음**(InstigatorActor만).
- ✅ Explosion: `ApplyExplosion` void `:135-216`(bAnyKill 내부집계 `:189`). 콜러 2: Projectile.cpp:265(FireCtx 부재) + FPSRWeaponFragment.cpp:15(ExplosiveRounds, full Context 有).
- ✅ GAS: `UFPSRGameplayAbility`(`FPSRGameplayAbility.h:10`, LocalPredicted/InstancedPerActor, **OnAvatarSet 없음**). ASC=PlayerState 소유. 리졸버 `GetAbilitySystemComponentFromActor`(`FPSRWeaponInventoryComponent.cpp:94`), GiveAbility idiom `:218`. ResetRunState(`FPSRPlayerState.cpp:270-318`, `:313` RemoveActiveEffects) — **어빌리티 스펙 미클리어(의도)**. SendGameplayEvent 선례 0.
- ✅ Aiming: `ServerSetAiming_Implementation`(`FPSRCharacter.cpp:468-475`, 유일 서버권위, bIsAiming 미복제). RPC `FPSRCharacter.h:115`.
- ✅ 태그: `GameplayEvent.*` 네임스페이스 존재(`DefaultGameplayTags.ini:40-44`), DamageType.*(`:19-23`), ImportTagsFromConfig=True. **`GameplayEvent.Player.DealtDamage` 추가 필요**.
- ✅ 탄약 API: GetCurrentAmmo/SetCurrentAmmo(**클램프 없음·AddAmmo 헬퍼 없음**, `FPSRWeaponInstance.cpp:74-78`), GetResolvedStats().MagSize, StartReload(권한+프리즈 게이트, `FPSRWeaponInventoryComponent.cpp:335-365`).
- ✅ 카드효과: FFPSRCardEffectContext{Player/PS/ASC/Inventory/TargetWeapon}(`FPSRCardEffect.h:24-32`), 4효과+GrantWeapon::Apply 선례(`:197-204`).

---

## c1 — 무기 훅 + bJustKilled + 5경로 공통 브릿지 [Opus 직접]

### c1-1. bJustKilled + DamageDealt (FPSRCombatStatics.cpp:81-88) ★Codex P1 반영
`FDamageResult`에 **`float DamageDealt`** 신설(실제 입힌 데미지 = HealthBefore-After 클램프). 코어:
```cpp
if (UFPSREnemyHealthComponent* HealthComp = Target->FindComponentByClass<UFPSREnemyHealthComponent>())
{
    const bool  bWasDeadBefore = HealthComp->IsDead();             // NEW
    const float HealthBefore   = HealthComp->GetHealth();          // NEW
    HealthComp->ApplyDamage(FinalDamage, Instigator, DamageType);
    Result.bApplied    = true;                                     // receiver found (관통/임팩트 지오메트리)
    Result.bWasEnemy   = true;
    Result.DamageDealt = FMath::Max(0.0f, HealthBefore - HealthComp->GetHealth()); // NEW: 실측(오버킬/코프스=0)
    Result.bKilled     = (!bWasDeadBefore && HealthComp->IsDead());// CHANGED: alive→dead 전이만
    return Result;
}
```
- **`FDamageResult` 의미 분리**(Codex P2 — bApplied 혼선 해소): `bApplied`=리시버 발견(관통/임팩트 지오메트리 구동, 코프스도 true=관통 소모 유지) / `DamageDealt`=실제 체력 감소(히트마커·GAS이벤트·킬 구동) / `bKilled`=alive→dead 전이. `.h:30-35` 주석에 3축 명시.
- **마커 게이트 4경로 변경**: `if (Result.bWasEnemy && Result.bApplied)` → `... && Result.DamageDealt > 0.0f`(Hitscan:222·Melee:163·ChargeLaser:272 + Projectile NotifyInstigatorHitMarker). → **코프스 재타격 = 마커 0(완전 inert)**. 정상 hit은 DamageDealt>0≡bApplied라 **무회귀**(코프스만 변화: 구=Kill마커버그 → 신=무마커).
- **무회귀**: 갓 죽인 적 여전히 bKilled=true(정상 킬마커·넉백제외 유지). 변화=죽음윈도우 코프스 재타격 bKilled=false·DamageDealt=0.
- **넉백 코너(수용)**: 코프스 폭발 재타격 시 `!bKilled`라 넉백 적용(시체 살짝 밀림). 갓 죽인 적 무회귀. PIE 체크항목.
- **6 리더 무회귀**: bKilled 의미만 바뀌고 4 킬마커+폭발집계(:189)+넉백제외(:194) 동일 적용. + 마커 게이트가 DamageDealt로 이동(코프스 inert).

### c1-2. 신규 훅 (FPSRWeaponFragment.h, 기존 7훅 패턴 — const, 빈 바디)
```cpp
virtual void OnAim(const FFPSRFireContext& Context, bool bAiming) const {}
virtual void OnFire(const FFPSRFireContext& Context) const {}
virtual void OnMiss(const FFPSRFireContext& Context) const {}
virtual void OnKill(const FFPSRFireContext& Context, AActor* KilledActor) const {}
virtual void OnStatusKill(const FFPSRFireContext& Context, AActor* KilledActor) const {} // D3 발화
```

### c1-3. 공통 브릿지 (namespace FPSRWeaponHooks; 선언=.h, 정의=.cpp)
```cpp
namespace FPSRWeaponHooks {
    void NotifyFire(const FFPSRFireContext& Ctx);              // 활성화당 1
    void NotifyMiss(const FFPSRFireContext& Ctx);              // 활성화당 1 (적 미타격)
    void NotifyKill(const FFPSRFireContext& Ctx, AActor* Killed); // 갓 죽인 적당 1
    void NotifyAim(const FFPSRFireContext& Ctx, bool bAiming);
}
```
- 바디: `if(!Ctx.Instance) return; for (Frag : Ctx.Instance->GetActiveFragments()) if(Frag) Frag->OnXxx(...);`
- **빈-빠른**: Instance null 조기탈출 + 무fragment=0이터레이션. 훅 바디는 `ExplosiveRounds::OnImpact`처럼 `Ctx.bAuthority` 내부 게이트(상태변경 서버only).
- 정의를 .cpp에 두는 이유: GetActiveFragments()는 FPSRWeaponInstance.h 의존(순환 include 회피).

### c1-4. 5경로 배선
| 경로 | OnKill | OnFire | OnMiss | FireCtx 비고 |
|---|---|---|---|---|
| Hitscan | ApplyToTarget `if(bKilled)` 내 per-actor `:225` | 탄약커밋 `:151` 직후(서버) | 루프후 `!bServerHit` `~:329` | 존재 `:120-126` |
| ChargeLaser | `:271-275`(payoff-only) | payoff블록 `:324`(bIsPayoffShot) | payoff `!bServerHit` | cached `:67` |
| Melee | 루프내 `:164` per-actor | swing후 `:172` | `!bAnyHit` | **신규 구성**(`:41-43`,`:78`, bAuthority=`:102`) |
| Projectile | TryDamageActor `:362` per-kill + 폭발반환 | spawn `:214` | — defer | **weak-instance 스레딩** |
| Explosion | ApplyExplosion 반환 per-kill | (투사체) | — | Fragment경로=full Ctx |

- **ChargeLaser warm-up 예외(의도, Codex P2)**: OnKill/OnFire/OnMiss는 **payoff 빔에서만**(warm-up 틱은 fragment 훅 전체 스킵=기존 설계). warm-up 칩뎀 처치도 OnKill 미발화 — 기능 소비자 없음(ChargeLaser≠처치트리거 무기), 마커도 현재 payoff-only라 일관. SSOT §2-3-5 명시.
- **OnMiss 비대칭(의도)**: 동기 경로(Hitscan/ChargeLaser/Melee)만 OnMiss(활성화 종료 시 `!bServerHit` 동기 판정). Projectile/Explosion 미스=비동기(lifetime expiry, degraded ctx)+소비자 없음(Feature B=히트스캔 LMG) → c1 미배선, 후속 시임.

### c1-5. 구조변경 2 (보안배선 핵심)
**(A) Projectile weak-instance 스레딩** — 최대 위험 (Codex P2 반영: 결정 확정):
- **확인됨**: `AFPSRProjectile::Params`는 UPROPERTY지만 **GetLifetimeReplicatedProps에 없음 = 비복제**(서버 전용). → `FFPSRProjectileParams`에 `TWeakObjectPtr<UFPSRWeaponInstance> WeaponInstance` 추가 안전(와이어 미통과). **불변식**: Params는 비복제 유지(향후 복제화 시 이 필드 제외 의무) — `.h` 주석 명시.
- GA `:192`서 채움(Params.WeaponInstance=Instance). Launch/Deactivate(ReleaseToPool)서 init/clear(다음 Launch가 Params 덮어쓰나 명시적 clear).
- TryDamageActor(`:362`, bKilled시) + HandleImpact 폭발경로서 minimal FireCtx 재구성{Avatar=Cast<APawn>(Params.InstigatorActor), Instance=weak.Get(), Controller, World, bAuthority=HasAuthority()} → NotifyKill. Instance null(스왑)이면 브릿지 조기탈출(graceful).

**(B) ApplyExplosion void→`TArray<AActor*, TInlineAllocator<8>>`** (갓 죽인 적 반환) — Codex P2 반영:
- `.h:81-82` decl + 2콜러. 내부 `KilledEnemies.Add(Target)` (bWasEnemy&&bKilled), NotifyHitMarker는 `KilledEnemies.Num()>0`로 bAnyKill 도출(거동 무회귀).
- ⚠️ **"무할당" 정정**: 폭발경로는 이미 Overlaps(TArray)+Processed(TSet) 할당 → per-폭발(per-hit 아님) 비용 클래스. `TInlineAllocator<8>`로 ≤8킬 인라인(heap 회피), >8킬만 1할당(희귀). per-hit 500적 무할당 조건과 무충돌(폭발은 저빈도).
- Projectile.cpp:265: 반환값 순회 → 재구성 FireCtx로 NotifyKill(바주카 Feature C 의존).
- FPSRWeaponFragment.cpp:15(ExplosiveRounds::OnImpact): 반환값 순회 → full Context로 NotifyKill.
- 제1원리: ① 인라인 반환=콜러 무시 가능·콜백 TFunction heap 회피. ② ApplyExplosion 이미 내부 집계 → 추출 trivial. ③ 반환이 out-param보다 호출부 간결.

### c1-6. OnAim (FPSRCharacter.cpp:468-475)
ServerSetAiming_Implementation의 IsDeadLocal 게이트 후 SetAiming 다음에, 현 무기 Instance로 FireCtx 구성 → NotifyAim(Ctx, bNewAiming). **이번엔 발화만**(지속 조준버프=활성효과 슬롯=후속). 서버권위(bIsAiming 미복제 확인).

**c1 게이트**: 빌드(-WaitMutex)+헤드리스 스모크 + PIE(킬마커 정상·코프스재타격 마커 소거·5경로 OnKill 발화).

---

## c2 — 캐릭터 GAS-native 패시브 [Opus 직접, precedent 0]

### c2-1. 이벤트브릿지 (ApplyDamage 적 브랜치) ★Codex P1 반영
- `Config/DefaultGameplayTags.ini:44` 다음: `+GameplayTagList=(Tag="GameplayEvent.Player.DealtDamage",DevComment="플레이어 데미지 적립")`.
- ApplyDamage 적 브랜치(DamageDealt 산정 후): **`DamageDealt > 0` AND 리스너 게이트** 통과 시에만 instigator(플레이어 폰) ASC에 `HandleGameplayEvent(GameplayEvent.Player.DealtDamage, &Data)`(**Data.EventMagnitude=DamageDealt 실측**, 오버킬/코프스 제외). 리졸버=`GetAbilitySystemComponentFromActor(Instigator)`.
- ⚠️ **적500 핫패스 게이트(Codex P1)**: `AFPSRCharacter`에 **`int32 DamageEventListeners`**(흡혈류 패시브 grant 시 ++, ResetRunState서 0) → ApplyDamage는 `Cast<AFPSRCharacter>(Instigator) && Char->DamageEventListeners>0`일 때만 ASC 리졸브+이벤트(흡혈 미보유 플레이어 0비용). 카운트=`UFPSRPassiveAbility::RequiresDealtDamageEvent()` true인 grant마다(스택 지원). U9 리스폰 재동기=시임(현 DBNO 없음).
- **EventMagnitude=실제뎀**: 오버킬(50HP에 1000뎀=50만 회복)·코프스(no-op=0) 익스플로잇 차단.

### c2-2. UFPSRPassiveAbility 베이스 (신규)
`: UFPSRGameplayAbility`, NetExecutionPolicy=ServerOnly, InstancedPerActor. OnAvatarSet/OnGiveAbility 오버라이드(`bActivateOnGrant`시 TryActivate — 향후 always-on 패시브용). `virtual bool RequiresDealtDamageEvent() const {return false;}`(흡혈=true, c2-1 리스너 카운트용). **`OnRemoveAbility` 정리 계약(Codex P3)**: 활성 task/타이머 EndAbility로 정리(흡혈=이벤트트리거 즉시종료라 잔존상태 0이지만 계약 명시) → ResetRunState ClearAbility가 안전.

### c2-3. UFPSRPassiveAbility_Lifesteal (흡혈)
AbilityTrigger=GameplayEvent.Player.DealtDamage(TriggerSource=GameplayEvent). ActivateAbility: EventData.EventMagnitude(뎀) 읽어 힐GE(instant, SetByCaller heal = 뎀 × HealRatio) 적용 → EndAbility.
- HealRatio = GA-authored UPROPERTY 기본값(예 0.05). ⚠️ **rarity 스케일링은 폴리시 후속**(카드 grant→GA 인스턴스 rolled magnitude 주입은 GAS상 비자명).

### c2-4. 체력 재생 (HP/sec) — 기존 인프라(코드 0)
**기존 `UCardEffect_CharacterGE` + GE_Regen(Duration=Infinite, Period=1.0s, Health 가산 모디파이어)** = 순수 콘텐츠. 패시브 GA 불요. (기존 CharacterGE의 periodic heal 동작 = 콘텐츠 검증항목.)

### c2-5. UCardEffect_CharacterPassive (5번째 효과)
`: UFPSRCardEffect`, `TSubclassOf<UFPSRPassiveAbility> PassiveAbility`. Apply→`Context.ASC->GiveAbility(FGameplayAbilitySpec(PassiveAbility,1,INDEX_NONE,nullptr))` + 핸들을 PS 추적배열에 append. CanApply(ASC 유효). GetDescription. (GrantWeapon::Apply 패턴 미러, 2-pass 트랜잭션 계약.)

### c2-6. 카드granted 핸들 부기 (신규)
`AFPSRPlayerState`: `TArray<FGameplayAbilitySpecHandle> CardGrantedAbilityHandles` + `AddCardGrantedAbility(Handle)`. ResetRunState(`:313` RemoveActiveEffects **전**): 핸들 순회 `ASC->ClearAbility(H)` → Empty(). (기존 무기 어빌리티는 장착 재부여라 미터치 유지.)

### c2-7. 콘텐츠
흡혈 카드(UCardEffect_CharacterPassive→Lifesteal) + 재생 카드(UCardEffect_CharacterGE→GE_Regen) + 힐GE 2종(흡혈=instant SetByCaller, 재생=periodic infinite) + 캐릭터 풀 등록. SSOT §2-3-5 문구 GameplayEvent.* 정합.

**c2 게이트**: 빌드+스모크 + PIE(흡혈 카드픽→데미지시 비율회복·재생 카드픽→초당회복·런종료 클리어).

---

## c3 — 미룬 기능해금 B+C [Haiku 구현/Opus 검증]

### c3-B. LMG 빗나감→탄 리필
`UFPSRFragment_AmmoOnMiss : UFPSRWeaponFragment`, OnMiss override:
```cpp
if (!Ctx.bAuthority || !Ctx.Instance) return;
const int32 Max = Ctx.Instance->GetResolvedStats().MagSize;
Ctx.Instance->SetCurrentAmmo(FMath::Min(Ctx.Instance->GetCurrentAmmo() + RefillAmount, Max));
```
필드 RefillAmount(기본 1). ExplosiveRounds 권한패턴 미러.

### c3-C. 샷건·바주카 처치→재장전
`UFPSRFragment_ReloadOnKill : UFPSRWeaponFragment`, OnKill override:
```cpp
if (!Ctx.bAuthority || !Ctx.Instance) return;
Ctx.Instance->SetCurrentAmmo(Ctx.Instance->GetResolvedStats().MagSize); // 즉시 풀 (bInstant 기본 true)
// bInstant=false 시: Avatar->FindComponentByClass<UFPSRWeaponInventoryComponent>()->StartReload();
```
바주카 킬=폭발경로(c1-5B 반환값 → 재구성 FireCtx). 스왑 시 Instance null→무동작(수용).

### c3 콘텐츠
DA fragment 2종 + LMG/Shotgun/Bazooka `UnlockableFeatures[]` 배치(헤드리스 commandlet, `Scripts/u18b_migrate.py` 패턴). 명명 SSOT §2-3-8(`DA_Frag_<Weapon>_<Behavior>`).

**c3 게이트**: 빌드+스모크 + PIE(B 리필·C 재장전).

---

## 검증/완료
각 서브유닛: 빌드(-WaitMutex)+헤드리스 스모크 Result={Success} + 사용자 PIE → Codex 플랜↔목표 게이트(구현 직전, 5분 워치독) + 머지게이트(`-Base main`) → PROGRESS·TaskPrompts 갱신 + 콘텐츠 동반커밋 질문 + --no-ff 머지. **U3 시임**: 보스 OnKill 미발화(GAS 경로) — 여기서 안 함(U3 소비). OnStatusKill 빈 시임(D3).

## 무회귀 절대조건
- 기존 7훅(PreFire/ModifyShotCount/OnHitActor/PostFire/OnProjectileSpawn/ModifyChargeTime/OnImpact)·U18b 거동(무기해금·라우팅·탄도) 유지.
- bKilled 의미 변경 6 리더 동일 적용 → 정상 킬 그대로·코프스 재타격만 차단.
- 적500 예산: 훅 루프 빈-빠른·무할당·서버only. 이벤트송신 플레이어-한정.
- 서버권위: 모든 상태변경 훅 bAuthority 게이트. 투사체 브릿지 핸들 클라 누출 금지.
