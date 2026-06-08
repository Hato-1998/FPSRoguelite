// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "Hero/FPSRFeedbackTypes.h"
#include "FPSRPlayerFeedbackComponent.generated.h"

class APawn;
class AController;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFPSRHitMarker, EFPSRHitMarkerType, MarkerType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFPSRThreatsUpdated, const TArray<FFPSRThreatDir>&, Threats);

/** Local-only player feedback (Game.MD §2-14): hit markers + screen-edge threat indicator detection.
 *  Purely cosmetic and client-local — NOT replicated. The WBP HUD binds OnHitMarker / OnThreatsUpdated.
 *
 *  Hit markers: weapon GAs call NotifyHitConfirmed() — the local trace fires an immediate "Hit" on the owning
 *  client (client-predicted), while the server delivers Crit/Kill upgrades via the owning PlayerController.
 *  Threats: a throttled scan (locally-controlled pawn only) finds alive enemies inside ThreatRadius but OUTSIDE
 *  the view cone (blind spots — behind/side) and reports their screen-edge directions.
 *
 *  First-principles: single-consumer (the local HUD), local, cosmetic → a pawn component + delegates is the
 *  minimal correct structure (no replication, no message bus). GameplayMessageSubsystem (§3) is the upgrade
 *  path if feedback gains multiple independent consumers. */
UCLASS(ClassGroup = (FPSR), meta = (BlueprintSpawnableComponent))
class FPSROGUELITE_API UFPSRPlayerFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSRPlayerFeedbackComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Local feedback: broadcast a hit-marker pulse. Called by the weapon GAs (local trace = Hit) and by the
	 *  owning PlayerController's server-confirm RPC (Crit / Kill). No-op off the local view. */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Feedback")
	void NotifyHitConfirmed(EFPSRHitMarkerType MarkerType);

	/** Latest out-of-view threats from the most recent scan (also pushed via OnThreatsUpdated). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Feedback")
	const TArray<FFPSRThreatDir>& GetActiveThreats() const { return ActiveThreats; }

	UPROPERTY(BlueprintAssignable, Category = "FPSR|Feedback")
	FOnFPSRHitMarker OnHitMarker;

	UPROPERTY(BlueprintAssignable, Category = "FPSR|Feedback")
	FOnFPSRThreatsUpdated OnThreatsUpdated;

protected:
	virtual void BeginPlay() override;

	/** True only when the owner is a pawn currently controlled by THIS machine's local player (the only place
	 *  threat scanning / hit markers are meaningful). False on the dedicated server and for remote proxies. */
	bool IsLocalView() const;

	/** Re-evaluate tick gating when possession changes — possession (and client controller replication) can
	 *  arrive after BeginPlay, so the local-view tick can't be latched at BeginPlay time. */
	UFUNCTION()
	void HandleControllerChanged(APawn* InPawn, AController* OldController, AController* NewController);

	/** Throttled scan: collect alive, visible enemies inside ThreatRadius but outside the view cone. */
	void ScanThreats();

	/** Stop scanning and clear any shown threats (broadcasts an empty set) — used when the local view is lost
	 *  (unpossess / becomes a remote proxy) so a bound HUD can't keep stale indicators on screen. */
	void DisableAndClearThreats();

	/** Radius (cm) within which an out-of-view enemy counts as a threat. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Feedback", meta = (ClampMin = "1.0"))
	float ThreatRadius = 1500.0f;

	/** Half-angle (deg) of the forward "safe" cone; enemies outside it (blind spots) are flagged as threats. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Feedback", meta = (ClampMin = "1.0", ClampMax = "179.0"))
	float ThreatViewHalfAngleDeg = 60.0f;

	/** Max threats reported per scan (strongest by proximity), bounding UI churn. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Feedback", meta = (ClampMin = "1"))
	int32 MaxThreats = 8;

	/** Seconds between threat scans (throttle — the indicator is coarse screen-edge markers). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Feedback", meta = (ClampMin = "0.02"))
	float ThreatScanInterval = 0.12f;

private:
	/** Latest scan result (sorted by Severity01 desc, capped to MaxThreats). */
	UPROPERTY(Transient)
	TArray<FFPSRThreatDir> ActiveThreats;
};
