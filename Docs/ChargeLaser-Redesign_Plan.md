# ChargeLaser 재설계 — 클릭 1회 자동 차징 시퀀스 (구현 플랜 / 세션 핸드오프)

> **상태**: 설계 확정·미구현. 브랜치 `fix/chargelaser-redesign`(코드 0줄, 이 문서만).
> **필독**: `Game.md` 허브 + `PROGRESS.md` + 본 문서 + `Docs/SSOT/Enemy.md`(§2-10 레이저=히트스캔).
> **모델 정책**: GAS 네트워킹 모델 변경 + 서버권위 데미지라 **Opus 직접 구현/검증**.
> ⚠️ **선행 의존**: 방금 머지된 **P5 FF**(`Combat/FPSRCombatStatics.h`: `ResolveDamage`/`ApplyDamage`/`AddDamageablePawnObjectTypes`) 위에서 동작 — FireBeam이 이걸 재사용한다.

---

## 0. 목표 & 확정 결정 (사용자, 2026-06-11)

현재 ChargeLaser = "꾹 눌러 차징 → 떼면 발사(차징량이 데미지 배수)". **이 기획 구조가 틀림.** 새 동작:

1. **클릭 1회 = 차징→발사 자동 진행** (꾹누르기 폐지). 차징 안 끝나면 발사 없음.
2. **차징 중**: `ChargeTickInterval`마다 소량 고정 히트스캔 데미지(`ChargeTickDamage`) 연속 (워밍업 빔, 조준 추적).
3. **차징 완료**: 본 데미지(`Damage`) 히트스캔 1발 (관통 빔, 페이오프).

- 틱 데미지 = **고정값 하나**(`ChargeTickDamage`, 디자이너가 DA에서 1~4 등으로 지정).
- 클릭하면 시퀀스 확정(취소 없음). 차징 중 이동/조준 자유(빔이 조준 추적).

---

## 1. 아키텍처 (제1원리 3줄)

1. **제1원리**: hold/release 입력·차지타임 RPC 동기화를 전부 버리고 **GA 내부 타이머 시퀀스**로 일원화 — 클릭=GA 활성, 서버가 타이머로 틱 빔+최종 빔을 권위적으로 구동. 입력/네트워크 대폭 단순화.
2. **Lyra/표준**: 채널링/차지 어빌리티는 보통 AbilityTask(WaitDelay)·타이머로 구현(표준). 현재의 cross-channel RPC 동기화는 과설계였음 — 단일 GA 타이머로 회귀.
3. **프로젝트 정합**: 빔 트레이스/데미지는 **P5 FF 헬퍼 그대로 재사용**(FireBeam). 다른 발사 GA처럼 FireOneShot 경로로 활성. 스탯만 교체.

---

## 2. 파일 단위 변경

### 2-1. 스탯 — `Weapon/FPSRWeaponTypes.h` (`FFPSRWeaponStatBlock`, ChargeLaser EditConditionHides 블록)
- **제거**: `ChargeFullDamageMultiplier`.
- **추가** (둘 다 `EditCondition = "Archetype == ChargeLaser"`):
  - `float ChargeTickDamage = 2.0f;` // 차징 중 틱당 데미지(고정)
  - `float ChargeTickInterval = 0.12f;` // 틱 간격(초), 최소 클램프 0.02
- **유지**: `ChargeTime`(차징 시간), `Damage`(완료 발사 본 데미지), `Range`, `SpreadDegrees`.
- ⚠️ `ChargeFullDamageMultiplier` 참조처 전부 제거(아래 GA). DataAsset IsDataValid의 ChargeTime 경고는 유지(2-5).

### 2-2. GA 재작성 — `AbilitySystem/Abilities/FPSRGA_WeaponFire_ChargeLaser.{h,cpp}` (핵심)
현재: `LocalOnly`, `FireComp->GetChargeStartWorldTime()` 읽어 alpha 스케일 1발. **전면 교체.**

**헤더**: 인스턴스화 + 타이머 + 캐시 + 헬퍼/콜백 + EndAbility 오버라이드.
```cpp
UCLASS()
class ... : public UFPSRGameplayAbility {
  GENERATED_BODY()
public:
  UFPSRGA_WeaponFire_ChargeLaser();
  virtual void ActivateAbility(...) override;
  virtual void EndAbility(const FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
      const FGameplayAbilityActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override; // 타이머 클리어
protected:
  void DoChargeTick();   // 타이머: FireBeam(틱, crit=false, marker=false)
  void DoFinalShot();    // 타이머: 틱타이머 정지 → FireBeam(본뎀, crit=true, marker=true) → EndAbility
  void FireBeam(float BeamDamage, bool bRollCrit, bool bSendMarker); // 현 빔로직 팩터링(현재 viewpoint 재트레이스)
  // 활성 시 캐시(서버)
  TWeakObjectPtr<APawn> CachedAvatar; TWeakObjectPtr<AController> CachedController;
  TWeakObjectPtr<UFPSRWeaponInstance> CachedInstance;
  float CachedDamage=0, CachedRange=0, CachedSpread=0, CachedTickDamage=0;
  FTimerHandle TickTimerHandle, FinalTimerHandle;
};
```

