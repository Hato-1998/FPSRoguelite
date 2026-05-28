// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRSmokeTest, "FPSRoguelite.Smoke.ModuleLoads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRSmokeTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("FPSRoguelite module is loaded"), FModuleManager::Get().IsModuleLoaded(TEXT("FPSRoguelite")));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
