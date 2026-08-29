// Copyright Epic Games, Inc. All Rights Reserved.

#include "Validation/FPSRArenaBakeValidator.h"

#include "Arena/FPSRArenaActor.h"
#include "Arena/FPSRArenaBakeHash.h"
#include "Arena/FPSRArenaBakeDataAsset.h" // SourceLevel (교차 레벨 베이크 오염 검사)
#include "Arena/FPSRArenaDestructible.h"  // IsSuppressor (보스 아레나엔 억제기가 없어야 한다)
#include "Boss/FPSRBossSpawnPoint.h"      // 보스 아레나 스폰포인트 유무 경고

#include "Engine/World.h"
#include "EngineUtils.h"
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

		// ── 베이크 에셋을 두 레벨이 공유하고 있다 (신설 2026-08-28) ────────────────────────────────────
		// 실제로 일어났다: 새 아레나 레벨을 기존 레벨의 복제로 만들면 `베이크 데이터` 참조도 함께 복제되고,
		// 새 레벨에서 굽는 순간 **원본 레벨의 마스크가 조용히 덮인다**. 스테일 검사는 이걸 못 잡는다 —
		// 덮은 쪽에서 보면 해시가 방금 구운 그대로라 `Fresh` 다. 피해는 반대쪽 레벨에서 "적이 벽을
		// 통과한다"로 나타나고, 그 레벨을 열어보기 전까지 아무도 모른다.
		if (const UFPSRArenaBakeDataAsset* Bake = Arena->GetBakeData())
		{
			// SourceLevel 이 비어 있으면 "아직 안 구움"이고 위 NoAsset/NotBaked 가 이미 말한다.
			// PIE 패키지 접두사(`UEDPIE_0_`)는 검증기 경로엔 나타나지 않는다(에디터 에셋 검사).
			const FString BakedFrom = Bake->SourceLevel.GetLongPackageName();
			// ⚠️ 비교 대상은 **아레나가 실제로 사는 레벨**이지, 검사 중인 월드가 아니다. FindAllInWorld 는
			//    TActorIterator 라 **로드된 서브레벨을 전부** 훑으므로, 지속 레벨(L_Arena)을 검사하면 서브레벨
			//    아레나가 전부 잡힌다. 검사 월드(=L_Arena)와 비교하면 그것들의 SourceLevel(=자기 서브레벨)이
			//    당연히 달라 **모든 아레나가 매번 오탐**으로 걸리고, Invalid 라 지속 레벨 커밋이 영구 차단된다.
			//    베이크가 쓰는 값과 같은 축으로 재야 한다(FPSRArenaAuthoringTool.cpp:783 —
			//    `Asset->SourceLevel = FSoftObjectPath(Arena->GetTypedOuter<UWorld>())`). 실사고 2026-08-29.
			const UWorld* ArenaWorld = Arena->GetTypedOuter<UWorld>();
			const FString ThisLevel = (ArenaWorld && ArenaWorld->GetPackage()) ? ArenaWorld->GetPackage()->GetName() : FString();
			if (!BakedFrom.IsEmpty() && !ThisLevel.IsEmpty() && BakedFrom != ThisLevel)
			{
				Context.AddError(FText::FromString(FString::Printf(
					TEXT("[%s] 베이크 에셋 '%s' 은 **다른 레벨**('%s')에서 구워졌습니다. 두 아레나 레벨이 한 베이크 에셋을 공유하면 나중에 구운 쪽이 앞선 쪽의 마스크를 덮어 버립니다(그 레벨에서 적이 벽을 통과합니다). 이 레벨 전용 베이크 에셋을 새로 만들어 `베이크 데이터` 에 걸고 다시 구우세요."),
					*Arena->GetName(), *Bake->GetName(), *BakedFrom)));
				Result = EDataValidationResult::Invalid;
			}
		}

		// ── 보스 아레나 저작 규칙 (보스 스테이지, 신설 2026-08-28) ─────────────────────────────────────
		// 레벨 하나 안에서 판정 가능한 것만 본다. "월드 전체에 보스 아레나는 하나" 는 어느 서브레벨이
		// 에디터에 로드돼 있느냐에 따라 답이 달라져 여기서는 신뢰할 수 없다 — 런타임 로스터가 가장 낮은
		// StageOrder 를 결정론적으로 고르는 것으로 대신한다.
		if (Arena->GetArenaRole() == EFPSRArenaRole::Boss)
		{
			// (1) 억제기가 있으면 안 된다. 보스전 도중 억제기를 부수는 것 자체는 스테이지 디렉터가 이미
			//     거부하지만(RunPhase != Combat), 보스가 **뜨기 전** 전환 구간에 부수면 그 요청이 통과해
			//     파티가 보스 아레나 밖으로 나가 버린다. 저작 단계에서 막는 것이 유일한 확실한 방어다.
			TArray<AFPSRArenaDestructible*> Owned;
			Arena->GetOwnedDestructibles(Owned);
			int32 SuppressorCount = 0;
			for (const AFPSRArenaDestructible* Destructible : Owned)
			{
				if (IsValid(Destructible) && Destructible->IsSuppressor())
				{
					++SuppressorCount;
				}
			}
			if (SuppressorCount > 0)
			{
				Context.AddError(FText::FromString(FString::Printf(
					TEXT("[%s] 보스 아레나에 억제기가 %d개 있습니다. 보스 스테이지는 **나가는 길이 없어야** 합니다 — 억제기를 전부 지우거나, 이 아레나의 `아레나 역할` 을 `전투` 로 되돌리세요."),
					*Arena->GetName(), SuppressorCount)));
				Result = EDataValidationResult::Invalid;
			}

			// (2) 런이 보스 스테이지에서 시작하면 안 된다.
			if (Arena->StartsActive())
			{
				Context.AddError(FText::FromString(FString::Printf(
					TEXT("[%s] 보스 아레나에 `시작 시 활성` 이 켜져 있습니다. 런이 보스 스테이지에서 시작하면 전투 스테이지가 한 번도 나오지 않습니다 — 끄세요."),
					*Arena->GetName())));
				Result = EDataValidationResult::Invalid;
			}

			// (3) 보스 스폰포인트는 경고에 그친다 — 없어도 런 디렉터가 아레나 중심에 스폰한다(의도된 폴백).
			bool bHasBossSpawnPoint = false;
			for (TActorIterator<AFPSRBossSpawnPoint> It(World); It; ++It)
			{
				const AFPSRBossSpawnPoint* Point = *It;
				if (IsValid(Point) && Arena->ContainsWorldLocation(Point->GetActorLocation()))
				{
					bHasBossSpawnPoint = true;
					break;
				}
			}
			if (!bHasBossSpawnPoint)
			{
				Context.AddWarning(FText::FromString(FString::Printf(
					TEXT("[%s] 보스 아레나에 AFPSRBossSpawnPoint 가 없습니다. 보스는 아레나 중심에 스폰됩니다(폴백) — 위치를 정하려면 스폰포인트를 놓으세요."),
					*Arena->GetName())));
			}
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
