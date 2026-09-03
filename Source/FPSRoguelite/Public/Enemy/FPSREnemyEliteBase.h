// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystemInterface.h"
#include "GameplayAbilitySpecHandle.h" // TArray<FGameplayAbilitySpecHandle> GrantedAbilityHandles
#include "Enemy/FPSREnemyBase.h"
#include "FPSREnemyEliteBase.generated.h"

class UFPSRAbilitySystemComponent;
class UFPSREliteGameplayAbility;

/** Elite tier (ADR 0013: Docs/Architecture/0013-enemy-tier-axis-and-elite-gas.md §「결정」, 후속 행 3 실행 1+2 —
 *  엘리트 ASC 실부착 + 4중 수명주기 폐쇄 + 어빌리티 부여 시임 + 프리즈-멈춤 쿨다운). Still the SAME
 *  movement/attack/pooling/net-cull behavior as AFPSREnemyBase — no virtual IsElite()-style seam, no dedicated
 *  net-cull radius (FPSRoguelite.Enemy.NetCull still assumes a UNIFORM radius across the whole swarm, ADR 0013
 *  반론 ②, which 후속 행 3 hasn't revisited yet), no elite concurrency-cap accounting (그건 사양 B). The
 *  divergence from the plain tier added here is an AbilitySystemComponent + a content-authored ability list
 *  (GrantedAbilities) for elite-only GAS abilities to run on — still no actual ability/GE CONTENT ships with this
 *  class itself (that's a content-team job; see GrantedAbilities' own comment).
 *
 *  🔴 왜 이 클래스에 UAttributeSet 서브클래스가 없는가 (사용자 결정 — ADR 문구에서 의도적 이탈, 재해석 금지):
 *  체력은 계속 UFPSREnemyHealthComponent 에 남는다 — D1 데미지 브릿지(FPSRCombatStatics::ResolveDamage/
 *  ApplyDamage)가 킬 크레딧·흡혈 등을 그 컴포넌트 경유로 배선해 뒀고, GAS 어트리뷰트로 이관하면 그 배선을 전부
 *  다시 만들어야 한다. 범용 상태이상(얼음→슬로우 등)도 GAS 로 가면 안 된다 — Docs/SSOT/CombatWeaponCard.md
 *  §2-3-7 이 상태이상을 "플레이어 쪽 패시브 GA 가 Event.DamageTaken(태그 페이로드)을 듣는" 구조로 D3 에 이미
 *  예약해 뒀는데, 그 패시브는 플레이어 ASC 에서 산다 — 이 엘리트 ASC 에 상태이상 어트리뷰트를 얹어도 일반
 *  티어(ASC 없음)·보스(AFPSRBossBase — ASC 없음, 그 클래스 자신의 선언 참조)에는 애초에 적용될 길이 없다.
 *  즉 지금 어트리뷰트 셋을 만들면 **소비자 0인 죽은 데이터**다 — 첫 소비자(예: 엘리트 전용 어빌리티의 자원
 *  소모)가 실제로 생기는 시점에 그때 만든다. */
