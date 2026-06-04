// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonGameViewportClient.h"
#include "FPSRGameViewportClient.generated.h"

/** Project viewport client. Subclassing UCommonGameViewportClient wires CommonUI's input action
 *  router (resolves the LogUIActionRouter error). Set via DefaultEngine.ini GameViewportClientClassName. */
UCLASS()
class FPSROGUELITE_API UFPSRGameViewportClient : public UCommonGameViewportClient
{
	GENERATED_BODY()
};
