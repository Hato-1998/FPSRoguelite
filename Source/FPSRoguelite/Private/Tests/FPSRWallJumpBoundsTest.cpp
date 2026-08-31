// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Hero/FPSRCharacterMovementComponent.h"

#if WITH_AUTOMATION_TESTS

// ADR 0001 (2026-08-31 revision, "the wall is not a state"): worldless proof for the two bounds the auto wall jump
// rests on. The launch itself needs a capsule, a level and a sphere sweep, so it is not reachable from here — but the
// bounds are pure arithmetic over three numbers, and they are the part that has to be right: one stops the impulse
// firing every frame, the other is the ONLY thing standing between this feature and a player ratcheting up a wall
// forever. No UWorld/UObject anywhere in this file, matching the worldless-core style of the other tests here.
//
// Why a cooldown cannot be the ascent bound (the arithmetic case 3 pins down): height gained per launch cycle is
// (Vz*T - g*T*T/2), which is positive for every T < 2*Vz/g -- and 2*Vz/g is the full airtime of a jump. Any cooldown
// short enough to feel responsive is far below that, so it always leaves a net gain per cycle. Counting the launches
// is what makes the total bounded, whatever the cooldown is tuned to.

namespace
{
	// ⚠️ 이름에 파일 고유 접미사(Wjb)를 붙인 것은 의도다 — 익명 네임스페이스는 **번역 단위**를 격리할 뿐이라,
	//    유니티 빌드가 두 테스트 .cpp 를 한 블롭으로 합치면 동명 정의가 재정의로 깨진다(에러는 엉뚱한 기존
	//    파일에서 뜬다). -DisableUnity 로는 재현되지 않는다 — 실사고 2026-08-28.

	/** The three numbers the component keeps for the auto wall jump, in one bag so a case reads as a timeline. */
	struct FWallJumpStateWjb
	{
		float Cooldown = 0.0f;
		int32 JumpsUsed = 0;
	};

	/** Try to launch once. Returns whether it fired, and charges both bounds when it did — mirroring the single path
	 *  in TryAutoWallJump that is allowed to spend them. */
	bool TryLaunchWjb(FWallJumpStateWjb& S, int32 MaxJumps, float CooldownOnFire)
	{
		if (!FPSRWallJumpBounds::CanFire(S.Cooldown, S.JumpsUsed, MaxJumps))
		{
			return false;
		}
		FPSRWallJumpBounds::Consume(S.Cooldown, S.JumpsUsed, CooldownOnFire);
		return true;
	}

