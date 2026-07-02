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

	// 1. Default multiplier is 1.0.
	TestEqual(TEXT("Default crosshair scale is 1.0"), Settings->GetCrosshairScale(), 1.0f);

	// 2. Clamp to [0.5, 2.5].
	Settings->SetCrosshairScale(0.1f, /*bSave=*/false);
	TestEqual(TEXT("Below-min clamps to 0.5"), Settings->GetCrosshairScale(), 0.5f);
	Settings->SetCrosshairScale(9.0f, /*bSave=*/false);
	TestEqual(TEXT("Above-max clamps to 2.5"), Settings->GetCrosshairScale(), 2.5f);
	Settings->SetCrosshairScale(1.75f, /*bSave=*/false);
	TestEqual(TEXT("In-range value passes through"), Settings->GetCrosshairScale(), 1.75f);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
