// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaAuthoringTool.h"

#include "Arena/FPSRArenaActor.h"
#include "Arena/FPSRArenaGenerator.h"
#include "Arena/FPSRArenaMarkers.h"
#include "Arena/FPSRArenaTypes.h"
#include "Arena/FPSRArenaValidator.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/MessageDialog.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "FPSRArenaAuthoring"

namespace
{
	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	/** The level's arena, or null with a dialog explaining what to place. */
	AFPSRArenaActor* FindArenaOrComplain(UWorld* World)
	{
		if (!World)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoWorld", "편집 중인 레벨이 없습니다."));
			return nullptr;
		}
		AFPSRArenaActor* Arena = AFPSRArenaActor::FindInWorld(World);
		if (!Arena)
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT("NoArena", "이 레벨에 FPSRArenaActor 가 없습니다.\n\nPlace Actors 패널에서 'FPSRArenaActor' 를 하나 놓고, "
					"디테일 패널의 '아레나 파라미터' 에 UFPSRArenaParamsDataAsset 을 지정한 뒤 다시 실행하세요."));
		}
		return Arena;
	}
}

void FFPSRArenaAuthoringTool::ProposeStartingLayout()
{
	UWorld* World = GetEditorWorld();
	AFPSRArenaActor* Arena = FindArenaOrComplain(World);
	if (!Arena)
	{
		return;
	}

	FFPSRArenaGenParams Params;
	FVector Origin;
	if (!Arena->GetGenParams(Params, Origin))
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("NoParams", "아레나 액터에 '아레나 파라미터' 가 지정되지 않았습니다.\n\n"
				"콘텐츠 브라우저 우클릭 > Miscellaneous > Data Asset > FPSRArenaParamsDataAsset 으로 만들어 지정하세요."));
		return;
	}

	FString ParamError;
	if (!Params.Validate(ParamError))
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
			LOCTEXT("BadParams", "아레나 파라미터가 기하학적으로 성립하지 않습니다:\n\n{0}"), FText::FromString(ParamError)));
		return;
	}

	// Existing blockers are hand-authored work, so replacing them is the designer's call, not the tool's. The whole
	// operation (delete + spawn) sits in ONE transaction, so Ctrl+Z puts the old layout back exactly — which is what
	// makes offering "replace" safe at all.
	TArray<AFPSRArenaBlocker*> Existing;
	for (TActorIterator<AFPSRArenaBlocker> It(World); It; ++It)
	{
		if (IsValid(*It)) { Existing.Add(*It); }
	}
	if (Existing.Num() > 0)
	{
		const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
			LOCTEXT("ReplaceBlockers",
				"이 레벨에 이미 블로커가 {0}개 있습니다.\n\n지우고 새 시작 배치를 제안할까요?\n"
				"(Ctrl+Z 한 번으로 전부 되돌릴 수 있습니다.)"),
			FText::AsNumber(Existing.Num())));
		if (Answer != EAppReturnType::Yes)
		{
			return;
		}
	}

	TArray<FFPSRArenaCluster> Clusters;
	FIntPoint Lattice = FIntPoint::ZeroValue;
	if (!FFPSRArenaGenerator::ProposeLatticeClusters(Arena->GetInitialSeed(), Params, Clusters, Lattice) || Clusters.Num() == 0)
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			LOCTEXT("ProposeFailed", "시작 배치를 만들지 못했습니다. 출력 로그의 [Arena] 항목을 확인하세요."));
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ProposeTransaction", "아레나 시작 배치 제안"));

	for (AFPSRArenaBlocker* Old : Existing)
	{
		World->EditorDestroyActor(Old, /*bShouldModifyLevel=*/true);
	}

	const float Height = Arena->GetClusterHeight();
	int32 Spawned = 0;
	for (const FFPSRArenaCluster& Cluster : Clusters)
	{
		// Round-tripped through the SAME cell->world conversion the rasteriser reads back, so what the tool places
		// and what the swarm believes are the same rectangle rather than two that agree by coincidence.
		const FFPSRArenaAuthoredBox Box = FFPSRArenaGenerator::ClusterToAuthoredBox(Cluster, Params, Origin);

		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags = RF_Transactional; // so the spawn itself is part of the undo above
		AFPSRArenaBlocker* Blocker = World->SpawnActor<AFPSRArenaBlocker>(
			FVector(Box.Center.X, Box.Center.Y, Origin.Z + Height * 0.5),
			FRotator::ZeroRotator, SpawnParams);
		if (!Blocker)
		{
			continue;
		}
		Blocker->SetFootprint(Box.HalfExtentXY, Height);
		++Spawned;
	}

	// Report through the layout the runtime would actually build, not through the proposal — if the two ever
	// disagreed, saying "proposed N clusters" would hide it.
	FFPSRArenaLayout Layout;
	FString Report;
	if (Arena->BuildLayoutForSeed(Arena->GetInitialSeed(), Layout))
	{
		Report = FFPSRArenaValidator::Summarize(FFPSRArenaValidator::Validate(Layout, Params));
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
		LOCTEXT("ProposeDone",
			"시작 배치를 놓았습니다.\n\n격자 {0}×{1} · 블로커 {2}개 · 시드 {3}\n\n{4}\n\n"
			"이제 손으로 옮기고 크기를 바꾸세요. 손댄 순간부터 순환·통로폭·오목은 검증기가 봅니다 — "
			"Tools > FPSR > '아레나 검증' 을 쓰세요.\n"
			"다른 배치를 보려면 아레나 액터의 '시작 시드' 를 바꾸고 다시 실행하면 됩니다."),
		FText::AsNumber(Lattice.X), FText::AsNumber(Lattice.Y), FText::AsNumber(Spawned),
		FText::AsNumber(Arena->GetInitialSeed()), FText::FromString(Report)));
}

void FFPSRArenaAuthoringTool::ValidateArenaInLevel()
{
	UWorld* World = GetEditorWorld();
	AFPSRArenaActor* Arena = FindArenaOrComplain(World);
	if (!Arena)
	{
		return;
	}

	FFPSRArenaGenParams Params;
	FVector Origin;
	if (!Arena->GetGenParams(Params, Origin))
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ValidateNoParams", "아레나 액터에 '아레나 파라미터' 가 지정되지 않았습니다."));
		return;
	}

	FFPSRArenaLayout Layout;
	if (!Arena->BuildLayoutForSeed(Arena->GetInitialSeed(), Layout))
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("ValidateBuildFailed", "레이아웃을 만들지 못했습니다. 출력 로그의 [Arena] 항목을 확인하세요."));
		return;
	}

	const FFPSRArenaValidationResult Result = FFPSRArenaValidator::Validate(Layout, Params);

	FString Body = FFPSRArenaValidator::Summarize(Result);
	if (Result.Errors.Num() > 0)
	{
		Body += TEXT("\n\n[오류]\n") + FString::Join(Result.Errors, TEXT("\n\n"));
	}
	if (Result.Warnings.Num() > 0)
	{
		Body += TEXT("\n\n[경고]\n") + FString::Join(Result.Warnings, TEXT("\n\n"));
	}
	if (Result.Passed() && Result.Warnings.Num() == 0)
	{
		Body += TEXT("\n\n통과 — 순환 · 통로 폭 · 오목 없음 전부 만족합니다.");
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
		LOCTEXT("ValidateResult", "아레나 검증 (시드 {0})\n\n{1}"),
		FText::AsNumber(Arena->GetInitialSeed()), FText::FromString(Body)));
}

#undef LOCTEXT_NAMESPACE
