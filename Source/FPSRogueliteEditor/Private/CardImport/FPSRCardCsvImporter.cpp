// Copyright Epic Games, Inc. All Rights Reserved.

#include "CardImport/FPSRCardCsvImporter.h"
#include "CardImport/FPSRCardCsvSchema.h"

#include "Card/FPSRCardDataAsset.h"
#include "Card/FPSRCardEffect.h"
#include "Card/FPSRCardPoolDataAsset.h"
#include "Card/FPSRCardTypes.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponFragment.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "AbilitySystem/Abilities/FPSRPassiveAbility.h"
#include "DataEditor/FPSRDataEditorHelpers.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/DataAssetFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "Editor.h"
#include "EditorValidatorSubsystem.h"
#include "Misc/DataValidation.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "Misc/FileHelper.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "Internationalization/Text.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPSRCardCsvImport, Log, All);

namespace
{
	const FString GCardsCsvRelPath = TEXT("Authoring/Cards.csv");
	const FString GCatalogCsvRelPath = TEXT("Authoring/CardCatalog.csv");
	const FString GStCardCsvRelPath = TEXT("StringTables/ST_Card.csv");
	const FString GImportedCardsFolder = TEXT("/Game/Cards/Imported");

	// ---------------------------------------------------------------------------------------------------------
	// Catalog lookup — AttrId -> resolved descriptor (mirrors the parsed CardCatalog.csv rows 1:1).
	// ---------------------------------------------------------------------------------------------------------

	struct FCatalogDescriptor
	{
		FName EffectType;
		FString Payload;
		FString DefaultOp;
		FString DefaultThisWeaponOnly;
		FString ShowAsPercent;
	};

	TMap<FName, FCatalogDescriptor> BuildCatalogMap(const TArray<FFPSRCardCatalogRow>& Catalog)
	{
		TMap<FName, FCatalogDescriptor> Map;
		Map.Reserve(Catalog.Num());
		for (const FFPSRCardCatalogRow& Row : Catalog)
		{
			FCatalogDescriptor Descriptor;
			Descriptor.EffectType = Row.EffectType;
			Descriptor.Payload = Row.Payload;
			Descriptor.DefaultOp = Row.DefaultOp;
			Descriptor.DefaultThisWeaponOnly = Row.DefaultThisWeaponOnly;
			Descriptor.ShowAsPercent = Row.ShowAsPercent;
			Map.Add(Row.AttrId, Descriptor);
		}
		return Map;
	}

	// "k=v;k=v" -> map, keys already validated by the schema parser (Op / ThisWeaponOnly / ShowAsPercent only).
	TMap<FString, FString> ParseOverridePairs(const FString& Override)
	{
		TMap<FString, FString> Pairs;
		TArray<FString> Entries;
		Override.ParseIntoArray(Entries, TEXT(";"), /*InCullEmpty=*/true);
		for (const FString& Entry : Entries)
		{
			FString Key, Value;
			if (Entry.Split(TEXT("="), &Key, &Value))
			{
				Pairs.Add(Key.TrimStartAndEnd(), Value.TrimStartAndEnd());
			}
		}
		return Pairs;
	}

	FString ResolveEffective(const TMap<FString, FString>& Overrides, const FString& Key, const FString& CatalogDefault, const FString& HardDefault)
	{
		if (const FString* OverrideValue = Overrides.Find(Key))
		{
			return *OverrideValue;
		}
		return CatalogDefault.IsEmpty() ? HardDefault : CatalogDefault;
	}

	bool ParseBoolStrict(const FString& InValue, bool bDefault)
	{
		if (InValue.IsEmpty())
		{
			return bDefault;
		}
		return InValue.Equals(TEXT("true"), ESearchCase::IgnoreCase);
	}

	// "C:15;R:30;E:60;L:100" -> sorted-ascending RarityTiers (syntax already validated by the schema parser).
	TArray<FFPSRCardRarityTier> ParseTiersToArray(const FString& TiersText)
	{
		TArray<FFPSRCardRarityTier> Tiers;
		TArray<FString> Entries;
		TiersText.ParseIntoArray(Entries, TEXT(";"), /*InCullEmpty=*/true);
		for (const FString& Entry : Entries)
		{
			FString InitialStr, MagnitudeStr;
			if (!Entry.Split(TEXT(":"), &InitialStr, &MagnitudeStr))
			{
				continue;
			}
			const TCHAR Initial = FChar::ToUpper(InitialStr.TrimStartAndEnd()[0]);
			ECardRarity Rarity = ECardRarity::Common;
			switch (Initial)
			{
			case TEXT('C'): Rarity = ECardRarity::Common; break;
			case TEXT('R'): Rarity = ECardRarity::Rare; break;
			case TEXT('E'): Rarity = ECardRarity::Epic; break;
			case TEXT('L'): Rarity = ECardRarity::Legendary; break;
			default: continue;
			}
			float Magnitude = 0.0f;
			FDefaultValueHelper::ParseFloat(MagnitudeStr.TrimStartAndEnd(), Magnitude);
			Tiers.Add(FFPSRCardRarityTier{ Rarity, Magnitude });
		}
		Tiers.Sort([](const FFPSRCardRarityTier& A, const FFPSRCardRarityTier& B) { return static_cast<uint8>(A.Rarity) < static_cast<uint8>(B.Rarity); });
		return Tiers;
	}

