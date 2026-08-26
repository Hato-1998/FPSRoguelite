// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Enemy/FPSREnemyDormantPool.h"
#include "Enemy/FPSREnemyBase.h"
#include "Enemy/FPSREnemyEliteBase.h"

#if WITH_AUTOMATION_TESTS

// FFPSREnemyDormantPool bucketing golden (ADR 0013 「Docs/Architecture/0013-enemy-tier-axis-and-elite-gas.md」
// 불변식 7 의 실행 수단). CDOs stand in for spawned actors — no world/SpawnActor needed, since Add/AcquireOfClass
// only key off GetClass() and never touch actor state (that's the subsystem's job via Activate/Deactivate).
// Same worldless-CDO technique FPSRDirectorSensorTest.cpp already uses. Add() takes a non-const pointer, so this
// uses GetMutableDefault (GetDefault returns const and won't bind).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSREnemyDormantPoolTest, "FPSRoguelite.Enemy.DormantPool",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSREnemyDormantPoolTest::RunTest(const FString& Parameters)
{
	AFPSREnemyBase* NormalCdo = GetMutableDefault<AFPSREnemyBase>();
	AFPSREnemyEliteBase* EliteCdo = GetMutableDefault<AFPSREnemyEliteBase>();
	UClass* const NormalClass = AFPSREnemyBase::StaticClass();
	UClass* const EliteClass = AFPSREnemyEliteBase::StaticClass();

	if (!NormalCdo || !EliteCdo)
	{
		AddError(TEXT("CDOs failed to resolve — cannot exercise the pool"));
		return false;
	}

	// --- (1) Class cross-contamination is 0 — a pool holding BOTH a normal and an elite CDO hands each class
	//         request back its OWN class only, never the other. -------------------------------------------------
	{
		FFPSREnemyDormantPool Pool;
		Pool.Add(NormalCdo);
		Pool.Add(EliteCdo);
		TestEqual(TEXT("(1) two distinct-class adds -> Num == 2"), Pool.Num(), 2);

		AFPSREnemyBase* AcquiredElite = Pool.AcquireOfClass(EliteClass);
		TestTrue(TEXT("(1) elite request returns exactly the elite CDO"), AcquiredElite == EliteCdo);

		AFPSREnemyBase* AcquiredNormal = Pool.AcquireOfClass(NormalClass);
		TestTrue(TEXT("(1) normal request returns exactly the normal CDO"), AcquiredNormal == NormalCdo);

		TestEqual(TEXT("(1) pool drained after both acquires"), Pool.Num(), 0);
	}

	// --- (2) Requesting a class the pool has never seen (or an entirely empty pool) is null. -------------------
	{
		FFPSREnemyDormantPool EmptyPool;
		TestNull(TEXT("(2) empty pool: normal request -> null"), EmptyPool.AcquireOfClass(NormalClass));
		TestNull(TEXT("(2) empty pool: elite request -> null"), EmptyPool.AcquireOfClass(EliteClass));
		TestEqual(TEXT("(2) empty pool: Num == 0"), EmptyPool.Num(), 0);

		FFPSREnemyDormantPool NormalOnlyPool;
		NormalOnlyPool.Add(NormalCdo);
		TestNull(TEXT("(2) a bucket that was never populated (elite, in a normal-only pool) -> null"),
			NormalOnlyPool.AcquireOfClass(EliteClass));
		TestEqual(TEXT("(2) the missing-bucket request must not have touched the normal entry"), NormalOnlyPool.Num(), 1);

		// Defensive null-input guards (Add/AcquireOfClass both no-op on null rather than crash).
		FFPSREnemyDormantPool GuardPool;
		GuardPool.Add(nullptr);
		TestEqual(TEXT("(2) Add(nullptr) is a no-op"), GuardPool.Num(), 0);
		TestNull(TEXT("(2) AcquireOfClass(nullptr) -> null"), GuardPool.AcquireOfClass(nullptr));
	}

	// --- (3) Release/re-acquire round-trip: an acquired actor can be Add()-ed back and acquired again; a second
	//         consecutive acquire with nothing re-added is null. -----------------------------------------------
	{
		FFPSREnemyDormantPool Pool;
		Pool.Add(NormalCdo);

		AFPSREnemyBase* First = Pool.AcquireOfClass(NormalClass);
		TestTrue(TEXT("(3) round-trip: first acquire returns the added CDO"), First == NormalCdo);
		TestNull(TEXT("(3) round-trip: a second consecutive acquire (nothing re-added) -> null"), Pool.AcquireOfClass(NormalClass));

		Pool.Add(First);
		AFPSREnemyBase* Second = Pool.AcquireOfClass(NormalClass);
		TestTrue(TEXT("(3) round-trip: re-added actor can be acquired again"), Second == NormalCdo);
		TestNull(TEXT("(3) round-trip: acquiring once more (only one was ever re-added) -> null"), Pool.AcquireOfClass(NormalClass));
	}

	// --- (4) Num() tally stays correct across an interleaved Add/Acquire sequence over two classes. -------------
	{
		FFPSREnemyDormantPool Pool;
		TestEqual(TEXT("(4) Num tally: starts at 0"), Pool.Num(), 0);
		Pool.Add(NormalCdo);
		TestEqual(TEXT("(4) Num tally: after one normal add"), Pool.Num(), 1);
		Pool.Add(EliteCdo);
		TestEqual(TEXT("(4) Num tally: after adding a second, different-class enemy"), Pool.Num(), 2);
		Pool.AcquireOfClass(EliteClass);
		TestEqual(TEXT("(4) Num tally: after acquiring the elite back out"), Pool.Num(), 1);
		Pool.AcquireOfClass(NormalClass);
		TestEqual(TEXT("(4) Num tally: after acquiring the normal back out (drained)"), Pool.Num(), 0);
	}

	// --- (5) THE core property this test exists for: an elite request must not pick up a normal enemy, even
	//         though AFPSREnemyEliteBase IS a child of AFPSREnemyBase (an IsChildOf-based implementation would
	//         satisfy this substitutability and pass a normal instance off as the elite that was asked for — or,
	//         the other direction, hand an elite instance to a plain request). Isolated single-class pools make
	//         either direction of that bug unmissable: the ONLY entry present is the wrong class, so any non-null
	//         return is necessarily the bug. -----------------------------------------------------------------
	{
		FFPSREnemyDormantPool NormalOnlyPool;
		NormalOnlyPool.Add(NormalCdo);
		TestNull(TEXT("(5) elite request against a NORMAL-only pool must be null — must NOT return the normal CDO"),
			NormalOnlyPool.AcquireOfClass(EliteClass));
		TestEqual(TEXT("(5) the rejected elite request must not have consumed the normal entry"), NormalOnlyPool.Num(), 1);

		FFPSREnemyDormantPool EliteOnlyPool;
		EliteOnlyPool.Add(EliteCdo);
		TestNull(TEXT("(5) normal request against an ELITE-only pool must be null — must NOT return the elite CDO"),
			EliteOnlyPool.AcquireOfClass(NormalClass));
		TestEqual(TEXT("(5) the rejected normal request must not have consumed the elite entry"), EliteOnlyPool.Num(), 1);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
