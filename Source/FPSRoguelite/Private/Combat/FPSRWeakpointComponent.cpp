// Copyright Epic Games, Inc. All Rights Reserved.
#include "Combat/FPSRWeakpointComponent.h"
#include "FPSRCollisionChannels.h"

UFPSRWeakpointComponent::UFPSRWeakpointComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	InitSphereRadius(30.0f);
	SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SetCollisionObjectType(ECC_FPSRWeakpoint);
	SetCollisionResponseToAllChannels(ECR_Ignore);
	// Projectiles are ECC_WorldDynamic query spheres; overlap them so a direct projectile hit on a weakpoint that
	// misses the body capsule (e.g. a protruding boss core) still registers. Line-trace paths find this component
	// by object type regardless of response, so they need no response entry.
	SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
	SetGenerateOverlapEvents(true);
	SetCanEverAffectNavigation(false);
	bHiddenInGame = true;
}
