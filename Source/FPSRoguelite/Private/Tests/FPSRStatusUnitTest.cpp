// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Status/FPSRStatus.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#if WITH_AUTOMATION_TESTS

// STAT1 §10 B단계 — pure-function / DataAsset-validation checks only (no world, no actor; mirrors
// FPSRVitalsTest.cpp/FPSRCardSynergyTest.cpp's worldless style, since FPSRStatus::Apply/Advance/Resolve hold no
// state of their own and need none to exercise). Covers §10 unit items 1-10 and 14 — items 11-13 need a C단계
// driver (batch pass / boss Tick / bStatusDriverPresent wiring) or a live World and are explicitly out of scope for
// this phase (see FPSRStatusClockTest.cpp for the sibling A단계 clock-only world test).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRStatusUnitTest, "FPSRoguelite.Status.Unit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRStatusUnitTest::RunTest(const FString& Parameters)
{
	// Small local fixture builder — every field defaults to the DataAsset's own no-op default (§5-1), so each test
	// only sets the fields it actually cares about (mirrors FPSRVitalsTest.cpp's per-block-fresh-fixture style).
	auto MakeStatus = [](uint8 Slot, EFPSRStatusKind Kind, float Duration) -> UFPSRStatusEffectDataAsset*
	{
		UFPSRStatusEffectDataAsset* Status = NewObject<UFPSRStatusEffectDataAsset>();
		Status->SlotIndex = Slot;
		Status->Kind = Kind;
		Status->DurationSeconds = Duration;
		return Status;
	};
	auto MakeCatalog = [](const TArray<UFPSRStatusEffectDataAsset*>& Statuses) -> UFPSRStatusCatalogDataAsset*
	{
		UFPSRStatusCatalogDataAsset* Catalog = NewObject<UFPSRStatusCatalogDataAsset>();
		for (UFPSRStatusEffectDataAsset* Status : Statuses)
		{
			Catalog->Statuses.Add(Status);
		}
		return Catalog;
	};

	// --- 1. 재적용 = 지속시간 갱신, 스택 없음 --------------------------------------------------------------------------
	{
		UFPSRStatusEffectDataAsset* Weak0 = MakeStatus(0, EFPSRStatusKind::Weak, 5.0f);
		UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Weak0 });

		uint8 Bits = 0;
		FFPSRStatusServerState State;
		TArray<uint8, TInlineAllocator<8>> Fired;

		TestTrue(TEXT("1. first Apply succeeds"), FPSRStatus::Apply(Bits, State, *Catalog, 0, 0.0f, 1.0f, 1.0f, Fired));
		TestEqual(TEXT("1. bit 0 set"), (int32)Bits, 1);
		TestEqual(TEXT("1. SlotExpiry[0] = Now(0) + Duration(5)"), State.SlotExpiry[0], 5.0f);

		TestTrue(TEXT("1. reapply while active succeeds"), FPSRStatus::Apply(Bits, State, *Catalog, 0, 2.0f, 1.0f, 1.0f, Fired));
		TestEqual(TEXT("1. reapply does NOT stack (still just bit 0)"), (int32)Bits, 1);
		TestEqual(TEXT("1. reapply REFRESHES to Now(2) + Duration(5) = 7, not 5+5=10"), State.SlotExpiry[0], 7.0f);
	}

	// --- 2. 조합 성립 -> 강한 ON + 재료 2비트 OFF ----------------------------------------------------------------------
	{
		UFPSRStatusEffectDataAsset* Weak0 = MakeStatus(0, EFPSRStatusKind::Weak, 5.0f);
		UFPSRStatusEffectDataAsset* Weak1 = MakeStatus(1, EFPSRStatusKind::Weak, 5.0f);
		UFPSRStatusEffectDataAsset* Strong2 = MakeStatus(2, EFPSRStatusKind::Strong, 4.0f);
		Strong2->RequiredWeakSlots = { 0, 1 };
		Strong2->bConsumeSources = true;
		UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Weak0, Weak1, Strong2 });

		uint8 Bits = 0;
		FFPSRStatusServerState State;
		TArray<uint8, TInlineAllocator<8>> Fired;

		FPSRStatus::Apply(Bits, State, *Catalog, 0, 0.0f, 1.0f, 1.0f, Fired);
		TestEqual(TEXT("2. after material 1: only bit 0"), (int32)Bits, 1);

		FPSRStatus::Apply(Bits, State, *Catalog, 1, 0.0f, 1.0f, 1.0f, Fired);
		TestEqual(TEXT("2. combo fires: bit 2 ON, bits 0+1 OFF"), (int32)Bits, 1 << 2);
		TestEqual(TEXT("2. OutFired reports exactly slot 2"), Fired.Num(), 1);
		if (Fired.Num() == 1)
		{
			TestEqual(TEXT("2. OutFired[0] == 2"), (int32)Fired[0], 2);
		}
		TestEqual(TEXT("2. Strong's SlotExpiry = Now(0) + its OWN Duration(4)"), State.SlotExpiry[2], 4.0f);
	}

	// --- 3. 재료 하나로는 미성립 --------------------------------------------------------------------------------------
	{
		UFPSRStatusEffectDataAsset* Weak0 = MakeStatus(0, EFPSRStatusKind::Weak, 5.0f);
		UFPSRStatusEffectDataAsset* Weak1 = MakeStatus(1, EFPSRStatusKind::Weak, 5.0f);
		UFPSRStatusEffectDataAsset* Strong2 = MakeStatus(2, EFPSRStatusKind::Strong, 4.0f);
		Strong2->RequiredWeakSlots = { 0, 1 };
		UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Weak0, Weak1, Strong2 });

		uint8 Bits = 0;
		FFPSRStatusServerState State;
		TArray<uint8, TInlineAllocator<8>> Fired;

		FPSRStatus::Apply(Bits, State, *Catalog, 0, 0.0f, 1.0f, 1.0f, Fired);
		TestEqual(TEXT("3. one material only -> no combo (bit 2 stays off)"), (int32)Bits, 1);
		TestEqual(TEXT("3. OutFired empty"), Fired.Num(), 0);
	}

	// --- 4. 다중 성립 시 앞선 것 하나만 (카탈로그 배열 순서) ------------------------------------------------------------
	{
		UFPSRStatusEffectDataAsset* Weak0 = MakeStatus(0, EFPSRStatusKind::Weak, 5.0f);
		UFPSRStatusEffectDataAsset* Weak1 = MakeStatus(1, EFPSRStatusKind::Weak, 5.0f);
		UFPSRStatusEffectDataAsset* Weak2 = MakeStatus(2, EFPSRStatusKind::Weak, 5.0f);
		UFPSRStatusEffectDataAsset* Strong3 = MakeStatus(3, EFPSRStatusKind::Strong, 4.0f);
		Strong3->RequiredWeakSlots = { 0, 1 }; // array order: BEFORE Strong4
		UFPSRStatusEffectDataAsset* Strong4 = MakeStatus(4, EFPSRStatusKind::Strong, 4.0f);
		Strong4->RequiredWeakSlots = { 1, 2 };
		UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Weak0, Weak1, Weak2, Strong3, Strong4 });

		uint8 Bits = 0;
		FFPSRStatusServerState State;
		TArray<uint8, TInlineAllocator<8>> Fired;

		FPSRStatus::Apply(Bits, State, *Catalog, 0, 0.0f, 1.0f, 1.0f, Fired); // slot 0
		FPSRStatus::Apply(Bits, State, *Catalog, 2, 0.0f, 1.0f, 1.0f, Fired); // slot 2 — both Strong3 and Strong4 still short one material
		TestEqual(TEXT("4. two unrelated materials, no combo yet"), (int32)Bits, (1 << 0) | (1 << 2));

		// The deciding Apply — all 3 materials (0,1,2) are present the instant this call's combo check runs.
		FPSRStatus::Apply(Bits, State, *Catalog, 1, 0.0f, 1.0f, 1.0f, Fired);

		TestTrue(TEXT("4. bit 3 (Strong3) is ON"), (Bits & (1 << 3)) != 0);
		TestTrue(TEXT("4. bit 4 (Strong4) is OFF — its material (slot 1) was already consumed by Strong3"), (Bits & (1 << 4)) == 0);
		TestTrue(TEXT("4. bit 2 (Weak2, not part of Strong3's pair) is still ON"), (Bits & (1 << 2)) != 0);
		TestTrue(TEXT("4. bits 0/1 (Strong3's consumed materials) are OFF"), (Bits & ((1 << 0) | (1 << 1))) == 0);
		TestEqual(TEXT("4. OutFired reports exactly Strong3 (slot 3), not Strong4"), Fired.Num(), 1);
		if (Fired.Num() == 1)
		{
			TestEqual(TEXT("4. OutFired[0] == 3"), (int32)Fired[0], 3);
		}
	}

	// --- 5. 저항 0=거부 / 0.5=절반, Weak·Strong 독립 -------------------------------------------------------------------
	{
		UFPSRStatusEffectDataAsset* Weak0 = MakeStatus(0, EFPSRStatusKind::Weak, 10.0f);
		UFPSRStatusEffectDataAsset* Strong1 = MakeStatus(1, EFPSRStatusKind::Strong, 10.0f);
		Strong1->RequiredWeakSlots = { 0, 2 }; // irrelevant here — this test applies Slot 1 directly, never via combo
		UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Weak0, Strong1 });
		TArray<uint8, TInlineAllocator<8>> Fired;

		// (a) Weak resist 0 -> reject.
		{
			uint8 Bits = 0;
			FFPSRStatusServerState State;
			TestFalse(TEXT("5a. WeakResist 0 rejects a Weak apply"), FPSRStatus::Apply(Bits, State, *Catalog, 0, 0.0f, 0.0f, 1.0f, Fired));
			TestEqual(TEXT("5a. rejected apply touches no bits"), (int32)Bits, 0);
		}
		// (b) Weak resist 0.5 -> half duration.
		{
			uint8 Bits = 0;
			FFPSRStatusServerState State;
			TestTrue(TEXT("5b. WeakResist 0.5 still applies"), FPSRStatus::Apply(Bits, State, *Catalog, 0, 0.0f, 0.5f, 1.0f, Fired));
			TestEqual(TEXT("5b. duration halved: 10 * 0.5 = 5"), State.SlotExpiry[0], 5.0f);
		}
		// (c) Strong resist 0 -> reject (direct apply of a Strong slot).
		{
			uint8 Bits = 0;
			FFPSRStatusServerState State;
			TestFalse(TEXT("5c. StrongResist 0 rejects a Strong apply"), FPSRStatus::Apply(Bits, State, *Catalog, 1, 0.0f, 1.0f, 0.0f, Fired));
			TestEqual(TEXT("5c. rejected apply touches no bits"), (int32)Bits, 0);
		}
		// (d) Independence: WeakResist 0 must NOT affect a Strong-kind slot's own application.
		{
			uint8 Bits = 0;
			FFPSRStatusServerState State;
			TestTrue(TEXT("5d. WeakResist 0 does NOT block a Strong apply (independent axes)"),
				FPSRStatus::Apply(Bits, State, *Catalog, 1, 0.0f, 0.0f, 1.0f, Fired));
			TestEqual(TEXT("5d. Strong bit set despite WeakResist=0"), (int32)Bits, 1 << 1);
		}
	}

	// --- 6. Resolve 축별 곱·OR ----------------------------------------------------------------------------------------
	{
		UFPSRStatusEffectDataAsset* StatusA = MakeStatus(0, EFPSRStatusKind::Weak, 5.0f);
		StatusA->MoveSpeedMultiplier = 0.5f;
		StatusA->AttackIntervalMultiplier = 2.0f;
		StatusA->IncomingDamageMultiplier = 1.5f;
		StatusA->bDisableAttack = false;
		StatusA->bDisableMovement = true;

		UFPSRStatusEffectDataAsset* StatusB = MakeStatus(1, EFPSRStatusKind::Weak, 5.0f);
		StatusB->MoveSpeedMultiplier = 0.5f;
		StatusB->AttackIntervalMultiplier = 2.0f;
		StatusB->IncomingDamageMultiplier = 2.0f;
		StatusB->bDisableAttack = true;
		StatusB->bDisableMovement = false;

		UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ StatusA, StatusB });

		const FFPSRResolvedStatus NoneActive = FPSRStatus::Resolve(0, *Catalog);
		TestEqual(TEXT("6. no bits active -> MoveSpeedMultiplier default 1.0"), NoneActive.MoveSpeedMultiplier, 1.0f);
		TestFalse(TEXT("6. no bits active -> bDisableAttack default false"), NoneActive.bDisableAttack);

		const FFPSRResolvedStatus Both = FPSRStatus::Resolve((1 << 0) | (1 << 1), *Catalog);
		TestEqual(TEXT("6. MoveSpeedMultiplier multiplies: 0.5 * 0.5 = 0.25"), Both.MoveSpeedMultiplier, 0.25f);
		TestEqual(TEXT("6. AttackIntervalMultiplier multiplies: 2.0 * 2.0 = 4.0"), Both.AttackIntervalMultiplier, 4.0f);
		TestEqual(TEXT("6. IncomingDamageMultiplier multiplies: 1.5 * 2.0 = 3.0"), Both.IncomingDamageMultiplier, 3.0f);
		TestTrue(TEXT("6. bDisableAttack ORs: false | true = true"), Both.bDisableAttack);
		TestTrue(TEXT("6. bDisableMovement ORs: true | false = true"), Both.bDisableMovement);
	}

	// --- 7. 쿨다운 미래면 부여 거부 ------------------------------------------------------------------------------------
	{
		UFPSRStatusEffectDataAsset* Weak0 = MakeStatus(0, EFPSRStatusKind::Weak, 2.0f);
		Weak0->RetriggerCooldownSeconds = 5.0f;
		UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Weak0 });

		uint8 Bits = 0;
		FFPSRStatusServerState State;
		TArray<uint8, TInlineAllocator<8>> Fired;

		TestTrue(TEXT("7. first apply succeeds"), FPSRStatus::Apply(Bits, State, *Catalog, 0, 0.0f, 1.0f, 1.0f, Fired));
		TestEqual(TEXT("7. SlotCooldownUntil = Now(0) + RetriggerCooldownSeconds(5)"), State.SlotCooldownUntil[0], 5.0f);
		const float ExpiryAfterFirstApply = State.SlotExpiry[0];

		TestFalse(TEXT("7. reapply BEFORE cooldown elapses is rejected"), FPSRStatus::Apply(Bits, State, *Catalog, 0, 3.0f, 1.0f, 1.0f, Fired));
		TestEqual(TEXT("7. rejected reapply does not touch SlotExpiry"), State.SlotExpiry[0], ExpiryAfterFirstApply);

		TestTrue(TEXT("7. reapply exactly AT the cooldown boundary succeeds"), FPSRStatus::Apply(Bits, State, *Catalog, 0, 5.0f, 1.0f, 1.0f, Fired));
		TestEqual(TEXT("7. accepted reapply refreshes: Now(5) + Duration(2) = 7"), State.SlotExpiry[0], 7.0f);
	}

	// --- 8. 강한 상태 재발동이 기존 강한 비트의 만료를 갱신 -------------------------------------------------------------
	{
		UFPSRStatusEffectDataAsset* Weak0 = MakeStatus(0, EFPSRStatusKind::Weak, 5.0f);
		UFPSRStatusEffectDataAsset* Weak1 = MakeStatus(1, EFPSRStatusKind::Weak, 5.0f);
		UFPSRStatusEffectDataAsset* Strong2 = MakeStatus(2, EFPSRStatusKind::Strong, 5.0f);
		Strong2->RequiredWeakSlots = { 0, 1 };
		Strong2->bConsumeSources = false; // materials must stay up for a SECOND combo check to see them again
		UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Weak0, Weak1, Strong2 });

		uint8 Bits = 0;
		FFPSRStatusServerState State;
		TArray<uint8, TInlineAllocator<8>> Fired;

		FPSRStatus::Apply(Bits, State, *Catalog, 0, 0.0f, 1.0f, 1.0f, Fired);
		FPSRStatus::Apply(Bits, State, *Catalog, 1, 0.0f, 1.0f, 1.0f, Fired); // combo fires here
		TestEqual(TEXT("8. combo fired: Strong's first SlotExpiry = 0 + 5"), State.SlotExpiry[2], 5.0f);
		TestTrue(TEXT("8. materials stayed up (bConsumeSources=false)"), (Bits & ((1 << 0) | (1 << 1))) == ((1 << 0) | (1 << 1)));

		// Reapplying a material while the Strong bit is STILL active re-triggers its combo check (§7-2/§7-1).
		FPSRStatus::Apply(Bits, State, *Catalog, 0, 2.0f, 1.0f, 1.0f, Fired);
		TestEqual(TEXT("8. Strong re-fire REFRESHES its expiry: Now(2) + Duration(5) = 7, not left at 5"), State.SlotExpiry[2], 7.0f);
		TestTrue(TEXT("8. Strong bit is still just ONE bit (no stacking)"), (Bits & (1 << 2)) != 0);
	}

	// --- 9. DoT가 활성 구간으로 클램프 — [LastStatusStepClock, Now] ∩ [부여, 만료]만 적용 --------------------------------
	{
		UFPSRStatusEffectDataAsset* Dot0 = MakeStatus(0, EFPSRStatusKind::Weak, 3.0f); // expires at Grant+3
		Dot0->DamagePerSecond = 10.0f;
		Dot0->DotTickIntervalSeconds = 0.5f;
		UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Dot0 });

		uint8 Bits = 0;
		FFPSRStatusServerState State;
		TArray<uint8, TInlineAllocator<8>> Fired, Expired;
		float DotDamage = 0.0f;

		// Granted at Now=10 (mid-run, not at t=0) — exercises the cold-start anchor (Apply stamps LastStatusStepClock
		// to the grant moment since Bits was 0 going in), not just a lucky "starts at 0" coincidence.
		FPSRStatus::Apply(Bits, State, *Catalog, 0, 10.0f, 1.0f, 1.0f, Fired);
		TestEqual(TEXT("9. cold-start anchor: LastStatusStepClock == grant time (10), not 0"), State.LastStatusStepClock, 10.0f);
		TestEqual(TEXT("9. SlotExpiry = 10 + 3 = 13"), State.SlotExpiry[0], 13.0f);

		// Step entirely inside the active window: [10, 11] ⊂ [10, 13] -> full 1.0s counted, exactly 2 ticks.
		FPSRStatus::Advance(Bits, State, *Catalog, 11.0f, 1.0f, 1.0f, DotDamage, Expired, Fired);
		TestEqual(TEXT("9. 1.0s active at 10 DPS -> exactly 10 damage (2 ticks of 5, not 3)"), DotDamage, 10.0f);
		TestEqual(TEXT("9. nothing expired yet"), Expired.Num(), 0);

		// Step that runs PAST the expiry: [11, 15], but the status only lives until 13 -> clamp to [11, 13] = 2.0s,
		// NOT the full 4.0s (which would wrongly pay out 40 instead of 20 — the exact bug §7-4 warns about).
		FPSRStatus::Advance(Bits, State, *Catalog, 15.0f, 1.0f, 1.0f, DotDamage, Expired, Fired);
		TestEqual(TEXT("9. clamped to the active sliver [11,13]=2.0s -> 20 damage, NOT the unclamped 40"), DotDamage, 20.0f);
		TestEqual(TEXT("9. status now reports expired"), Expired.Num(), 1);
		if (Expired.Num() == 1)
		{
			TestEqual(TEXT("9. expired slot == 0"), (int32)Expired[0], 0);
		}
		TestEqual(TEXT("9. bit 0 cleared"), (int32)Bits, 0);

		// A further step after the bit is gone must pay out nothing at all.
		FPSRStatus::Advance(Bits, State, *Catalog, 20.0f, 1.0f, 1.0f, DotDamage, Expired, Fired);
		TestEqual(TEXT("9. no active DoT source left -> 0 damage"), DotDamage, 0.0f);
	}

	// --- 10. 카탈로그 IsDataValid 음성 검사 ----------------------------------------------------------------------------
