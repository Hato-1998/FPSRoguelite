// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponPartSelector.h"
#include "Weapon/FPSRWeaponPartRule.h"
#include "Weapon/FPSRWeaponPartCondition.h"

void FPSRWeaponPartSelector::SelectParts(const UFPSRWeaponDataAsset& Weapon,
	const FFPSRWeaponStatBlock& Resolved,
	const TArray<TObjectPtr<UFPSRWeaponFragment>>& Fragments,
	TArray<FFPSRWeaponPartAttachment>& OutSelected)
{
	OutSelected.Reset();

	// Slotless structural parts (WeaponParts1P) are always-attached — unchanged from the pre-W-U1 behavior. Null-part
	// filtering happens at attach time (RebuildPartsFromSelection), so every entry passes through here.
	for (const FFPSRWeaponPartAttachment& P : Weapon.WeaponParts1P)
	{
		OutSelected.Add(P);
	}

	// Per-slot winner: highest Tier, then highest Priority, then lowest rule index (deterministic total order).
	struct FWinner { const FFPSRWeaponPartAttachment* Part = nullptr; int32 Tier = 0; int32 Priority = 0; int32 RuleIndex = 0; };
	TMap<FGameplayTag, FWinner> Winners;
	for (int32 i = 0; i < Weapon.PartRules.Num(); ++i)
	{
		const UFPSRWeaponPartRule* Rule = Weapon.PartRules[i];
		if (!Rule || !Rule->Slot.IsValid())
		{
			continue; // slotless/null rule ignored (IsDataValid errors)
		}
		const bool bMet = Rule->Condition ? Rule->Condition->IsMet(Resolved, Fragments) : true;
		if (!bMet)
		{
			continue;
		}
		FWinner Cand{ &Rule->Part, Rule->Tier, Rule->Priority, i };
		FWinner* Cur = Winners.Find(Rule->Slot);
		// higher Tier wins; equal → higher Priority; equal → LOWER RuleIndex (deterministic total order)
		const bool bBeats = !Cur
			|| Cand.Tier > Cur->Tier
			|| (Cand.Tier == Cur->Tier && Cand.Priority > Cur->Priority)
			|| (Cand.Tier == Cur->Tier && Cand.Priority == Cur->Priority && Cand.RuleIndex < Cur->RuleIndex);
		if (bBeats)
		{
			Winners.Add(Rule->Slot, Cand);
		}
	}

	// Append winners in lexical slot order so the selected set (and its signature) is order-stable across runs.
	TArray<FGameplayTag> Slots;
	Winners.GetKeys(Slots);
	Slots.Sort([](const FGameplayTag& A, const FGameplayTag& B) { return A.GetTagName().Compare(B.GetTagName()) < 0; });
	for (const FGameplayTag& S : Slots)
	{
		OutSelected.Add(*Winners[S].Part);
	}
}

uint32 FPSRWeaponPartSelector::ComputeSignature(const TArray<FFPSRWeaponPartAttachment>& Selected)
{
	uint32 Hash = 0;
	for (const FFPSRWeaponPartAttachment& P : Selected)
	{
		Hash = HashCombine(Hash, GetTypeHash(P.Part.ToSoftObjectPath()));
	}
	return Hash;
}
