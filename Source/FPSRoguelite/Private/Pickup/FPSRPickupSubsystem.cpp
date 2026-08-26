// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pickup/FPSRPickupSubsystem.h"
#include "Pickup/FPSRXPPickup.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRLogChannels.h" // LogFPSR
#include "Enemy/FPSRFlowFieldSubsystem.h" // FindNearestOpenLocation (CarryPickupsToNewStage's snap, same as the enemy carry-over)

#include "Engine/World.h"

bool UFPSRPickupSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}

bool UFPSRPickupSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return World && World->GetNetMode() != NM_Client;
}

void UFPSRPickupSubsystem::PruneActivePickups()
{
	ActivePickups.RemoveAll([](const TObjectPtr<AFPSRXPPickup>& Pickup)
	{
		return !IsValid(Pickup);
	});
}

void UFPSRPickupSubsystem::SpawnXPPickup(const FVector& Location, int32 XPValue)
{
	UWorld* World = GetWorld();
	if (!World || !HasServerAuthority())
	{
		return;
	}

	PruneActivePickups();

	// Over the active cap: grant XP directly to the party rather than spawning another actor.
	if (ActivePickups.Num() >= MaxActivePickups)
	{
		if (AFPSRGameState* GameState = World->GetGameState<AFPSRGameState>())
		{
			GameState->AddSharedXP(XPValue);
		}
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFPSRXPPickup* Pickup = World->SpawnActor<AFPSRXPPickup>(AFPSRXPPickup::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
	if (Pickup == nullptr)
	{
		return;
	}

	Pickup->SetXPValue(XPValue);
	ActivePickups.Add(Pickup);
}

void UFPSRPickupSubsystem::CarryPickupsToNewStage(const TArray<FVector>& OldPlayerLocs, const TArray<FVector>& NewPlayerLocs)
{
	if (!HasServerAuthority())
	{
		return;
	}

	PruneActivePickups();

	// No delta to carry by (no player actually teleported this swap, or a caller bug pairing mismatched arrays) —
	// mirrors UFPSREnemySpawnSubsystem::CarryEnemiesToNewStage's same-condition fallback (ReleaseAllEnemies): there
	// is no coordinate-space delta to translate into, so rather than guess, destroy every live gem outright. Leaving
	// them behind in the OLD arena would silently occupy a MaxActivePickups slot forever — that arena is about to
	// lose its collision and is not revisited — starving every future SpawnXPPickup call into the direct-grant branch.
	if (OldPlayerLocs.Num() == 0 || NewPlayerLocs.Num() != OldPlayerLocs.Num())
	{
		UE_LOG(LogFPSR, Warning,
			TEXT("[Pickup] CarryPickupsToNewStage: no player delta to carry by (%d old / %d new loc) — destroying %d live gem(s) instead."),
			OldPlayerLocs.Num(), NewPlayerLocs.Num(), ActivePickups.Num());
		for (const TObjectPtr<AFPSRXPPickup>& Pickup : ActivePickups)
		{
			if (AFPSRXPPickup* PickupPtr = Pickup.Get())
			{
				PickupPtr->Destroy();
			}
		}
		ActivePickups.Reset();
		return;
	}

	const UWorld* World = GetWorld();
	const UFPSRFlowFieldSubsystem* FlowField = World ? World->GetSubsystem<UFPSRFlowFieldSubsystem>() : nullptr;

	// Same call-site constant UFPSREnemySpawnSubsystem::CarryEnemiesToNewStage uses for its own FindNearestOpenLocation
	// snap, kept identical so a gem lands within the same search radius of the new arena's walkable mask as the
	// enemy that might have dropped it.
	constexpr int32 CarrySnapMaxRadiusCells = 16;

	int32 CarriedCount = 0;
	int32 SnapFailCount = 0;
	for (const TObjectPtr<AFPSRXPPickup>& PickupObj : ActivePickups)
	{
		AFPSRXPPickup* Pickup = PickupObj.Get();
		if (!IsValid(Pickup))
		{
			continue; // PruneActivePickups above already dropped stale entries — stay defensive anyway
		}

		const FVector CurrentLoc = Pickup->GetActorLocation();

		// Nearest OLD player location (XY) decides whose teleport delta this gem rides — the same "nearest player"
		// metric CarryEnemiesToNewStage uses for its own per-enemy delta.
		int32 NearestOldIdx = 0;
		float BestOldDistSq = FVector::DistSquaredXY(CurrentLoc, OldPlayerLocs[0]);
		for (int32 i = 1; i < OldPlayerLocs.Num(); ++i)
		{
			const float DistSq = FVector::DistSquaredXY(CurrentLoc, OldPlayerLocs[i]);
			if (DistSq < BestOldDistSq)
			{
				BestOldDistSq = DistSq;
				NearestOldIdx = i;
			}
		}

		const FVector Delta = NewPlayerLocs[NearestOldIdx] - OldPlayerLocs[NearestOldIdx];
		const FVector Candidate = CurrentLoc + Delta;

		FVector SnapLoc;
		if (FlowField && FlowField->FindNearestOpenLocation(Candidate, CarrySnapMaxRadiusCells, SnapLoc))
		{
			Pickup->ServerRelocateForStageCarry(SnapLoc);
			++CarriedCount;
			continue;
		}

		// Snap failed (or there is no flow field at all — off-authority/pre-bake edge case) — do NOT destroy the
		// gem. Unlike an enemy (ReleaseEnemy just frees a pool slot), destroying a gem here would silently eat a
		// player's already-earned reward. Granting the XP directly is available (AddSharedXP) but calling it from
		// mid-PerformSwap risks the exact reentrancy PerformSwap's own top-of-function comment warns about —
		// AddSharedXP -> RefreshPauseState can raise a card-selection freeze while the swap is still unwinding. A
		// player's own stood position is, by definition, open and reachable, so drop the gem there instead. This is
		// a rare geometry-failure path (correctness over elegance is the right trade here, not performance).
		++SnapFailCount;
		int32 NearestNewIdx = 0;
		float BestNewDistSq = FVector::DistSquaredXY(Candidate, NewPlayerLocs[0]);
		for (int32 i = 1; i < NewPlayerLocs.Num(); ++i)
		{
			const float DistSq = FVector::DistSquaredXY(Candidate, NewPlayerLocs[i]);
			if (DistSq < BestNewDistSq)
			{
				BestNewDistSq = DistSq;
				NearestNewIdx = i;
			}
		}
		Pickup->ServerRelocateForStageCarry(NewPlayerLocs[NearestNewIdx]);
		++CarriedCount;
	}

	// No carry cap here, unlike CarryEnemiesToNewStage's MaxCarry: MaxActivePickups (above, checked in SpawnXPPickup)
	// already bounds how many gems can be alive at once, so a second cap on top of an already-bounded list would
	// just be an arbitrary truncation with no separate reason to exist.
	UE_LOG(LogFPSR, Log, TEXT("[Pickup] CarryPickupsToNewStage: carried %d (snap-failed %d, placed at nearest player)."),
		CarriedCount, SnapFailCount);
}
