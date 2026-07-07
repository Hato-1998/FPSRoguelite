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

#endif // WITH_AUTOMATION_TESTS
