// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "GameFramework/SaveGame.h"
#include "RogueliteSaveGame.generated.h"

/** Versioned root of the per-player local meta save (U10 infrastructure). The real meta schema (currency,
 *  unlock tree, characters, difficulty) is undefined until P0-③ — this holds only a version + a neutral
 *  placeholder so the save/load/migration path can be landed and tested now. Extend by adding a field, bumping
 *  CurrentSaveVersion, and adding a matching case in MigrateIfNeeded(). Only ever created/loaded/saved through
 *  UFPSRSaveGameSubsystem (never SaveGameToSlot directly from UI/Actors). */
UCLASS()
class FPSROGUELITE_API URogueliteSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** Bump when the persisted schema changes; add a matching case in MigrateIfNeeded(). */
	static constexpr int32 CurrentSaveVersion = 1;

	/** Schema version this instance was written at (drives MigrateIfNeeded). */
	UPROPERTY()
	int32 SaveVersion = CurrentSaveVersion;

	/** Neutral placeholder — exercises serialization round-trip + migration only, NO meta meaning. P0-③ replaces
	 *  the real schema. Do not read this as gameplay state. */
	UPROPERTY()
	int64 Reserved0 = 0;

	/** Forward-migrate a loaded older save up to CurrentSaveVersion. Returns true if the version changed (caller
	 *  should re-persist). A newer-than-current save (downgrade) is left intact and logged (can't migrate backward). */
	bool MigrateIfNeeded();
};
