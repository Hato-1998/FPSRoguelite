// Copyright Epic Games, Inc. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "MetaProgression/RogueliteSaveGame.h"
#include "MetaProgression/FPSRSaveGameSubsystem.h"
#include "Kismet/GameplayStatics.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRSaveGameTest, "FPSRoguelite.Smoke.SaveGame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRSaveGameTest::RunTest(const FString& Parameters)
{
	// 1. A fresh save is at the current version with neutral defaults.
	URogueliteSaveGame* Fresh = NewObject<URogueliteSaveGame>();
	TestNotNull(TEXT("Fresh save allocates"), Fresh);
	if (Fresh)
	{
		TestEqual(TEXT("Fresh save is at current version"), Fresh->SaveVersion, URogueliteSaveGame::CurrentSaveVersion);
		TestEqual(TEXT("Fresh reserved defaults to 0"), Fresh->Reserved0, static_cast<int64>(0));
	}

	// 2. A legacy (v0) save migrates forward once; a second call is a no-op.
	URogueliteSaveGame* Legacy = NewObject<URogueliteSaveGame>();
	Legacy->SaveVersion = 0;
	TestTrue(TEXT("Legacy save reports a migration"), Legacy->MigrateIfNeeded());
	TestEqual(TEXT("Migrated to current version"), Legacy->SaveVersion, URogueliteSaveGame::CurrentSaveVersion);
	TestFalse(TEXT("Re-migrating a current save is a no-op"), Legacy->MigrateIfNeeded());

	// 3. Round-trip through the real slot serializer (the path the subsystem uses).
	const FString Slot = TEXT("FPSRAutomation_SaveGameTest");
	const int32 UserIdx = 0;
	UGameplayStatics::DeleteGameInSlot(Slot, UserIdx); // clean any stale slot from a prior run
	URogueliteSaveGame* ToSave = NewObject<URogueliteSaveGame>();
	ToSave->Reserved0 = 1234;
	TestTrue(TEXT("SaveGameToSlot succeeds"), UGameplayStatics::SaveGameToSlot(ToSave, Slot, UserIdx));
	TestTrue(TEXT("DoesSaveGameExist after save"), UGameplayStatics::DoesSaveGameExist(Slot, UserIdx));
	URogueliteSaveGame* Loaded = Cast<URogueliteSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, UserIdx));
	TestNotNull(TEXT("Loaded save casts back to URogueliteSaveGame"), Loaded);
	if (Loaded)
	{
		TestEqual(TEXT("Round-trip preserves Reserved0"), Loaded->Reserved0, static_cast<int64>(1234));
		TestEqual(TEXT("Round-trip preserves version"), Loaded->SaveVersion, URogueliteSaveGame::CurrentSaveVersion);
	}

	// 4. Absence fallback: a missing slot loads null (the subsystem falls back to defaults).
	UGameplayStatics::DeleteGameInSlot(Slot, UserIdx);
	TestFalse(TEXT("Slot deleted"), UGameplayStatics::DoesSaveGameExist(Slot, UserIdx));
	TestNull(TEXT("Loading a missing slot returns null"), UGameplayStatics::LoadGameFromSlot(Slot, UserIdx));

	// 5. CardId rename/merge fallback resolution (pure, no GameInstance needed).
	TMap<FName, FName> Redirects;
	Redirects.Add(FName("card.old"), FName("card.new"));  // old -> new
	Redirects.Add(FName("card.mid"), FName("card.old"));  // chain: mid -> old -> new
	TestEqual(TEXT("Unmapped id passes through"),
		UFPSRSaveGameSubsystem::ResolveCardId(Redirects, FName("card.keep")), FName("card.keep"));
	TestEqual(TEXT("Single redirect resolves"),
		UFPSRSaveGameSubsystem::ResolveCardId(Redirects, FName("card.old")), FName("card.new"));
	TestEqual(TEXT("Chained redirect resolves to final"),
		UFPSRSaveGameSubsystem::ResolveCardId(Redirects, FName("card.mid")), FName("card.new"));
	TestEqual(TEXT("None passes through"),
		UFPSRSaveGameSubsystem::ResolveCardId(Redirects, NAME_None), FName(NAME_None));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
