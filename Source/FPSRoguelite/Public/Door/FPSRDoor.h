// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Destructible/FPSRDestructible.h"
#include "GameplayTagContainer.h"
#include "FPSRDoor.generated.h"

class UStaticMeshComponent;

/** Destructible door barrier for the progressive room-spawn system (AFPSRDestructible subclass — ADR 0010 D7
 *  generalized the door into the base; this class is now just the door-specific leaf/frame mesh split + multimap
 *  streaming). A pure physical gate: shoot the leaves to break them, which opens the passage. It is NOT an enemy —
 *  see AFPSRDestructible's header for the HealthComponent/SetCountsAsKill(false) rationale (unchanged by this
 *  refactor: EVERY weapon path damages it through the existing combat bridge, zero new damage code, but breaking
 *  it fires no kill credit / on-kill fragments / lifesteal — Combat: FPSRCombat::ApplyDamage).
 *
 *  C++ = gameplay only (collision, health, broken replication), inherited from AFPSRDestructible unchanged. The
 *  mesh and the broken presentation are content: the designer assigns meshes in BP and implements OnDoorBroken
 *  (anim / Chaos / VFX) — the base never changes when the visual does (Game.MD §2 — no hardcoded asset paths). */
UCLASS()
class FPSROGUELITE_API AFPSRDoor : public AFPSRDestructible
{
	GENERATED_BODY()

public:
	AFPSRDoor();

	/** The map this door streams in when broken (multimap Tier 0). Set per door BP to the adjacent sublevel's MapId.
	 *  On break (server), HandleBrokenAuthority asks the MapStreamSubsystem to stream this map in (S3). Unset = a
	 *  non-streaming door (a plain room gate, existing behavior). The door lives in the persistent level so its
	 *  broken state + this target reach late joiners (persistent actors are always relevant). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPSR|Door")
	FGameplayTag TargetMapId;

	/** The authored streaming sublevel (short package name, e.g. "L_MapB") this door streams in on break — must be a
	 *  streaming sublevel of the persistent map. Paired with TargetMapId (the logical key). Unset = non-streaming gate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "FPSR|Door")
	FName TargetLevelName;

	/** Which map this door streams in when broken (unset = non-streaming gate). */
	const FGameplayTag& GetTargetMapId() const { return TargetMapId; }

protected:
	/** Server: open the flow-field seam this door blocked + kick off the target map's stream-in, THEN (Super) pay
	 *  out any Rewards authored on this door. Multimap content itself was dropped by ADR 0010, but the streaming
	 *  path stays wired and unused (a door with TargetMapId unset — all current doors — simply no-ops that half). */
	virtual void HandleBrokenAuthority(AActor* Breaker) override;

	/** Leaf only: disable collision + hide DoorMesh (propagated to its sub-meshes). Does NOT call Super — the base
	 *  implementation hides the WHOLE actor, which would also take down FrameMesh (a sibling that must stay solid/
	 *  visible: the frame is a wall, not a leaf). Not SetActorHiddenInGame/SetActorEnableCollision on the actor. */
	virtual void ApplyBrokenState() override;

	/** Reverse of ApplyBrokenState() above (F2): re-enable collision + unhide DoorMesh (propagated to its
	 *  sub-meshes), back to its pre-broken QueryOnly/visible state. Does NOT call Super, same reason as
	 *  ApplyBrokenState — the frame was never disabled, so Super::ClearBrokenState() would be reversing state this
	 *  subclass never touched. */
	virtual void ClearBrokenState() override;

	/** Fires OnDoorBroken only — does NOT call Super (the base's OnDestructibleBroken), so BP_Door's existing event
	 *  binding is the only thing that fires and nothing double-fires. */
	virtual void FireBrokenPresentation() override;

	/** Fires OnDoorDamageStage only — same non-additive override shape as FireBrokenPresentation. */
	virtual void FireDamageStagePresentation(int32 StageIndex, float HealthPct, float Threshold) override;

	/** Broken-door presentation (anim open / Chaos shatter / VFX) — BP-implemented so the visual can change without
	 *  a C++ rebuild. Fires on the server and on clients (via OnRep, inherited from AFPSRDestructible). */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Door")
	void OnDoorBroken();

	/** Progressive-damage presentation (crack / shake / tint), fired once per crossed threshold as the door takes
	 *  damage — BP-implemented so the visual is content. StageIndex is 0-based into DamageStageThresholds (0 =
	 *  first/highest threshold), so the BP can escalate intensity. Fires on the server and on clients (via OnRep). */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Door")
	void OnDoorDamageStage(int32 StageIndex, float HealthPct, float Threshold);

	/** Breakable door leaves (상·하단). Object type ECC_FPSRDestructible: gathered by EVERY weapon damage query so all
	 *  weapon types damage it via HealthComponent (no new code), blocked by players AND enemies (the channel defaults
	 *  to Block), STOPS an in-flight projectile, and immune to the ECC_Pawn pass-through windows (grace / downed only
	 *  ignore ECC_Pawn, so a player can never walk through a door leaf). Designer assigns the mesh(es) in BP; attach
	 *  the top/bottom leaves under this so a single break hides them together (and OnDoorBroken can animate each). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPSR|Door")
	TObjectPtr<UStaticMeshComponent> DoorMesh;

	/** Door frame (문틀) — an INERT wall: object type WorldStatic, so weapon object-queries never gather it (shots
	 *  just stop on it as cover, no damage) while it still blocks movement. Optional (leave empty for a frameless
	 *  door). NOT hidden when the door breaks — it is a sibling of DoorMesh, not a child. Designer assigns the mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "FPSR|Door")
	TObjectPtr<UStaticMeshComponent> FrameMesh;
};
