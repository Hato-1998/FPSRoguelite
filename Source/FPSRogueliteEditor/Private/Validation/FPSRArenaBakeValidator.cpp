// Copyright Epic Games, Inc. All Rights Reserved.

#include "Validation/FPSRArenaBakeValidator.h"

#include "Arena/FPSRArenaActor.h"
#include "Arena/FPSRArenaBakeHash.h"

#include "Engine/World.h"
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "FPSRArenaBakeValidator"

bool UFPSRArenaBakeValidator::CanValidateAsset_Implementation(const FAssetData& InAssetData, UObject* InAsset,
	FDataValidationContext& InContext) const
{
	// 레벨만 본다. 아레나가 없는 레벨은 아래에서 즉시 통과하므로, 여기서 아레나 유무까지 보려고 레벨을
	// 미리 로드할 이유는 없다.
	return InAsset != nullptr && InAsset->IsA<UWorld>();
}

EDataValidationResult UFPSRArenaBakeValidator::ValidateLoadedAsset_Implementation(const FAssetData& InAssetData,
	UObject* InAsset, FDataValidationContext& Context)
{
	// 아래 두 이른 반환이 Valid 인 이유(NotValidated 가 아니라):
	// `확인됨` UEditorValidatorBase::ValidateLoadedAsset 이 CanValidateAsset 을 통과한 에셋에 대해
	// NotValidated 를 돌려주면 엔진이 ensure 로 잡는다 — "Validator did not return a validation result"
	// (EditorValidatorBase.cpp, UE 5.7). "할 말이 없다"를 표현하는 자리는 반환값이 아니라 CanValidateAsset
	// 이고, 아레나 유무는 레벨을 열어 봐야 알 수 있어 거기서는 판단할 수 없다. 그래서 아레나 없는 레벨은
	// "검사했고 문제 없음"으로 답한다. (처음에 NotValidated 를 돌려줬다가 커맨드렛 실행에서 L_Lobby 로
	// ensure 가 터졌다.)
	const UWorld* World = Cast<UWorld>(InAsset);
	if (!World)
	{
		return EDataValidationResult::Valid;
	}

	TArray<AFPSRArenaActor*> Arenas;
	AFPSRArenaActor::FindAllInWorld(World, Arenas);
	if (Arenas.Num() == 0)
	{
		return EDataValidationResult::Valid; // 로비·메뉴·구 맵 — 이 검사가 다룰 대상이 아니다
	}

	EDataValidationResult Result = EDataValidationResult::Valid;

	for (const AFPSRArenaActor* Arena : Arenas)
	{
		if (!IsValid(Arena))
		{
			continue;
		}

		const FFPSRArenaBakeCheck Check = FFPSRArenaBakeHash::CheckFreshness(*Arena);
		switch (Check.Freshness)
		{
		case EFPSRArenaBakeFreshness::Fresh:
			break;

		case EFPSRArenaBakeFreshness::NoAsset:
			// 경고에 그친다. 아레나를 막 놓고 아직 굽지 않은 것은 정상적인 저작 중간 상태이고, 여기서
			// 커밋을 막으면 작업 중인 레벨을 저장조차 공유할 수 없게 된다.
			Context.AddWarning(FText::FromString(Check.Message));
			break;

		case EFPSRArenaBakeFreshness::NotBaked:
		case EFPSRArenaBakeFreshness::Stale:
			// 오류 = 커밋 차단. 스테일 베이크는 "화면과 적이 서로 다른 레벨을 본다"는 뜻이고, 그 증상은
			// 실행해 보기 전에는 어디에도 드러나지 않는다 — 다른 사람에게 도달하기 전에 멈춰야 한다.
			Context.AddError(FText::FromString(Check.Message));
			Result = EDataValidationResult::Invalid;
			break;
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
