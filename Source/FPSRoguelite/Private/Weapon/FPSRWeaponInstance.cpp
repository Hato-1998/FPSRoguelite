// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponInstance.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "Weapon/FPSRWeaponInventoryComponent.h"
#include "Core/FPSRPlayerState.h"
#include "Hero/FPSRCharacter.h"

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

bool UFPSRWeaponInstance::AddFragment(UFPSRWeaponFragment* Fragment)
{
	// Stackable up to the fragment's MaxStacks: each accepted pick appends another copy, so the per-element fire
	// hooks (e.g. MultiShot's ModifyShotCount) apply once per stack. Server-authoritative.
	if (!Fragment)
	{
		return false;
	}
	const int32 ExistingStacks = GetFragmentStackCount(Fragment);
	if (ExistingStacks >= FMath::Max(Fragment->MaxStacks, 1))
	{
		return false; // already at this fragment's stack limit
	}
	// The slot cap applies ONLY to a brand-new distinct fragment — stacking one already held doesn't consume a new
	// slot. A new distinct pick at the cap must go through the replacement flow (RemoveFragment first), so reject here.
	if (ExistingStacks == 0 && GetDistinctFragmentCount() >= GetMaxFragmentSlots())
	{
		return false;
	}
	ActiveFragments.Add(Fragment);
	MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInstance, ActiveFragments, this);
	return true;
}

void UFPSRWeaponInstance::RemoveFragment(UFPSRWeaponFragment* Fragment)
{
	// Remove every stack of this fragment (the replacement flow drops a whole distinct slot). Server-authoritative.
	if (!Fragment)
	{
		return;
	}
	if (ActiveFragments.Remove(Fragment) > 0)
	{
		MARK_PROPERTY_DIRTY_FROM_NAME(UFPSRWeaponInstance, ActiveFragments, this);
	}
}

void UFPSRWeaponInstance::GetDistinctFragments(TArray<UFPSRWeaponFragment*>& OutFragments) const
{
	OutFragments.Reset();
	for (const TObjectPtr<UFPSRWeaponFragment>& Frag : ActiveFragments)
	{
		if (Frag && !OutFragments.Contains(Frag.Get()))
		{
			OutFragments.Add(Frag.Get());
		}
	}
}

int32 UFPSRWeaponInstance::GetDistinctFragmentCount() const
{
	TArray<UFPSRWeaponFragment*> Distinct;
	GetDistinctFragments(Distinct);
	return Distinct.Num();
}

int32 UFPSRWeaponInstance::GetMaxFragmentSlots() const
{
	// Data-driven per weapon; fall back to the DA default when no source is bound.
	return Source ? FMath::Max(1, Source->MaxFragmentSlots) : 3;
}

bool UFPSRWeaponInstance::IsAtFragmentSlotCap() const
{
	return GetDistinctFragmentCount() >= GetMaxFragmentSlots();
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
	// SetReloading is server-authoritative (only ever called on the authority). The authority does NOT receive its
	// own RepNotify, so a listen-server host would miss reload cosmetics — its own 1P arms montage AND the 3P body
	// montages of remote players on the server-rendered copies. Drive the notify manually here so the host sees them
	// (HandleReloadStateChanged no-ops on a dedicated server where nothing is rendered). Remote clients still get the
	// real replicated OnRep exactly once, so there is no double-play.
	OnRep_Reloading();
}

void UFPSRWeaponInstance::OnRep_Source()
{
	MarkResolvedDirty();
}

void UFPSRWeaponInstance::OnRep_Modifiers()
{
	MarkResolvedDirty();
}

void UFPSRWeaponInstance::OnRep_Reloading()
{
	// Route the server-confirmed reload edge to the owning character's cosmetics. This fires on every client that
	// holds this replicated subobject; the character gates owner-vs-remote (1P arms vs 3P body) and the freeze
	// internally. Only the current weapon reloads, so this instance is the equipped one. No new replication —
	// this is a RepNotify on the pre-existing bReloading flag.
	const UFPSRWeaponInventoryComponent* Comp = Cast<UFPSRWeaponInventoryComponent>(GetOuter());
	if (AFPSRCharacter* Char = Comp ? Cast<AFPSRCharacter>(Comp->GetOwner()) : nullptr)
	{
		Char->HandleReloadStateChanged(bReloading);
	}
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

	// Exclusions (optional) drop mods on listed axes — used ONLY for the AllWeapons stack, so a broad "all weapons"
	// card can't touch an axis the weapon opted out of (e.g. ChargeLaser recoil). ThisWeapon mods always apply.
	auto GatherStack = [&Accum](const FFPSRWeaponModContainer& Container, const TArray<EFPSRWeaponStat>* Exclusions)
	{
		for (const FFPSRWeaponStatMod& Mod : Container.Mods)
		{
			if (Exclusions && Exclusions->Contains(Mod.Stat))
			{
				continue; // this weapon opts out of AllWeapons mods on this axis
			}
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

	GatherStack(Modifiers, nullptr); // ThisWeapon: never filtered (deliberately targeted at this weapon)
	if (const AFPSRPlayerState* PS = ResolveOwningPlayerState())
	{
		GatherStack(PS->GetAllWeaponsMods(), &Source->AllWeaponsStatExclusions);
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
