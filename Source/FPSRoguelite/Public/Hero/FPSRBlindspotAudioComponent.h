// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "FPSRBlindspotAudioComponent.generated.h"

class USoundBase;
class AFPSREnemyBase;

/** Local-only blind-spot threat audio (Game.MD §2-14): warns the owning player, by spatialized sound only,
 *  when a swarm enemy is close AND outside the player's forward view (behind/to the side). No visual indicator
 *  (the §2-14 visual threat marker was intentionally dropped — audio only).
 *
 *  First-principles (Game.MD §1/§5): a server computing per-player blind-spot angles for hundreds of enemies
 *  would blow the replication/CPU budget, and blind-spot awareness is purely local cosmetic — so this is a
 *  client-local throttled poll. A cheap distance-squared cull runs first (only S0/S1 near enemies survive,
 *  §5-1), then a single dot product decides "outside the forward cone". Direction is conveyed by playing the
 *  cue at a fixed short distance in the threat's direction so the engine spatializes it relative to the
 *  listener (behind-you panning for free) — no manual stereo math, loudness independent of enemy distance.
 *  Reuses the UFPSRPlayerFeedbackComponent pattern (local, non-replicated, IsLocalView-gated) but polls
 *  rather than being event-driven (no server event exists for "an enemy is behind you"). */
UCLASS(ClassGroup = (FPSR), meta = (BlueprintSpawnableComponent))
class FPSROGUELITE_API UFPSRBlindspotAudioComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSRBlindspotAudioComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if !UE_BUILD_SHIPPING
	/** Debug: force a warning cue at AngleDeg from the local camera forward (no enemy needed). */
	void DebugForceWarn(float AngleDeg);
#endif

protected:
	/** True only when the owner is a pawn controlled by THIS machine's local player. */
	bool IsLocalView() const;

	/** Scan nearby enemies and, if a close one sits outside the forward view cone, play a directional warning
	 *  cue (subject to WarnCooldown). Called from the throttled tick. */
	void ScanAndWarn();

	/** Play the spatialized warning cue toward ThreatDirection (unit, world) from the listener view location. */
	void PlayWarningCue(const FVector& ViewLocation, const FVector& ThreatDirection);

	/** Sound played when a blind-spot threat is detected. Spatialized (set Attenuation on the asset for falloff).
	 *  Designer-assigned in BP; null = feature inert. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "FPSR|Blindspot")
	TObjectPtr<USoundBase> WarningSound;

	/** Only enemies within this range are considered (cm). Doubles as the Significance gate — far S2/S3 enemies
	 *  fail this cull before any angle test (§5-1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "FPSR|Blindspot", meta = (ClampMin = "0.0"))
	float ThreatRadius = 1200.0f;

	/** Half-angle (deg) of the player's forward "safe" cone. An enemy whose direction is beyond this from the
	 *  camera forward counts as a blind-spot threat (behind/side). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "FPSR|Blindspot", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float BlindspotHalfAngleDeg = 75.0f;

	/** Minimum seconds between warning cues while a threat persists (over-fire suppression). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "FPSR|Blindspot", meta = (ClampMin = "0.0"))
	float WarnCooldown = 1.5f;

	/** Seconds between scans (throttle; the tick accumulates delta and scans at this cadence, not every frame). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "FPSR|Blindspot", meta = (ClampMin = "0.0"))
	float ScanInterval = 0.2f;

	/** Distance (cm) in front of the listener at which the cue is placed along the threat direction. Keeps the
	 *  cue audibly consistent regardless of how far the enemy is — it communicates direction, not distance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "FPSR|Blindspot", meta = (ClampMin = "1.0"))
	float CueDistance = 200.0f;

private:
	/** Accumulated time since the last scan (throttle). */
	float ScanAccumulator = 0.0f;

	/** World time of the last warning cue (cooldown gate). */
	float LastWarnTime = -1000.0f;
};
