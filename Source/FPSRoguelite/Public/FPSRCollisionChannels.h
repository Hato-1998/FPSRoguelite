// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Custom collision object channel for the player pawn.
 *
 * Why: enemy capsules are object type ECC_Pawn and weapon traces query ECC_Pawn to find enemies. To let the
 * swarm OVERLAP each other (cheap soft-separation steering instead of expensive mutual physics blocking, which
 * gridlocks a dense crowd — Game.MD §1/§5) while STILL blocking the player (surround + collision-ignore dash,
 * §2-13), the PLAYER is given this distinct object type. Enemies then set their ECC_Pawn response to Ignore
 * (ignoring each other) while their default Block response keeps them blocking PlayerPawn and walls.
 *
 * Registered in Config/DefaultEngine.ini as DefaultChannelResponses Channel=ECC_GameTraceChannel1 Name="PlayerPawn".
 * Keep this alias and the .ini channel index in sync.
 *
 * NOTE (follow-up): systems that must hit the player by object query (enemy ranged projectiles B1, friendly fire)
 * should query BOTH ECC_Pawn (enemies) and ECC_FPSRPlayerPawn (players) and team-filter.
 */
constexpr ECollisionChannel ECC_FPSRPlayerPawn = ECC_GameTraceChannel1;

/**
 * Custom object channel for enemy/boss weakpoint shapes (headshot/weak zones, U3a).
 *
 * Why a DEDICATED channel (not ECC_Pawn): weakpoint shapes must be found ONLY by the precise-aim damage paths
 * (hitscan/charge-laser line traces, single-target projectile, melee). Putting them on ECC_Pawn would make
 * AOE explosions (FPSRCombat::ApplyExplosion's OverlapMultiByObjectType(ECC_Pawn)) also gather them, breaking
 * "AOE never reads weakpoints" (no-regression). On their own object channel, only queries that explicitly add
 * this type see them; movement/ground(WorldStatic)/separation/flow-field never query it, so zero interference.
 *
 * Registered in Config/DefaultEngine.ini as DefaultChannelResponses Channel=ECC_GameTraceChannel2 Name="Weakpoint".
 * Keep this alias and the .ini channel index in sync.
 */
constexpr ECollisionChannel ECC_FPSRWeakpoint = ECC_GameTraceChannel2;
