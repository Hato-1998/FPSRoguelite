// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Enemy/FPSREnemyPursuit.h"

#if WITH_AUTOMATION_TESTS

// ADR 0008 (Docs/Architecture/0008-hover-enemy-pursuit-reachability-modes.md): headless proof for the pure
// FFPSRPursuitState::Tick judgment core — no UObject/world, exercises exactly the mode-transition and climb-
// escalation logic AFPSREnemyBase::TickServerMovement drives. FPSRFlowFieldUnitTest.cpp's worldless-core
// convention (IMPLEMENT_SIMPLE_AUTOMATION_TEST, no NewObject, hand-authored inputs).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSREnemyPursuitTest, "FPSRoguelite.Enemy.Pursuit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSREnemyPursuitTest::RunTest(const FString& Parameters)
{
	FFPSRPursuitParams P;
	P.StallTime = 1.0f;
	P.StallMinMove = 50.0f;
	P.ClimbStep = 100.0f;
	P.ClimbCeiling = 300.0f;
	P.ClearDecayTime = 0.5f;
	P.ModeMinHold = 1.0f;

	// ---- (1) Flow-zero + a target -> Seek3D immediately (no need to wait out the stall window) ----
	{
		FFPSRPursuitState S;
		S.Tick(0.1f, FVector::ZeroVector, /*bHasTarget=*/true, /*bFlowZero=*/true, /*bForwardBlocked=*/false,
			/*LastAttackTime=*/-1000.0f, /*Now=*/0.0f, P);
		TestTrue(TEXT("flow-zero + target -> Seek3D on the very first tick"), S.Mode == EFPSRPursuitMode::Seek3D);
		TestTrue(TEXT("entering Seek3D arms the mode-hold"), S.ModeHoldRemaining > 0.0f);
	}

	// ---- (2) Stall (T elapsed, D undercut, no recent attack) opens Seek3D; sufficient movement OR a recent
	//          attack keeps resetting the window so it never trips ----
	{
		// (a) Stationary for < T -> no trip yet; stationary through >= T -> trips.
		FFPSRPursuitState S;
		S.Tick(0.6f, FVector::ZeroVector, true, /*bFlowZero=*/false, false, -1000.0f, 0.6f, P);
		TestTrue(TEXT("stall: a sub-T tick alone doesn't trip yet"), S.Mode == EFPSRPursuitMode::Flow);
		S.Tick(0.6f, FVector::ZeroVector, true, false, false, -1000.0f, 1.2f, P);
		TestTrue(TEXT("stall: T elapsed with < D net movement -> Seek3D"), S.Mode == EFPSRPursuitMode::Seek3D);

		// (b) Sufficient net movement every tick keeps resetting the window -> never trips, even over many ticks.
		FFPSRPursuitState S2;
		FVector Loc = FVector::ZeroVector;
		float Now2 = 0.0f;
		for (int32 i = 0; i < 10; ++i)
		{
			Loc += FVector(P.StallMinMove * 2.0f, 0.0f, 0.0f); // well past D every tick
			Now2 += 0.6f;
			S2.Tick(0.6f, Loc, true, false, false, -1000.0f, Now2, P);
		}
		TestTrue(TEXT("stall: sufficient movement each tick never trips (stays Flow)"), S2.Mode == EFPSRPursuitMode::Flow);

		// (c) A recent attack (LastAttackTime close to Now) keeps invalidating the window even while stationary.
		FFPSRPursuitState S3;
		float Now3 = 0.0f;
		for (int32 i = 0; i < 10; ++i)
		{
			Now3 += 0.6f;
			S3.Tick(0.6f, FVector::ZeroVector, true, false, false, /*LastAttackTime=*/Now3 - 0.1f, Now3, P);
		}
		TestTrue(TEXT("stall: a recent attack keeps invalidating the window (stays Flow)"), S3.Mode == EFPSRPursuitMode::Flow);
	}

	// ---- (3) Repeated blocking -> ClimbOffset increases monotonically and caps at ClimbCeiling ----
	{
		FFPSRPursuitState S;
		S.Mode = EFPSRPursuitMode::Seek3D; // isolate the climb-escalation behavior directly
		FFPSRPursuitParams LP = P;
		LP.StallTime = 1000.0f; // keep the surplus-descent reset (case 5) inert for this test
		float Now = 0.0f;
		float PrevOffset = -1.0f;
		for (int32 i = 0; i < 5; ++i)
		{
			Now += 0.1f;
			S.Tick(0.1f, FVector::ZeroVector, true, /*bFlowZero=*/true, /*bForwardBlocked=*/true, -1000.0f, Now, LP);
			TestTrue(TEXT("ClimbOffset is monotonically non-decreasing while blocked"), S.ClimbOffset >= PrevOffset);
			PrevOffset = S.ClimbOffset;
		}
		TestEqual(TEXT("ClimbOffset caps at ClimbCeiling"), S.ClimbOffset, LP.ClimbCeiling);
	}

	// ---- (4) Sustained clear decays ClimbOffset by exactly one ClimbStep per ClearDecayTime ----
	{
		FFPSRPursuitState S;
		S.Mode = EFPSRPursuitMode::Seek3D;
		S.ClimbOffset = P.ClimbStep * 2.0f; // below ClimbCeiling so the cap never interferes
		FFPSRPursuitParams LP = P;
		LP.StallTime = 1000.0f;
		float Now = 0.0f;

		Now += 0.3f; // sub-ClearDecayTime (0.5) -> no decay yet
		S.Tick(0.3f, FVector::ZeroVector, true, true, /*bForwardBlocked=*/false, -1000.0f, Now, LP);
		TestEqual(TEXT("decay: a sub-ClearDecayTime clear tick doesn't decay yet"), S.ClimbOffset, P.ClimbStep * 2.0f);

		Now += 0.3f; // cumulative clear time 0.6 >= 0.5 -> exactly one step decays
		S.Tick(0.3f, FVector::ZeroVector, true, true, false, -1000.0f, Now, LP);
		TestEqual(TEXT("decay: crossing ClearDecayTime decays exactly one ClimbStep"), S.ClimbOffset, P.ClimbStep * 1.0f);
	}

	// ---- (5) Pinned at ClimbCeiling AND re-stalled -> the surplus-descent reset (ClimbOffset + stall window -> 0) ----
	{
		FFPSRPursuitState S;
		S.Mode = EFPSRPursuitMode::Seek3D;
		S.ClimbOffset = P.ClimbCeiling; // already pinned at the ceiling
		float Now = 0.0f;

		Now += 0.6f; // stall window not yet full (T=1.0)
		S.Tick(0.6f, FVector::ZeroVector, true, /*bFlowZero=*/true, true, -1000.0f, Now, P);
		TestTrue(TEXT("surplus-descent: still pinned before the window refills"), S.ClimbOffset == P.ClimbCeiling);

		Now += 0.6f; // cumulative 1.2 >= StallTime(1.0) -> re-stalled while pinned
		S.Tick(0.6f, FVector::ZeroVector, true, true, true, -1000.0f, Now, P);
		TestTrue(TEXT("surplus-descent: re-stalling at the ceiling resets ClimbOffset to 0"), S.ClimbOffset == 0.0f);
		TestTrue(TEXT("surplus-descent: the stall window resets too"), S.StallWindowElapsed == 0.0f);
		TestTrue(TEXT("surplus-descent: mode stays Seek3D (flow is still zero)"), S.Mode == EFPSRPursuitMode::Seek3D);
	}

	// ---- (6) Flow alive + hold expired -> Flow return; flow alive but hold still active -> no return (hysteresis) ----
	{
		FFPSRPursuitState S;
		S.Mode = EFPSRPursuitMode::Seek3D;
		S.ModeHoldRemaining = P.ModeMinHold; // 1.0s, as if just entered
		float Now = 0.0f;

		Now += 0.6f;
		S.Tick(0.6f, FVector::ZeroVector, true, /*bFlowZero=*/false, false, -1000.0f, Now, P);
		TestTrue(TEXT("hysteresis: flow alive but hold still active -> stays Seek3D"), S.Mode == EFPSRPursuitMode::Seek3D);

		Now += 0.6f; // cumulative hold countdown 0.6+0.6=1.2 >= ModeMinHold(1.0) -> expired this tick
		S.Tick(0.6f, FVector::ZeroVector, true, false, false, -1000.0f, Now, P);
		TestTrue(TEXT("hysteresis: hold expired + flow alive -> returns to Flow"), S.Mode == EFPSRPursuitMode::Flow);
		TestTrue(TEXT("returning to Flow clears the climb escalation"), S.ClimbOffset == 0.0f);
	}

	// ---- (7) Reset is idempotent ----
	{
		FFPSRPursuitState S;
		S.Mode = EFPSRPursuitMode::Seek3D;
		S.ModeHoldRemaining = 5.0f;
		S.StallWindowElapsed = 2.0f;
		S.StallAnchorLoc = FVector(10.0f, 20.0f, 30.0f);
		S.ClimbOffset = 400.0f;
		S.ClearElapsed = 0.3f;

		S.Reset();
		TestTrue(TEXT("Reset returns to Flow with every accumulator zeroed"),
			S.Mode == EFPSRPursuitMode::Flow && S.ModeHoldRemaining == 0.0f && S.StallWindowElapsed == 0.0f &&
			S.StallAnchorLoc.IsNearlyZero() && S.ClimbOffset == 0.0f && S.ClearElapsed == 0.0f);

		S.Reset(); // calling again on an already-reset state must change nothing
		TestTrue(TEXT("Reset is idempotent"),
			S.Mode == EFPSRPursuitMode::Flow && S.ModeHoldRemaining == 0.0f && S.StallWindowElapsed == 0.0f &&
			S.StallAnchorLoc.IsNearlyZero() && S.ClimbOffset == 0.0f && S.ClearElapsed == 0.0f);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
