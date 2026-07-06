// Copyright Epic Games, Inc. All Rights Reserved.

#include "Validation/FPSRAnchoredValidationService.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetIdentifier.h"
#include "Card/FPSRCardPoolDataAsset.h"
#include "Card/FPSRCardDataAsset.h"
#include "Run/FPSRRunScheduleDataAsset.h"
#include "Run/Mission/FPSRMissionDataAsset.h"
#include "Weapon/FPSRLoadoutPoolDataAsset.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "Weapon/FPSRWeaponFragment.h"

namespace
{
	/** Builds an FARFilter matching exact instances (not subclasses) of the given asset classes. Anchors and leaves
	 *  are concrete DataAsset types with no BP subclassing in this project, so bRecursiveClasses stays false — a
	 *  narrower match than "everything derived from" is exactly what discovery wants here. */
	FARFilter MakeClassFilter(const TArray<UClass*>& Classes)
	{
		FARFilter Filter;
		Filter.bRecursiveClasses = false;
		for (UClass* Class : Classes)
		{
			Filter.ClassPaths.Add(Class->GetClassPathName());
		}
		return Filter;
	}
}

bool FFPSRAnchoredValidationService::IsExcludedPath(FName PackagePath)
{
	const FString PathString = PackagePath.ToString();
	if (PathString.StartsWith(TEXT("/Game/Developers")) || PathString.StartsWith(TEXT("/Game/Test")))
	{
		return true;
	}
	return PathString.Contains(TEXT("_Scratch"), ESearchCase::IgnoreCase);
}

TArray<FAssetData> FFPSRAnchoredValidationService::FindAnchorAssets()
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter = MakeClassFilter({
		UFPSRCardPoolDataAsset::StaticClass(),
		UFPSRRunScheduleDataAsset::StaticClass(),
		UFPSRLoadoutPoolDataAsset::StaticClass(),
	});

	TArray<FAssetData> Anchors;
	AssetRegistry.GetAssets(Filter, Anchors);
	Anchors.RemoveAll([](const FAssetData& Asset) { return IsExcludedPath(Asset.PackagePath); });
	return Anchors;
}

TArray<FAssetData> FFPSRAnchoredValidationService::FindLeafCandidates()
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	FARFilter Filter = MakeClassFilter({
		UFPSRCardDataAsset::StaticClass(),
		UFPSRWeaponDataAsset::StaticClass(),
		UFPSRMissionDataAsset::StaticClass(),
		UFPSRWeaponFragment::StaticClass(),
	});

	TArray<FAssetData> Leaves;
	AssetRegistry.GetAssets(Filter, Leaves);
	Leaves.RemoveAll([](const FAssetData& Asset) { return IsExcludedPath(Asset.PackagePath); });
	return Leaves;
}

TArray<FAssetData> FFPSRAnchoredValidationService::GatherAssetsToValidate()
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> Anchors = FindAnchorAssets();
	const TArray<FAssetData> LeafCandidates = FindLeafCandidates();

	// Index leaf candidates by package name so the BFS below only has to check set membership, not re-filter the
	// asset registry per hop.
	TMap<FName, FAssetData> LeafByPackage;
	LeafByPackage.Reserve(LeafCandidates.Num());
	for (const FAssetData& Leaf : LeafCandidates)
	{
		LeafByPackage.Add(Leaf.PackageName, Leaf);
	}

	// Reachability = on-disk PACKAGE dependency BFS from each anchor (Hard or Soft — a soft TSoftObjectPtr reference
	// such as UCardEffect_GrantWeapon::WeaponToGrant is still "this run can load it" and must validate). This is the
	// pragmatic choice from the design doc: walking every typed UPROPERTY reflectively (Instanced Effects arrays,
	// TSoftObjectPtr, TObjectPtr) would require per-effect-class knowledge and break the moment a new fragment/effect
	// type is added; the on-disk dependency graph the AssetRegistry already maintains gives the same reachable set
	// (anything the anchor's package references becomes a package dependency, instanced subobjects included, because
	// their outer's package is what's saved to disk) without any per-type coupling here.
	TSet<FName> Visited;
	TArray<FAssetData> Result;
	TArray<FName> Frontier;

	for (const FAssetData& Anchor : Anchors)
	{
		Result.Add(Anchor);
		if (!Visited.Contains(Anchor.PackageName))
		{
			Visited.Add(Anchor.PackageName);
			Frontier.Add(Anchor.PackageName);
		}
	}

	while (Frontier.Num() > 0)
	{
		const FName Current = Frontier.Pop(EAllowShrinking::No);

		TArray<FAssetIdentifier> Dependencies;
		AssetRegistry.GetDependencies(FAssetIdentifier(Current), Dependencies, UE::AssetRegistry::EDependencyCategory::Package);

		for (const FAssetIdentifier& Dependency : Dependencies)
		{
			const FName DependencyPackage = Dependency.PackageName;
			if (DependencyPackage.IsNone() || Visited.Contains(DependencyPackage))
			{
				continue;
			}
			Visited.Add(DependencyPackage);

			// Keep walking through ANY dependency (not just leaf types) — a card can reference a fragment that in
			// turn is only one hop from the anchor via another card, and non-leaf packages (e.g. GameplayEffect
			// blueprints) may sit between an anchor and a leaf we do care about.
			Frontier.Add(DependencyPackage);

			if (const FAssetData* LeafAsset = LeafByPackage.Find(DependencyPackage))
			{
				Result.Add(*LeafAsset);
			}
		}
	}

	return Result;
}

TArray<FAssetData> FFPSRAnchoredValidationService::FindOrphans()
{
	const TArray<FAssetData> Reachable = GatherAssetsToValidate();
	TSet<FName> ReachablePackages;
	ReachablePackages.Reserve(Reachable.Num());
	for (const FAssetData& Asset : Reachable)
	{
		ReachablePackages.Add(Asset.PackageName);
	}

	TArray<FAssetData> Orphans;
	for (const FAssetData& Leaf : FindLeafCandidates())
	{
		if (!ReachablePackages.Contains(Leaf.PackageName))
		{
			Orphans.Add(Leaf);
		}
	}
	return Orphans;
}
