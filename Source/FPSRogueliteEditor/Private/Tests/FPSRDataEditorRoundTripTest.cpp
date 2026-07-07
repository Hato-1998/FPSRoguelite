// Copyright Epic Games, Inc. All Rights Reserved.
#include "Misc/AutomationTest.h"

#include "DataEditor/FPSRDataEditorHelpers.h"
#include "Card/FPSRCardDataAsset.h"
#include "Card/FPSRCardEffect.h"
#include "Card/FPSRCardPoolDataAsset.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRDataEditorMagnitudeRoundTripTest, "FPSRoguelite.Editor.DataEditor.MagnitudeRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRDataEditorMagnitudeRoundTripTest::RunTest(const FString& Parameters)
{
	// In-memory only (transient NewObject, no save/reload) — the disk round-trip (save -> AssetRegistry rescan ->
	// orphan/anchor recompute) is covered by the manual validate-data commandlet run, not this automation test.
	UFPSRCardDataAsset* Card = NewObject<UFPSRCardDataAsset>();
	TestNotNull(TEXT("Card allocates"), Card);
	if (!Card)
	{
		return false;
	}

	UCardEffect_WeaponStat* Effect = NewObject<UCardEffect_WeaponStat>(Card);
	TestNotNull(TEXT("Effect allocates"), Effect);
	if (!Effect)
	{
		return false;
	}
	Effect->RarityTiers.Add(FFPSRCardRarityTier{ ECardRarity::Common, 0.1f });
	Effect->RarityTiers.Add(FFPSRCardRarityTier{ ECardRarity::Rare, 0.2f });
	Card->Effects.Add(Effect);

	// PostEditChangeProperty (as SetEffectRarityMagnitude itself routes through) derives OfferRarities from the
	// first magnitude-bearing effect's tiers — call it once up front so the card starts in the same state a
	// designer's details-panel edit would have left it in.
	FPropertyChangedEvent InitialEvt(nullptr);
	Card->PostEditChangeProperty(InitialEvt);
	TestEqual(TEXT("OfferRarities derives from the two authored tiers"), Card->OfferRarities.Num(), 2);

	// 1. Edit an EXISTING tier (Rare) — succeeds, in place, no new tier created.
	TestTrue(TEXT("SetEffectRarityMagnitude succeeds for an existing tier (Rare)"),
		FFPSRDataEditorHelpers::SetCardEffectMagnitude(Card, 0, ECardRarity::Rare, 0.5f));
	const FFPSRCardRarityTier* RareTier = Effect->RarityTiers.FindByPredicate(
		[](const FFPSRCardRarityTier& T) { return T.Rarity == ECardRarity::Rare; });
	TestNotNull(TEXT("Rare tier still exists after the edit"), RareTier);
	if (RareTier)
	{
		TestEqual(TEXT("Rare tier magnitude was written"), RareTier->Magnitude, 0.5f);
	}
	TestEqual(TEXT("RarityTiers count unchanged (no tier created)"), Effect->RarityTiers.Num(), 2);

	// 2. A rarity with NO tier (Epic) is rejected — P1 edits existing tiers only, never creates one.
	TestFalse(TEXT("SetEffectRarityMagnitude fails for a rarity with no tier (Epic)"),
		FFPSRDataEditorHelpers::SetCardEffectMagnitude(Card, 0, ECardRarity::Epic, 999.0f));
	TestEqual(TEXT("RarityTiers count still unchanged after the rejected Epic write"), Effect->RarityTiers.Num(), 2);

	// 3. CardId is untouched by any of the above (magnitude edits must not disturb card identity).
	TestTrue(TEXT("CardId untouched by magnitude edits (still None)"), Card->CardId.IsNone());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRDataEditorMembershipRoundTripTest, "FPSRoguelite.Editor.DataEditor.MembershipRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRDataEditorMembershipRoundTripTest::RunTest(const FString& Parameters)
{
	UFPSRCardPoolDataAsset* Pool = NewObject<UFPSRCardPoolDataAsset>();
	TestNotNull(TEXT("Pool allocates"), Pool);

	UFPSRCardDataAsset* Card = NewObject<UFPSRCardDataAsset>();
	TestNotNull(TEXT("Card allocates"), Card);
	if (!Pool || !Card)
	{
		return false;
	}

	UCardEffect_CharacterGE* Effect = NewObject<UCardEffect_CharacterGE>(Card);
	TestNotNull(TEXT("CharacterGE effect allocates"), Effect);
	if (Effect)
	{
		Card->Effects.Add(Effect);
	}

	// Routing: a lone CharacterGE effect is eligible for LevelUpGlobal only (see
	// UCardEffect_CharacterGE::GetEditorEligibleRoutes) — never LevelUpWeapon.
	const TArray<EFPSRCardRoute> Eligible = FFPSRDataEditorHelpers::GetCardEligibleRoutes(Card);
	TestTrue(TEXT("Eligible routes contain LevelUpGlobal"), Eligible.Contains(EFPSRCardRoute::LevelUpGlobal));
	TestFalse(TEXT("Eligible routes do NOT contain LevelUpWeapon"), Eligible.Contains(EFPSRCardRoute::LevelUpWeapon));

	FText Reason;
	TestTrue(TEXT("CheckCardRoute(LevelUpGlobal) is Allowed"),
		FFPSRDataEditorHelpers::CheckCardRoute(Card, EFPSRCardRoute::LevelUpGlobal, Reason) == EFPSRWiringVerdict::Allowed);
	TestTrue(TEXT("CheckCardRoute(LevelUpWeapon) is Blocked"),
		FFPSRDataEditorHelpers::CheckCardRoute(Card, EFPSRCardRoute::LevelUpWeapon, Reason) == EFPSRWiringVerdict::Blocked);

	// Membership add/remove round-trip on Pool->Cards (bUnlockArray = false).
	TestTrue(TEXT("AddCardToPool succeeds"), FFPSRDataEditorHelpers::AddCardToPool(Pool, Card, /*bUnlockArray=*/false));
	TestTrue(TEXT("Pool->Cards contains the card after add"), Pool->Cards.Contains(Card));
	TestEqual(TEXT("Pool->Cards has exactly one entry"), Pool->Cards.Num(), 1);

	// Duplicate add is a no-op (AddUnique semantics) and reports false so the caller can distinguish it from a
	// real change.
	TestFalse(TEXT("Re-adding the same card is a no-op (returns false)"), FFPSRDataEditorHelpers::AddCardToPool(Pool, Card, /*bUnlockArray=*/false));
	TestEqual(TEXT("Pool->Cards still has exactly one entry after the duplicate add attempt"), Pool->Cards.Num(), 1);

	TestTrue(TEXT("RemoveCardFromPool succeeds"), FFPSRDataEditorHelpers::RemoveCardFromPool(Pool, Card, /*bUnlockArray=*/false));
	TestEqual(TEXT("Pool->Cards is empty after remove"), Pool->Cards.Num(), 0);

	// SetCardGroup round-trip (guided-add sets Group=Weapon for the LevelUpWeapon route so the draw targets the
	// source weapon — FPSRCardSubsystem.cpp:603). No-op when already the target group.
	Card->Group = ECardGroup::Character;
	TestTrue(TEXT("SetCardGroup changes Character -> Weapon"), FFPSRDataEditorHelpers::SetCardGroup(Card, ECardGroup::Weapon));
	TestTrue(TEXT("Group is now Weapon"), Card->Group == ECardGroup::Weapon);
	TestFalse(TEXT("SetCardGroup is a no-op when already Weapon"), FFPSRDataEditorHelpers::SetCardGroup(Card, ECardGroup::Weapon));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRDataEditorTierAndBulkTest, "FPSRoguelite.Editor.DataEditor.TierAndBulk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRDataEditorTierAndBulkTest::RunTest(const FString& Parameters)
{
	// ---------------------------------------------------------------------------------------------------------
	// 1. Unit seam: GetEditorMagnitudeUnit() classifies each concrete effect correctly.
	// ---------------------------------------------------------------------------------------------------------
	{
		UCardEffect_CharacterGE* GEPercent = NewObject<UCardEffect_CharacterGE>(GetTransientPackage());
		GEPercent->bShowAsPercent = true;
		TestTrue(TEXT("CharacterGE bShowAsPercent=true -> Percent"),
			GEPercent->GetEditorMagnitudeUnit() == EFPSREditorMagnitudeUnit::Percent);

		UCardEffect_CharacterGE* GEFlat = NewObject<UCardEffect_CharacterGE>(GetTransientPackage());
		GEFlat->bShowAsPercent = false;
		TestTrue(TEXT("CharacterGE bShowAsPercent=false -> Flat"),
			GEFlat->GetEditorMagnitudeUnit() == EFPSREditorMagnitudeUnit::Flat);

		UCardEffect_WeaponStat* StatPercent = NewObject<UCardEffect_WeaponStat>(GetTransientPackage());
		StatPercent->Op = EFPSRWeaponModOp::PercentMultiply;
		TestTrue(TEXT("WeaponStat Op=PercentMultiply -> Percent"),
			StatPercent->GetEditorMagnitudeUnit() == EFPSREditorMagnitudeUnit::Percent);

		UCardEffect_WeaponStat* StatFlat = NewObject<UCardEffect_WeaponStat>(GetTransientPackage());
		StatFlat->Op = EFPSRWeaponModOp::Additive;
		TestTrue(TEXT("WeaponStat Op=Additive -> Flat"),
			StatFlat->GetEditorMagnitudeUnit() == EFPSREditorMagnitudeUnit::Flat);

		UCardEffect_GrantWeapon* Grant = NewObject<UCardEffect_GrantWeapon>(GetTransientPackage());
		TestTrue(TEXT("GrantWeapon -> None (no override)"),
			Grant->GetEditorMagnitudeUnit() == EFPSREditorMagnitudeUnit::None);
	}

	// ---------------------------------------------------------------------------------------------------------
	// 2. Tier creation on a SINGLE magnitude effect: seeds from the nearest lower rarity.
	// ---------------------------------------------------------------------------------------------------------
	UFPSRCardDataAsset* Card = NewObject<UFPSRCardDataAsset>();
	TestNotNull(TEXT("Card allocates"), Card);
	if (!Card)
	{
		return false;
	}
	Card->CardId = FName("TestCard_TierAndBulk");

	UCardEffect_WeaponStat* FlatEffect = NewObject<UCardEffect_WeaponStat>(Card);
	TestNotNull(TEXT("FlatEffect allocates"), FlatEffect);
	if (!FlatEffect)
	{
		return false;
	}
	FlatEffect->Op = EFPSRWeaponModOp::Additive; // Flat, so bulk Add operand is NOT divided by 100
	FlatEffect->RarityTiers.Add(FFPSRCardRarityTier{ ECardRarity::Common, 10.0f });
	FlatEffect->RarityTiers.Add(FFPSRCardRarityTier{ ECardRarity::Rare, 20.0f });
	Card->Effects.Add(FlatEffect);

	FPropertyChangedEvent InitialEvt(nullptr);
	Card->PostEditChangeProperty(InitialEvt);
	TestEqual(TEXT("OfferRarities derives from the two authored tiers (Common, Rare)"), Card->OfferRarities.Num(), 2);

	TestTrue(TEXT("CreateEffectRarityTier(Epic) returns true"), Card->CreateEffectRarityTier(ECardRarity::Epic));
	{
		const FFPSRCardRarityTier* EpicTier = FlatEffect->RarityTiers.FindByPredicate(
			[](const FFPSRCardRarityTier& T) { return T.Rarity == ECardRarity::Epic; });
		TestNotNull(TEXT("Epic tier exists on FlatEffect after creation"), EpicTier);
		if (EpicTier)
		{
			// Nearest LOWER rarity to Epic among {Common, Rare} is Rare (20.0f) — seed should copy that, not Common.
			TestEqual(TEXT("Epic tier seeded from nearest lower tier (Rare=20)"), EpicTier->Magnitude, 20.0f);
		}
	}
	TestEqual(TEXT("OfferRarities now has 3 entries (Common, Rare, Epic)"), Card->OfferRarities.Num(), 3);

	// ---------------------------------------------------------------------------------------------------------
	// 3. Tier creation across a MULTI-effect card: coverage is preserved on every magnitude effect.
	// ---------------------------------------------------------------------------------------------------------
	UFPSRCardDataAsset* MultiCard = NewObject<UFPSRCardDataAsset>();
	TestNotNull(TEXT("MultiCard allocates"), MultiCard);
	if (!MultiCard)
	{
		return false;
	}
	MultiCard->CardId = FName("TestCard_Multi");
	MultiCard->CardFamily = FGameplayTag::RequestGameplayTag(FName("Card.Family.MagSize")); // reuse a real registered tag (native table lookup) — an ad-hoc "Test.*" tag would resolve invalid and fail the IsValid() check below

	UCardEffect_WeaponStat* MultiA = NewObject<UCardEffect_WeaponStat>(MultiCard);
	UCardEffect_WeaponStat* MultiB = NewObject<UCardEffect_WeaponStat>(MultiCard);
	TestNotNull(TEXT("MultiA allocates"), MultiA);
	TestNotNull(TEXT("MultiB allocates"), MultiB);
	if (!MultiA || !MultiB)
	{
		return false;
	}
	MultiA->Op = EFPSRWeaponModOp::Additive;
	MultiB->Op = EFPSRWeaponModOp::Additive;
	MultiA->RarityTiers.Add(FFPSRCardRarityTier{ ECardRarity::Common, 5.0f });
	MultiA->RarityTiers.Add(FFPSRCardRarityTier{ ECardRarity::Rare, 8.0f });
	MultiB->RarityTiers.Add(FFPSRCardRarityTier{ ECardRarity::Common, 1.0f });
	MultiB->RarityTiers.Add(FFPSRCardRarityTier{ ECardRarity::Rare, 2.0f });
	MultiCard->Effects.Add(MultiA);
	MultiCard->Effects.Add(MultiB);
	MultiCard->PostEditChangeProperty(InitialEvt);
	TestEqual(TEXT("MultiCard OfferRarities starts at (Common, Rare)"), MultiCard->OfferRarities.Num(), 2);

	TestTrue(TEXT("CreateEffectRarityTier(Epic) on MultiCard returns true"), MultiCard->CreateEffectRarityTier(ECardRarity::Epic));
	TestTrue(TEXT("MultiA has an Epic tier"), MultiA->RarityTiers.ContainsByPredicate(
		[](const FFPSRCardRarityTier& T) { return T.Rarity == ECardRarity::Epic; }));
	TestTrue(TEXT("MultiB has an Epic tier"), MultiB->RarityTiers.ContainsByPredicate(
		[](const FFPSRCardRarityTier& T) { return T.Rarity == ECardRarity::Epic; }));
	TestEqual(TEXT("MultiCard OfferRarities now has 3 entries"), MultiCard->OfferRarities.Num(), 3);

	// ---------------------------------------------------------------------------------------------------------
	// 4. Tier deletion: removes from every magnitude effect; refuses to delete the card's LAST rarity.
	// ---------------------------------------------------------------------------------------------------------
	TestTrue(TEXT("DeleteEffectRarityTier(Rare) on MultiCard returns true"), MultiCard->DeleteEffectRarityTier(ECardRarity::Rare));
	TestFalse(TEXT("MultiA no longer has a Rare tier"), MultiA->RarityTiers.ContainsByPredicate(
		[](const FFPSRCardRarityTier& T) { return T.Rarity == ECardRarity::Rare; }));
	TestFalse(TEXT("MultiB no longer has a Rare tier"), MultiB->RarityTiers.ContainsByPredicate(
		[](const FFPSRCardRarityTier& T) { return T.Rarity == ECardRarity::Rare; }));
	TestFalse(TEXT("MultiCard OfferRarities no longer contains Rare"), MultiCard->OfferRarities.Contains(ECardRarity::Rare));
	TestEqual(TEXT("MultiCard OfferRarities now has 2 entries (Common, Epic)"), MultiCard->OfferRarities.Num(), 2);

	// Delete down to the last rarity, then verify the final delete is refused.
	TestTrue(TEXT("DeleteEffectRarityTier(Common) on MultiCard returns true"), MultiCard->DeleteEffectRarityTier(ECardRarity::Common));
	TestEqual(TEXT("MultiCard OfferRarities down to 1 entry (Epic)"), MultiCard->OfferRarities.Num(), 1);
	TestFalse(TEXT("DeleteEffectRarityTier refuses to remove the card's ONLY rarity"), MultiCard->DeleteEffectRarityTier(ECardRarity::Epic));
	TestEqual(TEXT("MultiCard OfferRarities unchanged after the refused delete"), MultiCard->OfferRarities.Num(), 1);

	// ---------------------------------------------------------------------------------------------------------
	// 5. Bulk multiply (×N): unit-agnostic, applies to every valid cell.
	// ---------------------------------------------------------------------------------------------------------
	{
		TArray<FFPSRMagnitudeCellRef> Cells;
		FFPSRMagnitudeCellRef Cell;
		Cell.Card = Card;
		Cell.EffectIndex = 0; // FlatEffect, Common tier = 10.0f (unchanged by prior steps)
		Cell.Rarity = ECardRarity::Common;
		Cells.Add(Cell);

		FText Status;
		const int32 NumChanged = FFPSRDataEditorHelpers::BulkApplyMagnitude(Cells, EFPSRBulkMagnitudeOp::Multiply, 2.0f, Status);
		TestEqual(TEXT("Bulk multiply changes exactly 1 cell"), NumChanged, 1);
		const FFPSRCardRarityTier* CommonTier = FlatEffect->RarityTiers.FindByPredicate(
			[](const FFPSRCardRarityTier& T) { return T.Rarity == ECardRarity::Common; });
		TestNotNull(TEXT("Common tier still exists after bulk multiply"), CommonTier);
		if (CommonTier)
		{
			TestEqual(TEXT("Bulk multiply doubled the Common magnitude (10 -> 20)"), CommonTier->Magnitude, 20.0f);
		}
	}

	// ---------------------------------------------------------------------------------------------------------
	// 6. Bulk add (+N), homogeneous FLAT selection: raw += Operand (no /100).
	// ---------------------------------------------------------------------------------------------------------
	{
		UFPSRCardDataAsset* FlatBulkCard = NewObject<UFPSRCardDataAsset>();
		FlatBulkCard->CardId = FName("TestCard_FlatBulk");
		FlatBulkCard->CardFamily = FGameplayTag::RequestGameplayTag(FName("Test.Card.FlatBulk"), false);
		UCardEffect_WeaponStat* FlatA = NewObject<UCardEffect_WeaponStat>(FlatBulkCard);
		UCardEffect_WeaponStat* FlatB = NewObject<UCardEffect_WeaponStat>(FlatBulkCard);
		FlatA->Op = EFPSRWeaponModOp::Additive;
		FlatB->Op = EFPSRWeaponModOp::Additive;
		FlatA->RarityTiers.Add(FFPSRCardRarityTier{ ECardRarity::Common, 10.0f });
		FlatB->RarityTiers.Add(FFPSRCardRarityTier{ ECardRarity::Common, 3.0f });
		FlatBulkCard->Effects.Add(FlatA);
		FlatBulkCard->Effects.Add(FlatB);
		FlatBulkCard->PostEditChangeProperty(InitialEvt);

		TArray<FFPSRMagnitudeCellRef> Cells;
		FFPSRMagnitudeCellRef CellA; CellA.Card = FlatBulkCard; CellA.EffectIndex = 0; CellA.Rarity = ECardRarity::Common;
		FFPSRMagnitudeCellRef CellB; CellB.Card = FlatBulkCard; CellB.EffectIndex = 1; CellB.Rarity = ECardRarity::Common;
		Cells.Add(CellA);
		Cells.Add(CellB);

		FText Status;
		const int32 NumChanged = FFPSRDataEditorHelpers::BulkApplyMagnitude(Cells, EFPSRBulkMagnitudeOp::Add, 5.0f, Status);
		TestEqual(TEXT("Bulk add (Flat) changes both cells"), NumChanged, 2);
		const FFPSRCardRarityTier* TierA = FlatA->RarityTiers.FindByPredicate([](const FFPSRCardRarityTier& T) { return T.Rarity == ECardRarity::Common; });
		const FFPSRCardRarityTier* TierB = FlatB->RarityTiers.FindByPredicate([](const FFPSRCardRarityTier& T) { return T.Rarity == ECardRarity::Common; });
		TestNotNull(TEXT("FlatA Common tier exists"), TierA);
		TestNotNull(TEXT("FlatB Common tier exists"), TierB);
		if (TierA) { TestEqual(TEXT("FlatA: 10 + 5 = 15 (raw add, no /100)"), TierA->Magnitude, 15.0f); }
		if (TierB) { TestEqual(TEXT("FlatB: 3 + 5 = 8 (raw add, no /100)"), TierB->Magnitude, 8.0f); }
	}

	// ---------------------------------------------------------------------------------------------------------
	// 7. Bulk add (+N), homogeneous PERCENT selection: raw += Operand/100 (display unit is percentage points).
	// ---------------------------------------------------------------------------------------------------------
	{
		UFPSRCardDataAsset* PercentBulkCard = NewObject<UFPSRCardDataAsset>();
		PercentBulkCard->CardId = FName("TestCard_PercentBulk");
		UCardEffect_CharacterGE* PercentEffect = NewObject<UCardEffect_CharacterGE>(PercentBulkCard);
		PercentEffect->bShowAsPercent = true;
		PercentEffect->RarityTiers.Add(FFPSRCardRarityTier{ ECardRarity::Common, 0.05f });
		PercentBulkCard->Effects.Add(PercentEffect);
		PercentBulkCard->PostEditChangeProperty(InitialEvt);

		TArray<FFPSRMagnitudeCellRef> Cells;
		FFPSRMagnitudeCellRef Cell; Cell.Card = PercentBulkCard; Cell.EffectIndex = 0; Cell.Rarity = ECardRarity::Common;
		Cells.Add(Cell);

		FText Status;
		const int32 NumChanged = FFPSRDataEditorHelpers::BulkApplyMagnitude(Cells, EFPSRBulkMagnitudeOp::Add, 5.0f, Status);
		TestEqual(TEXT("Bulk add (Percent) changes the cell"), NumChanged, 1);
		const FFPSRCardRarityTier* Tier = PercentEffect->RarityTiers.FindByPredicate([](const FFPSRCardRarityTier& T) { return T.Rarity == ECardRarity::Common; });
		TestNotNull(TEXT("Percent tier exists after bulk add"), Tier);
		if (Tier)
		{
			// 0.05 (5%) + 5/100 (5 percentage points) = 0.10 (10%).
			TestEqual(TEXT("Percent: 0.05 + (5/100) = 0.10"), Tier->Magnitude, 0.10f);
		}
	}

	// ---------------------------------------------------------------------------------------------------------
	// 8. Bulk add (+N), MIXED unit selection: refused entirely (returns 0, no change to either value).
	// ---------------------------------------------------------------------------------------------------------
	{
		UFPSRCardDataAsset* MixedCard = NewObject<UFPSRCardDataAsset>();
		MixedCard->CardId = FName("TestCard_Mixed");
		MixedCard->CardFamily = FGameplayTag::RequestGameplayTag(FName("Test.Card.Mixed"), false);
		UCardEffect_CharacterGE* MixedPercent = NewObject<UCardEffect_CharacterGE>(MixedCard);
		MixedPercent->bShowAsPercent = true;
		MixedPercent->RarityTiers.Add(FFPSRCardRarityTier{ ECardRarity::Common, 0.05f });
		UCardEffect_WeaponStat* MixedFlat = NewObject<UCardEffect_WeaponStat>(MixedCard);
		MixedFlat->Op = EFPSRWeaponModOp::Additive;
		MixedFlat->RarityTiers.Add(FFPSRCardRarityTier{ ECardRarity::Common, 10.0f });
		MixedCard->Effects.Add(MixedPercent);
		MixedCard->Effects.Add(MixedFlat);
		MixedCard->PostEditChangeProperty(InitialEvt);

		TArray<FFPSRMagnitudeCellRef> Cells;
		FFPSRMagnitudeCellRef CellPercent; CellPercent.Card = MixedCard; CellPercent.EffectIndex = 0; CellPercent.Rarity = ECardRarity::Common;
		FFPSRMagnitudeCellRef CellFlat; CellFlat.Card = MixedCard; CellFlat.EffectIndex = 1; CellFlat.Rarity = ECardRarity::Common;
		Cells.Add(CellPercent);
		Cells.Add(CellFlat);

		FText Status;
		const int32 NumChanged = FFPSRDataEditorHelpers::BulkApplyMagnitude(Cells, EFPSRBulkMagnitudeOp::Add, 5.0f, Status);
		TestEqual(TEXT("Bulk add refuses a mixed-unit selection (returns 0)"), NumChanged, 0);
		TestFalse(TEXT("Mixed-unit refusal produces a non-empty status message"), Status.IsEmpty());

		const FFPSRCardRarityTier* PercentTier = MixedPercent->RarityTiers.FindByPredicate([](const FFPSRCardRarityTier& T) { return T.Rarity == ECardRarity::Common; });
		const FFPSRCardRarityTier* FlatTier = MixedFlat->RarityTiers.FindByPredicate([](const FFPSRCardRarityTier& T) { return T.Rarity == ECardRarity::Common; });
		if (PercentTier) { TestEqual(TEXT("Percent value unchanged by the refused mixed bulk add"), PercentTier->Magnitude, 0.05f); }
		if (FlatTier) { TestEqual(TEXT("Flat value unchanged by the refused mixed bulk add"), FlatTier->Magnitude, 10.0f); }
	}

	// ---------------------------------------------------------------------------------------------------------
	// 9. Identity/no-corruption checks: CardId/CardFamily untouched, None-unit effect never grows RarityTiers.
	// ---------------------------------------------------------------------------------------------------------
	TestTrue(TEXT("Card CardId untouched by tier/bulk edits"), Card->CardId == FName("TestCard_TierAndBulk"));
	TestTrue(TEXT("MultiCard CardId untouched"), MultiCard->CardId == FName("TestCard_Multi"));
	TestTrue(TEXT("MultiCard CardFamily still valid/unchanged"), MultiCard->CardFamily.IsValid());
	{
		UCardEffect_GrantWeapon* NoneUnitEffect = NewObject<UCardEffect_GrantWeapon>(Card);
		TestEqual(TEXT("A None-unit effect (GrantWeapon) never has RarityTiers populated"), NoneUnitEffect->RarityTiers.Num(), 0);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
