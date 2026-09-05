// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Combat/FPSRCombatStatics.h"
#include "Combat/FPSRCritTypes.h"

#if WITH_AUTOMATION_TESTS

// CRIT1 §12-4 — pure-function checks only (no world, no SpawnActor; mirrors FPSRVitalsTest.cpp's pure-input style),
// since FPSRCombat::RollCrit/ComputeCritRiderMagnitudes hold no state and need none to exercise:
//   (a) Chance=0 -> RollCrit never crits.
//   (b) Chance=1 -> RollCrit always crits.
//   (c) bWeakpointAlwaysCrit && WeakpointMult>1 -> always true even at Chance=0 (and skips the roll entirely).
//   (d) bWeakpointAlwaysCrit && WeakpointMult==1 -> falls through to the ordinary roll (Chance=0 / Chance=1 both
//       checked, so this also proves bWeakpointAlwaysCrit alone does nothing without an actual weakpoint hit).
//   (e) ComputeCritRiderMagnitudes arithmetic: positive DamageDealt, DamageDealt<=0 (zero and negative), and
//       zero ratios with positive DamageDealt.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRCritResolverTest, "FPSRoguelite.Combat.CritResolver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRCritResolverTest::RunTest(const FString& Parameters)
{
	// Iteration count for the edges below. Chance=0 is genuinely deterministic (`0.0f > 0.0f` short-circuits before
	// FRand() is even called). Chance=1 is NOT quite deterministic — FMath::FRand() is documented [0,1] INCLUSIVE
	// (GenericPlatformMath::FRand: `(Rand() & RandMax) / (float)RandMax`, which reaches exactly 1.0 once every
	// ~16.7 million draws), so `FRand() < 1.0f` can in principle miss. Looping doesn't remove that residual (astronomically
	// unlikely) flake risk, but it does catch a future implementation that drifts off the Chance>0.0f short-circuit
	// in a way a single draw wouldn't reliably expose.
	constexpr int32 NumRolls = 20;

	// --- (a) Chance=0 -> RollCrit never crits, regardless of the RNG draw. -----------------------------------------
	{
		FFPSRCritContext Crit; // Chance=0 default
		bool bAnyCrit = false;
		for (int32 Index = 0; Index < NumRolls; ++Index)
		{
			bAnyCrit |= FPSRCombat::RollCrit(Crit, 1.0f);
		}
		TestFalse(TEXT("(a) Chance=0 -> RollCrit never crits"), bAnyCrit);
	}

	// --- (b) Chance=1 -> RollCrit always crits. -----------------------------------------------------------------
	{
		FFPSRCritContext Crit;
		Crit.Chance = 1.0f;
		bool bAllCrit = true;
		for (int32 Index = 0; Index < NumRolls; ++Index)
		{
			bAllCrit &= FPSRCombat::RollCrit(Crit, 1.0f);
		}
		TestTrue(TEXT("(b) Chance=1 -> RollCrit always crits"), bAllCrit);
	}

	// --- (c) bWeakpointAlwaysCrit && WeakpointMult>1 -> always true even at Chance=0 (skips the roll). -------------
	{
		FFPSRCritContext Crit;
		Crit.Chance = 0.0f;
		Crit.bWeakpointAlwaysCrit = true;
		bool bAllCrit = true;
		for (int32 Index = 0; Index < NumRolls; ++Index)
		{
			bAllCrit &= FPSRCombat::RollCrit(Crit, 1.5f); // WeakpointMult > 1 = this hit landed on a weakpoint
		}
		TestTrue(TEXT("(c) bWeakpointAlwaysCrit && WeakpointMult>1 -> always crits, even at Chance=0"), bAllCrit);
	}

	// --- (d) bWeakpointAlwaysCrit && WeakpointMult==1 -> falls through to the ordinary Chance-based roll. -----------
	{
		FFPSRCritContext CritZero;
		CritZero.Chance = 0.0f;
		CritZero.bWeakpointAlwaysCrit = true;
		bool bAnyCrit = false;
		for (int32 Index = 0; Index < NumRolls; ++Index)
		{
			bAnyCrit |= FPSRCombat::RollCrit(CritZero, 1.0f); // WeakpointMult == 1 -> always-crit does NOT trigger
		}
		TestFalse(TEXT("(d) bWeakpointAlwaysCrit && WeakpointMult==1, Chance=0 -> never crits"), bAnyCrit);

		FFPSRCritContext CritOne;
		CritOne.Chance = 1.0f;
		CritOne.bWeakpointAlwaysCrit = true;
		bool bAllCrit = true;
		for (int32 Index = 0; Index < NumRolls; ++Index)
		{
			bAllCrit &= FPSRCombat::RollCrit(CritOne, 1.0f);
		}
		TestTrue(TEXT("(d) bWeakpointAlwaysCrit && WeakpointMult==1, Chance=1 -> always crits"), bAllCrit);
	}

	// --- (e) ComputeCritRiderMagnitudes: BonusDamage = DamageDealt x BonusInstanceRatio, HealAmount = DamageDealt x
	//         HealRatio; DamageDealt<=0 zeroes both regardless of the ratios. --------------------------------------
	{
		FFPSRCritContext Crit;
		Crit.BonusInstanceRatio = 0.5f;
		Crit.HealRatio = 0.10f;

		float OutBonusDamage = -1.0f;
		float OutHealAmount = -1.0f;

		FPSRCombat::ComputeCritRiderMagnitudes(Crit, 100.0f, OutBonusDamage, OutHealAmount);
		TestEqual(TEXT("(e) positive DamageDealt: BonusDamage = DamageDealt * BonusInstanceRatio"), OutBonusDamage, 50.0f);
		TestEqual(TEXT("(e) positive DamageDealt: HealAmount = DamageDealt * HealRatio"), OutHealAmount, 10.0f);

		FPSRCombat::ComputeCritRiderMagnitudes(Crit, 0.0f, OutBonusDamage, OutHealAmount);
		TestEqual(TEXT("(e) DamageDealt<=0 (zero): BonusDamage clamps to 0"), OutBonusDamage, 0.0f);
		TestEqual(TEXT("(e) DamageDealt<=0 (zero): HealAmount clamps to 0"), OutHealAmount, 0.0f);

		FPSRCombat::ComputeCritRiderMagnitudes(Crit, -25.0f, OutBonusDamage, OutHealAmount);
		TestEqual(TEXT("(e) DamageDealt<=0 (negative): BonusDamage clamps to 0"), OutBonusDamage, 0.0f);
		TestEqual(TEXT("(e) DamageDealt<=0 (negative): HealAmount clamps to 0"), OutHealAmount, 0.0f);

		FFPSRCritContext CritNoRiders; // BonusInstanceRatio=0, HealRatio=0 default
		FPSRCombat::ComputeCritRiderMagnitudes(CritNoRiders, 100.0f, OutBonusDamage, OutHealAmount);
		TestEqual(TEXT("(e) zero ratios: BonusDamage is 0 even with positive DamageDealt"), OutBonusDamage, 0.0f);
		TestEqual(TEXT("(e) zero ratios: HealAmount is 0 even with positive DamageDealt"), OutHealAmount, 0.0f);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