	bool TiersEqual(const TArray<FFPSRCardRarityTier>& A, const TArray<FFPSRCardRarityTier>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 i = 0; i < A.Num(); ++i)
		{
			if (A[i].Rarity != B[i].Rarity || !FMath::IsNearlyEqual(A[i].Magnitude, B[i].Magnitude, KINDA_SMALL_NUMBER))
			{
				return false;
			}
		}
		return true;
	}

	UClass* EffectTypeToClass(FName EffectType)
	{
		if (EffectType == TEXT("CharGE")) { return UCardEffect_CharacterGE::StaticClass(); }
		if (EffectType == TEXT("CharPassive")) { return UCardEffect_CharacterPassive::StaticClass(); }
		if (EffectType == TEXT("WeaponStat")) { return UCardEffect_WeaponStat::StaticClass(); }
		if (EffectType == TEXT("WeaponBehavior")) { return UCardEffect_WeaponBehavior::StaticClass(); }
		if (EffectType == TEXT("GrantWeapon")) { return UCardEffect_GrantWeapon::StaticClass(); }
		return nullptr;
	}

	// Reuse the existing Effects[Index] subobject in place if its class already matches TargetClass; otherwise
	// trash the old one (Rename to the transient package — §8 lifecycle, no orphaned subobjects survive a
	// reimport) and create a fresh one named deterministically ("Effect_<i>", §5). Growing the array is handled
	// here too. Only calls Card->Modify() when the ARRAY itself actually changes shape/identity — a slot that
	// already holds the right class is left completely untouched (idempotency).
	UFPSRCardEffect* GetOrCreateEffectSlot(UFPSRCardDataAsset* Card, int32 Index, UClass* TargetClass, bool& bOutCardChanged)
	{
		if (Card->Effects.IsValidIndex(Index) && Card->Effects[Index] && Card->Effects[Index]->GetClass() == TargetClass)
		{
			return Card->Effects[Index];
		}

		Card->Modify();
		bOutCardChanged = true;

		if (Card->Effects.IsValidIndex(Index) && Card->Effects[Index])
		{
			Card->Effects[Index]->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
		}
		if (!Card->Effects.IsValidIndex(Index))
		{
			Card->Effects.SetNum(Index + 1);
		}

		UFPSRCardEffect* NewEffect = NewObject<UFPSRCardEffect>(Card, TargetClass, FName(*FString::Printf(TEXT("Effect_%d"), Index)));
		Card->Effects[Index] = NewEffect;
		return NewEffect;
	}

	bool IsAlreadyStringTableRef(const FText& Text, const FString& ExpectedKey)
	{
		FName TableId;
		FString Key;
		if (FTextInspector::GetTableIdAndKey(Text, TableId, Key))
		{
			return TableId == FName(TEXT("Card")) && Key == ExpectedKey;
		}
		return false;
	}

	// Applies one effect column (E1/E2/E3) to Card->Effects[Index], creating/reusing the subobject and diffing
	// each field before writing. Returns true if anything on the card or effect actually changed.
	bool ApplyEffectColumn(UFPSRCardDataAsset* Card, int32 Index, const FFPSRCardCsvRow::FEffectCol& EffectCol,
		const TMap<FName, FCatalogDescriptor>& CatalogMap, TArray<FString>& OutErrors)
	{
		const FCatalogDescriptor* Descriptor = CatalogMap.Find(EffectCol.AttrId);
		if (!Descriptor)
		{
			// The schema parser already rejects an E*_Attr missing from the catalog before ImportAll is reached,
			// so this can only happen if the caller bypassed parsing — defensive, not a normal-path error.
			OutErrors.Add(FString::Printf(TEXT("Import: '%s' effect %d: AttrId '%s' not found in the resolved catalog."), *Card->GetName(), Index, *EffectCol.AttrId.ToString()));
			return false;
		}

		UClass* TargetClass = EffectTypeToClass(Descriptor->EffectType);
		if (!TargetClass)
		{
			OutErrors.Add(FString::Printf(TEXT("Import: '%s' effect %d: catalog EffectType '%s' is not one of the 5 known kinds."), *Card->GetName(), Index, *Descriptor->EffectType.ToString()));
			return false;
		}

		bool bChanged = false;
		UFPSRCardEffect* Effect = GetOrCreateEffectSlot(Card, Index, TargetClass, bChanged);

		const TMap<FString, FString> Overrides = ParseOverridePairs(EffectCol.Override);

		if (UCardEffect_CharacterGE* GE = Cast<UCardEffect_CharacterGE>(Effect))
		{
			UClass* PayloadClass = FSoftClassPath(Descriptor->Payload).TryLoadClass<UGameplayEffect>();
			if (!PayloadClass)
			{
				OutErrors.Add(FString::Printf(TEXT("Import: '%s' effect %d: CharGE payload '%s' failed to load as a UGameplayEffect class."), *Card->GetName(), Index, *Descriptor->Payload));
			}
			else if (GE->Effect != PayloadClass)
			{
				Effect->Modify();
				GE->Effect = PayloadClass;
				bChanged = true;
			}
			const bool bShowAsPercent = ParseBoolStrict(ResolveEffective(Overrides, TEXT("ShowAsPercent"), Descriptor->ShowAsPercent, TEXT("false")), false);
			if (GE->bShowAsPercent != bShowAsPercent)
			{
				Effect->Modify();
				GE->bShowAsPercent = bShowAsPercent;
				bChanged = true;
			}
		}
		else if (UCardEffect_CharacterPassive* Passive = Cast<UCardEffect_CharacterPassive>(Effect))
		{
			UClass* PayloadClass = FSoftClassPath(Descriptor->Payload).TryLoadClass<UFPSRPassiveAbility>();
			if (!PayloadClass)
			{
				OutErrors.Add(FString::Printf(TEXT("Import: '%s' effect %d: CharPassive payload '%s' failed to load as a UFPSRPassiveAbility class."), *Card->GetName(), Index, *Descriptor->Payload));
			}
			else if (Passive->PassiveAbility != PayloadClass)
			{
				Effect->Modify();
				Passive->PassiveAbility = PayloadClass;
				bChanged = true;
			}
		}
		else if (UCardEffect_WeaponStat* Stat = Cast<UCardEffect_WeaponStat>(Effect))
		{
			const int64 StatValue = StaticEnum<EFPSRWeaponStat>()->GetValueByNameString(Descriptor->Payload);
			if (StatValue == INDEX_NONE)
			{
				OutErrors.Add(FString::Printf(TEXT("Import: '%s' effect %d: WeaponStat payload '%s' is not a valid EFPSRWeaponStat name."), *Card->GetName(), Index, *Descriptor->Payload));
			}
			else if (Stat->Stat != static_cast<EFPSRWeaponStat>(StatValue))
			{
				Effect->Modify();
				Stat->Stat = static_cast<EFPSRWeaponStat>(StatValue);
				bChanged = true;
			}

			const FString OpStr = ResolveEffective(Overrides, TEXT("Op"), Descriptor->DefaultOp, TEXT("PercentMultiply"));
			const int64 OpValue = StaticEnum<EFPSRWeaponModOp>()->GetValueByNameString(OpStr);
			if (OpValue == INDEX_NONE)
			{
				OutErrors.Add(FString::Printf(TEXT("Import: '%s' effect %d: Op '%s' is not a valid EFPSRWeaponModOp name."), *Card->GetName(), Index, *OpStr));
			}
			else if (Stat->Op != static_cast<EFPSRWeaponModOp>(OpValue))
			{
				Effect->Modify();
				Stat->Op = static_cast<EFPSRWeaponModOp>(OpValue);
				bChanged = true;
			}

			const bool bThisWeaponOnly = ParseBoolStrict(ResolveEffective(Overrides, TEXT("ThisWeaponOnly"), Descriptor->DefaultThisWeaponOnly, TEXT("true")), true);
			if (Stat->bThisWeaponOnly != bThisWeaponOnly)
			{
				Effect->Modify();
				Stat->bThisWeaponOnly = bThisWeaponOnly;
				bChanged = true;
			}
		}
		else if (UCardEffect_WeaponBehavior* Behavior = Cast<UCardEffect_WeaponBehavior>(Effect))
		{
			UFPSRWeaponFragment* Fragment = Cast<UFPSRWeaponFragment>(FSoftObjectPath(Descriptor->Payload).TryLoad());
			if (!Fragment)
			{
				OutErrors.Add(FString::Printf(TEXT("Import: '%s' effect %d: WeaponBehavior payload '%s' failed to load as a UFPSRWeaponFragment asset."), *Card->GetName(), Index, *Descriptor->Payload));
			}
			else if (Behavior->Fragment != Fragment)
			{
				Effect->Modify();
				Behavior->Fragment = Fragment;
				bChanged = true;
			}
		}
		else if (UCardEffect_GrantWeapon* Grant = Cast<UCardEffect_GrantWeapon>(Effect))
		{
			UFPSRWeaponDataAsset* Weapon = Cast<UFPSRWeaponDataAsset>(FSoftObjectPath(Descriptor->Payload).TryLoad());
			if (!Weapon)
			{
				OutErrors.Add(FString::Printf(TEXT("Import: '%s' effect %d: GrantWeapon payload '%s' failed to load as a UFPSRWeaponDataAsset asset."), *Card->GetName(), Index, *Descriptor->Payload));
			}
			else if (Grant->WeaponToGrant != Weapon)
			{
				Effect->Modify();
				Grant->WeaponToGrant = Weapon;
				bChanged = true;
			}
		}

		const TArray<FFPSRCardRarityTier> DesiredTiers = ParseTiersToArray(EffectCol.Tiers);
		if (!TiersEqual(Effect->RarityTiers, DesiredTiers))
		{
			Effect->Modify();
			Effect->RarityTiers = DesiredTiers;
			bChanged = true;
		}

		return bChanged;
	}

	// Applies the (existing-tag-only, pre-B-6) CardFamily. Blank CSV Family derives from E1's AttrId — this only
	// succeeds when that exact string happens to already be a REGISTERED gameplay tag (raw AttrIds like
	// "weapon.magsize" normally are not); otherwise the card's existing value is left untouched and IsDataValid's
	// existing multi-effect-requires-CardFamily check surfaces the gap. B-6 changes this field to FName and this
	// function's body along with it (§ commit B-6).
	bool ApplyCardFamily(UFPSRCardDataAsset* Card, const FFPSRCardCsvRow& Row)
	{
		FString DesiredTagName = Row.Family.IsNone() ? FString() : Row.Family.ToString();
		if (DesiredTagName.IsEmpty() && Row.Effects.Num() > 0)
		{
			DesiredTagName = Row.Effects[0].AttrId.ToString();
		}
		if (DesiredTagName.IsEmpty())
		{
			return false;
		}

		const FGameplayTag DesiredTag = FGameplayTag::RequestGameplayTag(FName(*DesiredTagName), /*ErrorIfNotFound=*/false);
		if (!DesiredTag.IsValid())
		{
			return false;
		}
		if (Card->CardFamily != DesiredTag)
		{
			Card->Modify();
			Card->CardFamily = DesiredTag;
			return true;
		}
		return false;
	}

	// ---------------------------------------------------------------------------------------------------------
	// Asset resolution: existing-by-CardId (1st) -> existing-by-AssetName (2nd, records CardId) -> new under
	// /Game/Cards/Imported/<AssetName> (§5 "임포터 상세 계약").
	// ---------------------------------------------------------------------------------------------------------

	UFPSRCardDataAsset* FindCardByCardId(const TArray<FAssetData>& AllCards, FName CardId)
	{
		for (const FAssetData& AssetData : AllCards)
		{
			if (UFPSRCardDataAsset* Card = Cast<UFPSRCardDataAsset>(AssetData.GetAsset()))
			{
				if (Card->CardId == CardId)
				{
					return Card;
				}
			}
		}
		return nullptr;
	}

	UFPSRCardDataAsset* FindCardByAssetName(const TArray<FAssetData>& AllCards, const FString& AssetName)
	{
		for (const FAssetData& AssetData : AllCards)
		{
			if (AssetData.AssetName == FName(*AssetName))
			{
				return Cast<UFPSRCardDataAsset>(AssetData.GetAsset());
			}
		}
		return nullptr;
	}

	UFPSRCardDataAsset* CreateNewCardAsset(const FString& AssetName)
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
		Factory->DataAssetClass = UFPSRCardDataAsset::StaticClass();
		UObject* NewAsset = AssetToolsModule.Get().CreateAsset(AssetName, GImportedCardsFolder, UFPSRCardDataAsset::StaticClass(), Factory);
		return Cast<UFPSRCardDataAsset>(NewAsset);
	}

	TSet<UFPSRCardDataAsset*> ToCardSet(const TArray<TObjectPtr<UFPSRCardDataAsset>>& InArray)
	{
		TSet<UFPSRCardDataAsset*> Out;
		Out.Reserve(InArray.Num());
		for (const TObjectPtr<UFPSRCardDataAsset>& Element : InArray)
		{
			Out.Add(Element.Get());
		}
		return Out;
	}

	UFPSRWeaponDataAsset* FindWeaponByAssetName(const TArray<FAssetData>& AllWeapons, const FString& AssetName)
	{
		if (AssetName.IsEmpty())
		{
			return nullptr;
		}
		for (const FAssetData& AssetData : AllWeapons)
		{
			if (AssetData.AssetName == FName(*AssetName))
			{
				return Cast<UFPSRWeaponDataAsset>(AssetData.GetAsset());
			}
		}
		return nullptr;
	}

	// ---------------------------------------------------------------------------------------------------------
	// ST_Card.csv regeneration (§5): Cards.csv ko/en/ja -> Key,SourceString,en,ja, preserving Debug.LocSmoke.
	// ---------------------------------------------------------------------------------------------------------

	bool RegenerateStCardCsv(const TArray<FFPSRCardCsvRow>& Rows, TArray<FString>& OutErrors)
	{
		const FString StCardPath = FPaths::ProjectContentDir() / GStCardCsvRelPath;

		// Preserve the LOC0 smoke-test seed row verbatim if present (Docs/SSOT/Localization.md L-2 — "삭제 금지").
		FString SeedLine;
		{
			FString ExistingContent;
			if (FFileHelper::LoadFileToString(ExistingContent, *StCardPath))
			{
				TArray<FString> ExistingLines;
				ExistingContent.ParseIntoArrayLines(ExistingLines);
				for (const FString& Line : ExistingLines)
				{
					if (Line.StartsWith(TEXT("Debug.LocSmoke,")))
					{
						SeedLine = Line;
						break;
					}
				}
			}
		}

		TArray<FString> Lines;
		Lines.Add(TEXT("Key,SourceString,en,ja"));
		if (!SeedLine.IsEmpty())
		{
			Lines.Add(SeedLine);
		}

		auto EscapeCell = [](const FString& In) -> FString
		{
			const bool bNeedsQuote = In.Contains(TEXT(",")) || In.Contains(TEXT("\"")) || In.Contains(TEXT("\n")) || In.Contains(TEXT("\r"));
			if (!bNeedsQuote)
			{
				return In;
			}
			FString Escaped = In;
			Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
			return FString::Printf(TEXT("\"%s\""), *Escaped);
		};

		for (const FFPSRCardCsvRow& Row : Rows)
		{
			if (Row.CardId.IsNone())
			{
				continue;
			}
			// Only emit a key when SOME translation exists — a card whose author left every localized column
			// blank should not seed an all-empty StringTable row (matches the "en/ja blanks are a warning, not a
			// failure" LOC0 rule; here it also avoids a Key with a genuinely empty SourceString).
			if (!Row.DisplayName[0].IsEmpty() || !Row.DisplayName[1].IsEmpty() || !Row.DisplayName[2].IsEmpty())
			{
				const FString SourceString = Row.DisplayName[0].IsEmpty() ? Row.DisplayName[1] : Row.DisplayName[0];
				Lines.Add(FString::Printf(TEXT("%s,%s,%s,%s"),
					*EscapeCell(FString::Printf(TEXT("%s.DisplayName"), *Row.CardId.ToString())),
					*EscapeCell(SourceString), *EscapeCell(Row.DisplayName[1]), *EscapeCell(Row.DisplayName[2])));
			}
			if (!Row.Description[0].IsEmpty() || !Row.Description[1].IsEmpty() || !Row.Description[2].IsEmpty())
			{
				const FString SourceString = Row.Description[0].IsEmpty() ? Row.Description[1] : Row.Description[0];
				Lines.Add(FString::Printf(TEXT("%s,%s,%s,%s"),
					*EscapeCell(FString::Printf(TEXT("%s.Description"), *Row.CardId.ToString())),
					*EscapeCell(SourceString), *EscapeCell(Row.Description[1]), *EscapeCell(Row.Description[2])));
			}
		}

		const FString NewContent = FString::Join(Lines, TEXT("\n")) + TEXT("\n");

		// Idempotency at the file level too: don't rewrite (and don't touch mtime) if the content is unchanged.
		FString ExistingContent;
		if (FFileHelper::LoadFileToString(ExistingContent, *StCardPath) && ExistingContent == NewContent)
		{
			return true;
		}

		if (!FFileHelper::SaveStringToFile(NewContent, *StCardPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			OutErrors.Add(FString::Printf(TEXT("Import: failed to write '%s'."), *StCardPath));
			return false;
		}
		return true;
	}

	// ---------------------------------------------------------------------------------------------------------
	// Pool membership: declarative sync (add + remove) driven by each row's Route/OwnerWeapon (§5).
	// ---------------------------------------------------------------------------------------------------------

	void SyncPoolMembership(const TArray<FFPSRCardCsvRow>& Rows, const TMap<FName, UFPSRCardDataAsset*>& CardsByCardId,
		TSet<UObject*>& OutTouchedForValidation, TArray<UPackage*>& OutDirtyPackages, TArray<FString>& OutErrors)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		FARFilter PoolFilter;
		PoolFilter.ClassPaths.Add(UFPSRCardPoolDataAsset::StaticClass()->GetClassPathName());
		TArray<FAssetData> PoolAssets;
		AssetRegistry.GetAssets(PoolFilter, PoolAssets);

		if (PoolAssets.Num() != 1)
		{
			OutErrors.Add(FString::Printf(TEXT("Import: expected exactly 1 UFPSRCardPoolDataAsset in the project to sync LevelUpGlobal/MissionClearNewWeapon membership against, found %d — pool membership sync skipped."), PoolAssets.Num()));
			return;
		}
		UFPSRCardPoolDataAsset* Pool = Cast<UFPSRCardPoolDataAsset>(PoolAssets[0].GetAsset());
		if (!Pool)
		{
			OutErrors.Add(TEXT("Import: failed to load the project's card pool asset."));
			return;
		}

		FARFilter WeaponFilter;
		WeaponFilter.ClassPaths.Add(UFPSRWeaponDataAsset::StaticClass()->GetClassPathName());
		TArray<FAssetData> WeaponAssets;
		AssetRegistry.GetAssets(WeaponFilter, WeaponAssets);

		// Target sets per route, built from the CSV (the single source of truth for membership going forward).
		TSet<UFPSRCardDataAsset*> TargetLevelUpGlobal, TargetMissionClearNewWeapon;
		TMap<UFPSRWeaponDataAsset*, TSet<UFPSRCardDataAsset*>> TargetWeaponCards, TargetUnlockableFeatures;

		for (const FFPSRCardCsvRow& Row : Rows)
		{
			UFPSRCardDataAsset* const* CardPtr = CardsByCardId.Find(Row.CardId);
			if (!CardPtr || !*CardPtr)
			{
				continue; // resolution failure already reported elsewhere
			}
			UFPSRCardDataAsset* Card = *CardPtr;

			switch (Row.Route)
			{
			case EFPSRCardRoute::LevelUpGlobal:
				TargetLevelUpGlobal.Add(Card);
				break;
			case EFPSRCardRoute::MissionClearNewWeapon:
				TargetMissionClearNewWeapon.Add(Card);
				break;
			case EFPSRCardRoute::LevelUpWeapon:
			case EFPSRCardRoute::MissionClearWeaponFeature:
			{
				UFPSRWeaponDataAsset* Weapon = FindWeaponByAssetName(WeaponAssets, Row.OwnerWeapon);
				if (!Weapon)
				{
					OutErrors.Add(FString::Printf(TEXT("Import: card '%s' OwnerWeapon '%s' does not resolve to a UFPSRWeaponDataAsset asset."), *Row.CardId.ToString(), *Row.OwnerWeapon));
					continue;
				}
				if (Row.Route == EFPSRCardRoute::LevelUpWeapon)
				{
					TargetWeaponCards.FindOrAdd(Weapon).Add(Card);
				}
				else
				{
					TargetUnlockableFeatures.FindOrAdd(Weapon).Add(Card);
				}
				break;
			}
			}
		}

		// Declarative sync (§5 "CSV에 없는 기존 멤버 제거는 경고 로그 동반"): add everything in Target not yet in
		// Current, remove everything in Current not in Target. Pool.Cards/WeaponUnlockCards below; weapon arrays
		// (WeaponCards/UnlockableFeatures) follow via SyncWeaponArray further down.
		{
			TSet<UFPSRCardDataAsset*> Current = ToCardSet(Pool->Cards);
			for (UFPSRCardDataAsset* Card : TargetLevelUpGlobal)
			{
				if (!Current.Contains(Card) && FFPSRDataEditorHelpers::AddCardToPool(Pool, Card, /*bUnlockArray=*/false))
				{
					OutTouchedForValidation.Add(Pool);
				}
			}
			for (UFPSRCardDataAsset* Card : Current)
			{
				if (!TargetLevelUpGlobal.Contains(Card))
				{
					UE_LOG(LogFPSRCardCsvImport, Warning, TEXT("Import: removing '%s' from Pool.Cards — not present in Cards.csv with Route=LevelUpGlobal."), Card ? *Card->GetName() : TEXT("<null>"));
					if (FFPSRDataEditorHelpers::RemoveCardFromPool(Pool, Card, /*bUnlockArray=*/false))
					{
						OutTouchedForValidation.Add(Pool);
					}
				}
			}
		}
		{
			TSet<UFPSRCardDataAsset*> Current = ToCardSet(Pool->WeaponUnlockCards);
			for (UFPSRCardDataAsset* Card : TargetMissionClearNewWeapon)
			{
				if (!Current.Contains(Card) && FFPSRDataEditorHelpers::AddCardToPool(Pool, Card, /*bUnlockArray=*/true))
				{
					OutTouchedForValidation.Add(Pool);
				}
			}
			for (UFPSRCardDataAsset* Card : Current)
			{
				if (!TargetMissionClearNewWeapon.Contains(Card))
				{
					UE_LOG(LogFPSRCardCsvImport, Warning, TEXT("Import: removing '%s' from Pool.WeaponUnlockCards — not present in Cards.csv with Route=MissionClearNewWeapon."), Card ? *Card->GetName() : TEXT("<null>"));
					if (FFPSRDataEditorHelpers::RemoveCardFromPool(Pool, Card, /*bUnlockArray=*/true))
					{
						OutTouchedForValidation.Add(Pool);
					}
				}
			}
		}
		if (OutTouchedForValidation.Contains(Pool))
		{
			OutDirtyPackages.AddUnique(Pool->GetPackage());
		}

		auto SyncWeaponArray = [&](UFPSRWeaponDataAsset* Weapon, const TSet<UFPSRCardDataAsset*>& Target, bool bUnlockableFeatures, const TCHAR* WarningLabel)
		{
			const TArray<TObjectPtr<UFPSRCardDataAsset>>& CurrentArray = bUnlockableFeatures ? Weapon->UnlockableFeatures : Weapon->WeaponCards;
			TSet<UFPSRCardDataAsset*> Current = ToCardSet(CurrentArray);
			bool bWeaponTouched = false;
			for (UFPSRCardDataAsset* Card : Target)
			{
				if (!Current.Contains(Card) && FFPSRDataEditorHelpers::AddCardToWeapon(Weapon, Card, bUnlockableFeatures))
				{
					bWeaponTouched = true;
				}
			}
			for (UFPSRCardDataAsset* Card : Current)
			{
				if (!Target.Contains(Card))
				{
					UE_LOG(LogFPSRCardCsvImport, Warning, TEXT("Import: removing '%s' from Weapon '%s'.%s — not present in Cards.csv for that weapon/route."), Card ? *Card->GetName() : TEXT("<null>"), *Weapon->GetName(), WarningLabel);
					if (FFPSRDataEditorHelpers::RemoveCardFromWeapon(Weapon, Card, bUnlockableFeatures))
					{
						bWeaponTouched = true;
					}
				}
			}
			if (bWeaponTouched)
			{
				OutTouchedForValidation.Add(Weapon);
				OutDirtyPackages.AddUnique(Weapon->GetPackage());
			}
		};

		// Every weapon referenced from EITHER side (CSV target OR the weapon's current arrays) needs a pass, so a
		// weapon whose Cards.csv rows all disappeared still gets its stale members removed.
		TSet<UFPSRWeaponDataAsset*> WeaponsToSync;
		for (const TPair<UFPSRWeaponDataAsset*, TSet<UFPSRCardDataAsset*>>& Entry : TargetWeaponCards) { WeaponsToSync.Add(Entry.Key); }
		for (const TPair<UFPSRWeaponDataAsset*, TSet<UFPSRCardDataAsset*>>& Entry : TargetUnlockableFeatures) { WeaponsToSync.Add(Entry.Key); }
		for (const FAssetData& WeaponAssetData : WeaponAssets)
		{
			if (UFPSRWeaponDataAsset* Weapon = Cast<UFPSRWeaponDataAsset>(WeaponAssetData.GetAsset()))
			{
				if (Weapon->WeaponCards.Num() > 0 || Weapon->UnlockableFeatures.Num() > 0)
				{
					WeaponsToSync.Add(Weapon);
				}
			}
		}

		for (UFPSRWeaponDataAsset* Weapon : WeaponsToSync)
		{
			const TSet<UFPSRCardDataAsset*>* TargetCards = TargetWeaponCards.Find(Weapon);
			SyncWeaponArray(Weapon, TargetCards ? *TargetCards : TSet<UFPSRCardDataAsset*>(), /*bUnlockableFeatures=*/false, TEXT("WeaponCards"));
			const TSet<UFPSRCardDataAsset*>* TargetFeatures = TargetUnlockableFeatures.Find(Weapon);
			SyncWeaponArray(Weapon, TargetFeatures ? *TargetFeatures : TSet<UFPSRCardDataAsset*>(), /*bUnlockableFeatures=*/true, TEXT("UnlockableFeatures"));
		}
	}
}

