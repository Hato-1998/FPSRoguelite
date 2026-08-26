// Copyright Epic Games, Inc. All Rights Reserved.

#include "Enemy/FPSREnemyEliteBase.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "AbilitySystem/Abilities/FPSREliteGameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameplayAbilitySpec.h"
#include "Core/FPSRLogChannels.h"

AFPSREnemyEliteBase::AFPSREnemyEliteBase()
{
	AbilitySystem = CreateDefaultSubobject<UFPSRAbilitySystemComponent>(TEXT("AbilitySystem"));
	AbilitySystem->SetIsReplicated(true);

	// Minimal, NOT Mixed like the player (AFPSRPlayerState::AbilitySystemComponent, FPSRPlayerState.cpp) — see the
	// header's AbilitySystem field comment for the engine quote (AbilitySystemComponent.h:82) this contrast rests on.
	AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
}

UAbilitySystemComponent* AFPSREnemyEliteBase::GetAbilitySystemComponent() const
{
	return AbilitySystem;
}

void AFPSREnemyEliteBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Owner == Avatar == self — see the header doc on this override for why (no PlayerState to split them across,
	// and why this can't move to the constructor). Runs once per actor real-lifetime; not re-run on pool reuse.
	if (AbilitySystem)
	{
		AbilitySystem->InitAbilityActorInfo(this, this);

		// 실행 2 시간축 런타임 가드 등록 — see header doc (this override + RejectTimeBasedGameplayEffect's own
		// comment). Once per actor real-lifetime, same as InitAbilityActorInfo just above.
		AbilitySystem->GameplayEffectApplicationQueries.Add(
			FGameplayEffectApplicationQuery::CreateUObject(this, &AFPSREnemyEliteBase::RejectTimeBasedGameplayEffect));
	}
}

void AFPSREnemyEliteBase::EnterDyingState()
{
	// Cancel any in-flight ability activation BEFORE Super — see header doc. Mirrors Super::EnterDyingState's own
	// ReleaseRangedHold call inside its body: "gameplay ends now" covers the ranged hold AND the ASC alike, so both
	// close at the same point in the teardown, ahead of the collision-off Super performs.
	if (AbilitySystem)
	{
		AbilitySystem->CancelAbilities();
	}
	Super::EnterDyingState();
}

void AFPSREnemyEliteBase::Deactivate()
{
	// Strip active GEs BEFORE Super::Deactivate() flips this actor to DORM_DormantAll at the end of its body — see
	// header doc: the removal's replication must ride the awake->dormant flush, which only happens while still
	// DORM_Awake. (CancelAbilities already ran earlier, in EnterDyingState, for the death path; a non-death teardown
	// — e.g. an immediate ReleaseEnemy with no dying dwell — reaches Deactivate directly, so this doesn't assume
	// EnterDyingState always ran first.)
	if (AbilitySystem)
	{
		AbilitySystem->RemoveActiveEffects(FGameplayEffectQuery());
	}
	Super::Deactivate();
}

void AFPSREnemyEliteBase::Activate(const FVector& Location)
{
	Super::Activate(Location);

	// Defensive re-clear AFTER Super — same position as HealthComponent::ResetForReuse() inside Super's own body.
	// Deactivate/EnterDyingState already close this life's ASC state on every teardown path, but Activate is the
	// ONE pool-reuse entry point every life passes through, so belt-and-suspenders here means a reused elite can
	// never start a new life carrying a stale GE forward from a path that missed a teardown call.
	if (AbilitySystem)
	{
		AbilitySystem->RemoveActiveEffects(FGameplayEffectQuery());
	}

	// 실행 2 어빌리티 부여 시임: 보관한 핸들 ClearAbility 후 GrantedAbilities 를 다시 GiveAbility (명시적 왕복 —
	// 첫 스폰에서는 GrantedAbilityHandles 가 비어 있어 클리어 루프가 그냥 no-op). InstancedPerActor 라
	// ClearAbility 가 이전 삶의 어빌리티 인스턴스를 함께 끝내므로, UFPSREliteGameplayAbility::
	// LastActivationClockSeconds 같은 인스턴스 상태도 새 삶에서 자동으로 새 인스턴스로 리셋된다.
	if (AbilitySystem)
	{
		for (const FGameplayAbilitySpecHandle& Handle : GrantedAbilityHandles)
		{
			AbilitySystem->ClearAbility(Handle);
		}
		GrantedAbilityHandles.Reset();

		for (const TSubclassOf<UFPSREliteGameplayAbility>& AbilityClass : GrantedAbilities)
		{
			if (AbilityClass)
			{
				GrantedAbilityHandles.Add(AbilitySystem->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this)));
			}
		}
	}

	// Freeze-paused clock: not load-bearing for correctness (a freshly re-granted ability instance's own
	// LastActivationClockSeconds sentinel already makes its first CheckCooldown this life pass regardless of what
	// this clock currently holds — see that field's comment), but resetting it here matches every OTHER per-life
	// accumulator's pool-reuse hygiene in this class's Super (PursuitState, KnockbackVelocityXY, etc.).
	EliteCooldownClockSeconds = 0.0f;
}

void AFPSREnemyEliteBase::ServerResetEliteForStageCarry()
{
	if (!HasAuthority())
	{
		return;
	}

	// See header doc: cancel in-progress abilities only — deliberately do NOT touch active GEs. Infinite GEs (the
	// accumulated pressure the carry-over is meant to preserve) ride along with the enemy untouched.
	if (AbilitySystem)
	{
		AbilitySystem->CancelAbilities();
	}
}

void AFPSREnemyEliteBase::ServerTickAttack(const FFPSRServerAttackContext& Ctx)
{
	Super::ServerTickAttack(Ctx);

	// See header doc: unconditional — this pass fires for BOTH the has-target and the empty-context (no-target)
	// branch alike (UFPSREnemySpawnSubsystem's two ServerTickAttack call sites), so gating this behind a target
	// check would silently stall the cooldown clock whenever this elite has no target in range.
	EliteCooldownClockSeconds += Ctx.DeltaSeconds;
}

bool AFPSREnemyEliteBase::RejectTimeBasedGameplayEffect(const FActiveGameplayEffectsContainer& ActiveGEContainer, const FGameplayEffectSpec& Spec) const
{
	const bool bHasDuration = Spec.Def && Spec.Def->DurationPolicy == EGameplayEffectDurationType::HasDuration;
	const bool bHasPeriod = Spec.GetPeriod() > 0.0f;
	if (!bHasDuration && !bHasPeriod)
	{
		return true; // Instant, or Infinite with NO period — allowed (see header doc for why Period must be checked too)
	}

	// Dev-time noise only (ensureMsgf never crashes, in Shipping or otherwise — that's the whole point of using it
	// instead of check/checkf here) + an always-fires log line so a live server's repeat offenders aren't silenced
	// after ensure's own one-shot-per-callsite suppression.
	ensureMsgf(false, TEXT("[Elite ASC] Rejected GE '%s' on %s — HasDuration/periodic GE timers run on the world ")
		TEXT("FTimerManager and are NOT paused by the §2-2 freeze gate. Use UFPSREliteGameplayAbility::")
		TEXT("CooldownSeconds, or ServerTickAttack's freeze-paused accumulator, instead."),
		*GetNameSafe(Spec.Def), *GetName());
	UE_LOG(LogFPSR, Warning,
		TEXT("[Elite ASC] Rejected time-based GE '%s' on %s (HasDuration=%d, Period=%.2f)."),
		*GetNameSafe(Spec.Def), *GetName(), bHasDuration ? 1 : 0, Spec.GetPeriod());
	return false;
}
