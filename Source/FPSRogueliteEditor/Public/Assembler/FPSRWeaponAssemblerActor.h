// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "FPSRWeaponAssemblerActor.generated.h"

class UFPSRWeaponDataAsset;
class USkeletalMeshComponent;
class UStaticMeshComponent;

/** Editor-only preview actor for the Weapon Part Assembler tool. Spawned transient by the tool from a weapon DA:
 *  body = SkeletalMeshComponent, each WeaponParts1P entry = a StaticMeshComponent attached to the body that the
 *  designer positions with the viewport gizmo. Never saved (Transient).
 *
 *  Socket/runtime assumption: FPSRWeaponAssemblerHelpers::CaptureToSockets bakes each part component's relative
 *  transform (relative to BodyMesh) straight into a body-mesh socket on the root bone, on the assumption that the
 *  root bone sits at the skeletal mesh COMPONENT origin (identity) — true for this project's Synty weapon bodies.
 *  If a future body mesh's root bone isn't at the component origin, captured placements will read offset in PIE and
 *  need a root-bone ref-transform correction (not handled here). */
UCLASS(NotPlaceable, Transient, NotBlueprintable)
class AFPSRWeaponAssemblerActor : public AActor
{
	GENERATED_BODY()
public:
	AFPSRWeaponAssemblerActor();

	UPROPERTY(VisibleAnywhere, Category="Assembler")
	TObjectPtr<USkeletalMeshComponent> BodyMesh;

	/** Source weapon DA this preview was built from. */
	UPROPERTY(VisibleAnywhere, Category="Assembler")
	TObjectPtr<UFPSRWeaponDataAsset> SourceDA;

	/** Index-aligned to SourceDA->WeaponParts1P: one static-mesh component per part (designer moves these). */
	UPROPERTY(VisibleAnywhere, Category="Assembler")
	TArray<TObjectPtr<UStaticMeshComponent>> PartComponents;

	/** Build the body + part components from the DA (call right after spawn). Loads soft refs synchronously (editor). */
	void BuildFromDA(UFPSRWeaponDataAsset* DA);
};