	/** Advance N frames of DeltaSeconds each, airborne throughout unless bOnGround says otherwise. */
	void AdvanceFramesWjb(FWallJumpStateWjb& S, int32 Frames, float DeltaSeconds, bool bOnGround = false)
	{
		for (int32 i = 0; i < Frames; ++i)
		{
			FPSRWallJumpBounds::Advance(S.Cooldown, S.JumpsUsed, DeltaSeconds, bOnGround);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRWallJumpBoundsTest, "FPSRoguelite.Movement.WallJumpBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRWallJumpBoundsTest::RunTest(const FString& Parameters)
{
	// Authored defaults, restated rather than read off a CDO so the cases stay worldless.
	const float Cooldown = 0.35f;
	const int32 MaxJumps = 2;
	const float FrameDt = 1.0f / 60.0f;

	// ---- (1) The per-airborne budget stops the launch at exactly MaxWallJumpsPerAirborne. ----
	// Cooldown is elapsed between every attempt, so the ONLY thing that can refuse the third is the counter.
	{
		FWallJumpStateWjb S;
		int32 Fired = 0;
		for (int32 Attempt = 0; Attempt < 10; ++Attempt)
		{
			if (TryLaunchWjb(S, MaxJumps, Cooldown))
			{
				++Fired;
			}
			// Generous: well past the cooldown, still airborne, so the counter is unassisted.
			AdvanceFramesWjb(S, 60, FrameDt);
		}
		TestEqual(TEXT("(1) launches are capped at MaxWallJumpsPerAirborne while airborne"), Fired, MaxJumps);
		TestEqual(TEXT("(1) the counter stops at the cap rather than running past it"), S.JumpsUsed, MaxJumps);
	}

	// ---- (2) Landing, and only landing, refills the budget. ----
	{
		FWallJumpStateWjb S;
		while (TryLaunchWjb(S, MaxJumps, Cooldown))
		{
			AdvanceFramesWjb(S, 60, FrameDt);
		}
		TestEqual(TEXT("(2) budget is spent before landing"), S.JumpsUsed, MaxJumps);

		// A long airborne stretch must NOT refill it — if it did, the ascent bound would be a cooldown in disguise.
		AdvanceFramesWjb(S, 600, FrameDt, /*bOnGround=*/false);
		TestEqual(TEXT("(2) staying airborne never refills the budget"), S.JumpsUsed, MaxJumps);
		TestFalse(TEXT("(2) and the launch stays refused while airborne"),
			FPSRWallJumpBounds::CanFire(S.Cooldown, S.JumpsUsed, MaxJumps));

		// One grounded frame is enough.
		AdvanceFramesWjb(S, 1, FrameDt, /*bOnGround=*/true);
		TestEqual(TEXT("(2) landing refills the budget"), S.JumpsUsed, 0);
		TestTrue(TEXT("(2) and the launch is available again"),
			FPSRWallJumpBounds::CanFire(S.Cooldown, S.JumpsUsed, MaxJumps));
	}

	// ---- (3) The cooldown refuses a re-fire inside its window, and permits one at the far edge. ----
	// This is the anti-jitter bound: without it the trigger re-evaluates the very next frame with the input still held
	// into a wall that is still there, and fires again immediately.
	{
		FWallJumpStateWjb S;
		TestTrue(TEXT("(3) the first launch fires from a clean state"), TryLaunchWjb(S, MaxJumps, Cooldown));

		// One frame later: budget still has room (MaxJumps is 2), so a refusal here can only be the cooldown.
		AdvanceFramesWjb(S, 1, FrameDt);
		TestTrue(TEXT("(3) budget still has room, so the next check isolates the cooldown"), S.JumpsUsed < MaxJumps);
		TestFalse(TEXT("(3) re-fire is refused one frame after a launch"), TryLaunchWjb(S, MaxJumps, Cooldown));

		// Step to just SHORT of the full window. The preconditions are asserted rather than assumed: deriving a frame
		// count from Cooldown/FrameDt and trusting it is how this case was wrong on first writing (it landed exactly ON
		// the boundary and the "still refused" expectation fired against an elapsed cooldown).
		const int32 FramesInWindow = FMath::CeilToInt(Cooldown / FrameDt);
		AdvanceFramesWjb(S, FramesInWindow - 3, FrameDt); // 1 already spent above, so this stops two frames short
		TestTrue(TEXT("(3) precondition: still inside the cooldown window"), S.Cooldown > 0.0f);
		TestFalse(TEXT("(3) re-fire is still refused just before the cooldown lapses"), TryLaunchWjb(S, MaxJumps, Cooldown));

		// Past the window: the second (and last) launch of this airborne period goes through.
		AdvanceFramesWjb(S, 4, FrameDt);
		TestEqual(TEXT("(3) precondition: the cooldown has now lapsed"), S.Cooldown, 0.0f);
		TestTrue(TEXT("(3) re-fire is allowed once the cooldown has lapsed"), TryLaunchWjb(S, MaxJumps, Cooldown));
		TestEqual(TEXT("(3) and that spent the second of two"), S.JumpsUsed, 2);
	}

	// ---- (4) Advance never drives the cooldown negative. ----
	// A negative cooldown would still read as "ready" through CanFire, so this is only a tidiness check — but a
	// runaway negative would also be the first sign Advance had been given a wrong sign somewhere.
	{
		FWallJumpStateWjb S;
		TryLaunchWjb(S, MaxJumps, Cooldown);
		AdvanceFramesWjb(S, 600, FrameDt);
		TestEqual(TEXT("(4) cooldown floors at zero rather than going negative"), S.Cooldown, 0.0f);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
