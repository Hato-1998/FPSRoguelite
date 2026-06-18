// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "FPSRLobbyDisplayPawn.generated.h"

class USkeletalMeshComponent;
class UFPSRWeaponDataAsset;
class AFPSRPlayerState;

/** Display-only podium pawn for the lobby (U11a §C). One spawns per player at a PlayerStart (the lobby GameMode's
 *  DefaultPawnClass override) purely to show each player's body + chosen weapon — it is NOT the gameplay character
 *  (AFPSRCharacter), so the gameplay pawn stays uncontaminated. The lobby view is a room-overview CameraActor, not
 *  this pawn. C++ owns only the data wiring: resolve the (replicated) PlayerState, watch its loadout pick, and hand
 *  the chosen weapon to the BP via OnDisplayWeaponChanged. The BP supplies the placeholder body mesh and attaches
 *  the (placeholder) weapon mesh — actual 3P character/weapon art is a later art unit. */
UCLASS(Abstract)
class FPSROGUELITE_API AFPSRLobbyDisplayPawn : public APawn
{
	GENERATED_BODY()

public:
	AFPSRLobbyDisplayPawn();

	/** The weapon the owning player picked in the lobby (read off the replicated PlayerState). Null until chosen. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Lobby")
	UFPSRWeaponDataAsset* GetDisplayedWeapon() const;

	/** Fired whenever the displayed (selected) weapon changes (and once on bind). The BP attaches/swaps the
	 *  placeholder weapon mesh from the weapon DA's (soft) mesh fields. */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Lobby")
	void OnDisplayWeaponChanged(UFPSRWeaponDataAsset* Weapon);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRep_PlayerState() override;   // client: PlayerState (re)replicated
	virtual void PossessedBy(AController* NewController) override;   // server: PlayerState assigned

	/** Placeholder body mesh (assign a mannequin in the BP). Root component; cosmetic only. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPSR|Lobby")
	TObjectPtr<USkeletalMeshComponent> BodyMesh;

private:
	/** Resolve the AFPSRPlayerState, (re)bind its OnLoadoutChanged exactly once, and refresh the display. No-op
	 *  while the PlayerState hasn't replicated yet (OnRep_PlayerState re-runs this when it arrives). */
	void BindToPlayerState();

	UFUNCTION()
	void HandleLoadoutChanged();

	/** The PlayerState we're bound to (guards against double-binding and lets EndPlay unbind cleanly). */
	UPROPERTY(Transient)
	TObjectPtr<AFPSRPlayerState> BoundPlayerState;
};
