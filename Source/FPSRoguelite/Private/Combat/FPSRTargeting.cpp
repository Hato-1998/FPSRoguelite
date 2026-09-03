// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/FPSRTargeting.h"

#include "Core/FPSRPlayerState.h"
#include "GameFramework/PlayerController.h"

namespace FPSRTargeting
{
	bool IsEligibleTarget(const APlayerController* PC, float Now, bool bRequireTopologyAck)
	{
		if (!PC)
		{
			return false;
		}

		const AFPSRPlayerState* PS = PC->GetPlayerState<AFPSRPlayerState>();

		// B17 (U9): enemies don't target non-alive players (DBNO downed or Dead) — a downed teammate stops drawing
		// aggro and the swarm re-targets the living. (Downed players also take no contact damage.)
		if (PS && !PS->IsAlive())
		{
			return false;
		}

		// U (P-F): a late joiner that hasn't acked the current topology is excluded from the WHOLE movement+attack
		// pass (targeting + contact/ranged damage in one choke) until its ack lands (or the fail-open timeout). Only
		// with a unified field (multimap) — single-map has no topology to confirm, so it's a strict no-op there (no
		// sub-RTT exclusion for a mid-combat single-map joiner). Host = local authority -> instantly satisfied.
		// DBNO/Dead already excluded above; a revived player is already acked (marked long before), so it re-participates.
		if (bRequireTopologyAck && PS && !PS->HasAckedJoinTopology(Now))
		{
			return false;
		}

		return true;
	}
}
