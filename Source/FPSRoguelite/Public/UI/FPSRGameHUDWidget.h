// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonActivatableWidget.h"
#include "FPSRGameHUDWidget.generated.h"

/** Game-layer HUD root (CommonUI Activatable). Owns the "playing" input config — capture the mouse for look,
 *  hide the cursor, route input to the game — so that when a modal above it (e.g. card select) is dismissed,
 *  game input is restored. Purely a container: all HUD content (run state / XP, hit marker, damage & threat
 *  indicators) lives in child widgets. The local PlayerController pushes one of these to the Game layer. */
UCLASS()
class FPSROGUELITE_API UFPSRGameHUDWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

protected:
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
};
