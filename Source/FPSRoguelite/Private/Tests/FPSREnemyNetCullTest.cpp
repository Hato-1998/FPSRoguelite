// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"

#if WITH_AUTOMATION_TESTS

// Headless invariant net for the multimap U P-H unified net-cull sizing PURE helper (ComputeUnifiedNetCullRadius) — the
// exact formula the spawn subsystem applies uniformly at acquire (single source of truth). The adversarial plan gate proved
// a symmetric distance cull cannot do per-slot "seam-only" relevancy; this asserts the RELATIONSHIPS the chosen Option A
// (engagement/weapon-range bubble, capped to the slot footprint, floored at weapon range) must hold — so PIE re-tuning of the
// weapon-range / seam-margin knobs doesn't break the test while a FORMULA regression still does. Load-bearing properties:
//   (A) shoot-ability floor: R >= WeaponRange for ANY footprint (an in-range enemy is never culled / alive-but-unshootable),
//   (B) the footprint cap is LIVE (bounds R below the bubble for a small slot), not inert,
//   (C) monotone non-decreasing in slot size then PLATEAU at the bubble (never spans the whole grid),
//   (D) seam-margin additive in the bubble regime,
//   (E) on the near-max 132m whitebox slot R is REDUCED vs full-slot coverage (Option A < Option B) yet < the 3x3 grid width.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSREnemyNetCullTest, "FPSRoguelite.Enemy.NetCull",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSREnemyNetCullTest::RunTest(const FString& Parameters)
{
	using U = UFPSREnemySpawnSubsystem;

	// Representative production knobs (mirror the subsystem's private constexpr NetCullWeaponRangeCm / NetCullSeamMarginCm).
	// The assertions are RELATIONSHIPS that hold for any positive W/M, so these specific values are only for the exact-value
	// checks — re-tuning them in code must keep the properties true.
	constexpr float W = 10000.0f; // weapon range = bubble base AND floor (cm, 100m)
	constexpr float M = 4000.0f;  // across-seam lookahead (cm, 40m)
	constexpr float Tol = 0.5f;

	// Whitebox slot: 132m/side -> XY diagonal = 13200 * sqrt(2).
	const float WhiteboxSide = 13200.0f;
	const float WhiteboxDiag = FMath::Sqrt(2.0f) * WhiteboxSide; // ~18667.6
	const float GridWidth = 3.0f * WhiteboxSide;                 // 39600 (3x3 unified extent, one axis)

	// --- (A) Shoot-ability floor: R >= W for ANY footprint (degenerate, tiny, whitebox, huge) --------------------------------
	for (const float D : { 0.0f, 100.0f, 5000.0f, WhiteboxDiag, 100000.0f })
	{
		TestTrue(TEXT("R >= WeaponRange floor (in-range enemy always replicated)"),
			U::ComputeUnifiedNetCullRadius(D, W, M) >= W - Tol);
	}

	// --- Exact values across the three regimes -----------------------------------------------------------------------------
	// Degenerate footprint (invalid slot -> diagonal 0): floor wins, no NaN.
	TestEqual(TEXT("D=0 -> floor (W)"), U::ComputeUnifiedNetCullRadius(0.0f, W, M), W, Tol);
	// Small slot (diag 5000, below W): cap (5000+M=9000) < floor -> floor wins.
	TestEqual(TEXT("D small (<W-M) -> floor (W)"), U::ComputeUnifiedNetCullRadius(5000.0f, W, M), W, Tol);
	// (B) Mid slot (diag 8000, in (W-M, W)): cap (12000) binds ABOVE floor and BELOW the bubble (14000) -> proves the
	// footprint cap is live (R tracks D+M, not the bubble). This is the load-bearing "not inert" case.
	TestEqual(TEXT("D=8000 -> footprint cap binds (D+M)"), U::ComputeUnifiedNetCullRadius(8000.0f, W, M), 8000.0f + M, Tol);
	TestTrue(TEXT("footprint cap keeps R below the bubble"), U::ComputeUnifiedNetCullRadius(8000.0f, W, M) < (W + M) - Tol);
	// Whitebox (diag ~18667 >= W): bubble binds -> R == W + M (the uniform 140m).
	TestEqual(TEXT("D=whitebox -> bubble (W+M)"), U::ComputeUnifiedNetCullRadius(WhiteboxDiag, W, M), W + M, Tol);
	// Huge slot: still the bubble (plateau) -> R == W + M.
	TestEqual(TEXT("D huge -> bubble plateau (W+M)"), U::ComputeUnifiedNetCullRadius(100000.0f, W, M), W + M, Tol);

	// --- (C) Monotone non-decreasing in slot size, then plateau at the bubble (W+M) ----------------------------------------
	float Prev = -1.0f;
	for (float D = 0.0f; D <= 40000.0f; D += 1000.0f)
	{
		const float R = U::ComputeUnifiedNetCullRadius(D, W, M);
		TestTrue(TEXT("R non-decreasing in slot diagonal"), R >= Prev - Tol);
		TestTrue(TEXT("R never exceeds the bubble (W+M) — never spans the whole grid"), R <= (W + M) + Tol);
		Prev = R;
	}
	// The plateau is actually reached (a large slot caps at the bubble, not the footprint).
	TestEqual(TEXT("large slot plateaus at the bubble"), U::ComputeUnifiedNetCullRadius(50000.0f, W, M), W + M, Tol);

	// --- (D) Seam-margin additive in the bubble regime (D >= W so the bubble binds): R(M) - R(0) == M --------------------
	for (const float D : { WhiteboxDiag, 30000.0f, 100000.0f })
	{
		const float RWithMargin = U::ComputeUnifiedNetCullRadius(D, W, M);
		const float RNoMargin = U::ComputeUnifiedNetCullRadius(D, W, 0.0f);
		TestEqual(TEXT("seam margin is additive on the bubble (R(M)-R(0)==M)"), RWithMargin - RNoMargin, M, Tol);
	}

	// --- (E) Whitebox: Option A REDUCES relevancy vs full-slot coverage (Option B) and never spans the 3x3 grid -----------
	const float RWhitebox = U::ComputeUnifiedNetCullRadius(WhiteboxDiag, W, M);
	TestTrue(TEXT("Option A < full-slot coverage (R < slot diagonal)"), RWhitebox < WhiteboxDiag - Tol);
	TestTrue(TEXT("R < 3x3 grid width (never the whole grid)"), RWhitebox < GridWidth - Tol);
	TestTrue(TEXT("R still covers the weapon-range engagement bubble (R >= W)"), RWhitebox >= W - Tol);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
