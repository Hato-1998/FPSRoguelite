// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/PlayerController.h"
#include "FPSRPlayerController.generated.h"

class UInputMappingContext;

/** Base player controller. Adds the default Enhanced Input mapping context for the local player. */
UCLASS()
class FPSROGUELITE_API AFPSRPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AFPSRPlayerController();

protected:
	virtual void SetupInputComponent() override;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;
};