**ctor 네트워킹 결정** (둘 중 택1 — 구현 시 PIE로 확정):
- **권장: `NetExecutionPolicy = ServerOnly`** + 인스턴스화(`InstancedPerActor`). 클라 `TryActivateAbilityByClass`는 서버 활성 요청으로 라우팅 → 서버가 타이머 시퀀스 전부 구동(데미지 권위). 긴(1.5s) 차징에 예측키 이슈 없음. 클라 빔 cosmetic은 후속.
- 대안: `LocalPredicted` + FireBeam 데미지를 `HasAuthority` 게이트. 타이머는 양쪽(클라=cosmetic 준비). 다른 발사 GA와 일관되나 긴 차징 예측키 주의.
> `InstancedPerActor`면 ActivateAbility 진입 시 **캐시/타이머 상태를 리셋**(이전 stale 타이머 ClearTimer)할 것.

**ActivateAbility**(서버 권위):
1. `CommitAbility` → 실패 시 End.
2. Avatar/Controller/World null 가드.
3. **프리즈 게이트**(`GameState::IsRunPaused`) → End.
4. 스탯 해석(`Inventory->GetCurrentInstance()->GetResolvedStats()`). 없으면 End.
5. **서버 게이트**(HasAuthority): `IsReloading()||GetCurrentAmmo()<=0` → End. `ServerTryConsumeFireInterval(1/FireRate)` 실패 → End. `ConsumeAmmo(1)`.
6. 캐시 세팅(Avatar/Controller/Instance/Damage/Range/Spread/TickDamage). `ChargeTime`/`TickInterval` 로컬.
7. **프래그먼트**: FireCtx 구성 → `PreFire` + `ModifyChargeTime(ChargeTime)` 적용(차징시간 모디파이어).
8. `ChargeTime<=0`이면 즉시 `DoFinalShot()`(즉발) 후 종료.
9. 아니면 타이머:
   - `if (CachedTickDamage>0) SetTimer(TickTimerHandle, DoChargeTick, max(0.02,TickInterval), /*loop*/true);`
   - `SetTimer(FinalTimerHandle, DoFinalShot, ChargeTime, /*loop*/false);`
   - **여기서 End하지 않음**(차징 동안 어빌리티 active 유지 → 재발사 차단).

**DoFinalShot**: `ClearTimer(TickTimerHandle)` → `FireBeam(CachedDamage, true, true)` → `EndAbility(GetCurrentAbilitySpecHandle(), CurrentActorInfo, GetCurrentActivationInfo(), true, false)`.

**DoChargeTick**: `FireBeam(CachedTickDamage, false, false)`.

**FireBeam(BeamDamage, bRollCrit, bSendMarker)** = **현재 GA 빔 로직(라인 158~256)을 그대로 팩터링**, 단:
- 트레이스 시작 = **매 호출 시 `CachedController->GetPlayerViewPoint`**(조준 추적).
- 데미지 = `BeamDamage × GlobalDamageMultiplier`; `bRollCrit`이면 크릿 롤(틱은 false).
- **프리즈 중 스킵**: 진입부 `IsRunPaused` → return(데미지 없음).
- pawn-gather 양채널(`FPSRCombat::AddDamageablePawnObjectTypes`) + 벽(Visibility, 폰 ignore) + `ResolveDamage(Avatar, Hit, dmg, bAllowSelf=false, World)` + `ApplyDamage` (현행 그대로 — FF/넉백 유지).
- `bSendMarker`일 때만 적 명중 집계 → `ClientNotifyHitMarker`(틱은 마커 없음).
- 프래그먼트 `OnHitActor`는 **최종 샷에서만**(틱은 순수 소뎀). `PostFire`는 최종 후 1회.

**EndAbility 오버라이드**: `TickTimerHandle`/`FinalTimerHandle` `ClearTimer` 후 `Super::EndAbility`. (무기교체/사망으로 ASC가 어빌리티 취소 시 타이머 누수 방지.)

### 2-3. FireComponent — `Weapon/FPSRWeaponFireComponent.{h,cpp}` (제거·단순화)
- **StartFiring**: **ChargeLaser 분기 제거**(`if Archetype==ChargeLaser { bChargingLaser=...; ChargeStartWorldTime=...; return; }`). → ChargeLaser가 일반 `FireOneShot` 경로로 흐름(Single 발사 → GA 활성 + 반동).
- **StopFiring**: 차지 release 로직 전부 제거(`bChargingLaser` 블록).
- **제거 멤버/메서드**: `bChargingLaser`, `ChargeStartWorldTime`, `GetChargeStartWorldTime()`, `IsChargingLaser()`, `ServerBeginCharge()`, `ServerReleaseCharge()`, `ResetCharge()`.
- **OnWeaponEquipped**: `ResetCharge()` 호출 제거(차지 상태 없어짐). `NextFireReadyTime` 리셋은 유지.
- (FireOneShot/NextFireReadyTime/반동은 그대로 — ChargeLaser도 동일 경로 사용.)

