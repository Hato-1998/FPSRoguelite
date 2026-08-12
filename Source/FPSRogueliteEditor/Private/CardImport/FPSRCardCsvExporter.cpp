// Copyright Epic Games, Inc. All Rights Reserved.

#include "CardImport/FPSRCardCsvExporter.h"

#include "Card/FPSRCardDataAsset.h"
#include "Card/FPSRCardEffect.h"
#include "Card/FPSRCardPoolDataAsset.h"
#include "Card/FPSRCardTypes.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "AbilitySystem/Abilities/FPSRPassiveAbility.h"
#include "DataEditor/FPSRDataEditorHelpers.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "UObject/SoftObjectPath.h"
#include "Misc/FileHelper.h"
#include "Misc/DefaultValueHelper.h"
#include "Internationalization/Text.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPSRCardCsvExport, Log, All);

namespace
{
	// ---------------------------------------------------------------------------------------------------------
	// CSV text helpers (minimal quoting per LOC0 convention — Docs/SSOT/Localization.md L-4/§2-3-10).
	// ---------------------------------------------------------------------------------------------------------

	FString EscapeCsvField(const FString& InField)
	{
		const bool bNeedsQuote = InField.Contains(TEXT(",")) || InField.Contains(TEXT("\"")) || InField.Contains(TEXT("\n")) || InField.Contains(TEXT("\r"));
		if (!bNeedsQuote)
		{
			return InField;
		}
		FString Escaped = InField;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}

	// Multiline authored text -> literal "\n" (two characters), matching L-4's "멀티라인 = 셀 안 \n 리터럴" rule —
	// a real newline inside a cell is never written, so the exporter's own output stays a valid single-line-per-row CSV.
	FString FlattenMultilineForCsv(const FString& InText)
	{
		FString Out = InText;
		Out.ReplaceInline(TEXT("\r\n"), TEXT("\\n"));
		Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Out.ReplaceInline(TEXT("\r"), TEXT("\\n"));
		return Out;
	}

	FString JoinCsvRow(const TArray<FString>& Cells)
	{
		TArray<FString> Escaped;
		Escaped.Reserve(Cells.Num());
		for (const FString& Cell : Cells)
		{
			Escaped.Add(EscapeCsvField(FlattenMultilineForCsv(Cell)));
		}
		return FString::Join(Escaped, TEXT(","));
	}

	bool ContainsHangul(const FString& InText)
	{
		for (const TCHAR C : InText)
		{
			// Hangul Syllables (AC00-D7A3) + Hangul Jamo (1100-11FF) — enough to distinguish authored Korean
			// source text from English placeholder text for the [KO-TODO] migration marker (§2-3-10).
			if ((C >= 0xAC00 && C <= 0xD7A3) || (C >= 0x1100 && C <= 0x11FF))
			{
				return true;
			}
		}
		return false;
	}

	// Splits an authored FText's SOURCE string (culture-independent — FTextInspector::GetSourceString, not
	// Text.ToString() which would return the currently active culture's display value) into the ko/en/ja triple
	// the CSV pipeline uses. Migration rule (§2-3-10, commit B-3 instructions): Korean source -> ko verbatim;
	// English source -> en verbatim + ko gets a "[KO-TODO] <en original>" marker so the gap is visible in-sheet.
	// ja is never populated here (the exporter has no Japanese source to migrate) — it is a translation gap the
	// sheet fills in later, same as any other missing en/ja cell (LOC0 test: blanks are a warning, not a failure).
	void SplitMigrationText(const FText& InText, FString OutColumns[3])
	{
		OutColumns[0].Reset();
		OutColumns[1].Reset();
		OutColumns[2].Reset();

		const FString* SourcePtr = FTextInspector::GetSourceString(InText);
		const FString Source = SourcePtr ? *SourcePtr : InText.ToString();
		if (Source.IsEmpty())
		{
			return;
		}

		if (ContainsHangul(Source))
		{
			OutColumns[0] = Source; // ko
		}
		else
		{
			OutColumns[1] = Source; // en
			OutColumns[0] = FString::Printf(TEXT("[KO-TODO] %s"), *Source);
		}
	}

