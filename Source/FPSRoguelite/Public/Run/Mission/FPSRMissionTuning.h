// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
// Full includes (not fwd-decls): these actor types back TSubclassOf<> UPROPERTYs below, whose TSubclassOf::operator*
// (Class->IsChildOf(T::StaticClass())) needs the COMPLETE type. A fwd-decl only compiled by luck of the unity-build
// grouping (the merge of the multimap-U arc reshuffled it and surfaced the latent incomplete-type error). IWYU.
#include "Run/Mission/FPSRMissionOrb.h"
#include "Run/Mission/FPSRMissionFleeTarget.h"
#include "FPSRMissionTuning.generated.h"

/**
 * Polymorphic mission tuning object (§2-8-1). A mission DataAsset owns one Instanced UFPSRMissionTuning; the
 * mission actor casts it to its own tuning subclass and reads parameters from it. Mirrors the UFPSRCardEffect
 * pattern (Card/FPSRCardEffect.h) — a NEW mission type = one tuning subclass, zero central edits (OCP).
 *
 * Fallback: when Data->Tuning is null (or the wrong subclass), AFPSRMissionActor::GetTuning<T>() returns the
 * tuning subclass's own compiled-in CDO defaults (*GetDefault<T>()) — so a mission with no Tuning asset assigned
 * still runs on sensible defaults. The mission subclasses no longer carry per-actor legacy tuning fields; each
 * caches its resolved values from GetTuning<T>() at activation.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, CollapseCategories)
class FPSROGUELITE_API UFPSRMissionTuning : public UObject
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	/** Editor-tool summary line for this tuning object. Base = the class display name. */
	virtual FText GetEditorSummary() const;
#endif
};

/** Tuning for AFPSRMission_HoldZone: hold a circular zone for N seconds. */
UCLASS(meta = (DisplayName = "Tuning: HoldZone"))
class FPSROGUELITE_API UFPSRMissionTuning_HoldZone : public UFPSRMissionTuning
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Tuning")
	float ZoneRadius = 700.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Tuning")
	float RequiredHoldSeconds = 30.0f;
};

/** Tuning for AFPSRMission_StandStill: stay below a speed threshold for N seconds. */
UCLASS(meta = (DisplayName = "Tuning: StandStill"))
class FPSROGUELITE_API UFPSRMissionTuning_StandStill : public UFPSRMissionTuning
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Tuning")
	float RequiredStillSeconds = 15.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Tuning", meta = (ClampMin = "0.0"))
	float StillSpeedThreshold = 50.0f;
};

/** Tuning for AFPSRMission_MovingZone: tour a point set, holding each point for N seconds. */
UCLASS(meta = (DisplayName = "Tuning: MovingZone"))
class FPSROGUELITE_API UFPSRMissionTuning_MovingZone : public UFPSRMissionTuning
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Tuning")
	float ZoneRadius = 700.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Tuning")
	float RequiredHoldSeconds = 30.0f;
};

/** Tuning for AFPSRMission_CollectOrbs: collect every orb spawned at configured relative locations. */
UCLASS(meta = (DisplayName = "Tuning: CollectOrbs"))
class FPSROGUELITE_API UFPSRMissionTuning_CollectOrbs : public UFPSRMissionTuning
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Tuning")
	TSubclassOf<AFPSRMissionOrb> OrbClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Tuning")
	TArray<FVector> OrbRelativeLocations;
};

/** Tuning for AFPSRMission_CarryNoHit: carry an orb without taking damage for N seconds. */
UCLASS(meta = (DisplayName = "Tuning: CarryNoHit"))
class FPSROGUELITE_API UFPSRMissionTuning_CarryNoHit : public UFPSRMissionTuning
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Tuning")
	TSubclassOf<AFPSRMissionOrb> OrbClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Tuning")
	float RequiredCarrySeconds = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Tuning")
	float CarryHeight = 120.0f;
};

/** Tuning for AFPSRMission_DefeatFleeing: kill a high-HP target that flees while a player is near. */
UCLASS(meta = (DisplayName = "Tuning: DefeatFleeing"))
class FPSROGUELITE_API UFPSRMissionTuning_DefeatFleeing : public UFPSRMissionTuning
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Tuning")
	TSubclassOf<AFPSRMissionFleeTarget> TargetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Tuning")
	float FleeSpeed = 350.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Tuning")
	float FleeTriggerRange = 900.0f;
};

/** Tuning for AFPSRMission_LimitedVision: endure a restricted field of view for N seconds. */
UCLASS(meta = (DisplayName = "Tuning: LimitedVision"))
class FPSROGUELITE_API UFPSRMissionTuning_LimitedVision : public UFPSRMissionTuning
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mission Tuning", meta = (ClampMin = "0.0"))
	float RequiredSeconds = 20.0f;
};
