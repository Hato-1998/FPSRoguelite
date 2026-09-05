// Copyright Epic Games, Inc. All Rights Reserved.

#include "Combat/FPSRCritTypes.h"

#include "GameplayEffect.h"

bool FFPSRCritContext::HasRiders() const
{
	// Out of line so the header can forward-declare UGameplayEffect (see the note there): the nullptr comparison
	// goes through TSubclassOf::operator*(), which needs the complete type.
	return BonusInstanceRatio > 0.0f || (HealRatio > 0.0f && HealEffect != nullptr);
}