	// ---------------------------------------------------------------------------------------------------------
	// AttrId derivation (§5: "명명: 소문자 도트 계층", auto-SUGGESTED — not a strict contract, the two spec
	// examples "weapon.frag.dot" / "unlock.smg" are the shape this mirrors). WeaponStat is fully deterministic
	// (from the closed EFPSRWeaponStat enum); the other 4 effect types strip a common authoring prefix off the
	// referenced class/asset name and lowercase the remainder.
	// ---------------------------------------------------------------------------------------------------------

	FString StripKnownPrefix(const FString& InName)
	{
		static const TCHAR* Prefixes[] = {
			TEXT("GE_Card_"), TEXT("BP_Card_"), TEXT("DA_Fragment_"), TEXT("DA_Weapon_"),
			TEXT("UFPSRPassiveAbility_"), TEXT("FPSRPassiveAbility_"), TEXT("GA_"), TEXT("BP_")
		};
		FString Name = InName;
		Name.RemoveFromEnd(TEXT("_C")); // Blueprint generated class suffix
		for (const TCHAR* Prefix : Prefixes)
		{
			if (Name.StartsWith(Prefix))
			{
				Name.RightChopInline(FCString::Strlen(Prefix));
				break;
			}
		}
		return Name;
	}

	FString SanitizeAttrIdSegment(const FString& InName)
	{
		FString Lower = StripKnownPrefix(InName).ToLower();
		FString Result;
		Result.Reserve(Lower.Len());
		for (const TCHAR C : Lower)
		{
			if (FChar::IsAlnum(C))
			{
				Result.AppendChar(C);
			}
		}
		return Result.IsEmpty() ? TEXT("unnamed") : Result;
	}

	// One resolved effect descriptor — everything the CSV needs, extracted from a live UFPSRCardEffect subclass.
	struct FResolvedEffect
	{
		FName EffectType;      // CharGE | CharPassive | WeaponStat | WeaponBehavior | GrantWeapon
		FString PayloadKey;    // the raw payload identity (class/asset path, or the WeaponStat enum name) — catalog dedup key
		FString AttrIdSuggestion;
		FString DefaultOp;             // WeaponStat only
		FString DefaultThisWeaponOnly; // WeaponStat only
		FString ShowAsPercent;         // CharGE only
		FString TiersCsv;              // "C:15;R:30;..." ascending rarity, empty if no tiers
		bool bValid = false;
	};

