// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"
#include "Run/Mission/FPSRMissionTypes.h"
#include "FPSRMissionActor.generated.h"

class AFPSRMissionActor;
class UFPSRMissionDataAsset;
class AFPSRMissionPointSet;
class UFPSRMissionTuning;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMissionEndedNative, AFPSRMissionActor* /*Mission*/, bool /*bSuccess*/);

/** Server-authoritative replicated mission actor base class.
 *  Subclasses override lifecycle hooks (OnMissionActivated, OnMissionTickServer, OnMissionEnded)
 *  to implement specific mission types (hold zone, defeat enemies, etc.).
 *  The director owns the actor and destroys it after EndMissionInternal broadcasts. */
UCLASS()
class FPSROGUELITE_API AFPSRMissionActor : public AActor
{
	GENERATED_BODY()

public:
	AFPSRMissionActor();

	/** Server: activate this mission with the given data. Sets state to Active and begins the mission. */
	void ServerActivate(UFPSRMissionDataAsset* InData);

	/** Server: mark mission as Completed and trigger end callbacks. */
	void CompleteMission();

	/** Server: mark mission as Failed and trigger end callbacks. */
	void FailMission();

	UFUNCTION(BlueprintPure, Category = "FPSR|Mission")
	EFPSRMissionState GetMissionState() const { return MissionState; }

	UFUNCTION(BlueprintPure, Category = "FPSR|Mission")
	float GetMissionProgress() const { return MissionProgress; }

	UFUNCTION(BlueprintPure, Category = "FPSR|Mission")
	const UFPSRMissionDataAsset* GetMissionData() const { return MissionData; }

	/** Native multicast delegate for the director to subscribe to mission end events. */
	FOnMissionEndedNative OnMissionEndedNative;

	/** Whether this mission consumes a designer-placed point set (the director selects one for it via CDO). */
	virtual bool UsesPointSet() const { return false; }

	/** Server: the director assigns the selected point set before ServerActivate (no-op by default). */
	virtual void AssignPointSet(AFPSRMissionPointSet* InSet) {}

	/** §2-8-1: the tuning subclass this mission type reads its parameters from (e.g. UFPSRMissionTuning_HoldZone).
	 *  Called on the CDO by the mission DataAsset's IsDataValid to check Tuning's type. nullptr (base default) =
	 *  this mission type has no tuning object (or hasn't been migrated yet). Override per mission subclass. */
	virtual TSubclassOf<UFPSRMissionTuning> GetExpectedTuningClass() const { return nullptr; }

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** Server: called when mission is first activated (before first OnMissionTickServer). */
	virtual void OnMissionActivated() {}

	/** Server: called each tick while mission is Active. */
	virtual void OnMissionTickServer(float DeltaSeconds) {}

	/** Server: called after CompleteMission or FailMission, before broadcast. */
	virtual void OnMissionEnded(bool bSuccess) {}

	/** Server: set the mission progress (0..1) and replicate it. */
	void SetMissionProgress(float NewProgress);

	/** Effective mission-zone radius: returns the `FPSR.Mission.ZoneRadius` console override when > 0, else InRadius.
	 *  Lets designers live-tune hold/moving zone size during PIE (`FPSR.Mission.ZoneRadius 800`) without a recompile. */
	float ResolveZoneRadius(float InRadius) const;

	/** §2-8-1: the DataAsset's polymorphic Tuning object (untyped), or nullptr if MissionData is unset or Tuning
	 *  was never assigned. Prefer the typed GetTuning<T>() below; this is its untyped primitive. */
	const UFPSRMissionTuning* GetTuningBase() const;

	/** §2-8-1: the authored tuning typed as T, or T's CDO (its C++ defaults) when the DataAsset's Tuning is null
	 *  or a different subclass — a single source of defaults (the tuning subclass). Read sites use this instead of
	 *  per-mission fallback fields, with no null checks. IsDataValid warns when a DataAsset leaves Tuning unset. */
	template <typename T>
	const T& GetTuning() const
	{
		static_assert(TPointerIsConvertibleFromTo<T, UFPSRMissionTuning>::Value, "T must derive from UFPSRMissionTuning");
		if (const T* Authored = Cast<T>(GetTuningBase()))
		{
			return *Authored;
		}
		return *GetDefault<T>();
	}

	UFUNCTION()
	void OnRep_MissionState();

	// Replicated properties
	UPROPERTY(ReplicatedUsing = OnRep_MissionState)
	EFPSRMissionState MissionState = EFPSRMissionState::Inactive;

	UPROPERTY(Replicated)
	float MissionProgress = 0.0f;

	UPROPERTY(Replicated)
	TObjectPtr<UFPSRMissionDataAsset> MissionData = nullptr;

private:
	/** Server: internal end-of-mission cleanup (calls OnMissionEnded, broadcasts, does not Destroy). */
	void EndMissionInternal(bool bSuccess);

	float ElapsedTime = 0.0f;
};
