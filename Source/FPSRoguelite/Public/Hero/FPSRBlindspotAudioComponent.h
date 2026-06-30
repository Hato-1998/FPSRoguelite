// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "FPSRBlindspotAudioComponent.generated.h"

class USoundBase;
class USoundAttenuation;
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

	/** Play the spatialized warning cue toward ThreatDirection (unit, horizontal world dir) from the listener view
	 *  location. PitchMultiplier conveys the threat's ELEVATION (above = higher pitch, below = lower) since stereo
	 *  panning only resolves the horizontal plane (B9 — vertical blind spots). */
	void PlayWarningCue(const FVector& ViewLocation, const FVector& ThreatDirection, float PitchMultiplier);

	/** Sound played when a blind-spot threat is detected. Designer-assigned in BP; null = feature inert. The cue
	 *  is force-spatialized (see WarningAttenuation) so it pans by direction even if this asset is a plain 2D
	 *  SoundWave with no attenuation of its own. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "FPSR|Blindspot")
	TObjectPtr<USoundBase> WarningSound;

	/** Optional attenuation override for the cue. When null, a built-in spatialized attenuation (left/right
	 *  panning, falloff = SpatializeFalloffDistance) is used so directional audio works with zero asset setup.
	 *  Assign an asset (e.g. with HRTF) to customize — this is where U13 plugs in front/back binaural polish. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "FPSR|Blindspot")
	TObjectPtr<USoundAttenuation> WarningAttenuation;

	/** Only enemies within this range are considered (cm). Doubles as the Significance gate — far S2/S3 enemies
	 *  fail this cull before any angle test (§5-1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "FPSR|Blindspot", meta = (ClampMin = "0.0"))
	float ThreatRadius = 1200.0f;

	/** Half-angle (deg) of the player's forward "safe" cone. An enemy whose direction is beyond this from the
	 *  camera forward counts as a blind-spot threat (behind/side). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "FPSR|Blindspot", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float BlindspotHalfAngleDeg = 75.0f;

	/** Elevation (deg, above OR below the listener's horizontal plane) beyond which an enemy is a VERTICAL blind
	 *  spot even if it's horizontally in front — it's off the top/bottom of the screen (B9). The cue's pitch then
	 *  conveys the height (stereo panning can't). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "FPSR|Blindspot", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float VerticalBlindspotAngleDeg = 45.0f;

	/** Cue pitch multiplier for a threat directly BELOW (-90 deg elevation) / directly ABOVE (+90 deg). The actual
	 *  pitch lerps between these by the threat's elevation, so above-you reads higher and below-you reads lower (B9). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "FPSR|Blindspot", meta = (ClampMin = "0.1"))
	float BelowThreatPitch = 0.8f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "FPSR|Blindspot", meta = (ClampMin = "0.1"))
	float AboveThreatPitch = 1.35f;

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

	/** Falloff distance (cm) of the built-in spatialized attenuation (used when WarningAttenuation is null).
	 *  Generous so the near cue stays audible while still panning by direction. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "FPSR|Blindspot", meta = (ClampMin = "1.0"))
	float SpatializeFalloffDistance = 5000.0f;

private:
	/** Resolve the attenuation to play the cue with: the designer override, else a lazily-built, cached
	 *  spatialized attenuation configured from the tunables (forces panning regardless of the sound asset). */
	USoundAttenuation* ResolveAttenuation();

	/** Accumulated time since the last scan (throttle). */
	float ScanAccumulator = 0.0f;

	/** World time of the last warning cue (cooldown gate). */
	float LastWarnTime = -1000.0f;

	/** Cached built-in spatialized attenuation (created on first use when WarningAttenuation is null). */
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> RuntimeAttenuation;
};