#if WITH_EDITOR
	{
		// (a) Valid catalog -> not Invalid, no errors.
		{
			UFPSRStatusEffectDataAsset* Weak0 = MakeStatus(0, EFPSRStatusKind::Weak, 5.0f);
			UFPSRStatusEffectDataAsset* Weak1 = MakeStatus(1, EFPSRStatusKind::Weak, 5.0f);
			UFPSRStatusEffectDataAsset* Strong2 = MakeStatus(2, EFPSRStatusKind::Strong, 5.0f);
			Strong2->RequiredWeakSlots = { 0, 1 };
			UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Weak0, Weak1, Strong2 });

			FDataValidationContext Context;
			const EDataValidationResult Result = Catalog->IsDataValid(Context);
			TestTrue(TEXT("10a. a well-formed catalog is not Invalid"), Result != EDataValidationResult::Invalid);
			TestEqual(TEXT("10a. a well-formed catalog has zero errors"), (int32)Context.GetNumErrors(), 0);
		}
		// (b) Duplicate SlotIndex -> Invalid.
		{
			UFPSRStatusEffectDataAsset* A = MakeStatus(0, EFPSRStatusKind::Weak, 5.0f);
			UFPSRStatusEffectDataAsset* B = MakeStatus(0, EFPSRStatusKind::Weak, 5.0f); // same SlotIndex
			UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ A, B });

			FDataValidationContext Context;
			TestEqual(TEXT("10b. duplicate SlotIndex -> Invalid"), Catalog->IsDataValid(Context), EDataValidationResult::Invalid);
		}
		// (c) SlotIndex > 7 -> Invalid.
		{
			UFPSRStatusEffectDataAsset* A = MakeStatus(8, EFPSRStatusKind::Weak, 5.0f); // out of range
			UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ A });

			FDataValidationContext Context;
			TestEqual(TEXT("10c. SlotIndex > 7 -> Invalid"), Catalog->IsDataValid(Context), EDataValidationResult::Invalid);
		}
		// (d) Strong with RequiredWeakSlots.Num() != 2 -> Invalid.
		{
			UFPSRStatusEffectDataAsset* Weak0 = MakeStatus(0, EFPSRStatusKind::Weak, 5.0f);
			UFPSRStatusEffectDataAsset* Strong1 = MakeStatus(1, EFPSRStatusKind::Strong, 5.0f);
			Strong1->RequiredWeakSlots = { 0 }; // only 1, needs exactly 2
			UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Weak0, Strong1 });

			FDataValidationContext Context;
			TestEqual(TEXT("10d. Strong with 1 material (not 2) -> Invalid"), Catalog->IsDataValid(Context), EDataValidationResult::Invalid);
		}
		// (e) Strong's material pair resolves to another Strong (not Weak) -> Invalid.
		{
			UFPSRStatusEffectDataAsset* Weak0 = MakeStatus(0, EFPSRStatusKind::Weak, 5.0f);
			UFPSRStatusEffectDataAsset* StrongA = MakeStatus(1, EFPSRStatusKind::Strong, 5.0f);
			UFPSRStatusEffectDataAsset* StrongB = MakeStatus(2, EFPSRStatusKind::Strong, 5.0f);
			StrongB->RequiredWeakSlots = { 0, 1 }; // slot 1 is Strong, not Weak
			UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Weak0, StrongA, StrongB });

			FDataValidationContext Context;
			TestEqual(TEXT("10e. a material slot that isn't Kind=Weak -> Invalid"), Catalog->IsDataValid(Context), EDataValidationResult::Invalid);
		}
		// (f) Two different Strong entries share the same material pair -> Invalid.
		{
			UFPSRStatusEffectDataAsset* Weak0 = MakeStatus(0, EFPSRStatusKind::Weak, 5.0f);
			UFPSRStatusEffectDataAsset* Weak1 = MakeStatus(1, EFPSRStatusKind::Weak, 5.0f);
			UFPSRStatusEffectDataAsset* StrongA = MakeStatus(2, EFPSRStatusKind::Strong, 5.0f);
			StrongA->RequiredWeakSlots = { 0, 1 };
			UFPSRStatusEffectDataAsset* StrongB = MakeStatus(3, EFPSRStatusKind::Strong, 5.0f);
			StrongB->RequiredWeakSlots = { 1, 0 }; // same pair, reversed order — must still collide
			UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Weak0, Weak1, StrongA, StrongB });

			FDataValidationContext Context;
			TestEqual(TEXT("10f. two Strong entries sharing one material pair -> Invalid"), Catalog->IsDataValid(Context), EDataValidationResult::Invalid);
		}
		// (g) Weak entry with a non-empty RequiredWeakSlots (authoring leftover) -> Invalid.
		{
			UFPSRStatusEffectDataAsset* Weak0 = MakeStatus(0, EFPSRStatusKind::Weak, 5.0f);
			Weak0->RequiredWeakSlots = { 1, 2 }; // should never be set on a Weak entry
			UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Weak0 });

			FDataValidationContext Context;
			TestEqual(TEXT("10g. Weak entry with RequiredWeakSlots set -> Invalid"), Catalog->IsDataValid(Context), EDataValidationResult::Invalid);
		}
		// (h) Two DamagePerSecond>0 entries -> warning only, NOT Invalid (§7-4 single-DoT-source premise).
		{
			UFPSRStatusEffectDataAsset* Dot0 = MakeStatus(0, EFPSRStatusKind::Weak, 5.0f);
			Dot0->DamagePerSecond = 5.0f;
			UFPSRStatusEffectDataAsset* Dot1 = MakeStatus(1, EFPSRStatusKind::Weak, 5.0f);
			Dot1->DamagePerSecond = 5.0f;
			UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Dot0, Dot1 });

			FDataValidationContext Context;
			const EDataValidationResult Result = Catalog->IsDataValid(Context);
			TestTrue(TEXT("10h. two DoT sources -> not Invalid (warning only)"), Result != EDataValidationResult::Invalid);
			TestTrue(TEXT("10h. two DoT sources -> at least one warning"), Context.GetNumWarnings() > 0);
		}
	}
