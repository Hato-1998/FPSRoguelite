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
//   ⑦ G2 P2-1 — a DoT right after a shield-BREAKING direct hit keeps that hit's broken delay (regen 5/tick > DoT
//                4/tick: rev4's "Now - Delay" anchor restarted regen at the first tick and the shield climbed).
//   ⑧ §10-12  — a DoT kept going PAST the delay still nets the shield down (rev3's "freeze the time anchor" refilled
//                it to 95 by t=6.0; rev4's anchor erased the partial delay and held it at 45).
//   ⑨ G2 P2-1 — a DoT taking the shield from partial to broken adds no delay of its own: ⓐ a direct hit's PARTIAL
//                delay survives a DoT that finishes the break ⓑ a DoT break while regen is already running resumes
//                at once.
// ⑦–⑨ drive the step order UFPSREnemyHealthComponent::ApplyDamage/CatchUpShieldRegen run (settle regen, monotonic
// increase only -> spend -> time anchor from the PREVIOUS value anchor -> value anchor). Whether that component still
// runs those steps in that order is a code-review question a pure test cannot see.
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

	// --- ⑦–⑨ shared tick model (STAT1 §6-2 time-anchor policy, G2 P2-1) — the step order is the file header's. ------
	//     Constants: Regen 10/s, PartialDelay 3s, BrokenDelay 6s, MaxShield 100, default mitigation.
	struct FSimRegenSubject
	{
		FPSRVitals::FPool Pool;
		float ShieldAtLastDamage = 0.0f;
		float LastDamageTime = 0.0f;
	};

	const float SimRegenPerSecond = 10.0f;
	const float SimPartialDelay = 3.0f;
	const float SimBrokenDelay = 6.0f;
	const FPSRVitals::FMitigation SimMit; // defaults
	const FFPSRDamageSpec SimDirectSpec; // bDotRegenAnchorPolicy false
	FFPSRDamageSpec SimDotSpec;
	SimDotSpec.bDotRegenAnchorPolicy = true;

	// CatchUpShieldRegen: settle owed regen; never lowers the shield.
	auto SimSettleRegen = [&](FSimRegenSubject& Subject, float Now)
	{
		if (Subject.Pool.MaxShield <= 0.0f || Subject.Pool.Shield >= Subject.Pool.MaxShield)
		{
			return;
		}
		const float NewShield = FPSRVitals::ComputeRegeneratedShield(Subject.ShieldAtLastDamage, Subject.Pool.MaxShield,
			Now - Subject.LastDamageTime, SimRegenPerSecond, SimPartialDelay, SimBrokenDelay);
		if (NewShield > Subject.Pool.Shield + UE_KINDA_SMALL_NUMBER)
		{
			Subject.Pool.Shield = NewShield;
		}
	};

	// ApplyDamage: settle -> spend -> time anchor (reads the OLD value anchor) -> value anchor.
	auto SimHit = [&](FSimRegenSubject& Subject, float Now, float Damage, const FFPSRDamageSpec& HitSpec)
	{
		SimSettleRegen(Subject, Now);
		FPSRVitals::ApplyDamage(Subject.Pool, Damage, HitSpec, SimMit);
		Subject.LastDamageTime = FPSRVitals::ComputeRegenTimeAnchor(Subject.LastDamageTime, Subject.ShieldAtLastDamage,
			Now, Subject.Pool.Shield, SimPartialDelay, SimBrokenDelay, HitSpec.bDotRegenAnchorPolicy);
		Subject.ShieldAtLastDamage = Subject.Pool.Shield;
	};

	auto MakeSimSubject = [](float Shield, float Health, float ShieldAtLastDamage, float LastDamageTime)
	{
		FSimRegenSubject Subject;
		Subject.Pool.Shield = Shield;
		Subject.Pool.MaxShield = 100.0f;
		Subject.Pool.Health = Health;
		Subject.Pool.MaxHealth = Health;
		Subject.ShieldAtLastDamage = ShieldAtLastDamage;
		Subject.LastDamageTime = LastDamageTime;
		return Subject;
	};

	// --- ⑦ G2 P2-1: a DoT tick must not cancel the delay a shield-BREAKING direct hit imposed. Regen 5/tick > DoT
	//     4/tick is where rev4's anchor made a DoT round a net LOSS (regen restarted at the first tick: Shield 1 at
	//     t=1.0, Health only 986 after the round).
	{
		FSimRegenSubject Subject = MakeSimSubject(20.0f, 1000.0f, 20.0f, 0.0f);

		SimHit(Subject, 0.0f, 30.0f, SimDirectSpec); // breaks the shield -> broken delay, regen due at t=6.0
		TestEqual(TEXT("P2-1: the direct hit breaks the shield"), Subject.Pool.Shield, 0.0f);
		TestEqual(TEXT("P2-1: the direct hit overflows 10 to health"), Subject.Pool.Health, 990.0f);

		float FirstRegenTime = -1.0f;
		for (int32 Tick = 1; Tick <= 11; ++Tick) // DoT 4 every 0.5s over t=0.5..5.5
		{
			const float Now = 0.5f * Tick;
			SimHit(Subject, Now, 4.0f, SimDotSpec);
			if (FirstRegenTime < 0.0f && Subject.Pool.Shield > 0.0f)
			{
				FirstRegenTime = Now;
			}
		}
		TestTrue(FString::Printf(TEXT("P2-1: shield stays broken for every DoT tick inside the direct hit's delay (first regen at t=%.1f)"), FirstRegenTime),
			FirstRegenTime < 0.0f);
		TestEqual(TEXT("P2-1: every DoT tick lands on health (990 - 11x4)"), Subject.Pool.Health, 946.0f);

		SimSettleRegen(Subject, 6.0f);
		TestEqual(TEXT("P2-1: t=6.0 (the direct hit's broken delay) -> nothing regenerated yet"), Subject.Pool.Shield, 0.0f);
		SimSettleRegen(Subject, 6.5f);
		TestEqual(TEXT("P2-1: t=6.5 -> regen resumed on time (10/s x 0.5s)"), Subject.Pool.Shield, 5.0f);
	}

	// --- ⑧ STAT1 §10-12: a DoT kept going PAST the regen delay still nets the shield down. Start = "a direct hit at
	//     t=0 left 50" (partial delay, regen due at t=3.0); once regen resumes, DoT 5/0.5s cancels regen 10/s exactly.
	//     rev3 ("freeze the time anchor") compounded regen back in (rising from t=4.0, 95 by t=6.0); rev4 held 45.
	{
		FSimRegenSubject Subject = MakeSimSubject(50.0f, 1000.0f, 50.0f, 0.0f);

		float PreviousShield = Subject.Pool.Shield;
		float FirstRiseTime = -1.0f;
		for (int32 Tick = 1; Tick <= 12; ++Tick) // DoT 5 every 0.5s over t=0.5..6.0
		{
			const float Now = 0.5f * Tick;
			SimHit(Subject, Now, 5.0f, SimDotSpec);
			if (FirstRiseTime < 0.0f && Subject.Pool.Shield > PreviousShield + UE_KINDA_SMALL_NUMBER)
			{
				FirstRiseTime = Now;
			}
			PreviousShield = Subject.Pool.Shield;
		}
		TestTrue(FString::Printf(TEXT("10-12: shield never rises tick-over-tick under the DoT (first rise at t=%.1f)"), FirstRiseTime),
			FirstRiseTime < 0.0f);
		TestEqual(TEXT("10-12: 6 ticks spend 30 before t=3.0, then regen and DoT cancel -> 20"), Subject.Pool.Shield, 20.0f);
		TestEqual(TEXT("10-12: the shield absorbed every tick"), Subject.Pool.Health, 1000.0f);
	}

	// --- ⑨ G2 P2-1: a DoT taking the shield from partial to broken imposes no delay of its own. This is what separates
	//     the resume-time anchor from Max(PreviousAnchor, Now - Delay) (the red team's first proposal), which measured
	//     the BROKEN delay from the previous anchor: ⓐ regen at t=6.0 instead of 3.0 · ⓑ nothing at t=0.5.
	{
		// ⓐ a direct hit leaves 10 (partial delay -> regen due at t=3.0); a DoT tick finishes the break at t=0.5.
		FSimRegenSubject Subject = MakeSimSubject(100.0f, 1000.0f, 100.0f, -100.0f);
		SimHit(Subject, 0.0f, 90.0f, SimDirectSpec);
		SimHit(Subject, 0.5f, 20.0f, SimDotSpec);
		TestEqual(TEXT("P2-1 (a): the DoT tick breaks the chipped shield"), Subject.Pool.Shield, 0.0f);
		TestEqual(TEXT("P2-1 (a): its overflow reaches health"), Subject.Pool.Health, 990.0f);
		SimSettleRegen(Subject, 3.0f);
		TestEqual(TEXT("P2-1 (a): t=3.0 (the direct hit's PARTIAL delay) -> nothing regenerated yet"), Subject.Pool.Shield, 0.0f);
		SimSettleRegen(Subject, 3.5f);
		TestEqual(TEXT("P2-1 (a): t=3.5 -> regen resumed on the partial delay, not the broken one"), Subject.Pool.Shield, 5.0f);
	}
	{
		// ⓑ regen is already running (the previous tick, at t=-0.5, left 5 and anchored at -3.5); this tick breaks it.
		FSimRegenSubject Subject = MakeSimSubject(5.0f, 1000.0f, 5.0f, -3.5f);
		SimHit(Subject, 0.0f, 12.0f, SimDotSpec); // settles 5 -> 10 first, then 12 breaks it with 2 overflow
		TestEqual(TEXT("P2-1 (b): the DoT tick breaks a regenerating shield"), Subject.Pool.Shield, 0.0f);
		TestEqual(TEXT("P2-1 (b): its overflow reaches health"), Subject.Pool.Health, 998.0f);
		SimSettleRegen(Subject, 0.5f);
		TestEqual(TEXT("P2-1 (b): t=0.5 -> regen continues with no new delay"), Subject.Pool.Shield, 5.0f);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
