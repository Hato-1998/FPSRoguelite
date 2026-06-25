// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "FPSRDoor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UFPSREnemyHealthComponent;

/** Destructible door barrier for the progressive room-spawn system. A pure physical gate: shoot the leaves to break
 *  them, which opens the passage. It is NOT an enemy — it carries a UFPSREnemyHealthComponent (so EVERY weapon path
 *  damages it through the existing combat bridge, zero new damage code) but with bCountsAsKill=false, so breaking it
 *  fires no kill credit / on-kill fragments / lifesteal (Combat: FPSRCombat::ApplyDamage).
 *
 *  C++ = gameplay only (collision, health, broken replication). The mesh and the broken presentation are content:
 *  the designer assigns meshes in BP and implements OnDoorBroken (anim / Chaos / VFX) — the base never changes when
 *  the visual does (Game.MD §2 — no hardcoded asset paths). */
UCLASS()
class FPSROGUELITE_API AFPSRDoor : public AActor
{
	GENERATED_BODY()

public:
	AFPSRDoor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "FPSR|Door")
	bool IsBroken() const { return bBroken; }

protected:
	virtual void BeginPlay() override;

	/** Server: door health reached zero — mark broken (replicated), open the passage, run the BP presentation. */
	UFUNCTION()
	void HandleBroken(AActor* DeadActor, AActor* Killer);

	UFUNCTION()
	void OnRep_Broken();

	/** Broken-door presentation (anim open / Chaos shatter / VFX) — BP-implemented so the visual can change without
	 *  a C++ rebuild. Fires on the server and on clients (via OnRep). */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Door")
	void OnDoorBroken();

	/** Breakable door leaves (상·하단). Object type ECC_FPSRPlayerPawn: gathered by EVERY weapon object-query so all
	 *  weapon types damage it via HealthComponent (no new code), blocked by players AND enemies (both block the
	 *  player channel), and dash-proof (dash only ignores ECC_Pawn). Designer assigns the mesh(es) in BP; attach
	 *  the top/bottom leaves under this so a single break hides them together (and OnDoorBroken can animate each). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPSR|Door")
	TObjectPtr<UStaticMeshComponent> DoorMesh;

	/** Door frame (문틀) — an INERT wall: object type WorldStatic, so weapon object-queries never gather it (shots
	 *  just stop on it as cover, no damage) while it still blocks movement. Optional (leave empty for a frameless
	 *  door). NOT hidden when the door breaks — it is a sibling of DoorMesh, not a child. Designer assigns the mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPSR|Door")
	TObjectPtr<UStaticMeshComponent> FrameMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPSR|Door")
	TObjectPtr<UFPSREnemyHealthComponent> HealthComponent;

	/** Door durability (HP), applied to HealthComponent at BeginPlay. Tune per door BP (~ rifle N shots). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Door", meta = (ClampMin = "1.0"))
	float Durability = 150.0f;

	UPROPERTY(ReplicatedUsing = OnRep_Broken)
	bool bBroken = false;

private:
	/** Hide + disable collision on the door leaves ONLY (the frame, a sibling, stays solid and visible). Shared by
	 *  the server break path and the client OnRep so both open the passage identically. */
	void ApplyBrokenState();
};
