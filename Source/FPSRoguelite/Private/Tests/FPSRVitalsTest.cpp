// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Combat/FPSRVitals.h"

#if WITH_AUTOMATION_TESTS

// VIT1 §12-4 — pure-function checks only (no world, no SpawnActor; mirrors FPSRDestructibleTest.cpp's CDO/pure-input
// style), since FPSRVitals::ApplyDamage/ComputeRegeneratedShield hold no state and need none to exercise:
//   ① V2      — MaxShield=0 is arithmetically identical to the pre-VIT1 Clamp(Health-Damage, 0, Max) behavior.
//   ② V1      — the "no combination of mitigations may fully block a hit" invariant, TWO fixtures (a single
//                fixture is a vacuous pass — see each block's own comment for why).
//   ③         — ComputeRegeneratedShield is idempotent (same inputs, same output).
//   ④ V3      — chunking neutrality: 100x1 and 50x2 against the same starting pool spend the same total health.
//   ⑤         — SDM=0 (shield-ignoring) sends the whole hit to health, spends no shield.
//   ⑥         — SDM=2 depletes the shield exactly 2x for the same nominal damage.
// ⑥ locks the ARITHMETIC only — whether an authored weapon's ShieldDamageMultiplier actually reaches this function
// at runtime is a wiring question a unit test cannot answer (the test constructs FFPSRDamageSpec directly); that is
// §12-10 PIE check 2's job, not this file's.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRVitalsTest, "FPSRoguelite.Combat.Vitals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRVitalsTest::RunTest(const FString& Parameters)
{
	// --- ① V2: MaxShield=0 matches the current Clamp(Health-Damage, 0, MaxHealth) behavior exactly. -------------
	{
		FPSRVitals::FPool Pool;
		Pool.Shield = 0.0f;
		Pool.MaxShield = 0.0f;
		Pool.Health = 100.0f;
		Pool.MaxHealth = 100.0f;

		const FFPSRDamageSpec Spec; // SDM=1, DamageType empty (Physical)
		const FPSRVitals::FMitigation Mit; // all defaults (1/1/0/0.95)

		const FPSRVitals::FResult Result = FPSRVitals::ApplyDamage(Pool, 30.0f, Spec, Mit);

		TestEqual(TEXT("V2: MaxShield=0 -> ShieldSpent stays 0"), Result.ShieldSpent, 0.0f);
		TestEqual(TEXT("V2: MaxShield=0 -> HealthSpent equals the full Incoming"), Result.HealthSpent, 30.0f);
		TestEqual(TEXT("V2: MaxShield=0 -> Pool.Health matches Clamp(Health-Damage,0,MaxHealth)"),
			Pool.Health, FMath::Clamp(100.0f - 30.0f, 0.0f, 100.0f));
	}

	// --- ② V1 (worst-case mitigation: MaxTotalReduction=0.99, DirectionalArmorDR=1.0, layer coefficients 0) —
	//     two fixtures. A single fixture would be a vacuous pass: ⓐ alone can't tell "shield fully absorbed the
	//     hit" (correct, TotalSpent>0 but HealthSpent==0) apart from "the hit vanished" (a real V1 violation);
	//     ⓑ alone never exercises the Shield>0 path. Together they cover both branches of the invariant.
	{
		FPSRVitals::FMitigation WorstCaseMit;
		WorstCaseMit.ShieldDefense = 0.0f;
		WorstCaseMit.HealthDefense = 0.0f;
		WorstCaseMit.DirectionalArmorDR = 1.0f;
		WorstCaseMit.MaxTotalReduction = 0.99f;
		const FFPSRDamageSpec Spec; // SDM=1 (>0) — V1a's precondition

		// ⓐ Shield>0: the hit must spend SOMETHING (TotalSpent>0). 🔴 Do NOT assert HealthSpent>0 here — a shield
		// fully absorbing a hit (HealthSpent==0) is normal, not a V1 violation (G1 2nd-pass caught exactly this
		// mistake in an earlier draft of this invariant).
		{
			FPSRVitals::FPool Pool;
			Pool.Shield = 50.0f;
			Pool.MaxShield = 50.0f;
			Pool.Health = 100.0f;
			Pool.MaxHealth = 100.0f;

			const FPSRVitals::FResult Result = FPSRVitals::ApplyDamage(Pool, 10.0f, Spec, WorstCaseMit);
			TestTrue(TEXT("V1a: Shield>0, worst-case mitigation -> TotalSpent() > 0 (NOT asserting HealthSpent>0)"),
				Result.TotalSpent() > 0.0f);
		}

		// ⓑ Shield==0, Health>0: the hit must reach health (HealthSpent>0). Without this fixture, ⓐ alone would pass
		// even if the floor were broken for an EMPTY shield (WantShield's floor only matters once Shield can't pay).
		{
			FPSRVitals::FPool Pool;
			Pool.Shield = 0.0f;
			Pool.MaxShield = 50.0f; // has a shield layer, just currently empty — not the MaxShield=0 case (V2)
			Pool.Health = 100.0f;
			Pool.MaxHealth = 100.0f;

			const FPSRVitals::FResult Result = FPSRVitals::ApplyDamage(Pool, 10.0f, Spec, WorstCaseMit);
			TestTrue(TEXT("V1b: Shield==0 and Health>0, worst-case mitigation -> HealthSpent > 0"),
				Result.HealthSpent > 0.0f);
		}
	}

	// --- ③ ComputeRegeneratedShield is idempotent: the same inputs always produce the same output. ---------------
	{
		const float FirstCall = FPSRVitals::ComputeRegeneratedShield(20.0f, 100.0f, 5.0f, 10.0f, 3.0f, 6.0f);
		const float SecondCall = FPSRVitals::ComputeRegeneratedShield(20.0f, 100.0f, 5.0f, 10.0f, 3.0f, 6.0f);
		TestEqual(TEXT("ComputeRegeneratedShield is idempotent (same inputs -> same output)"), FirstCall, SecondCall);
	}

	// --- ④ V3 chunking-neutral: Shield 30 / Health 100, 100x1 vs 50x2 spend the SAME total health. -----------------
	// ⚠️ This does NOT test "a big hit is better" (that's not what the arithmetic does — see §2 goal 5 / §5-1 V3);
	// it tests that splitting a hit into chunks neither wastes nor gains total health damage.
	{
		const FFPSRDamageSpec Spec; // SDM=1
		const FPSRVitals::FMitigation Mit; // defaults

		FPSRVitals::FPool PoolOneHit;
		PoolOneHit.Shield = 30.0f;
		PoolOneHit.MaxShield = 30.0f;
		PoolOneHit.Health = 100.0f;
		PoolOneHit.MaxHealth = 100.0f;
		FPSRVitals::ApplyDamage(PoolOneHit, 100.0f, Spec, Mit);
		const float HealthLostOneHit = 100.0f - PoolOneHit.Health;

		FPSRVitals::FPool PoolTwoHits;
		PoolTwoHits.Shield = 30.0f;
		PoolTwoHits.MaxShield = 30.0f;
		PoolTwoHits.Health = 100.0f;
		PoolTwoHits.MaxHealth = 100.0f;
		FPSRVitals::ApplyDamage(PoolTwoHits, 50.0f, Spec, Mit);
		FPSRVitals::ApplyDamage(PoolTwoHits, 50.0f, Spec, Mit);
		const float HealthLostTwoHits = 100.0f - PoolTwoHits.Health;

		TestEqual(TEXT("V3 chunking-neutral: 100x1 and 50x2 spend the same total health"), HealthLostOneHit, HealthLostTwoHits);
	}

	// --- ⑤ ShieldDamageMultiplier=0 (shield-ignoring): spends no shield, the whole hit lands on health. -----------
	{
		FPSRVitals::FPool Pool;
		Pool.Shield = 50.0f;
		Pool.MaxShield = 50.0f;
		Pool.Health = 100.0f;
		Pool.MaxHealth = 100.0f;

		FFPSRDamageSpec Spec;
		Spec.ShieldDamageMultiplier = 0.0f;
		const FPSRVitals::FMitigation Mit; // defaults

		const FPSRVitals::FResult Result = FPSRVitals::ApplyDamage(Pool, 20.0f, Spec, Mit);
		TestEqual(TEXT("SDM=0: ShieldSpent stays 0"), Result.ShieldSpent, 0.0f);
		TestEqual(TEXT("SDM=0: the full Incoming lands on health"), Result.HealthSpent, 20.0f);
	}

	// --- ⑥ ShieldDamageMultiplier=2 depletes the shield exactly 2x for the same nominal damage (arithmetic ONLY —
	//        whether an authored weapon's multiplier actually reaches this function is §12-10 PIE check 2's job). --
	{
		const FPSRVitals::FMitigation Mit; // defaults

		FPSRVitals::FPool PoolNormal;
		PoolNormal.Shield = 100.0f;
		PoolNormal.MaxShield = 100.0f;
		PoolNormal.Health = 100.0f;
		PoolNormal.MaxHealth = 100.0f;
		const FFPSRDamageSpec SpecNormal; // SDM=1
		FPSRVitals::ApplyDamage(PoolNormal, 30.0f, SpecNormal, Mit);
		const float ShieldLostNormal = 100.0f - PoolNormal.Shield;

		FPSRVitals::FPool PoolDouble;
		PoolDouble.Shield = 100.0f;
		PoolDouble.MaxShield = 100.0f;
		PoolDouble.Health = 100.0f;
		PoolDouble.MaxHealth = 100.0f;
		FFPSRDamageSpec SpecDouble;
		SpecDouble.ShieldDamageMultiplier = 2.0f;
		FPSRVitals::ApplyDamage(PoolDouble, 30.0f, SpecDouble, Mit);
		const float ShieldLostDouble = 100.0f - PoolDouble.Shield;

		TestEqual(TEXT("SDM=2 depletes the shield exactly 2x for the same nominal damage"), ShieldLostDouble, ShieldLostNormal * 2.0f);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
