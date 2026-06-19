// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Core/FPSRGameFlowTypes.h"
#include "Input/UIActionBindingHandle.h"
#include "FPSRResultWidget.generated.h"

class UCommonButtonBase;
class AFPSRPlayerController;

/** Run result widget (Menu layer): shows outcome (Victory/Defeat) and a Return button.
 *  Pushed by AFPSRPlayerController::ClientShowRunResult when a run ends.
 *  Handles menu input mode and delegates return to flow subsystem or server RPC. */
UCLASS(Abstract)
class FPSROGUELITE_API UFPSRResultWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	UFPSRResultWidget();

	/** Set the outcome and notify the blueprint (OnOutcomeSet event). */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Flow")
	void SetOutcome(EFPSRRunOutcome Outcome);

	/** Blueprint event: fired when SetOutcome is called, allowing UI to swap victory/defeat text. */
	UFUNCTION(BlueprintImplementableEvent, Category = "FPSR|Flow")
	void OnOutcomeSet(EFPSRRunOutcome Outcome);

protected:
	virtual void NativeOnInitialized() override;

	/** Establish menu input mode (menu input, no mouse capture). */
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCommonButtonBase> ReturnButton;

private:
	UFUNCTION()
	void HandleReturnClicked();
};
