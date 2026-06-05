// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "Run/Mission/FPSRMissionTypes.h"
#include "FPSRMissionActor.generated.h"

class AFPSRMissionActor;
class UFPSRMissionDataAsset;

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
