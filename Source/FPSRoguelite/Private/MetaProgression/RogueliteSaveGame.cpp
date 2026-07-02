// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetaProgression/RogueliteSaveGame.h"
#include "Core/FPSRLogChannels.h"

bool URogueliteSaveGame::MigrateIfNeeded()
{
	if (SaveVersion == CurrentSaveVersion)
	{
		return false;
	}
	if (SaveVersion > CurrentSaveVersion)
	{
		// Save written by a newer build than this one. We cannot migrate backward; keep the data as-is and rely on
		// forward-compatible serialization to drop unknown fields. Logged so a version mismatch is diagnosable.
		UE_LOG(LogFPSR, Warning, TEXT("[Save] Save version %d is newer than current %d — using as-is (no backward migration)."), SaveVersion, CurrentSaveVersion);
		return false;
	}

	const int32 OldVersion = SaveVersion;
	while (SaveVersion < CurrentSaveVersion)
	{
		switch (SaveVersion)
		{
		case 0:
			// v0 (pre-versioned / legacy) -> v1: no real fields to carry yet; ensure neutral defaults.
			Reserved0 = 0;
			SaveVersion = 1;
			break;
		default:
			// Unknown older version we have no explicit step for: snap forward to current to avoid an infinite loop.
			SaveVersion = CurrentSaveVersion;
			break;
		}
	}
	UE_LOG(LogFPSR, Log, TEXT("[Save] Migrated meta save %d -> %d."), OldVersion, SaveVersion);
	return SaveVersion != OldVersion;
}
