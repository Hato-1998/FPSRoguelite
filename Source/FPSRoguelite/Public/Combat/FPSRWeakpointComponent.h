// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Components/SphereComponent.h"
#include "FPSRWeakpointComponent.generated.h"

/** Designer-placed weakpoint zone (headshot / weak spot, U3a). Attach to any enemy/boss actor in Blueprint and
 *  size/position it in the viewport; a hit inside it multiplies the shot's damage by DamageMultiplier. Generic
 *  (not Enemy-only): the U3 boss base inherits the same component. Multiple per actor allowed (1-3 on swarm,
 *  more on bosses) — the damage paths take the HIGHEST multiplier among the weakpoints a shot intersects.
 *
 *  Collision: object type ECC_FPSRWeakpoint, QueryOnly, all responses Ignore EXCEPT ECC_WorldDynamic = Overlap
 *  (so a projectile's WorldDynamic collision sphere fires an overlap event on it). Responds to NO movement /
 *  ground / separation / flow-field / visibility query, so placing one never affects anything but damage. */
UCLASS(ClassGroup=(FPSR), meta=(BlueprintSpawnableComponent), hidecategories=(Object, LOD, Lighting, TextureStreaming))
class FPSROGUELITE_API UFPSRWeakpointComponent : public USphereComponent
{
	GENERATED_BODY()
public:
	UFPSRWeakpointComponent();

	/** Damage multiplier applied to a shot that lands on this zone (>= 1.0). Designer-tunable per zone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "FPSR|Weakpoint", meta = (ClampMin = "1.0"))
	float DamageMultiplier = 2.0f;
};
