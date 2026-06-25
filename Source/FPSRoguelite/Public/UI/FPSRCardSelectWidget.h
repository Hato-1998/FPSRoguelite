// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CommonActivatableWidget.h"
#include "Card/FPSRCardTypes.h"
#include "FPSRCardSelectWidget.generated.h"

class UFPSRCardEntryWidget;
class UButton;
class UTextBlock;

/** Modal card-selection widget (Modal layer). Shows up to 3 card entries + a reroll button.
 *  The client only sends intent (selected index / reroll) to the server; the server applies from its
 *  cached offer (AFPSRPlayerController). The owning PlayerController drives show/dismiss lifecycle. */
UCLASS()
class FPSROGUELITE_API UFPSRCardSelectWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	/** Populate the entries from a server-issued offer (tagged with OfferId, echoed back on selection).
	 *  Re-callable to refresh (breather re-issue / reroll). */
	void SetCardOffers(int32 OfferId, const TArray<FFPSRCardDraw>& InOffers);

protected:
	// Bind once per instance — NativeConstruct can run again on pooled re-activation (would stack handlers).
	virtual void NativeOnInitialized() override;

	// Remove the PlayerState reroll-charge delegate on teardown (it outlives this widget). Matches Lobby/RunHUD cleanup.
	virtual void NativeDestruct() override;

	/** UI-only input while the modal is up (mouse visible, game input suppressed). */
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

	UFUNCTION()
	void OnCardEntrySelected(int32 CardIndex);

	UFUNCTION()
	void OnRerollPressed();

	/** Refresh the reroll-charges label (also fired when the charge count replicates late). */
	UFUNCTION()
	void UpdateRerollCharges();

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UFPSRCardEntryWidget> CardEntry_0;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UFPSRCardEntryWidget> CardEntry_1;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UFPSRCardEntryWidget> CardEntry_2;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> RerollButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> RerollChargesText;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UFPSRCardEntryWidget>> CardEntries;

	/** Id of the currently shown offer, echoed to the server on selection (anti double-apply). */
	int32 ShownOfferId = 0;
};
