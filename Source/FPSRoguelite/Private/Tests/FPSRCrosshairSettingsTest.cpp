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

	// 1. Default thickness is 1.0.
	TestEqual(TEXT("Default crosshair thickness is 1.0"), Settings->GetCrosshairThickness(), 1.0f);

	// 2. Clamp to [0.5, 2.0].
	Settings->SetCrosshairThickness(0.1f, /*bSave=*/false);
	TestEqual(TEXT("Below-min clamps to 0.5"), Settings->GetCrosshairThickness(), 0.5f);
	Settings->SetCrosshairThickness(9.0f, /*bSave=*/false);
	TestEqual(TEXT("Above-max clamps to 2.0"), Settings->GetCrosshairThickness(), 2.0f);
	Settings->SetCrosshairThickness(1.5f, /*bSave=*/false);
	TestEqual(TEXT("In-range value passes through"), Settings->GetCrosshairThickness(), 1.5f);

	// 3. Color round-trips (bSave=false so disk is never touched).
	const FLinearColor TestColor(0.1f, 1.0f, 0.1f, 1.0f);
	Settings->SetCrosshairColor(TestColor, /*bSave=*/false);
	TestEqual(TEXT("Crosshair color round-trips"), Settings->GetCrosshairColor(), TestColor);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