UCLASS()
class FPSROGUELITE_API AFPSREnemyEliteBase : public AFPSREnemyBase, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AFPSREnemyEliteBase();

	//~IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~End IAbilitySystemInterface

	/** Server: cancel any in-flight ability activation NOW (같은 배분의 원거리 홀드 선례 — AFPSREnemyBase 의
	 *  EnterDyingState/ReleaseRangedHold 참조). BEFORE Super::EnterDyingState(): 그 함수 자체가 "게임플레이는
	 *  지금 끝난다"는 선언이라(collision 도 여기서 꺼진다), ASC 정리도 그 선언의 일부로서 같은 타이밍에 앞서
	 *  실행돼야 한다. 놓치면: periodic GE 를 단 채 죽은 엘리트가 dwell + 풀 체류 내내 서버 타이머(월드
	 *  FTimerManager, GameplayEffect.cpp)로 계속 Execute 되어 숨은 휴면 액터의 복제 채널이 다시 열리고,
	 *  사망 직전 발동한 어빌리티가 시체에서 완주해 서버권위 데미지를 준다. */
	virtual void EnterDyingState() override;

	/** Server: pool release / death-dwell 종료 / kill-Z 회수 — 이 액터가 DORM_DormantAll 로 넘어가기 직전. 활성
	 *  GE 전부 제거(RemoveActiveEffects) + 진행 중 어빌리티 큐 정리를 **Super::Deactivate() 호출 앞**에 둔다:
	 *  Super 가 맨 끝에 SetNetDormancy(DORM_DormantAll) 을 거는데, 그 제거의 복제가 awake->dormant 플러시에
	 *  실려 나가려면 아직 DORM_Awake 인 동안 실행돼야 하기 때문이다(Activate 의 DORM_Awake 주석과 대칭). */
	virtual void Deactivate() override;

	/** Server: 풀에서 이 엘리트를 재활성화. Super::Activate(Location) **뒤**에 ASC 방어적 재클리어(잔여 GE 제거)
	 *  를 둔다 — HealthComponent::ResetForReuse() 가 Super 안에서 이미 실행된 것과 같은 순서(포지션)다. ASC 는
	 *  HealthComponent 와 달리 타이머·어빌리티·큐가 스스로 도는 능동 시스템이라 "리셋을 한곳에 모은다"는
	 *  단순한 유추가 성립하지 않지만, "죽음/재획득 경로마다 자기 몫을 닫는다"는 배분 자체는 원거리 홀드와
	 *  동일하게 유지한다. */
	virtual void Activate(const FVector& Location) override;

	/** Server (ADR 0013 후속 행 3 실행 1): 스테이지 전환으로 이 엘리트가 **이월**될 때 부르는 진입점 — 새
	 *  아레나로 옮겨지는 살아있는 적이라 Activate()도 Deactivate()도 밟지 않는다(그 둘은 죽음/재획득 전용
	 *  진입점 — ServerRelocateForStageCarry 로 위치만 옮겨질 뿐 이 액터의 라이프사이클은 계속된다).
	 *
	 *  진행 중이던 어빌리티만 취소하고(AbilitySystem->CancelAbilities()) **Infinite GE 는 보존한다** — 이월의
	 *  의도가 "쌓아 온 압박을 가져간다"이기 때문이다. 전환 중에는 공격 패스 전체가 early-return 이라 진행 중
	 *  어빌리티가 완주·정리할 기회가 없는 것은 원거리 충전을 취소하는 것과 같은 논리(원거리 쪽 선례 =
	 *  ServerCancelRangedForStageTransition). GE 를 지우지 않아도 시간이 도둑맞지 않는 이유는 뒤따르는 실행이
	 *  붙일 프리즈-멈춤 누산기가 전환 프리즈 동안 자동으로 멈추기 때문이다(원거리 차징이 쓰는 바로 그 관용구).
	 *
	 *  호출자 = UFPSREnemySpawnSubsystem::CancelRangedChargesForTransition **의 같은 루프**(활성 적 전수 순회) —
	 *  그 루프에서 AFPSREnemyEliteBase* 로 캐스트해서 호출한다(이 함수 자체는 엘리트 전용이라 AFPSREnemyBase
	 *  가상함수가 아니다). */
	void ServerResetEliteForStageCarry();

	/** Server: Super::ServerTickAttack(Ctx) 뒤에 프리즈-멈춤 누산기(EliteCooldownClockSeconds)를 Ctx.DeltaSeconds
	 *  만큼 쌓는다 — UFPSREliteGameplayAbility::CheckCooldown/ApplyCooldown 이 읽는 유일한 시계다. ⚠️
	 *  ServerTickAttack 은 타겟이 없는 패스에서도(빈 컨텍스트) 매번 불리므로(UFPSREnemySpawnSubsystem 의
	 *  두 호출 지점 — 타겟 있음/없음 분기 둘 다) 이 누산은 **조건 없이** 두 분기 모두에서 돌아야 한다: 어느
	 *  한쪽에서만 쌓으면 그 분기에 머무는 동안 쿨다운이 멈춰버린다. 프리즈-정확한 이유는 Ctx.DeltaSeconds 자체가
	 *  그렇기 때문이다 — 서브시스템이 런 전역 프리즈 중엔 공격 패스 전체를 early-return 해서 이 함수 호출 자체가
	 *  생기지 않는다(원거리 차징 누산기가 쓰는 바로 그 관용구). */
	virtual void ServerTickAttack(const FFPSRServerAttackContext& Ctx) override;

#if !UE_BUILD_SHIPPING
	/** Debug (FPSR.EliteDump): how many abilities this archetype is AUTHORED to grant. The dump compares it against
	 *  the ASC's live spec count — they must be equal on every life, and a live count that grows past this one is the
	 *  pool-reuse accumulation ADR 0013's failure scenario warns about. */
	int32 GetGrantedAbilityCount() const { return GrantedAbilities.Num(); }
#endif

	/** UFPSREliteGameplayAbility 가 읽는 프리즈-멈춤 누산기 값(위 ServerTickAttack 참조). 서로 다른 UObject
	 *  계층(어빌리티는 이 액터의 서브클래스가 아니다)이라 public 접근자가 필요하다. */
	float GetEliteCooldownClockSeconds() const { return EliteCooldownClockSeconds; }

