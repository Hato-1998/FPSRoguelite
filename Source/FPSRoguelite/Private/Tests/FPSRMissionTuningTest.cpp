// Copyright Epic Games, Inc. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "Run/Mission/FPSRMissionTuning.h"
#include "Run/Mission/FPSRMissionDataAsset.h"
#include "Run/Mission/FPSRMission_HoldZone.h"
#include "Run/Mission/FPSRMission_StandStill.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRMissionTuningTest, "FPSRoguelite.Mission.Tuning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRMissionTuningTest::RunTest(const FString& Parameters)
{
	// 1. Each tuning subclass's default values match the §2-8-1 inventory defaults —
	// a soft migration must never change a mission's out-of-the-box behavior.
	UFPSRMissionTuning_HoldZone* HoldZone = NewObject<UFPSRMissionTuning_HoldZone>();
	TestNotNull(TEXT("HoldZone tuning allocates"), HoldZone);
	if (HoldZone)
	{
		TestEqual(TEXT("HoldZone.ZoneRadius default"), HoldZone->ZoneRadius, 700.0f);
		TestEqual(TEXT("HoldZone.RequiredHoldSeconds default"), HoldZone->RequiredHoldSeconds, 30.0f);
	}

	UFPSRMissionTuning_StandStill* StandStill = NewObject<UFPSRMissionTuning_StandStill>();
	TestNotNull(TEXT("StandStill tuning allocates"), StandStill);
	if (StandStill)
	{
		TestEqual(TEXT("StandStill.RequiredStillSeconds default"), StandStill->RequiredStillSeconds, 15.0f);
		TestEqual(TEXT("StandStill.StillSpeedThreshold default"), StandStill->StillSpeedThreshold, 50.0f);
	}

	UFPSRMissionTuning_MovingZone* MovingZone = NewObject<UFPSRMissionTuning_MovingZone>();
	TestNotNull(TEXT("MovingZone tuning allocates"), MovingZone);
	if (MovingZone)
	{
		TestEqual(TEXT("MovingZone.ZoneRadius default"), MovingZone->ZoneRadius, 700.0f);
		TestEqual(TEXT("MovingZone.RequiredHoldSeconds default"), MovingZone->RequiredHoldSeconds, 30.0f);
	}

	UFPSRMissionTuning_CollectOrbs* CollectOrbs = NewObject<UFPSRMissionTuning_CollectOrbs>();
	TestNotNull(TEXT("CollectOrbs tuning allocates"), CollectOrbs);
	if (CollectOrbs)
	{
		TestNull(TEXT("CollectOrbs.OrbClass default is unset"), CollectOrbs->OrbClass.Get());
		TestEqual(TEXT("CollectOrbs.OrbRelativeLocations default is empty"), CollectOrbs->OrbRelativeLocations.Num(), 0);
	}

	UFPSRMissionTuning_CarryNoHit* CarryNoHit = NewObject<UFPSRMissionTuning_CarryNoHit>();
	TestNotNull(TEXT("CarryNoHit tuning allocates"), CarryNoHit);
	if (CarryNoHit)
	{
		TestNull(TEXT("CarryNoHit.OrbClass default is unset"), CarryNoHit->OrbClass.Get());
		TestEqual(TEXT("CarryNoHit.RequiredCarrySeconds default"), CarryNoHit->RequiredCarrySeconds, 20.0f);
		TestEqual(TEXT("CarryNoHit.CarryHeight default"), CarryNoHit->CarryHeight, 120.0f);
	}

	UFPSRMissionTuning_DefeatFleeing* DefeatFleeing = NewObject<UFPSRMissionTuning_DefeatFleeing>();
	TestNotNull(TEXT("DefeatFleeing tuning allocates"), DefeatFleeing);
	if (DefeatFleeing)
	{
		TestNull(TEXT("DefeatFleeing.TargetClass default is unset"), DefeatFleeing->TargetClass.Get());
		TestEqual(TEXT("DefeatFleeing.FleeSpeed default"), DefeatFleeing->FleeSpeed, 350.0f);
		TestEqual(TEXT("DefeatFleeing.FleeTriggerRange default"), DefeatFleeing->FleeTriggerRange, 900.0f);
	}

	UFPSRMissionTuning_LimitedVision* LimitedVision = NewObject<UFPSRMissionTuning_LimitedVision>();
	TestNotNull(TEXT("LimitedVision tuning allocates"), LimitedVision);
	if (LimitedVision)
	{
		TestEqual(TEXT("LimitedVision.RequiredSeconds default"), LimitedVision->RequiredSeconds, 20.0f);
	}

	// 2. AFPSRMission_HoldZone's CDO reports the matching expected tuning class.
	const AFPSRMission_HoldZone* HoldZoneCDO = GetDefault<AFPSRMission_HoldZone>();
	TestNotNull(TEXT("HoldZone CDO resolves"), HoldZoneCDO);
	if (HoldZoneCDO)
	{
		TestEqual(TEXT("HoldZone CDO's GetExpectedTuningClass matches UFPSRMissionTuning_HoldZone"),
			HoldZoneCDO->GetExpectedTuningClass(), TSubclassOf<UFPSRMissionTuning>(UFPSRMissionTuning_HoldZone::StaticClass()));
	}

#if WITH_EDITOR
	// 3. Mission DataAsset IsDataValid: Tuning must match MissionClass's expected tuning subclass.
	{
		UFPSRMissionDataAsset* Data = NewObject<UFPSRMissionDataAsset>();
		TestNotNull(TEXT("Mission DataAsset allocates"), Data);
		if (Data)
		{
			Data->MissionClass = AFPSRMission_HoldZone::StaticClass();

			// 3a. Matching tuning subclass -> not Invalid, no errors. (A clean asset returns NotValidated — the base
			// UObject::IsDataValid result — not Valid; the validation system treats NotValidated + zero errors as a
			// pass, so assert "not Invalid" rather than "== Valid".)
			Data->Tuning = NewObject<UFPSRMissionTuning_HoldZone>(Data);
			FDataValidationContext MatchContext;
			const EDataValidationResult MatchResult = Data->IsDataValid(MatchContext);
			TestTrue(TEXT("Matching Tuning subclass -> not Invalid"), MatchResult != EDataValidationResult::Invalid);
			TestEqual(TEXT("Matching Tuning subclass -> no errors"), (int32)MatchContext.GetNumErrors(), 0);

			// 3b. Mismatched tuning subclass -> Invalid.
			Data->Tuning = NewObject<UFPSRMissionTuning_StandStill>(Data);
			FDataValidationContext MismatchContext;
			const EDataValidationResult MismatchResult = Data->IsDataValid(MismatchContext);
			TestEqual(TEXT("Mismatched Tuning subclass -> Invalid"), MismatchResult, EDataValidationResult::Invalid);

			// 3c. Null tuning -> not Invalid (fallback to tuning-subclass CDO defaults works) but with at least one warning
			// (soft-migration fallback notice). Same NotValidated-vs-Valid note as 3a above.
			Data->Tuning = nullptr;
			FDataValidationContext NullContext;
			const EDataValidationResult NullResult = Data->IsDataValid(NullContext);
			TestTrue(TEXT("Null Tuning -> not Invalid (fallback to tuning CDO defaults)"), NullResult != EDataValidationResult::Invalid);
			TestTrue(TEXT("Null Tuning -> at least one warning"), NullContext.GetNumWarnings() > 0);
		}
	}
#endif // WITH_EDITOR

	return true;
}

#endif // WITH_AUTOMATION_TESTS
