// Copyright Epic Games, Inc. All Rights Reserved.

#include "Core/FPSRMenuGameMode.h"
#include "Core/FPSRMenuPlayerController.h"
#include "GameFramework/SpectatorPawn.h"

AFPSRMenuGameMode::AFPSRMenuGameMode()
{
	PlayerControllerClass = AFPSRMenuPlayerController::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();

	// Match the lobby/run GameModes (FPSRLobbyGameMode.cpp, FPSRGameMode.cpp) — the empty TransitionMap is already
	// configured in DefaultEngine.ini. In shipping this changes nothing: each player's main menu is NM_Standalone
	// (no net driver), so the Play->lobby travel has no connections to carry. It matters in PIE, where the extra
	// client windows ARE connected at the menu: without seamless travel the host's ServerTravel drops them
	// ("Host closed the connection"), they fall back to Standalone on their own main menu, and pressing Play there
	// spins up a SECOND independent lobby instead of a 2-player session — which makes co-op testing impossible.
	bUseSeamlessTravel = true;
}
