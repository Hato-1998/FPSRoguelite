// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/FPSRBossGameplayAbility.h"
#include "GameplayTagContainer.h"
#include "FPSRBossGA_SweepLaser.generated.h"

class AFPSRBossBase;

/** BOSS1 pattern 2 — the sweeping laser (`Docs/Specs/BOSS1_AbilityPatternFramework.md` §5-5).
 *  Beams radiate from the boss and rotate; the counterplay is a jump, and terrain does not block them
 *  (user decision 2026-09-02 — "map-wide"). Beam count scales with the phase.
 *
 *  🔴 **Warning first.** `Docs/SSOT/Enemy.md` §2-6 requires a charge delay plus a warning indicator for anything
 *  hitscan-like ("no unfair bullet-hell"). The beams render during `WarmupSeconds` but deal nothing, and the
 *  existing per-player ranged-warning RPC is bracketed around that window — zero new RPCs, the same reuse the
 *  ranged swarm does.
 *
 *  🔴 **Hit rule = exposure + latch, not a bare edge.** Anding the crossing edge with the gates (warmup over,
 *  not airborne) loses a whole pass whenever the gate opens WHILE the player is already inside the band — which
 *  happens in two ordinary situations: warmup ending on top of someone, and landing inside a beam. The second one
 *  directly contradicts "jump INTO the beam and you get hit". So exposure is evaluated every frame and a per-pawn
 *  latch, cleared on leaving the band, is what keeps it to one hit per pass.
 *
 *  🔴 **Damage is deferred**, not applied on the spot — see AFPSRBossBase::ServerScheduleLaserHit. A client's jump
 *  input reaches the server a latency late, and this is a mechanic whose entire counterplay is one timed input. */
UCLASS()
class FPSROGUELITE_API UFPSRBossGA_SweepLaser : public UFPSRBossGameplayAbility
{
	GENERATED_BODY()

public:
	UFPSRBossGA_SweepLaser();

protected:
	virtual void ServerBeginExecute() override;
	virtual bool ServerTickExecute(float DeltaSeconds) override;
	virtual void ServerEndExecute() override;

	/** The beam exists but stands STILL and deals nothing for this long. This is the pattern's own telegraph, on top
	 *  of the base's system wind-up: a beam born at a random cardinal can land on someone, and this is the window
	 *  that lets them walk out of it. Required in spirit by Enemy.md §2-6 — see class doc. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Laser", meta = (ClampMin = "0.0"))
	float BeamGraceSeconds = 1.5f;

	/** Sweep speed per phase (index = phase - 1; phases past the end reuse the last entry). An ARRAY rather than a
	 *  base value times a multiplier, so a designer sets the number they actually want to feel at each phase instead
	 *  of solving for it. Empty = fall back to 30 deg/s. Positive sweeps clockwise. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Laser")
	TArray<float> AngularSpeedByPhase = { 30.0f, 45.0f, 60.0f };

	/** Beam count = clamp(phase * this, 1, MaxBeams) — so "one more beam per phase" is authored, not hardcoded. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Laser", meta = (ClampMin = "1"))
	int32 BeamsPerPhase = 1;

	/** Upper bound on simultaneous beams. A number, not a C++ constant, so a boss with more phases stays authorable. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Laser", meta = (ClampMin = "1"))
	int32 MaxBeams = 5;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Laser", meta = (ClampMin = "0.1"))
	float BeamHalfWidthDeg = 1.5f;

	/** How many full turns before the pattern ends. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Laser", meta = (ClampMin = "0.1"))
	float Revolutions = 2.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Laser")
	float Damage = 30.0f;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Laser")
	FGameplayTag DamageType;

	/** Radius the beam visually starts at = the boss body's radius. NOT a safe zone: the boss capsule blocks players
	 *  from standing inside it, so the corresponding hit-test term is satisfied by everyone who can exist. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Laser", meta = (ClampMin = "0.0"))
	float InnerRadiusCm = 1250.0f;

	/** 🔴 COSMETIC ONLY. The dodge is decided by "were you airborne", not by comparing your feet to this height.
	 *  A geometric test would make anyone standing on a >= 60 cm prop permanently safe, which contradicts the user's
	 *  "the only dodge is a jump", and the engine-default jump apex (~90 cm) leaves a window too thin to aim at. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Laser", meta = (ClampMin = "0.0"))
	float BeamVisualHeightCm = 60.0f;

	/** Player capsule radius used for the angular-width correction. Read from the pawn when available; this is the
	 *  fallback for a pawn without a capsule. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Boss|Laser", meta = (ClampMin = "0.0"))
	float FallbackCapsuleRadiusCm = 40.0f;

private:
	/** Per-pawn beam-pass state. PrevRel is the previous frame's folded relative angle; bLatched marks "this pass
	 *  has already scored", cleared when the pawn leaves the band so the NEXT pass can score again. */
	struct FBeamTrack
	{
		float PrevRel = 0.0f;
		bool bLatched = false;
		bool bSeeded = false;
	};
	TMap<TWeakObjectPtr<APawn>, FBeamTrack> BeamTrackByPawn;

	/** Beam count and period are frozen at activation. If the phase rose mid-sweep the period would change under the
	 *  tracked angles, making every stored PrevRel a value from a different domain. Extra beams arrive next cast. */
	int32 ActiveBeamCount = 1;
	/** Grace, damage window and end are ALL measured against the pattern clock. Keeping a separate accumulator here
	 *  would be a second freeze-correct source that can still disagree with the first. */
	float SweepDurationSeconds = 0.0f;
	float StartAngleDeg = 0.0f;
	float StartClock = 0.0f;
	float GraceEndClock = 0.0f;
	float ActiveSpeedDegPerSec = 30.0f;
	bool bWarningActive = false;

	/** Bearing (deg) of Pawn around the boss, and its distance — one helper so the hit test and the start-angle
	 *  rule cannot disagree about what "the player's angle" means. */
	static bool GetPawnBearing(const AFPSRBossBase* Boss, const APawn* Pawn, float& OutDeg, float& OutDistanceCm);

	/** Bracket the existing per-player ranged warning. Must be called with false on EVERY exit path — a warning left
	 *  on sticks on the HUD forever (the ranged swarm's own abort path exists for exactly this reason). */
	void SetWarningActive(AFPSRBossBase* Boss, bool bActive);
};
