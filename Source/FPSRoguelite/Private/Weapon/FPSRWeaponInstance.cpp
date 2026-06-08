// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Core/FPSRPlayerState.h"

#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

void UFPSRWeaponInstance::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRWeaponInstance, Source, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRWeaponInstance, Modifiers, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRWeaponInstance, ActiveFragments, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRWeaponInstance, CurrentAmmo, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UFPSRWeaponInstance, bReloading, Params);
}

void UFPSRWeaponInstance::InitializeWithSource(UFPSRWeaponDataAsset* InSource)
{
	Source = InSource;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInstance, Source, this);
	MarkResolvedDirty();
}

void UFPSRWeaponInstance::AddModifier(const FFPSRWeaponStatMod& Mod)
{
	Modifiers.Mods.Add(Mod);
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInstance, Modifiers, this);
	MarkResolvedDirty();
}

bool UFPSRWeaponInstance::HasFragment(const UFPSRWeaponFragment* Fragment) const
{
	return Fragment != nullptr && ActiveFragments.Contains(Fragment);
}

int32 UFPSRWeaponInstance::GetFragmentStackCount(const UFPSRWeaponFragment* Fragment) const
{
	if (!Fragment)
	{
		return 0;
	}
	int32 Count = 0;
	for (const TObjectPtr<UFPSRWeaponFragment>& Active : ActiveFragments)
	{
		if (Active == Fragment)
		{
			++Count;
		}
	}
	return Count;
}

void UFPSRWeaponInstance::AddFragment(UFPSRWeaponFragment* Fragment)
{
	// Stackable up to the fragment's MaxStacks: each accepted pick appends another copy, so the per-element
	// fire hooks (e.g. MultiShot's ModifyShotCount) apply once per stack. Server-authoritative.
	if (!Fragment || GetFragmentStackCount(Fragment) >= FMath::Max(Fragment->MaxStacks, 1))
	{
		return;
	}
	ActiveFragments.Add(Fragment);
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInstance, ActiveFragments, this);
}

void UFPSRWeaponInstance::SetCurrentAmmo(int32 NewAmmo)
{
	CurrentAmmo = NewAmmo;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInstance, CurrentAmmo, this);
}

void UFPSRWeaponInstance::SetReloading(bool bNewReloading)
{
	bReloading = bNewReloading;
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInstance, bReloading, this);
}

void UFPSRWeaponInstance::OnRep_Source()
{
	MarkResolvedDirty();
}

void UFPSRWeaponInstance::OnRep_Modifiers()
{
	MarkResolvedDirty();
}

const FFPSRWeaponStatBlock& UFPSRWeaponInstance::GetResolvedStats()
{
	if (bResolvedDirty)
	{
		RecomputeResolved();
		bResolvedDirty = false;
	}
	return CachedResolved;
}

AFPSRPlayerState* UFPSRWeaponInstance::ResolveOwningPlayerState() const
{
	const UFPSRWeaponInventoryComponent* Comp = Cast<UFPSRWeaponInventoryComponent>(GetOuter());
	const APawn* Pawn = Comp ? Cast<APawn>(Comp->GetOwner()) : nullptr;
	return Pawn ? Pawn->GetPlayerState<AFPSRPlayerState>() : nullptr;
}

void UFPSRWeaponInstance::RecomputeResolved()
{
	if (!Source)
	{
		CachedResolved = FFPSRWeaponStatBlock();
		return;
	}

	CachedResolved = Source->BaseStats;

	// Accumulate additive and percent contributions per axis from ThisWeapon (this instance) and AllWeapons
	// (owning PlayerState) modifier stacks.
	struct FAxisAccum { float Add = 0.0f; float Pct = 0.0f; };
	// Keyed by the enum's integer value to avoid relying on enum-class TMap hashing.
	TMap<int32, FAxisAccum> Accum;

	auto GatherStack = [&Accum](const FFPSRWeaponModContainer& Container)
	{
		for (const FFPSRWeaponStatMod& Mod : Container.Mods)
		{
			FAxisAccum& A = Accum.FindOrAdd(static_cast<int32>(Mod.Stat));
			if (Mod.Op == EFPSRWeaponModOp::Additive)
			{
				A.Add += Mod.Value;
			}
			else
			{
				A.Pct += Mod.Value;
			}
		}
	};

	GatherStack(Modifiers);
	if (const AFPSRPlayerState* PS = ResolveOwningPlayerState())
	{
		GatherStack(PS->GetAllWeaponsMods());
	}

	for (const TPair<int32, FAxisAccum>& Pair : Accum)
	{
		const float Add = Pair.Value.Add;
		const float Mult = 1.0f + Pair.Value.Pct;
		switch (static_cast<EFPSRWeaponStat>(Pair.Key))
		{
		case EFPSRWeaponStat::MagSize:
			CachedResolved.MagSize = FMath::Max(1, FMath::RoundToInt((CachedResolved.MagSize + Add) * Mult));
			break;
		case EFPSRWeaponStat::FireRate:
			CachedResolved.FireRate = FMath::Max(0.01f, (CachedResolved.FireRate + Add) * Mult);
			break;
		case EFPSRWeaponStat::RecoilVertical:
			CachedResolved.RecoilVertical = FMath::Max(0.0f, (CachedResolved.RecoilVertical + Add) * Mult);
			break;
		case EFPSRWeaponStat::Damage:
			CachedResolved.Damage = FMath::Max(0.0f, (CachedResolved.Damage + Add) * Mult);
			break;
		case EFPSRWeaponStat::SpreadDegrees:
			CachedResolved.SpreadDegrees = FMath::Max(0.0f, (CachedResolved.SpreadDegrees + Add) * Mult);
			break;
		case EFPSRWeaponStat::ReloadTime:
			CachedResolved.ReloadTime = FMath::Max(0.0f, (CachedResolved.ReloadTime + Add) * Mult);
			break;
		default:
			break;
		}
	}
}
