// Copyright Epic Games, Inc. All Rights Reserved.

#include "Status/FPSRStatusTypes.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "FPSRStatusCatalogDataAsset"

EDataValidationResult UFPSRStatusCatalogDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// Pass 1: SlotIndex range + uniqueness, Weak/RequiredWeakSlots leftover check, and the DoT-source count (§7-4
	// single-DoT-source premise) — builds BySlot for pass 2 (a Strong entry's material pair can reference a slot
	// defined LATER in the array, so the pair check can't run in the same iteration as the slot it looks up).
	TMap<uint8, const UFPSRStatusEffectDataAsset*> BySlot;
	int32 DotSourceCount = 0;
	for (int32 Index = 0; Index < Statuses.Num(); ++Index)
	{
		const UFPSRStatusEffectDataAsset* Status = Statuses[Index];
		if (!Status)
		{
			continue; // in-progress authoring — same null tolerance as FPSRCardPoolDataAsset::IsDataValid
		}

		if (Status->SlotIndex > 7)
		{
			Context.AddError(FText::Format(
				LOCTEXT("SlotIndexOutOfRange", "Statuses[{0}] ('{1}') has SlotIndex {2} — the runtime bitmask is a uint8, so valid slots are 0..7."),
				FText::AsNumber(Index), FText::FromString(Status->GetName()), FText::AsNumber(static_cast<int32>(Status->SlotIndex))));
			Result = EDataValidationResult::Invalid;
		}

		if (const UFPSRStatusEffectDataAsset* const* Existing = BySlot.Find(Status->SlotIndex))
		{
			Context.AddError(FText::Format(
				LOCTEXT("DuplicateSlotIndex", "Statuses[{0}] ('{1}') shares SlotIndex {2} with '{3}' — each slot must map to exactly one status, or FPSRStatus's pure functions can't tell which one owns that bit."),
				FText::AsNumber(Index), FText::FromString(Status->GetName()), FText::AsNumber(static_cast<int32>(Status->SlotIndex)), FText::FromString((*Existing)->GetName())));
			Result = EDataValidationResult::Invalid;
		}
		else
		{
			BySlot.Add(Status->SlotIndex, Status);
		}

		if (Status->DamagePerSecond > 0.0f)
		{
			++DotSourceCount;
		}

		if (Status->Kind == EFPSRStatusKind::Weak && Status->RequiredWeakSlots.Num() > 0)
		{
			Context.AddError(FText::Format(
				LOCTEXT("WeakHasRequiredSlots", "Statuses[{0}] ('{1}') is Kind=Weak but has a non-empty RequiredWeakSlots — only a Strong status's material pair is ever read; this is almost certainly a leftover from switching Kind."),
				FText::AsNumber(Index), FText::FromString(Status->GetName())));
			Result = EDataValidationResult::Invalid;
		}
	}

	// Pass 2: Strong entries' material pairs.
	TMap<uint64, const UFPSRStatusEffectDataAsset*> SeenMaterialPairs; // key = (min slot)<<8 | (max slot), order-independent
	for (int32 Index = 0; Index < Statuses.Num(); ++Index)
	{
		const UFPSRStatusEffectDataAsset* Status = Statuses[Index];
		if (!Status || Status->Kind != EFPSRStatusKind::Strong)
		{
			continue;
		}

		if (Status->RequiredWeakSlots.Num() != 2)
		{
			Context.AddError(FText::Format(
				LOCTEXT("StrongNeedsTwoMaterials", "Statuses[{0}] ('{1}') is Kind=Strong but RequiredWeakSlots has {2} entries — a combo always fires from exactly 2 Weak materials (STAT1 §5-1)."),
				FText::AsNumber(Index), FText::FromString(Status->GetName()), FText::AsNumber(Status->RequiredWeakSlots.Num())));
			Result = EDataValidationResult::Invalid;
			continue; // nothing else below is meaningful without exactly 2
		}

		const uint8 SlotA = Status->RequiredWeakSlots[0];
		const uint8 SlotB = Status->RequiredWeakSlots[1];
		const UFPSRStatusEffectDataAsset* const* MaterialA = BySlot.Find(SlotA);
		const UFPSRStatusEffectDataAsset* const* MaterialB = BySlot.Find(SlotB);
		const bool bBothWeak = MaterialA && (*MaterialA)->Kind == EFPSRStatusKind::Weak
			&& MaterialB && (*MaterialB)->Kind == EFPSRStatusKind::Weak;
		if (!bBothWeak)
		{
			Context.AddError(FText::Format(
				LOCTEXT("MaterialsMustBeWeak", "Statuses[{0}] ('{1}')'s RequiredWeakSlots ({2}, {3}) must both resolve to a Kind=Weak status in this same catalog."),
				FText::AsNumber(Index), FText::FromString(Status->GetName()), FText::AsNumber(static_cast<int32>(SlotA)), FText::AsNumber(static_cast<int32>(SlotB))));
			Result = EDataValidationResult::Invalid;
		}

		const uint8 LowSlot = FMath::Min(SlotA, SlotB);
		const uint8 HighSlot = FMath::Max(SlotA, SlotB);
		const uint64 PairKey = (static_cast<uint64>(LowSlot) << 8) | HighSlot;
		if (const UFPSRStatusEffectDataAsset* const* Existing = SeenMaterialPairs.Find(PairKey))
		{
			Context.AddError(FText::Format(
				LOCTEXT("DuplicateMaterialPair", "Statuses[{0}] ('{1}') shares its material pair ({2}, {3}) with '{4}' — two different Strong statuses can't both claim the same two Weak slots (Apply would have no way to choose which one fires)."),
				FText::AsNumber(Index), FText::FromString(Status->GetName()), FText::AsNumber(static_cast<int32>(SlotA)), FText::AsNumber(static_cast<int32>(SlotB)), FText::FromString((*Existing)->GetName())));
			Result = EDataValidationResult::Invalid;
		}
		else
		{
			SeenMaterialPairs.Add(PairKey, Status);
		}
	}

	if (DotSourceCount > 1)
	{
		Context.AddWarning(FText::Format(
			LOCTEXT("MultipleDotSources", "{0} statuses in this catalog have DamagePerSecond > 0 — FPSRStatus::Advance assumes a single active DoT source at a time (§7-4) and only ticks the first one it finds active, in array order. The rest silently never tick while the first stays active."),
			FText::AsNumber(DotSourceCount)));
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