protected:
	/** ASC 오너/아바타 초기화 — 오너=아바타=자기 자신(this, this). 적은 AFPSRPlayerState 같은 오너가 없다
	 *  (플레이어는 PlayerState 가 ASC 를 소유하고 캐릭터가 아바타인 소유/아바타 분리 패턴이지만, 엘리트는 그
	 *  자체가 곧 둘 다다 — ADR 소유권 표). 생성자에서는 호출 불가(그 시점엔 CDO 라 ActorInfo 가 가리킬 "실제
	 *  액터"가 없다) — 이 액터의 진짜 수명 전체에 딱 한 번만 도는 PostInitializeComponents 가 유일하고 올바른
	 *  호출 지점이다: 풀 재사용(Activate/Deactivate)은 같은 액터 인스턴스를 반복 재활용할 뿐 오너·아바타는
	 *  절대 바뀌지 않으므로 재호출이 불필요하다(BeginPlay/EndPlay 가 "실제 수명당 1회"인 것과 동일한 근거,
	 *  AFPSREnemyBase::EndPlay 의 S4 등록 해제 주석 참조).
	 *
	 *  실행 2: 여기서 시간축 런타임 가드도 함께 켠다 — `AbilitySystem->EnableTimeAxisGuard()`.
	 *  HasDuration 이거나 Period > 0 인 GE 의 **적용 자체를 엔진 레벨에서 차단**한다.
	 *  🔁 **BOSS1 에서 가드 본체가 `UFPSRAbilitySystemComponent` 로 이동했다** — 보스도 같은 계약에 묶이는데
	 *  구현이 엘리트 액터에 있으면 2벌이 되고, 두 벌은 언젠가 어긋난다. 거동은 무변이다(같은 콜백을 같은
	 *  배열에 같은 시점에 등록한다). 마찬가지로 실제 액터당 1회면 충분: 델리게이트 배열이 ASC 인스턴스
	 *  소유라 풀 재사용으로 사라지지 않는다. */
	virtual void PostInitializeComponents() override;

	/** 소유자 = 액터 자신(CreateDefaultSubobject, 플레이어의 PlayerState 소유 패턴과 다름 — 위 클래스 주석
	 *  「ADR 소유권 표」 참조). Minimal 복제 모드(생성자에서 SetReplicationMode — PostInitializeComponents 는 InitAbilityActorInfo·가드 등록 담당) — 엔진
	 *  AbilitySystemComponent.h:82 "does not work for Owned AbilitySystemComponents (Use Mixed instead)" 는
	 *  오너 클라이언트가 있는 ASC를 말하는 것이고, 엘리트는 오너 클라가 아예 없으니(스폰 서브시스템이 서버에서
	 *  풀 관리) 정확히 그 반대 조건을 만족한다 — 플레이어 ASC(AFPSRPlayerState::AbilitySystemComponent)가
	 *  Mixed 인 이유도 같은 문장의 앞부분이다(FPSRPlayerState.cpp). */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Enemy|Elite")
	TObjectPtr<UFPSRAbilitySystemComponent> AbilitySystem;

	/** 콘텐츠(기획)가 이 엘리트에 저작하는 어빌리티 목록 — ADR 0013 후속 행 3 실행 2 검증 시나리오 4단계가
	 *  요구하는 "콘텐츠가 BP 로 어빌리티를 저작할 방법" 그 자체다(로직 시임이지 콘텐츠가 아니다 — 실제 GE/GA
	 *  구현체는 이 커밋의 범위 밖). Activate() 가 풀 재사용마다 새로 부여한다(GrantedAbilityHandles 참조). 실제
	 *  자원 소모·상태이상 등이 필요해지면 그때가 어트리뷰트 셋의 첫 소비자다(클래스 주석 참조). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Enemy|Elite")
	TArray<TSubclassOf<UFPSREliteGameplayAbility>> GrantedAbilities;

private:
	/** GrantedAbilities 를 지금 부여한 핸들 — Activate() 의 "보관한 핸들 ClearAbility 후 GrantedAbilities 를
	 *  다시 GiveAbility" 왕복이 여기 저장/조회한다. 서버 전용 ASC 상태라 복제하지 않는다(카드가 부여하는 패시브
	 *  핸들 — AFPSRPlayerState::CardGrantedAbilityHandles — 과 같은 이유). */
	TArray<FGameplayAbilitySpecHandle> GrantedAbilityHandles;

	/** 프리즈-멈춤 누산기(초) — ServerTickAttack 이 Ctx.DeltaSeconds 로 쌓는다. UFPSREliteGameplayAbility 가
	 *  GetEliteCooldownClockSeconds() 로 읽는다. 서버 전용, 복제하지 않는다(원거리 FSM 의 ChargeElapsed/
	 *  CooldownElapsed 와 같은 성격 — 시각 효과가 아니라 서버 판정용 시계). */
	float EliteCooldownClockSeconds = 0.0f;

	// 시간축 런타임 가드의 본체는 UFPSRAbilitySystemComponent::RejectTimeBasedGameplayEffect 로 이동했다
	// (BOSS1 — 보스가 같은 계약을 쓰므로 1벌로 통합). 등록은 PostInitializeComponents 의 EnableTimeAxisGuard().
};
