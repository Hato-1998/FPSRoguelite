// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "FPSRLoadoutPoolDataAsset.generated.h"

class UFPSRWeaponDataAsset;

/** Designer-curated set of weapons a player may pick from in the lobby (P7 §3-8). Each player chooses one entry
 *  as the single weapon they bring into the run. Referenced (soft) by UFPSRGameFlowSettings::LoadoutPool so the
 *  lobby UI (client) and the selection RPC (server) read the same content-defined list — no hardcoded paths. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRLoadoutPoolDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Weapons offered in the lobby loadout pick, in display order. The selection RPC validates the chosen index
	 *  against this array (server authority — the client only ever sends an index). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loadout")
	TArray<TObjectPtr<UFPSRWeaponDataAsset>> SelectableWeapons;

	/** Returns the weapon at Index, or nullptr if the index is out of range / the entry is null. */
	UFUNCTION(BlueprintPure, Category = "Loadout")
	UFPSRWeaponDataAsset* GetWeaponAt(int32 Index) const;

	/** True if Index addresses a valid (non-null) weapon entry. Used by the server to validate a client's pick. */
	UFUNCTION(BlueprintPure, Category = "Loadout")
	bool IsValidIndex(int32 Index) const;
};
