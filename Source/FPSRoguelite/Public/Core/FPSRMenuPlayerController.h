// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FPSRMenuPlayerController.generated.h"

class UFPSRPrimaryGameLayout;
class UCommonActivatableWidget;

/** Menu player controller: creates the primary layout and pushes the main menu widget.
 *  Only active on local controllers (single-player menu navigation). */
UCLASS()
class FPSROGUELITE_API AFPSRMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;

	/** The local-player layout root (so the main menu widget can push the shared settings overlay to a layer). */
	UFPSRPrimaryGameLayout* GetPrimaryLayout() const { return PrimaryLayout; }

protected:
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|UI")
	TSubclassOf<UFPSRPrimaryGameLayout> PrimaryLayoutClass;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|UI")
	TSubclassOf<UCommonActivatableWidget> MainMenuWidgetClass;

private:
	/** Local-player layout root (created in BeginPlay). */
	UPROPERTY(Transient)
	TObjectPtr<UFPSRPrimaryGameLayout> PrimaryLayout;
};
