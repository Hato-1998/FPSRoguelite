// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "FPSRBoundaryBlocker.generated.h"

class UBoxComponent;

/**
 * Replicated invisible wall at a streamed-map boundary (multimap Tier 0, Codex R5). Placed by the designer just past a
 * giant Door, it blocks PLAYERS from walking into the void while the adjacent sublevel streams in. It is REPLICATED (not
 * server-only): a remote client's CharacterMovement predicts locally, so without a client-side blocker the player would
 * appear to penetrate and then snap back. The MapStreamSubsystem drops the block (SetBlocking(false)) once TargetMapId's
 * collision is verified ready; on a stream-in timeout the block stays and the passage reads as "sealed" (with feedback).
 *
 * Enemies pass through it (blocks only the player object channel): the swarm boundary is enforced primarily by the
 * per-map flow field + MapId gating + allocator, and blocking enemies here would risk jamming boundary followers.
 * Object type WorldDynamic (NOT WorldStatic) so the flow-field bake's WorldStatic traces never pick it up.
 */
UCLASS()
class FPSROGUELITE_API AFPSRBoundaryBlocker : public AActor
{
	GENERATED_BODY()

public:
	AFPSRBoundaryBlocker();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Which streamed map this blocker guards — dropped when that map's collision is verified ready (S3). */
	const FGameplayTag& GetTargetMapId() const { return TargetMapId; }

	/** Server: enable/disable the block. Idempotent; replicates to all (clients apply via OnRep, host applies directly). */
	void SetBlocking(bool bNewBlocking);

	bool IsBlocking() const { return bBlocking; }

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnRep_Blocking();

	/** Enable QueryOnly collision when blocking, disable it otherwise. Runs on server + clients (initial + OnRep). */
	void ApplyBlockingState();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPSR|Boundary")
	TObjectPtr<UBoxComponent> BlockBox;

	/** The map whose stream-in this blocker guards. The MapStreamSubsystem finds blockers by this tag to drop them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPSR|Boundary")
	FGameplayTag TargetMapId;

	/** Replicated so remote clients' movement prediction sees the same wall (no penetrate-then-snap). Starts blocking. */
	UPROPERTY(ReplicatedUsing = OnRep_Blocking)
	bool bBlocking = true;
};
