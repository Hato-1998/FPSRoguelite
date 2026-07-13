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

		if (UFPSRWeaponFragment* Frag = Entry.EvolutionFragment.LoadSynchronous())
		{
			// Stack count = same asset-pointer-identity match as UFPSRWeaponInstance::HasFragment.
			int32 Stacks = 0;
			for (const TObjectPtr<UFPSRWeaponFragment>& F : Fragments)
			{
				if (F == Frag)
				{
					++Stacks;
				}
			}
			// Winner among met stages = the HIGHEST MinStacks satisfied by the current stack count.
			int32 BestMinStacks = 0;
			for (const FFPSRWeaponPartStage& Stage : Entry.Stages)
			{
				if (Stage.MinStacks <= Stacks && Stage.MinStacks > BestMinStacks)
				{
					BestMinStacks = Stage.MinStacks;
					WinMesh = Stage.Mesh;
					WinOffset = Stage.Offset;
					WinScope = Stage.Scope;
				}
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
		Hash = HashCombine(Hash, GetTypeHash(P.Scope.ScopeReticle.ToSoftObjectPath()));
		Hash = HashCombine(Hash, GetTypeHash(P.Scope.bScopeVignette));
		Hash = HashCombine(Hash, GetTypeHash(P.Scope.bHideWeaponWhileScoped));
	}
	return Hash;
}
