// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Abilities/FPSRBossGameplayAbility.h"
#include "Boss/FPSRBossBase.h"

AFPSRBossBase* UFPSRBossGameplayAbility::GetBoss() const
{
	const FGameplayAbilityActorInfo* Info = GetCurrentActorInfo();
	return Info ? Cast<AFPSRBossBase>(Info->AvatarActor.Get()) : nullptr;
}

float UFPSRBossGameplayAbility::GetPatternClock() const
{
	const AFPSRBossBase* Boss = GetBoss();
	return Boss ? Boss->GetPatternClockSeconds() : -1.0f;
}

float UFPSRBossGameplayAbility::GetCooldownClockSeconds(const FGameplayAbilityActorInfo* ActorInfo) const
{
	// -1 = "clock unreadable" -> the base fails open (see UFPSRFreezeCooldownAbility::GetCooldownClockSeconds).
	const AFPSRBossBase* Boss = ActorInfo ? Cast<AFPSRBossBase>(ActorInfo->AvatarActor.Get()) : nullptr;
	return Boss ? Boss->GetPatternClockSeconds() : -1.0f;
}

void UFPSRBossGameplayAbility::EnterStage(EFPSRBossPatternStage NewStage)
{
	Stage = NewStage;
	StageElapsedSeconds = 0.0f;

	// The replicated stage is what lets a Blueprint play a wind-up and a recovery. Published from ONE place so a new
	// stage can never be added without clients hearing about it.
	if (AFPSRBossBase* Boss = GetBoss())
	{
		Boss->ServerSetPatternStage(NewStage);
	}
}

void UFPSRBossGameplayAbility::ActivateAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	// Committing is what stamps the freeze-paused cooldown. Doing it here — in the base — is also why BP children
	// must not override ActivateAbility: an override would silently drop both the cooldown and the prep stage.
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	AFPSRBossBase* Boss = GetBoss();
	if (!Boss || !Boss->HasAuthority())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Every pattern starts by winding up. Nothing is spawned yet — that is the system half of the "telegraph before
	// anything that can hurt you" rule; each pattern's own grace (fuse / stationary beam / hovering orbs) is the
	// second half and stays inside the pattern, because those three mean different things.
	EnterStage(EFPSRBossPatternStage::Prep);
}

void UFPSRBossGameplayAbility::ServerTickPattern(float DeltaSeconds)
{
	if (Stage == EFPSRBossPatternStage::Finished)
	{
		return;
	}

	StageElapsedSeconds += DeltaSeconds;

	switch (Stage)
	{
	case EFPSRBossPatternStage::Prep:
		if (StageElapsedSeconds >= PrepSeconds)
		{
			EnterStage(EFPSRBossPatternStage::Execute);
			ServerBeginExecute();
		}
		break;

	case EFPSRBossPatternStage::Execute:
		// The derived pattern owns only this: it says when it is done, and the base handles what comes before and
		// after. A pattern that ends instantly still pays the recovery, so "no gap between patterns" is impossible
		// to author by accident.
		if (ServerTickExecute(DeltaSeconds))
		{
			ServerEndExecute();
			EnterStage(EFPSRBossPatternStage::Recovery);
		}
		break;

	case EFPSRBossPatternStage::Recovery:
		if (StageElapsedSeconds >= RecoverySeconds)
		{
			EnterStage(EFPSRBossPatternStage::Finished);
			EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
		}
		break;

	default:
		break;
	}
}

void UFPSRBossGameplayAbility::EndAbility(
	const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility,
	bool bWasCancelled)
{
	// A cancel (the boss dying calls CancelAbilities) skips the normal Execute -> Recovery transition entirely, so
	// without this the ONE case where cleanup matters most — dying mid-pattern — would be the one case that skips it.
	if (Stage == EFPSRBossPatternStage::Execute)
	{
		ServerEndExecute();
	}
	Stage = EFPSRBossPatternStage::Finished;

	if (AFPSRBossBase* Boss = GetBoss())
	{
		Boss->ServerSetPatternStage(EFPSRBossPatternStage::Finished);
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