	FString FormatBool(bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	FString BuildTiersString(const TArray<FFPSRCardRarityTier>& Tiers)
	{
		TArray<FFPSRCardRarityTier> Sorted = Tiers;
		Sorted.Sort([](const FFPSRCardRarityTier& A, const FFPSRCardRarityTier& B)
		{
			return static_cast<uint8>(A.Rarity) < static_cast<uint8>(B.Rarity);
		});

		TArray<FString> Entries;
		for (const FFPSRCardRarityTier& Tier : Sorted)
		{
			TCHAR Initial = TEXT('C');
			switch (Tier.Rarity)
			{
			case ECardRarity::Common:    Initial = TEXT('C'); break;
			case ECardRarity::Rare:      Initial = TEXT('R'); break;
			case ECardRarity::Epic:      Initial = TEXT('E'); break;
			case ECardRarity::Legendary: Initial = TEXT('L'); break;
			}
			Entries.Add(FString::Printf(TEXT("%c:%s"), Initial, *FString::SanitizeFloat(Tier.Magnitude)));
		}
		return FString::Join(Entries, TEXT(";"));
	}

	FResolvedEffect ResolveEffect(const UFPSRCardEffect* Effect)
	{
		FResolvedEffect Out;
		if (!Effect)
		{
			return Out;
		}

		if (const UCardEffect_CharacterGE* GE = Cast<UCardEffect_CharacterGE>(Effect))
		{
			Out.EffectType = TEXT("CharGE");
			Out.PayloadKey = GE->Effect ? FSoftClassPath(GE->Effect).ToString() : FString();
			Out.AttrIdSuggestion = FString::Printf(TEXT("char.%s"), *SanitizeAttrIdSegment(GE->Effect ? GE->Effect->GetName() : TEXT("unknown")));
			Out.ShowAsPercent = FormatBool(GE->bShowAsPercent);
			Out.bValid = (GE->Effect != nullptr);
		}
		else if (const UCardEffect_CharacterPassive* Passive = Cast<UCardEffect_CharacterPassive>(Effect))
		{
			Out.EffectType = TEXT("CharPassive");
			Out.PayloadKey = Passive->PassiveAbility ? FSoftClassPath(Passive->PassiveAbility).ToString() : FString();
			Out.AttrIdSuggestion = FString::Printf(TEXT("char.%s"), *SanitizeAttrIdSegment(Passive->PassiveAbility ? Passive->PassiveAbility->GetName() : TEXT("unknown")));
			Out.bValid = (Passive->PassiveAbility != nullptr);
		}
		else if (const UCardEffect_WeaponStat* Stat = Cast<UCardEffect_WeaponStat>(Effect))
		{
			Out.EffectType = TEXT("WeaponStat");
			const FString StatName = StaticEnum<EFPSRWeaponStat>()->GetNameStringByValue(static_cast<int64>(Stat->Stat));
			Out.PayloadKey = StatName;
			Out.AttrIdSuggestion = FString::Printf(TEXT("weapon.%s"), *StatName.ToLower());
			Out.DefaultOp = StaticEnum<EFPSRWeaponModOp>()->GetNameStringByValue(static_cast<int64>(Stat->Op));
			Out.DefaultThisWeaponOnly = FormatBool(Stat->bThisWeaponOnly);
			Out.bValid = true;
		}
		else if (const UCardEffect_WeaponBehavior* Behavior = Cast<UCardEffect_WeaponBehavior>(Effect))
		{
			Out.EffectType = TEXT("WeaponBehavior");
			Out.PayloadKey = Behavior->Fragment ? Behavior->Fragment->GetPathName() : FString();
			Out.AttrIdSuggestion = FString::Printf(TEXT("weapon.frag.%s"), *SanitizeAttrIdSegment(Behavior->Fragment ? Behavior->Fragment->GetName() : TEXT("unknown")));
			Out.bValid = (Behavior->Fragment != nullptr);
		}
		else if (const UCardEffect_GrantWeapon* Grant = Cast<UCardEffect_GrantWeapon>(Effect))
		{
			Out.EffectType = TEXT("GrantWeapon");
			Out.PayloadKey = Grant->WeaponToGrant ? Grant->WeaponToGrant->GetPathName() : FString();
			Out.AttrIdSuggestion = FString::Printf(TEXT("unlock.%s"), *SanitizeAttrIdSegment(Grant->WeaponToGrant ? Grant->WeaponToGrant->GetName() : TEXT("unknown")));
			Out.bValid = (Grant->WeaponToGrant != nullptr);
		}
		else
		{
			return Out; // unknown effect subclass — bValid stays false, caller reports an error
		}

		Out.TiersCsv = BuildTiersString(Effect->RarityTiers);
		return Out;
	}

	// ---------------------------------------------------------------------------------------------------------
	// Catalog registry: dedups (EffectType, PayloadKey) into ONE CardCatalog.csv row, assigning a stable AttrId
	// (collision-disambiguated) the first time it is seen, and reusing it on every later reference. Per-reference
	// deviation from the row's own recorded Default* becomes that card's E*_Override cell (built by the caller).
	// ---------------------------------------------------------------------------------------------------------

	struct FCatalogEntry
	{
		FName AttrId;
		FName EffectType;
		FString Payload;
		FString DefaultOp;
		FString DefaultThisWeaponOnly;
		FString ShowAsPercent;
	};

	class FCatalogRegistry
	{
	public:
		// Returns the AttrId for this effect, registering a new catalog row on first sight. OutOverride is set to
		// the "k=v;k=v" string this specific reference deviates from the row's recorded Default* (empty = no deviation).
		FName Resolve(const FResolvedEffect& Effect, FString& OutOverride)
		{
			OutOverride.Reset();
			const FString Key = Effect.EffectType.ToString() + TEXT("|") + Effect.PayloadKey;
			if (const FName* Existing = KeyToAttrId.Find(Key))
			{
				const FCatalogEntry& Entry = Entries[*Existing];
				BuildOverride(Effect, Entry, OutOverride);
				return *Existing;
			}

			// First sighting — mint an AttrId, disambiguating a collision with a numeric suffix.
			FName CandidateAttrId(*Effect.AttrIdSuggestion);
			int32 Suffix = 2;
			while (Entries.Contains(CandidateAttrId))
			{
				CandidateAttrId = FName(*FString::Printf(TEXT("%s_%d"), *Effect.AttrIdSuggestion, Suffix++));
			}

			FCatalogEntry NewEntry;
			NewEntry.AttrId = CandidateAttrId;
			NewEntry.EffectType = Effect.EffectType;
			NewEntry.Payload = Effect.PayloadKey;
			NewEntry.DefaultOp = Effect.DefaultOp;
			NewEntry.DefaultThisWeaponOnly = Effect.DefaultThisWeaponOnly;
			NewEntry.ShowAsPercent = Effect.ShowAsPercent;
			Entries.Add(CandidateAttrId, NewEntry);
			KeyToAttrId.Add(Key, CandidateAttrId);
			return CandidateAttrId;
		}

		const TMap<FName, FCatalogEntry>& GetEntries() const { return Entries; }

	private:
		void BuildOverride(const FResolvedEffect& Effect, const FCatalogEntry& Entry, FString& OutOverride) const
		{
			TArray<FString> Parts;
			if (Effect.EffectType == TEXT("WeaponStat"))
			{
				if (!Effect.DefaultOp.IsEmpty() && Effect.DefaultOp != Entry.DefaultOp)
				{
					Parts.Add(FString::Printf(TEXT("Op=%s"), *Effect.DefaultOp));
				}
				if (!Effect.DefaultThisWeaponOnly.IsEmpty() && Effect.DefaultThisWeaponOnly != Entry.DefaultThisWeaponOnly)
				{
					Parts.Add(FString::Printf(TEXT("ThisWeaponOnly=%s"), *Effect.DefaultThisWeaponOnly));
				}
			}
			else if (Effect.EffectType == TEXT("CharGE"))
			{
				if (!Effect.ShowAsPercent.IsEmpty() && Effect.ShowAsPercent != Entry.ShowAsPercent)
				{
					Parts.Add(FString::Printf(TEXT("ShowAsPercent=%s"), *Effect.ShowAsPercent));
				}
			}
			OutOverride = FString::Join(Parts, TEXT(";"));
		}

		TMap<FString, FName> KeyToAttrId; // "EffectType|PayloadKey" -> AttrId
		TMap<FName, FCatalogEntry> Entries;
	};

	// ---------------------------------------------------------------------------------------------------------
	// Route/OwnerWeapon membership: which pool array (or weapon array) each card lives in (§2-3-10 §5).
	// ---------------------------------------------------------------------------------------------------------

	struct FMembership
	{
		EFPSRCardRoute Route = EFPSRCardRoute::LevelUpGlobal;
		FString OwnerWeapon;
		bool bFound = false;
	};

	// Deterministic placeholder OwnerWeapon for an orphan ThisWeapon-scope card with no discoverable owner: the
	// alphabetically-first non-excluded (see UFPSRWeaponDataAsset::bExcludeFromProgression) weapon asset. Only
	// used as a syntactically-valid placeholder so the CSV round-trips through validation; the accompanying
	// warning tells a designer to confirm or correct the target weapon.
	FString FindPlaceholderOwnerWeaponName()
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		FARFilter WeaponFilter;
		WeaponFilter.ClassPaths.Add(UFPSRWeaponDataAsset::StaticClass()->GetClassPathName());
		TArray<FAssetData> WeaponAssets;
		AssetRegistry.GetAssets(WeaponFilter, WeaponAssets);

		FString Best;
		for (const FAssetData& WeaponAssetData : WeaponAssets)
		{
			const UFPSRWeaponDataAsset* Weapon = Cast<UFPSRWeaponDataAsset>(WeaponAssetData.GetAsset());
			if (!Weapon || Weapon->bExcludeFromProgression)
			{
				continue;
			}
			if (Best.IsEmpty() || Weapon->GetName() < Best)
			{
				Best = Weapon->GetName();
			}
		}
		return Best;
	}

