// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Engine/DeveloperSettings.h"
#include "FPSRSaveSettings.generated.h"

/** Slot naming for the meta SaveManager (UFPSRSaveGameSubsystem), so NO slot string is hard-coded in C++
 *  (Game.md §6-2). Authored in DefaultGame.ini [/Script/FPSRoguelite.FPSRSaveSettings]. Single fixed slot: this
 *  project has no account system and Steam is one user per machine, so no per-user suffix is needed. */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "FPSR Save"))
class FPSROGUELITE_API UFPSRSaveSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Primary meta save slot. */
	UPROPERTY(Config, EditAnywhere, Category = "Slots")
	FString PrimarySlotName = TEXT("PlayerMeta");

	/** Backup slot written after each successful primary save; used to recover a corrupt primary on load. */
	UPROPERTY(Config, EditAnywhere, Category = "Slots")
	FString BackupSlotName = TEXT("PlayerMeta_Backup");

	/** Platform user index for the slot (0 = primary local user). */
	UPROPERTY(Config, EditAnywhere, Category = "Slots")
	int32 UserIndex = 0;

	/** Rename/merge fallback for meta unlock keys (RunFlow §2-11 "해금 데이터 삭제/리네임 fallback"): maps a deprecated
	 *  CardId that may still live in an old save to its current CardId (old -> new; chains are followed). Empty by
	 *  default; authored in DefaultGame.ini. Resolved via UFPSRSaveGameSubsystem::ResolveCardId when unlocks are read
	 *  back — the unlock list itself lands in P0-③, this is the addressing seam. */
	UPROPERTY(Config, EditAnywhere, Category = "Card Identity")
	TMap<FName, FName> DeprecatedCardIdRedirects;

	virtual FName GetCategoryName() const override { return FName(TEXT("Game")); }
};
