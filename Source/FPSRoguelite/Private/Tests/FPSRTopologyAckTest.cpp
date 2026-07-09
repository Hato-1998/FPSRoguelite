// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Core/FPSRPlayerState.h"

#if WITH_AUTOMATION_TESTS

// U P-F Stage 2 (2026-07-09): headless net for the late-join topology-ack GATE — the pure decision
// AFPSRPlayerState::IsTopologyAckSatisfied the allocator/movement call to seal a not-yet-acked joiner out. No world:
// exercises the exact static predicate (single source of truth) so PIE re-tuning the timeout doesn't break the test
// while a regression in the GATE LOGIC still does. Registered under FPSRoguelite.Allocator so the existing headless run
// (FlowField+Smoke+Allocator) covers it — the gate lives adjacent to the allocator's occupancy pass.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRTopologyAckTest, "FPSRoguelite.Allocator.TopologyAck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRTopologyAckTest::RunTest(const FString& Parameters)
{
	using PS = AFPSRPlayerState;
	constexpr float T = 5.0f;   // fail-open timeout
	constexpr float Now = 100.0f;

	// --- (A) Local authority (host / standalone owning controller IS the server) -> ALWAYS satisfied ------------------
	// Regardless of gens or clock: the host authored the topology, there is no OnRep to ack against.
	TestTrue(TEXT("local authority -> satisfied (unmarked)"), PS::IsTopologyAckSatisfied(true, -1, -1, 0.0f, Now, T));
	TestTrue(TEXT("local authority -> satisfied (behind + no timeout)"), PS::IsTopologyAckSatisfied(true, 0, 9, Now, Now, T));

	// --- (B) Leading-edge seal: a remote client not yet MARKED (JoinGen < 0) -> fail-CLOSED --------------------------
	TestFalse(TEXT("remote unmarked -> fail-closed"), PS::IsTopologyAckSatisfied(false, -1, -1, 0.0f, Now, T));
	TestFalse(TEXT("remote unmarked -> fail-closed even if Acked is high"), PS::IsTopologyAckSatisfied(false, 9, -1, 0.0f, Now, T));

	// --- (C) Join-generation gate: marked but not yet acked (fresh joiner), inside the timeout -> gated OUT -----------
	TestFalse(TEXT("joined gen 3, acked nothing (-1), not timed out -> gated"),
		PS::IsTopologyAckSatisfied(false, -1, 3, Now, Now, T));           // JoinTime == Now => elapsed 0
	TestFalse(TEXT("joined gen 3, acked 2 (< join), not timed out -> gated"),
		PS::IsTopologyAckSatisfied(false, 2, 3, Now - 1.0f, Now, T));     // elapsed 1s < 5s

	// --- (D) Acked at/above the join generation -> satisfied ---------------------------------------------------------
	TestTrue(TEXT("acked == join -> satisfied"), PS::IsTopologyAckSatisfied(false, 3, 3, Now, Now, T));
	TestTrue(TEXT("acked > join -> satisfied"), PS::IsTopologyAckSatisfied(false, 4, 3, Now, Now, T));
	// Common case: joined at gen 0, acked gen 0 -> in immediately (single-map / present-from-start party).
	TestTrue(TEXT("joined 0, acked 0 -> satisfied"), PS::IsTopologyAckSatisfied(false, 0, 0, Now, Now, T));

	// --- (E) Door-open NON-regate: an existing player (joined at their old gen) re-acks the NEW higher gen and stays in.
	// Their JoinGen is fixed at entry (say 0); a door opening bumps the topology and they re-ack a higher value — the gate
	// keys off Acked >= Join, so they are NEVER re-sealed by a door opening (only a fresh joiner marks at the new gen).
	TestTrue(TEXT("existing player joined 0, re-acked 5 after doors opened -> still satisfied"),
		PS::IsTopologyAckSatisfied(false, 5, 0, Now, Now, T));

	// --- (F) Fail-open on timeout: a lost ack RPC must not softlock — past the timeout, participate anyway -----------
	TestFalse(TEXT("at the timeout boundary (elapsed == T) -> NOT yet open (strict >)"),
		PS::IsTopologyAckSatisfied(false, -1, 2, Now - T, Now, T));       // elapsed exactly T
	TestTrue(TEXT("just past the timeout -> fail-open"),
		PS::IsTopologyAckSatisfied(false, -1, 2, Now - (T + 0.01f), Now, T));
	TestTrue(TEXT("long past the timeout -> fail-open"),
		PS::IsTopologyAckSatisfied(false, 0, 9, Now - 1000.0f, Now, T));

	// --- (G) Monotone in Acked: once satisfied at a given (Join, time), a higher Acked stays satisfied ---------------
	{
		bool bSeenSatisfied = false;
		for (int32 Acked = -1; Acked <= 6; ++Acked)
		{
			const bool bSat = PS::IsTopologyAckSatisfied(false, Acked, 3, Now, Now, T); // no timeout (elapsed 0)
			if (bSat) { bSeenSatisfied = true; }
			else { TestFalse(TEXT("ack satisfaction never toggles back off as Acked grows"), bSeenSatisfied); }
		}
		TestTrue(TEXT("some Acked >= join is satisfied"), bSeenSatisfied);
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
