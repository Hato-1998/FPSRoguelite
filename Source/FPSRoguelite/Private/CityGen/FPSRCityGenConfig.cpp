// Copyright Epic Games, Inc. All Rights Reserved.

#include "CityGen/FPSRCityGenConfig.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "Engine/StaticMesh.h"

namespace
{
	int32 CountValid(const TArray<TObjectPtr<UStaticMesh>>& Meshes)
	{
		int32 Count = 0;
		for (const TObjectPtr<UStaticMesh>& Mesh : Meshes)
		{
			if (Mesh)
			{
				++Count;
			}
		}
		return Count;
	}
}

EDataValidationResult UFPSRCityGenConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// 비어 있어도 툴이 기본 메시로 폴백하므로 에러가 아니라 경고 — "왜 내가 고른 게 안 나오지"를 막는 안내용.
	const TPair<const TCHAR*, int32> Categories[] = {
		{ TEXT("Facades"),      CountValid(Facades)      },
		{ TEXT("Corners"),      CountValid(Corners)      },
		{ TEXT("Doors"),        CountValid(Doors)        },
		{ TEXT("RoofFloors"),   CountValid(RoofFloors)   },
		{ TEXT("CorniceTrims"), CountValid(CorniceTrims) },
	};
	for (const TPair<const TCHAR*, int32>& Category : Categories)
	{
		if (Category.Value == 0)
		{
			Context.AddWarning(FText::FromString(FString::Printf(
				TEXT("CityGenConfig: %s is empty - the tool will fall back to its built-in default mesh."),
				Category.Key)));
		}
	}

	if (RoofPropCount > 0 && CountValid(RoofProps) == 0)
	{
		Context.AddWarning(FText::FromString(TEXT(
			"CityGenConfig: RoofPropCount > 0 but RoofProps is empty - default antennas will be used.")));
	}

	return Result;
}
#endif
