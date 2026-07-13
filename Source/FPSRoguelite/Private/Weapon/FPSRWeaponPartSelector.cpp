// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponPartSelector.h"
#include "Weapon/FPSRWeaponFragment.h"

namespace
{
	/** FRotator has no engine GetTypeHash overload (unlike FVector) — hash it component-wise so ComputeSignature can
	 *  detect an offset ROTATION change (not just translation/scale) across an evolution-stage swap. */
	uint32 GetRotatorTypeHash(const FRotator& R)
	{
		uint32 Hash = GetTypeHash(R.Pitch);
		Hash = HashCombine(Hash, GetTypeHash(R.Yaw));
		Hash = HashCombine(Hash, GetTypeHash(R.Roll));
		return Hash;
	}

	/** StatThreshold 단계 트리거 비교. */
	bool CompareStat(float V, EFPSRStatCompare Cmp, float Value)
	{
		switch (Cmp)
		{
		case EFPSRStatCompare::GreaterOrEqual: return V >= Value;
		case EFPSRStatCompare::Greater:        return V > Value;
		case EFPSRStatCompare::LessOrEqual:    return V <= Value;
		case EFPSRStatCompare::Less:           return V < Value;
		case EFPSRStatCompare::Equal:          return FMath::IsNearlyEqual(V, Value);
		default:                               return false;
		}
	}
}

void FPSRWeaponPartSelector::SelectParts(const UFPSRWeaponDataAsset& Weapon,
	const FFPSRWeaponStatBlock& Resolved,
	const TArray<TObjectPtr<UFPSRWeaponFragment>>& Fragments,
	TArray<FFPSRWeaponPartAttachment>& OutSelected)
{
	OutSelected.Reset();

	// One resolved attachment per WeaponParts1P slot, in DA order (W-U1b 재설계 — 폴리모픽 규칙 폐기, 파츠별 스택 진화).
	// Null-part filtering still happens at attach time (RebuildPartsFromSelection), so every entry passes through here.
	for (const FFPSRWeaponPartAttachment& Entry : Weapon.WeaponParts1P)
	{
		// Base (stage 0): the slot's own Part/Offset/Scope — what a purely-structural slot always resolves to.
		TSoftObjectPtr<UStaticMesh> WinMesh = Entry.Part;
		FTransform WinOffset = Entry.Offset;
		FFPSRWeaponScopeDescriptor WinScope = Entry.Scope;

		// Stack count for FragmentStacks-trigger stages = same asset-pointer-identity match as
		// UFPSRWeaponInstance::HasFragment. StatThreshold-trigger stages need no fragment at all, so this is
		// evaluated regardless of whether the slot even has an EvolutionFragment assigned.
		int32 FragStacks = 0;
		if (UFPSRWeaponFragment* Frag = Entry.EvolutionFragment.LoadSynchronous())
		{
			for (const TObjectPtr<UFPSRWeaponFragment>& F : Fragments)
			{
				if (F == Frag)
				{
					++FragStacks;
				}
			}
		}

		// Winner among met stages = the LAST one satisfied in list order (author base→강한 순). Mixing both trigger
		// kinds in one Stages list is fine — each stage is evaluated independently against its own trigger.
		for (const FFPSRWeaponPartStage& Stage : Entry.Stages)
		{
			bool bMet = false;
			switch (Stage.Trigger)
			{
			case EFPSRPartStageTrigger::FragmentStacks:
				bMet = !Entry.EvolutionFragment.IsNull() && FragStacks >= Stage.MinStacks;
				break;
			case EFPSRPartStageTrigger::StatThreshold:
				bMet = CompareStat(Resolved.GetAxisValue(Stage.StatAxis), Stage.StatCompare, Stage.StatValue);
				break;
			default:
				break;
			}

			if (bMet)
			{
				WinMesh = Stage.Mesh;
				WinOffset = Stage.Offset;
				WinScope = Stage.Scope;
			}
		}

		FFPSRWeaponPartAttachment ResolvedEntry;
		ResolvedEntry.Part = WinMesh;
		ResolvedEntry.Socket = Entry.Socket; // FIXED mount — never changes across evolution stages
		ResolvedEntry.Offset = WinOffset;
		ResolvedEntry.Scope = WinScope;
		OutSelected.Add(ResolvedEntry);
	}
}

uint32 FPSRWeaponPartSelector::ComputeSignature(const TArray<FFPSRWeaponPartAttachment>& Selected)
{
	uint32 Hash = 0;
	for (const FFPSRWeaponPartAttachment& P : Selected)
	{
		// Full resolved-attachment fingerprint — not just the mesh — so an evolution stage swap that keeps the same
		// mesh but changes offset/scope (or a socket change) still churns a rebuild (fixes a pre-existing under-hash).
		Hash = HashCombine(Hash, GetTypeHash(P.Part.ToSoftObjectPath()));
		Hash = HashCombine(Hash, GetTypeHash(P.Socket));
		Hash = HashCombine(Hash, GetTypeHash(P.Offset.GetLocation()));
		Hash = HashCombine(Hash, GetRotatorTypeHash(P.Offset.Rotator()));
		Hash = HashCombine(Hash, GetTypeHash(P.Offset.GetScale3D()));
		Hash = HashCombine(Hash, GetTypeHash(P.Scope.bScopeOverlay));
		Hash = HashCombine(Hash, GetTypeHash(P.Scope.AimFieldOfView));
		Hash = HashCombine(Hash, GetTypeHash(P.Scope.ScopeOverlayWidgetClass.ToSoftObjectPath()));
		Hash = HashCombine(Hash, GetTypeHash(P.Scope.bScopeVignette));
		Hash = HashCombine(Hash, GetTypeHash(P.Scope.bHideWeaponWhileScoped));
	}
	return Hash;
}