#endif // WITH_EDITOR

	// --- 14. 강한 상태 Resolve가 §1 의미와 일치 — 재료를 소모하므로 약한 효과는 사라진다 -------------------------------
	{
		UFPSRStatusEffectDataAsset* Weak0 = MakeStatus(0, EFPSRStatusKind::Weak, 5.0f); // 둔화
		Weak0->MoveSpeedMultiplier = 0.5f;
		UFPSRStatusEffectDataAsset* Weak1 = MakeStatus(1, EFPSRStatusKind::Weak, 5.0f); // 방어력감소
		Weak1->IncomingDamageMultiplier = 1.5f;
		UFPSRStatusEffectDataAsset* Strong2 = MakeStatus(2, EFPSRStatusKind::Strong, 5.0f); // 실명
		Strong2->RequiredWeakSlots = { 0, 1 };
		Strong2->bConsumeSources = true;
		Strong2->bDisableAttack = true;
		UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Weak0, Weak1, Strong2 });

		uint8 Bits = 0;
		FFPSRStatusServerState State;
		TArray<uint8, TInlineAllocator<8>> Fired;
		FPSRStatus::Apply(Bits, State, *Catalog, 0, 0.0f, 1.0f, 1.0f, Fired);
		FPSRStatus::Apply(Bits, State, *Catalog, 1, 0.0f, 1.0f, 1.0f, Fired); // combo fires, consumes both materials

		const FFPSRResolvedStatus Resolved = FPSRStatus::Resolve(Bits, *Catalog);
		TestTrue(TEXT("14. Strong's own effect (bDisableAttack) is present"), Resolved.bDisableAttack);
		TestEqual(TEXT("14. consumed Weak0's MoveSpeedMultiplier no longer applies (back to 1.0)"), Resolved.MoveSpeedMultiplier, 1.0f);
		TestEqual(TEXT("14. consumed Weak1's IncomingDamageMultiplier no longer applies (back to 1.0)"), Resolved.IncomingDamageMultiplier, 1.0f);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
