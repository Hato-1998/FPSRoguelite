// Copyright Epic Games, Inc. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "Settings/FPSRGameUserSettings.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRCrosshairSettingsTest, "FPSRoguelite.Smoke.CrosshairSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRCrosshairSettingsTest::RunTest(const FString& Parameters)
{
	// Disk-free: a NewObject instance with bSave=false throughout, so the real singleton and
	// GameUserSettings.ini are never mutated by this test.
	UFPSRGameUserSettings* Settings = NewObject<UFPSRGameUserSettings>();
	TestNotNull(TEXT("Settings instance allocates"), Settings);
	if (!Settings)
	{
		return false;
	}

	// No assertion on the DEFAULT colour: CrosshairColor is a UPROPERTY(config), so even a fresh NewObject is seeded
	// from whatever the local GameUserSettings.ini holds. Asserting white here passes only on a machine that never
	// changed it — measured: it came back (1, 0.1, 0.1, 1) on a machine with a saved red crosshair.

	// Colour round-trips (bSave=false so disk is never touched) — config-independent, which is the point.
	const FLinearColor TestColor(0.1f, 1.0f, 0.1f, 1.0f);
	Settings->SetCrosshairColor(TestColor, /*bSave=*/false);
	TestEqual(TEXT("Crosshair color round-trips"), Settings->GetCrosshairColor(), TestColor);

	// Thickness is intentionally absent: it stopped being a player setting (line weight is authored per style on the
	// crosshair material instances). If a thickness accessor ever reappears here, that decision was reverted.

	return true;
}

#endif // WITH_AUTOMATION_TESTS
