// Copyright Epic Games, Inc. All Rights Reserved.

#include "AbilitySystem/Attributes/FPSRHealthSet.h"
#include "Hero/FPSRCharacter.h"
#include "Net/UnrealNetwork.h"
#include "Math/UnrealMathUtility.h"

UFPSRHealthSet::UFPSRHealthSet()
{
	InitHealth(100.0f);
	InitMaxHealth(100.0f);
	InitShield(0.0f);
	InitMaxShield(0.0f);
	InitShieldDefense(1.0f);
	InitHealthDefense(1.0f);
}

void UFPSRHealthSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRHealthSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRHealthSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRHealthSet, Shield, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRHealthSet, MaxShield, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRHealthSet, ShieldDefense, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UFPSRHealthSet, HealthDefense, COND_None, REPNOTIFY_Always);
}

void UFPSRHealthSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRHealthSet, Health, OldValue);
}

void UFPSRHealthSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRHealthSet, MaxHealth, OldValue);
}

void UFPSRHealthSet::OnRep_Shield(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRHealthSet, Shield, OldValue);
}

void UFPSRHealthSet::OnRep_MaxShield(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRHealthSet, MaxShield, OldValue);
}

void UFPSRHealthSet::OnRep_ShieldDefense(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRHealthSet, ShieldDefense, OldValue);
}

void UFPSRHealthSet::OnRep_HealthDefense(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UFPSRHealthSet, HealthDefense, OldValue);
}

void UFPSRHealthSet::ClampAttribute(const FGameplayAttribute& Attribute, float& NewValue) const
{
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxHealth());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		NewValue = FMath::Max(NewValue, 1.0f);
	}
	else if (Attribute == GetShieldAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.0f, GetMaxShield());
	}
	else if (Attribute == GetMaxShieldAttribute())
	{
		NewValue = FMath::Max(NewValue, 0.0f);
	}
	else if (Attribute == GetShieldDefenseAttribute() || Attribute == GetHealthDefenseAttribute())
	{
		// Multiplier, not flat mitigation (VIT1 §5-1 FMitigation note) — 0 is a legal "immune on this layer" card,
		// negative is not.
		NewValue = FMath::Max(NewValue, 0.0f);
	}
}

void UFPSRHealthSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UFPSRHealthSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampAttribute(Attribute, NewValue);
}

void UFPSRHealthSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);

	if (Attribute == GetHealthAttribute())
	{
		if (!bOutOfHealthBroadcast && OldValue > 0.0f && NewValue <= 0.0f)
		{
			bOutOfHealthBroadcast = true;
			OnOutOfHealth.Broadcast();
		}
		else if (NewValue > 0.0f)
		{
			bOutOfHealthBroadcast = false;
		}
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		// A MaxHealth increase (e.g. a card) also heals current Health by the same amount, so picking a
		// +MaxHealth upgrade feels immediately impactful. SetHealth re-clamps to the new max. Decreases are
		// left to the Health clamp (current health is capped to max on its next change).
		// Authority-only: the server heals and Health replicates to clients. A replicated MaxHealth change
		// also fires PostAttributeChange on clients (for active/infinite GEs), so guarding here prevents the
		// client from double-applying the heal on top of the already-replicated Health.
		const float Delta = NewValue - OldValue;
		const UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
		if (Delta > 0.0f && ASC && ASC->IsOwnerActorAuthoritative())
		{
			SetHealth(GetHealth() + Delta);
		}
	}
	else if (Attribute == GetMaxShieldAttribute())
	{
		// VIT1 §5-4 rule 1/2 — symmetric with MaxHealth's own rule above: a MaxShield increase (e.g. a card) raises
		// Shield by the same amount; MaxShield dropping to 0 ("forgo shield, gain health") force-zeroes Shield too,
		// rather than leaving a ghost value in the HUD until Shield's own [0, MaxShield] clamp next happens to run.
		// Authority-only, same reasoning as MaxHealth's guard.
		const UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
		if (ASC && ASC->IsOwnerActorAuthoritative())
		{
			if (NewValue > OldValue)
			{
				SetShield(GetShield() + (NewValue - OldValue));
			}
			else if (NewValue <= 0.0f)
			{
				SetShield(0.0f);
			}
		}
	}
	else if (Attribute == GetShieldAttribute())
	{
		// VIT1 requirement 5 — warn once on the break edge, mirroring the Health->OnOutOfHealth guard above exactly
		// (the same double-fire hazards apply: a hit that lands exactly on 0, a second 0-damage change while already
		// broken, etc.).
		if (!bShieldBrokenBroadcast && OldValue > 0.0f && NewValue <= 0.0f)
		{
			bShieldBrokenBroadcast = true;
			OnShieldBroken.Broadcast();
		}
		else if (NewValue > 0.0f)
		{
			bShieldBrokenBroadcast = false;
		}

		// §5-4-1 anchor rebase (G1 P2-1) — SERVER ONLY. A replicated Shield change also fires PostAttributeChange on
		// clients, but no client ever reads AFPSRCharacter's regen anchor, so this would be harmless left ungated;
		// gating explicitly keeps there being exactly one place (the server) that ever writes the anchor's truth.
		const UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
		if (ASC && ASC->IsOwnerActorAuthoritative())
		{
			// 🔴 GetOwningActor() returns the PlayerState (the ASC's owner), NOT the character — casting that
			// directly to AFPSRCharacter silently yields nullptr, and the anchor would never rebase again (this
			// compiles fine and passes unit tests while quietly reintroducing P2-1: a card's +Shield would keep
			// getting erased by the regen driver's next tick). GetAvatarActor() is the pawn
			// AFPSRCharacter::InitAbilitySystem sets via InitAbilityActorInfo(PS, this) (FPSRCharacter.cpp:506) — one
			// hop, always the live pawn (null only in the brief pawn-swap window, handled as a no-op below).
			// RebaseShieldAnchorOnExternalWrite itself no-ops while the character's own FScopedVitalsWrite is active
			// (the regen driver / ApplyContactDamage / profile init / revive already set the anchor explicitly), so
			// this call is unconditional here — the guard lives on the character's side of the boundary.
			if (AFPSRCharacter* Character = Cast<AFPSRCharacter>(ASC->GetAvatarActor()))
			{
				Character->RebaseShieldAnchorOnExternalWrite(NewValue - OldValue);
			}
		}
	}
}
