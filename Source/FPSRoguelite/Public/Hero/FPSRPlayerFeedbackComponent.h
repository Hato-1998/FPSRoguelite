// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "Hero/FPSRFeedbackTypes.h"
#include "FPSRPlayerFeedbackComponent.generated.h"

class APawn;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFPSRHitMarker, EFPSRHitMarkerType, MarkerType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFPSRDamageDirection, float, AngleDeg);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnFPSRRangedTargetWarning, const TArray<float>&, AngleDegs);

/** Local-only player feedback (Game.MD §2-14): purely event-driven, NOT replicated, no tick.
 *  - Hit marker ("I hit an enemy"): crosshair confirm from the weapon GA / server confirm.
 *  - Damage direction ("I got hit"): the server (ApplyContactDamage) sends the instigator's world location to
 *    the owning client via the PlayerController; this computes the camera-relative angle and broadcasts it.
 *  - Ranged target warning ("a ranged enemy is aiming at me", §2-6 pre-warning): producer = ranged enemy AI
 *    (follow-up); a debug command (FPSR.TestRangedWarn) drives it until then.
 *
 *  First-principles: single-consumer local HUD, local, cosmetic → a pawn component + delegates is the minimal
 *  correct structure (no replication, no message bus). GameplayMessageSubsystem (§3) is the upgrade path if
 *  feedback gains multiple independent consumers. */
UCLASS(ClassGroup = (FPSR), meta = (BlueprintSpawnableComponent))
class FPSROGUELITE_API UFPSRPlayerFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFPSRPlayerFeedbackComponent();

	/** Ticks ONLY while a ranged-target warning is active — recomputes the camera-relative angle each frame so
	 *  the warning indicator tracks as the player turns / the source moves. Idle otherwise (event-driven). */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Broadcast a hit-marker pulse (weapon GA local trace = Hit; PlayerController server confirm = Crit/Kill). */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Feedback")
	void NotifyHitConfirmed(EFPSRHitMarkerType MarkerType);

	/** Local: incoming damage came from InstigatorWorldLocation — broadcast its camera-relative angle. Called by
	 *  the owning PlayerController's ClientNotifyDamageFrom RPC (and the debug command). */
	void ReceiveDamageFromWorld(const FVector& InstigatorWorldLocation);

	/** Local: ranged enemy SourceId at SourceWorldLocation began (bActive) / ended (!bActive) targeting this
	 *  player (§2-6 pre-warning). Tracks multiple concurrent sources keyed by SourceId; a moving source is
	 *  re-sent by the producer with an updated location. Called by ClientNotifyRangedTarget (and the debug cmd). */
	void ReceiveRangedTarget(int32 SourceId, const FVector& SourceWorldLocation, bool bActive);

	UPROPERTY(BlueprintAssignable, Category = "FPSR|Feedback")
	FOnFPSRHitMarker OnHitMarker;

	/** "I got hit from this direction" — AngleDeg = signed yaw vs camera forward (0 = ahead, + = right, - = left). */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Feedback")
	FOnFPSRDamageDirection OnDamageDirection;

	/** "Ranged enemies are targeting me" — one camera-relative angle per active source (empty = none). Re-fired
	 *  every frame while any warning is active so the indicators track the player turning / sources moving. */
	UPROPERTY(BlueprintAssignable, Category = "FPSR|Feedback")
	FOnFPSRRangedTargetWarning OnRangedTargetWarning;

protected:
	/** True only when the owner is a pawn controlled by THIS machine's local player. */
	bool IsLocalView() const;

	/** Signed yaw (deg, -180..180; 0 = ahead, + = right, - = left) of WorldLocation vs the camera forward.
	 *  Returns false if there is no local view to measure against. */
	bool ComputeCameraRelativeAngle(const FVector& WorldLocation, float& OutAngleDeg) const;

	/** Recompute every active source's camera-relative angle and broadcast OnRangedTargetWarning (empty = none). */
	void BroadcastRangedWarnings();

private:
	/** Active ranged-target sources keyed by a producer-supplied id (e.g. the enemy's unique id). The tick
	 *  recomputes each one's camera-relative angle so all warnings track the player turning / the sources moving
	 *  (the producer re-sends a source with an updated location to move it). */
	TMap<int32, FVector> ActiveRangedSources;
};