	void BuildMembershipMap(TMap<const UFPSRCardDataAsset*, FMembership>& OutMap, TArray<FString>& OutWarnings)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		auto RecordMembership = [&OutMap, &OutWarnings](const UFPSRCardDataAsset* Card, EFPSRCardRoute Route, const FString& OwnerWeapon)
		{
			if (!Card)
			{
				return;
			}
			if (FMembership* Existing = OutMap.Find(Card))
			{
				if (Existing->bFound)
				{
					OutWarnings.Add(FString::Printf(TEXT("Export: card '%s' is wired into more than one pool/weapon array — using the first membership found, later ones ignored."), *Card->GetName()));
					return;
				}
			}
			FMembership M;
			M.Route = Route;
			M.OwnerWeapon = OwnerWeapon;
			M.bFound = true;
			OutMap.Add(Card, M);
		};

		FARFilter PoolFilter;
		PoolFilter.ClassPaths.Add(UFPSRCardPoolDataAsset::StaticClass()->GetClassPathName());
		TArray<FAssetData> PoolAssets;
		AssetRegistry.GetAssets(PoolFilter, PoolAssets);
		for (const FAssetData& PoolAssetData : PoolAssets)
		{
			const UFPSRCardPoolDataAsset* Pool = Cast<UFPSRCardPoolDataAsset>(PoolAssetData.GetAsset());
			if (!Pool)
			{
				continue;
			}
			for (const TObjectPtr<UFPSRCardDataAsset>& Card : Pool->Cards)
			{
				RecordMembership(Card, EFPSRCardRoute::LevelUpGlobal, FString());
			}
			for (const TObjectPtr<UFPSRCardDataAsset>& Card : Pool->WeaponUnlockCards)
			{
				RecordMembership(Card, EFPSRCardRoute::MissionClearNewWeapon, FString());
			}
		}

