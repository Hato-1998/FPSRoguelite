// Copyright Epic Games, Inc. All Rights Reserved.

#include "CityGen/FPSRCityGenConfig.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "Engine/StaticMesh.h"

EDataValidationResult UFPSRCityGenConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	// 파사드가 하나도 없으면 벽 없는 건물이 나오므로 최소 하나는 경고(툴은 DEFAULT_CONFIG로 폴백하나, 의도 확인용).
	int32 ValidFacades = 0;
	for (const TObjectPtr<UStaticMesh>& Mesh : Facades)
	{
		if (Mesh)
		{
			++ValidFacades;
		}
	}
	if (ValidFacades == 0)
	{
		Context.AddWarning(FText::FromString(TEXT("CityGenConfig: Facades is empty - the tool will fall back to default walls.")));
	}
	return Result;
}
#endif