FFPSRCardImportResult FPSRCardCsvImport::ImportAll(bool bSaveAssets)
{
	FFPSRCardImportResult Result;

	const FString CardsPath = FPaths::ProjectContentDir() / GCardsCsvRelPath;
	const FString CatalogPath = FPaths::ProjectContentDir() / GCatalogCsvRelPath;

	FString CardsText, CatalogText;
	if (!FFileHelper::LoadFileToString(CatalogText, *CatalogPath))
	{
		Result.Errors.Add(FString::Printf(TEXT("Import: failed to load '%s'."), *CatalogPath));
		return Result;
	}
	if (!FFileHelper::LoadFileToString(CardsText, *CardsPath))
	{
		Result.Errors.Add(FString::Printf(TEXT("Import: failed to load '%s'."), *CardsPath));
		return Result;
	}

	FFPSRCardCsvParseResult ParseResult;
	const bool bCatalogOk = FPSRCardCsv::ParseCatalog(CatalogText, ParseResult);
	const bool bCardsOk = FPSRCardCsv::ParseCards(CardsText, ParseResult.Catalog, ParseResult);
	if (!bCatalogOk || !bCardsOk)
	{
		Result.Errors = ParseResult.Errors;
		return Result;
	}
	if (ParseResult.Cards.Num() == 0)
	{
		// An empty (header-only) Cards.csv is not itself an error (§ B-2 ships the two files exactly this way,
		// before B-3's export seeds real data) — nothing to import, report success with zero counts.
		return Result;
	}

	const TMap<FName, FCatalogDescriptor> CatalogMap = BuildCatalogMap(ParseResult.Catalog);

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	AssetRegistry.SearchAllAssets(/*bSynchronousSearch=*/true);
	FARFilter CardFilter;
	CardFilter.ClassPaths.Add(UFPSRCardDataAsset::StaticClass()->GetClassPathName());
	TArray<FAssetData> AllCardAssets;
	AssetRegistry.GetAssets(CardFilter, AllCardAssets);

	TMap<FName, UFPSRCardDataAsset*> CardsByCardId;
	TSet<UObject*> TouchedForValidation;
	TArray<UPackage*> TouchedPackages;

	for (const FFPSRCardCsvRow& Row : ParseResult.Cards)
	{
		UFPSRCardDataAsset* Card = FindCardByCardId(AllCardAssets, Row.CardId);
		bool bNewlyCreated = false;
		bool bCardChanged = false;

		if (!Card)
		{
			Card = FindCardByAssetName(AllCardAssets, Row.AssetName);
			if (Card)
			{
				if (Card->CardId != Row.CardId)
				{
					Card->Modify();
					Card->CardId = Row.CardId;
					bCardChanged = true;
				}
			}
		}

		if (!Card)
		{
			Card = CreateNewCardAsset(Row.AssetName);
			if (!Card)
			{
				Result.Errors.Add(FString::Printf(TEXT("Import: failed to create a new card asset for CardId '%s' (AssetName '%s')."), *Row.CardId.ToString(), *Row.AssetName));
				continue;
			}
			Card->Modify();
			Card->CardId = Row.CardId;
			bNewlyCreated = true;
			bCardChanged = true;
			AllCardAssets.Add(FAssetData(Card)); // so a later duplicate CardId row in the same run still resolves
		}

		CardsByCardId.Add(Row.CardId, Card);

		if (Card->Group != Row.Group)
		{
			Card->Modify();
			Card->Group = Row.Group;
			bCardChanged = true;
		}
		if (!FMath::IsNearlyEqual(Card->Weight, Row.Weight, KINDA_SMALL_NUMBER))
		{
			Card->Modify();
			Card->Weight = Row.Weight;
			bCardChanged = true;
		}

		const FString DisplayNameKey = FString::Printf(TEXT("%s.DisplayName"), *Row.CardId.ToString());
		if (!IsAlreadyStringTableRef(Card->DisplayName, DisplayNameKey))
		{
			Card->Modify();
			Card->DisplayName = FText::FromStringTable(TEXT("Card"), DisplayNameKey);
			bCardChanged = true;
		}
		const FString DescriptionKey = FString::Printf(TEXT("%s.Description"), *Row.CardId.ToString());
		if (!IsAlreadyStringTableRef(Card->Description, DescriptionKey))
		{
			Card->Modify();
			Card->Description = FText::FromStringTable(TEXT("Card"), DescriptionKey);
			bCardChanged = true;
		}

		bool bEffectsChanged = false;
		for (int32 EffectIndex = 0; EffectIndex < Row.Effects.Num(); ++EffectIndex)
		{
			if (ApplyEffectColumn(Card, EffectIndex, Row.Effects[EffectIndex], CatalogMap, Result.Errors))
			{
				bEffectsChanged = true;
			}
		}
		// Trim surplus effects beyond what this row declares (§5 — "배열 길이 축소 시 잉여 효과 트래시").
		if (Card->Effects.Num() > Row.Effects.Num())
		{
			Card->Modify();
			for (int32 i = Row.Effects.Num(); i < Card->Effects.Num(); ++i)
			{
				if (Card->Effects[i])
				{
					Card->Effects[i]->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
				}
			}
			Card->Effects.SetNum(Row.Effects.Num());
			bEffectsChanged = true;
		}
		if (bEffectsChanged)
		{
			bCardChanged = true;
			FProperty* EffectsProp = FindFProperty<FProperty>(UFPSRCardDataAsset::StaticClass(), GET_MEMBER_NAME_CHECKED(UFPSRCardDataAsset, Effects));
			FPropertyChangedEvent Evt(EffectsProp, EPropertyChangeType::ValueSet);
			Card->PostEditChangeProperty(Evt); // re-derives OfferRarities (RefreshOfferRarities)
		}

		if (ApplyCardFamily(Card, Row))
		{
			bCardChanged = true;
		}

		if (bNewlyCreated)
		{
			++Result.CreatedCount;
		}
		else if (bCardChanged)
		{
			++Result.UpdatedCount;
		}
		else
		{
			++Result.UnchangedCount;
		}

		if (bCardChanged)
		{
			TouchedForValidation.Add(Card);
			TouchedPackages.AddUnique(Card->GetPackage());
		}
	}

	SyncPoolMembership(ParseResult.Cards, CardsByCardId, TouchedForValidation, TouchedPackages, Result.Errors);

	if (!RegenerateStCardCsv(ParseResult.Cards, Result.Errors))
	{
		// Non-fatal to the DA import itself, but surfaced — the CSV pipeline's text side failed to update.
	}

	// --- Validation gate (§6-2 / §2-3-8: IsDataValid + FPSRCardPoolValidator, reused not ported) ---
	TSet<UObject*> ValidatedInvalid;
	if (TouchedForValidation.Num() > 0 && GEditor)
	{
		UEditorValidatorSubsystem* ValidationSubsystem = GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>();
		if (ValidationSubsystem)
		{
			TArray<FAssetData> ToValidate;
			ToValidate.Reserve(TouchedForValidation.Num());
			for (UObject* Object : TouchedForValidation)
			{
				ToValidate.Add(FAssetData(Object));
			}

			FValidateAssetsSettings Settings;
			Settings.bSkipExcludedDirectories = true;
			Settings.bShowIfNoFailures = false;
			Settings.bCollectPerAssetDetails = true;
			Settings.ValidationUsecase = bSaveAssets ? EDataValidationUsecase::Commandlet : EDataValidationUsecase::Manual;

			FValidateAssetsResults ValidationResults;
			ValidationSubsystem->ValidateAssetsWithSettings(ToValidate, Settings, ValidationResults);

			for (UObject* Object : TouchedForValidation)
			{
				const FString ObjectPath = FAssetData(Object).GetObjectPathString();
				if (const FValidateAssetsDetails* Details = ValidationResults.AssetsDetails.Find(ObjectPath))
				{
					if (Details->Result == EDataValidationResult::Invalid)
					{
						ValidatedInvalid.Add(Object);
						for (const FText& Error : Details->ValidationErrors)
						{
							Result.Errors.Add(FString::Printf(TEXT("Import validation: %s: %s"), *Object->GetName(), *Error.ToString()));
						}
					}
				}
			}
		}
	}

	// --- Save (§6: bSaveAssets saves successful assets; a failed-validation asset stays dirty, unsaved) ---
	if (bSaveAssets)
	{
		TArray<UPackage*> PackagesToSave;
		for (UPackage* Package : TouchedPackages)
		{
			bool bAnyInvalidInPackage = false;
			for (UObject* Invalid : ValidatedInvalid)
			{
				if (Invalid && Invalid->GetPackage() == Package)
				{
					bAnyInvalidInPackage = true;
					break;
				}
			}
			if (!bAnyInvalidInPackage)
			{
				PackagesToSave.Add(Package);
			}
		}
		if (PackagesToSave.Num() > 0)
		{
			FFPSRDataEditorHelpers::SavePackages(PackagesToSave);
		}
	}

	return Result;
}