		FARFilter WeaponFilter;
		WeaponFilter.ClassPaths.Add(UFPSRWeaponDataAsset::StaticClass()->GetClassPathName());
		TArray<FAssetData> WeaponAssets;
		AssetRegistry.GetAssets(WeaponFilter, WeaponAssets);
		for (const FAssetData& WeaponAssetData : WeaponAssets)
		{
			const UFPSRWeaponDataAsset* Weapon = Cast<UFPSRWeaponDataAsset>(WeaponAssetData.GetAsset());
			if (!Weapon)
			{
				continue;
			}
			for (const TObjectPtr<UFPSRCardDataAsset>& Card : Weapon->WeaponCards)
			{
				RecordMembership(Card, EFPSRCardRoute::LevelUpWeapon, Weapon->GetName());
			}
			for (const TObjectPtr<UFPSRCardDataAsset>& Card : Weapon->UnlockableFeatures)
			{
				RecordMembership(Card, EFPSRCardRoute::MissionClearWeaponFeature, Weapon->GetName());
			}
		}
	}
}

bool FPSRCardCsvExport::ExportAll(const FString& OutCardsCsvPath, const FString& OutCatalogCsvPath, TArray<FString>& OutErrors)
{
	OutErrors.Reset();

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRegistry.SearchAllAssets(/*bSynchronousSearch=*/true);

	FARFilter CardFilter;
	CardFilter.ClassPaths.Add(UFPSRCardDataAsset::StaticClass()->GetClassPathName());
	TArray<FAssetData> CardAssets;
	AssetRegistry.GetAssets(CardFilter, CardAssets);

	if (CardAssets.Num() == 0)
	{
		OutErrors.Add(TEXT("Export: found zero UFPSRCardDataAsset assets in the project — nothing to export."));
		return false;
	}

	TMap<const UFPSRCardDataAsset*, FMembership> Membership;
	TArray<FString> Warnings;
	BuildMembershipMap(Membership, Warnings);

	FCatalogRegistry Catalog;

	struct FOutputCardRow
	{
		FString CardId, AssetName, Group, Route, OwnerWeapon, Weight, Family;
		FString DisplayName[3];
		FString Description[3];
		struct FEffectCell { FString AttrId, Override, Tiers; };
		TArray<FEffectCell> Effects;
	};
	TArray<FOutputCardRow> OutputRows;
	OutputRows.Reserve(CardAssets.Num());

	for (const FAssetData& CardAssetData : CardAssets)
	{
		const UFPSRCardDataAsset* Card = Cast<UFPSRCardDataAsset>(CardAssetData.GetAsset());
		if (!Card)
		{
			OutErrors.Add(FString::Printf(TEXT("Export: failed to load card asset '%s'."), *CardAssetData.GetObjectPathString()));
			continue;
		}
		if (Card->CardId.IsNone())
		{
			OutErrors.Add(FString::Printf(TEXT("Export: card '%s' has no CardId — cannot export (CardId is the CSV row key)."), *Card->GetName()));
			continue;
		}

		FOutputCardRow Row;
		Row.CardId = Card->CardId.ToString();
		Row.AssetName = Card->GetName();
		Row.Group = StaticEnum<ECardGroup>()->GetNameStringByValue(static_cast<int64>(Card->Group));
		Row.Weight = FString::SanitizeFloat(Card->Weight);

		const FMembership* Found = Membership.Find(Card);
		if (Found && Found->bFound)
		{
			Row.Route = StaticEnum<EFPSRCardRoute>()->GetNameStringByValue(static_cast<int64>(Found->Route));
			Row.OwnerWeapon = Found->OwnerWeapon;
		}
		else
		{
			// Orphan card (unreachable from any pool/weapon) — still exported (migration must not silently drop
			// content), routed to the FIRST route its own effects are actually eligible for (reusing the Data
			// Editor's routing preflight, FFPSRDataEditorHelpers::GetCardEligibleRoutes — not invented here) so the
			// importer's validation gate (IsDataValid + FPSRCardPoolValidator routing check, §2-3-8) doesn't reject
			// a placeholder wiring outright. A blind "always LevelUpGlobal" default was tried and failed exactly
			// this check for GrantWeapon/ThisWeapon-scope orphans (real-run finding). Flagged either way so a
			// designer reviews/corrects the guess.
			const TArray<EFPSRCardRoute> Eligible = FFPSRDataEditorHelpers::GetCardEligibleRoutes(Card);
			const EFPSRCardRoute FallbackRoute = Eligible.Num() > 0 ? Eligible[0] : EFPSRCardRoute::LevelUpGlobal;
			Row.Route = StaticEnum<EFPSRCardRoute>()->GetNameStringByValue(static_cast<int64>(FallbackRoute));
			if (FallbackRoute == EFPSRCardRoute::LevelUpWeapon || FallbackRoute == EFPSRCardRoute::MissionClearWeaponFeature)
			{
				Row.OwnerWeapon = FindPlaceholderOwnerWeaponName();
			}
			Warnings.Add(FString::Printf(TEXT("Export: card '%s' is not wired into any card pool or weapon — exported with a best-guess Route (%s%s%s) derived from its own effect eligibility; re-wire or delete it."),
				*Card->GetName(), *Row.Route,
				Row.OwnerWeapon.IsEmpty() ? TEXT("") : TEXT(", OwnerWeapon="), *Row.OwnerWeapon));
		}

		SplitMigrationText(Card->DisplayName, Row.DisplayName);
		SplitMigrationText(Card->Description, Row.Description);

		if (Card->Effects.Num() > 3)
		{
			OutErrors.Add(FString::Printf(TEXT("Export: card '%s' has %d effects — the CSV schema supports at most 3 (E1..E3, §2-3-10)."), *Card->GetName(), Card->Effects.Num()));
		}

		FString FirstAttrIdForFamilyDerivation;
		for (int32 EffectIndex = 0; EffectIndex < FMath::Min(Card->Effects.Num(), 3); ++EffectIndex)
		{
			const UFPSRCardEffect* Effect = Card->Effects[EffectIndex];
			const FResolvedEffect Resolved = ResolveEffect(Effect);
			if (!Resolved.bValid)
			{
				OutErrors.Add(FString::Printf(TEXT("Export: card '%s' effect index %d could not be resolved (unrecognized effect subclass or unset payload reference)."), *Card->GetName(), EffectIndex));
				continue;
			}

			FString Override;
			const FName AttrId = Catalog.Resolve(Resolved, Override);
			if (EffectIndex == 0)
			{
				FirstAttrIdForFamilyDerivation = AttrId.ToString();
			}

			FOutputCardRow::FEffectCell Cell;
			Cell.AttrId = AttrId.ToString();
			Cell.Override = Override;
			Cell.Tiers = Resolved.TiersCsv;
			Row.Effects.Add(MoveTemp(Cell));
		}

		if (Card->CardFamily.IsValid())
		{
			const FString TagName = Card->CardFamily.GetTagName().ToString();
			Row.Family = (TagName == FirstAttrIdForFamilyDerivation) ? FString() : TagName;
		}

		OutputRows.Add(MoveTemp(Row));
	}

	if (OutErrors.Num() > 0)
	{
		// Errors accumulated above are all fatal to the migration (unresolved effect / missing CardId) — no file
		// is written, matching the §6 contract ("false + Errors, 파일 미생성").
		return false;
	}

	// --- Write CardCatalog.csv ---
	{
		TArray<FString> Lines;
		Lines.Add(JoinCsvRow({ TEXT("AttrId"), TEXT("EffectType"), TEXT("Payload"), TEXT("DefaultOp"), TEXT("DefaultThisWeaponOnly"), TEXT("ShowAsPercent"), TEXT("Notes") }));

		TArray<FName> SortedAttrIds;
		Catalog.GetEntries().GenerateKeyArray(SortedAttrIds);
		SortedAttrIds.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		for (const FName& AttrId : SortedAttrIds)
		{
			const FCatalogEntry& Entry = Catalog.GetEntries()[AttrId];
			Lines.Add(JoinCsvRow({
				Entry.AttrId.ToString(), Entry.EffectType.ToString(), Entry.Payload,
				Entry.DefaultOp, Entry.DefaultThisWeaponOnly, Entry.ShowAsPercent, FString()
			}));
		}

		const FString Content = FString::Join(Lines, TEXT("\n")) + TEXT("\n");
		if (!FFileHelper::SaveStringToFile(Content, *OutCatalogCsvPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutErrors.Add(FString::Printf(TEXT("Export: failed to write '%s'."), *OutCatalogCsvPath));
			return false;
		}
	}

	// --- Write Cards.csv ---
	{
		TArray<FString> Lines;
		Lines.Add(JoinCsvRow({
			TEXT("CardId"), TEXT("AssetName"), TEXT("Group"), TEXT("Route"), TEXT("OwnerWeapon"), TEXT("Weight"), TEXT("Family"),
			TEXT("DisplayName_ko"), TEXT("DisplayName_en"), TEXT("DisplayName_ja"),
			TEXT("Description_ko"), TEXT("Description_en"), TEXT("Description_ja"),
			TEXT("E1_Attr"), TEXT("E1_Override"), TEXT("E1_Tiers"),
			TEXT("E2_Attr"), TEXT("E2_Override"), TEXT("E2_Tiers"),
			TEXT("E3_Attr"), TEXT("E3_Override"), TEXT("E3_Tiers")
		}));

		// Deterministic row order (asset diff / round-trip stability) — sort by CardId.
		OutputRows.Sort([](const FOutputCardRow& A, const FOutputCardRow& B) { return A.CardId < B.CardId; });

		for (const FOutputCardRow& Row : OutputRows)
		{
			TArray<FString> Cells = {
				Row.CardId, Row.AssetName, Row.Group, Row.Route, Row.OwnerWeapon, Row.Weight, Row.Family,
				Row.DisplayName[0], Row.DisplayName[1], Row.DisplayName[2],
				Row.Description[0], Row.Description[1], Row.Description[2]
			};
			for (int32 EffectIndex = 0; EffectIndex < 3; ++EffectIndex)
			{
				if (Row.Effects.IsValidIndex(EffectIndex))
				{
					Cells.Add(Row.Effects[EffectIndex].AttrId);
					Cells.Add(Row.Effects[EffectIndex].Override);
					Cells.Add(Row.Effects[EffectIndex].Tiers);
				}
				else
				{
					Cells.Add(FString());
					Cells.Add(FString());
					Cells.Add(FString());
				}
			}
			Lines.Add(JoinCsvRow(Cells));
		}

		const FString Content = FString::Join(Lines, TEXT("\n")) + TEXT("\n");
		if (!FFileHelper::SaveStringToFile(Content, *OutCardsCsvPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutErrors.Add(FString::Printf(TEXT("Export: failed to write '%s'."), *OutCardsCsvPath));
			return false;
		}
	}

	for (const FString& Warning : Warnings)
	{
		UE_LOG(LogFPSRCardCsvExport, Warning, TEXT("%s"), *Warning);
	}

	return true;
}