### 2-4. Character — `Hero/FPSRCharacter.{h,cpp}` (RPC·입력 제거)
- **제거 RPC**: `ServerStartChargeLaser`/`ServerReleaseChargeLaser`(선언+구현).
- **Input_Fire**: `if (WeaponFire->IsChargingLaser()) ServerStartChargeLaser()` 분기 제거 → `StartFiring()`만.
- **Input_FireReleased**: 차지 release 분기 제거 → `StopFiring()`만.

### 2-5. 기타 정합
- **Inventory** `EquipSlot`/`OnRep_CurrentSlotIndex`: 이미 `FireComp->OnWeaponEquipped()` 호출 → OnWeaponEquipped에서 ResetCharge만 빠지면 자동 정합(Inventory 직접 변경 없음, 확인만).
- **DataAsset** `FPSRWeaponDataAsset.cpp` IsDataValid: ChargeLaser+`ChargeTime<=0` 경고 유지. (선택) `ChargeTickDamage<=0` 경고 추가.
- `ChargeFullDamageMultiplier` 잔존 참조 0건 확인(grep).

---

## 3. 주의점 (gotcha)
- **차징 중 재발사**: GA active라 서버 재활성 거부. 단 클라 FireOneShot 반동은 `NextFireReadyTime`(1/FireRate) 게이트 → **DA `FireRate ≤ 1/ChargeTime` 권장**(차징 동안 재클릭 반동 억제). 스펙시트에 명시.
- **타이머 수명**: `EndAbility`/취소에서 반드시 ClearTimer. `InstancedPerActor`면 ActivateAbility 재진입 시 stale 타이머 클리어.
- **무기교체/사망 중 차징**: equip→RefreshEquippedAbility가 GA clear→취소→EndAbility(타이머 정리). 확인.
- **프리즈**: 틱/최종 FireBeam이 IsRunPaused면 데미지 스킵(타이밍은 계속 — 1.5s 차징 중 프리즈는 엣지).
- **반동**: FireOneShot가 클릭 시 반동 적용. ChargeLaser는 RecoilVertical 낮게(또는 0) 권장 — 킥은 본래 후속 폴리시.

---

## 4. 구현 순서
1. 스탯(2-1) → 빌드 깨짐(GA가 ChargeFullDamageMultiplier 참조) 예상.
2. GA 재작성(2-2) — FireBeam 팩터링 + 타이머 + EndAbility. 빌드 복구.
3. FireComponent 제거·단순화(2-3).
4. Character RPC·입력 제거(2-4).
5. 정합 확인(2-5) + grep로 잔존 참조 0 확인.
6. 빌드 + 헤드리스 스모크. 
7. (사용자) ChargeLaser DA 작성/갱신: ChargeTime/ChargeTickDamage/ChargeTickInterval/Damage + FireRate≤1/ChargeTime.

---

## 5. 검증
- 빌드 + 스모크(`FPSRoguelite.Smoke.ModuleLoads`).
- **PIE**(반동·시퀀스 로컬, 데미지 서버권위라 2-client 권장):
  - 클릭 1회 → 자동 차징(틱 소뎀 연속) → 완료 시 본뎀 1발. **꾹 안 눌러도 됨**.
  - 차징 중 조준 이동 → 빔이 따라감(`ENABLE_DRAW_DEBUG` 시안 선).
  - 차징 중 재클릭 = 무시(재발사 안 됨).
  - 차징 중 무기 교체 → 시퀀스 취소(타이머 정리, 크래시 없음).
  - 적/아군(FF)/관통 = 기존 FF 규칙대로(틱·최종 동일).
- 회귀: 다른 무기 발사/반동/교체 무영향.

---

## 6. 미결 세부
- 네트워킹 ServerOnly vs LocalPredicted (2-2) — PIE로 확정.
- 틱 마커/소리 = 없음(스팸 방지). 차징 빔 VFX·게이지 HUD = 후속 콘텐츠.
- 풀차징 자동발사는 이미 본 설계(타이머 만료=자동) — 추가 입력 불필요.

---

## 7. 새 세션 재개 프롬프트 (복붙)
```
Game.md + PROGRESS.md + Docs/ChargeLaser-Redesign_Plan.md 먼저 읽어.
브랜치 fix/chargelaser-redesign에서 ChargeLaser 재설계를 구현한다.
- 클릭1회→자동 차징 시퀀스(틱 소뎀 연속 → 완료 시 본뎀 1발). hold/release 폐지.
- 플랜 §2 파일단위·§4 순서대로. GAS 타이머 시퀀스 + FireBeam(P5 FF 헬퍼 재사용).
- 서버권위/GAS라 Opus 직접 구현/검증. 단계마다 빌드+스모크, PIE는 사용자.
- 콘텐츠(ChargeLaser DA)는 사용자 — 코드 베이스만 완성. FireRate≤1/ChargeTime 권장.
```
