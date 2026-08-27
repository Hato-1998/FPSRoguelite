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

	// --- (6) EvictOneFromLargestOtherBucket (C4, 「구현 사양 B — 엘리트 캡 회계 + 풀 기아 축출」) — the demand-driven
	//         eviction AcquireEnemy calls on a bucket-miss + hard-cap-reached, at most once per acquire. This
	//         function only REMOVES from the bucket (never Destroy()s) — actor teardown is the calling subsystem's
	//         job, so none of the sub-cases below call Destroy() on a CDO.
	//         ⚠️ This codebase ships only TWO C++ classes under AFPSREnemyBase (itself + AFPSREnemyEliteBase) — no
	//         third class is added here just for this test (the spec for this test explicitly says not to
	//         fabricate one). (6b) below still exercises "pick the larger of two DIFFERENT other buckets" by
	//         passing ExceptClass = nullptr: SubclassOf.h's TSubclassOf::operator*() returns null whenever its
	//         Class is null (`!Class || !Class->IsChildOf(...)`), so comparing any real bucket key against a
	//         null-constructed TSubclassOf is never equal — nullptr therefore matches NO real bucket key, and
	//         BOTH populated buckets stay eligible "other" candidates. What that trick still cannot cover: a
	//         genuine 3-class case where ExceptClass is itself a real, POPULATED class and two OTHER distinct
	//         classes of different sizes exist at the same time — that needs a third production enemy class,
	//         which this test does not add (see this comment / the spec it follows). ---------------------------

	// --- (6a) The ExceptClass bucket is NEVER an eviction target — not when it is the ONLY populated bucket
	//          (-> null), and not even when it is the LARGEST populated bucket (the smaller "other" bucket is
	//          evicted instead). ---------------------------------------------------------------------------------
	{
		FFPSREnemyDormantPool Pool;
		Pool.Add(NormalCdo);
		TestNull(TEXT("(6a) only the except-class (normal) bucket populated -> null"),
			Pool.EvictOneFromLargestOtherBucket(NormalClass));
		TestEqual(TEXT("(6a) the rejected attempt must not have consumed the except-class entry"), Pool.Num(), 1);
	}
	{
		FFPSREnemyDormantPool Pool;
		Pool.Add(EliteCdo);
		TestNull(TEXT("(6a) only the except-class (elite) bucket populated -> null"),
			Pool.EvictOneFromLargestOtherBucket(EliteClass));
		TestEqual(TEXT("(6a) the rejected attempt must not have consumed the except-class entry"), Pool.Num(), 1);
	}
	{
		FFPSREnemyDormantPool Pool;
		Pool.Add(NormalCdo);
		Pool.Add(NormalCdo); // same CDO added twice — Add only keys off GetClass(), so this is a valid way to make
							 // the normal bucket LARGER than the elite bucket without a third class/actor instance.
		Pool.Add(EliteCdo);
		TestEqual(TEXT("(6a) setup: normal bucket (2) outsizes the elite bucket (1)"), Pool.Num(), 3);

		AFPSREnemyBase* Evicted = Pool.EvictOneFromLargestOtherBucket(NormalClass);
		TestTrue(TEXT("(6a) ExceptClass's bucket is the BIGGER one but must still be skipped — evicts elite instead"),
			Evicted == EliteCdo);
		TestEqual(TEXT("(6a) only the elite entry left the pool"), Pool.Num(), 2);
	}

	// --- (6b) Among the OTHER (non-except) buckets, the LARGEST one is chosen. ExceptClass = nullptr matches no
	//          real bucket key (see the section comment above), so both the normal and elite buckets stay
	//          eligible — this is how "pick the larger of two DIFFERENT other buckets" gets exercised with just
	//          the two enemy classes this codebase has. -------------------------------------------------------
	{
		FFPSREnemyDormantPool Pool;
		Pool.Add(NormalCdo);                 // normal bucket size 1
		Pool.Add(EliteCdo);
		Pool.Add(EliteCdo);
		Pool.Add(EliteCdo);                  // elite bucket size 3 — the larger of the two candidates
		TestEqual(TEXT("(6b) setup: 4 entries total (1 normal + 3 elite)"), Pool.Num(), 4);

		AFPSREnemyBase* Evicted = Pool.EvictOneFromLargestOtherBucket(nullptr);
		TestTrue(TEXT("(6b) picks from the larger bucket (elite, size 3 > normal, size 1)"), Evicted == EliteCdo);
		TestEqual(TEXT("(6b) pool shrinks by exactly one"), Pool.Num(), 3);
	}

	// --- (6c) A completely empty pool, or a pool with no OTHER populated bucket at all, returns null. -----------
	{
		FFPSREnemyDormantPool EmptyPool;
		TestNull(TEXT("(6c) completely empty pool -> null"), EmptyPool.EvictOneFromLargestOtherBucket(NormalClass));
		TestNull(TEXT("(6c) completely empty pool, ExceptClass nullptr -> still null"),
			EmptyPool.EvictOneFromLargestOtherBucket(nullptr));
		TestEqual(TEXT("(6c) empty pool stays empty"), EmptyPool.Num(), 0);
	}

	// --- (6d) The evicted actor is ACTUALLY removed from the pool's data — Num() reflects it, and a follow-up
	//          eviction attempt finds nothing left (not merely "returned while still logically present"). -------
	{
		FFPSREnemyDormantPool Pool;
		Pool.Add(NormalCdo);
		Pool.Add(EliteCdo);
		TestEqual(TEXT("(6d) setup: two distinct-class entries"), Pool.Num(), 2);

		AFPSREnemyBase* Evicted = Pool.EvictOneFromLargestOtherBucket(NormalClass);
		TestTrue(TEXT("(6d) evicts the only other (elite) entry"), Evicted == EliteCdo);
		TestEqual(TEXT("(6d) Num() drops by exactly one"), Pool.Num(), 1);

		TestNull(TEXT("(6d) a second eviction attempt now finds no other bucket left"),
			Pool.EvictOneFromLargestOtherBucket(NormalClass));
		TestEqual(TEXT("(6d) Num unaffected by the null second attempt"), Pool.Num(), 1);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
