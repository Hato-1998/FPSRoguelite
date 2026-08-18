// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaAuthoringTool.h"

#include "Arena/FPSRArenaActor.h"
#include "Arena/FPSRArenaDestructible.h"
#include "Arena/FPSRArenaGenerator.h"
#include "Arena/FPSRArenaMarkers.h"
#include "Arena/FPSRArenaTypes.h"
#include "Arena/FPSRArenaValidator.h"
#include "Core/FPSRLogChannels.h"

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

	/** Every arena in the level, StageOrder order — or empty with a dialog explaining what to place. Multiple
	 *  arenas in one level are now normal (ADR 0010 D6, 2026-08-17: spare arenas parked beside the live one), so
	 *  both editor flows below must visit all of them rather than assume a single AFPSRArenaActor. */
	bool FindAllArenasOrComplain(UWorld* World, TArray<AFPSRArenaActor*>& OutArenas)
	{
		OutArenas.Reset();
		if (!World)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoWorld", "편집 중인 레벨이 없습니다."));
			return false;
		}
		AFPSRArenaActor::FindAllInWorld(World, OutArenas);
		if (OutArenas.Num() == 0)
		{
			FMessageDialog::Open(EAppMsgType::Ok,
				LOCTEXT("NoArena", "이 레벨에 FPSRArenaActor 가 없습니다.\n\nPlace Actors 패널에서 'FPSRArenaActor' 를 하나 놓고, "
					"디테일 패널의 '아레나 파라미터' 에 UFPSRArenaParamsDataAsset 을 지정한 뒤 다시 실행하세요."));
			return false;
		}
		return true;
	}

	/** Degrees. Below this, a rotation reads as noise from a viewport drag; above it, an assumption the rasteriser
	 *  actually depends on (destructible footprints grow along the WORLD grid axes regardless of actor rotation;
	 *  blockers rasterise from yaw alone, treating themselves as an XY-plane prism) is genuinely violated. */
	constexpr float ArenaAuthoringRotationToleranceDegrees = 1.0f;

	/** cm. How far a marker's Z may drift from its owning arena's floor before the editor calls it out — the
	 *  rasteriser itself never reads Z (XY-only membership + rasterisation, ADR 0010 D2 single-plane arena), so a
	 *  marker floating well above or buried well below the floor still blocks/reserves exactly as if it sat on it. */
	constexpr float ArenaAuthoringZToleranceCm = 200.0f;

	/**
	 * Level-wide checks that only exist because an arena is no longer alone in its level (ADR 0010 D6, 2026-08-17).
	 * Neither failure is visible at runtime — FindActiveInWorld picks an arena deterministically whatever the
	 * authoring says, and a marker outside every grid is simply never gathered — so the editor is the only place
	 * they can be caught (ADR 0011 E4: 판정 시점은 에디터다).
	 */
	FString CheckLevelWideArenaAuthoring(UWorld* World, const TArray<AFPSRArenaActor*>& Arenas)
	{
		FString Report;

		// (1) Exactly one arena may be authored as the starting one. Two means whichever sorts first silently wins;
		// zero means the live arena is picked by StageOrder, which is almost certainly not what was intended.
		TArray<FString> StartActiveNames;
		for (const AFPSRArenaActor* Arena : Arenas)
		{
			if (Arena && Arena->StartsActive()) { StartActiveNames.Add(Arena->GetName()); }
		}
		if (StartActiveNames.Num() != 1)
		{
			Report += (StartActiveNames.Num() == 0)
				? FString::Printf(TEXT("[오류] '시작 시 활성' 인 아레나가 없습니다. 하나를 켜세요 — 지금은 스테이지 순서로 아무거나 골라집니다.\n"))
				: FString::Printf(TEXT("[오류] '시작 시 활성' 인 아레나가 %d개입니다(%s). 하나만 켜세요 — 나머지는 예비 아레나입니다.\n"),
					StartActiveNames.Num(), *FString::Join(StartActiveNames, TEXT(", ")));
		}

		// (2) Markers that fall inside NO arena's grid. Membership is spatial, so a blocker nudged (or pasted) out
		// of every grid stops blocking anything without a word — it still looks like a wall in the viewport.
		TArray<FString> Orphans;
		auto CollectOrphans = [&Arenas, &Orphans](AActor* Actor)
		{
			if (!IsValid(Actor)) { return; }
			for (const AFPSRArenaActor* Arena : Arenas)
			{
				if (Arena && Arena->ContainsWorldLocation(Actor->GetActorLocation())) { return; }
			}
			Orphans.Add(Actor->GetName());
		};
		for (TActorIterator<AFPSRArenaBlocker> It(World); It; ++It) { CollectOrphans(*It); }
		for (TActorIterator<AFPSRArenaLandmark> It(World); It; ++It) { CollectOrphans(*It); }
		for (TActorIterator<AFPSRArenaDestructible> It(World); It; ++It) { CollectOrphans(*It); }
		if (Orphans.Num() > 0)
		{
			Report += FString::Printf(
				TEXT("[오류] 어느 아레나 격자에도 속하지 않은 마커 %d개: %s\n뷰포트에선 벽처럼 보이지만 아무것도 막지 않습니다 — "
				     "아레나 안으로 옮기거나 지우세요.\n"),
				Orphans.Num(), *FString::Join(Orphans, TEXT(", ")));
		}

		// (3) Destructible yaw != 0. AFPSRArenaDestructible's footprint grows along the GRID's own +X/+Y regardless
		// of the actor's rotation (FFPSRArenaGenerator::ComputeDestructibleCells) — a yawed actor's mesh no longer
		// matches the cells the generator actually blocks while intact / opens once it breaks.
		TArray<FString> YawedDestructibles;
		for (TActorIterator<AFPSRArenaDestructible> It(World); It; ++It)
		{
			const AFPSRArenaDestructible* Destructible = *It;
			if (IsValid(Destructible) && FMath::Abs(Destructible->GetActorRotation().Yaw) > ArenaAuthoringRotationToleranceDegrees)
			{
				YawedDestructibles.Add(Destructible->GetName());
			}
		}
		if (YawedDestructibles.Num() > 0)
		{
			Report += FString::Printf(
				TEXT("[경고] 회전(Yaw)이 있는 파괴물 %d개: %s\n발자국은 액터 회전과 무관하게 그리드 +X/+Y 로 자랍니다 — "
				     "마스크와 메시가 어긋납니다. 의도한 것이 아니라면 Yaw 를 0 으로 되돌리세요.\n"),
				YawedDestructibles.Num(), *FString::Join(YawedDestructibles, TEXT(", ")));
		}

		// (4) Blocker pitch/roll != 0. Rasterisation reads ONLY yaw — the generator treats every blocker as an
		// XY-plane prism (Generate()'s box loop; AFPSRArenaBlocker::GetYawDegrees never reads pitch/roll at all) —
		// so a pitched/rolled blocker looks tilted in the viewport but still blocks the same flat footprint its
		// yaw alone implies.
		TArray<FString> TiltedBlockers;
		for (TActorIterator<AFPSRArenaBlocker> It(World); It; ++It)
		{
			const AFPSRArenaBlocker* Blocker = *It;
			if (!IsValid(Blocker)) { continue; }
			const FRotator Rot = Blocker->GetActorRotation();
			if (FMath::Abs(Rot.Pitch) > ArenaAuthoringRotationToleranceDegrees || FMath::Abs(Rot.Roll) > ArenaAuthoringRotationToleranceDegrees)
			{
				TiltedBlockers.Add(Blocker->GetName());
			}
		}
		if (TiltedBlockers.Num() > 0)
		{
			Report += FString::Printf(
				TEXT("[경고] Pitch/Roll 이 있는 블로커 %d개: %s\n래스터화는 Yaw 만 읽습니다 — 기울어진 만큼 보이는 것과 "
				     "실제로 막는 셀이 달라집니다. 의도한 것이 아니라면 Pitch/Roll 을 0 으로 되돌리세요.\n"),
				TiltedBlockers.Num(), *FString::Join(TiltedBlockers, TEXT(", ")));
		}

		// (5) Marker Z far from ITS OWN arena's floor. Membership (ContainsWorldLocation) and rasterisation are
		// both XY-only (ADR 0010 D2 single-plane arena) — a marker floating well above or buried well below the
		// floor still blocks/reserves exactly as if it sat on the floor, which usually means it was dragged off
		// the floor plane by accident rather than authored airborne on purpose.
		TArray<FString> OffPlaneMarkers;
		auto CheckZ = [&Arenas, &OffPlaneMarkers](AActor* Actor)
		{
			if (!IsValid(Actor)) { return; }
			for (const AFPSRArenaActor* Arena : Arenas)
			{
				if (!Arena || !Arena->ContainsWorldLocation(Actor->GetActorLocation())) { continue; }
				const double DeltaZ = Actor->GetActorLocation().Z - Arena->GetActorLocation().Z;
				if (FMath::Abs(DeltaZ) > ArenaAuthoringZToleranceCm)
				{
					OffPlaneMarkers.Add(FString::Printf(TEXT("%s(%.0fcm)"), *Actor->GetName(), DeltaZ));
				}
				return; // membership is spatial + exclusive to one arena — orphans (no owning arena) are check (2)'s job
			}
		};
		for (TActorIterator<AFPSRArenaBlocker> It(World); It; ++It) { CheckZ(*It); }
		for (TActorIterator<AFPSRArenaLandmark> It(World); It; ++It) { CheckZ(*It); }
		for (TActorIterator<AFPSRArenaDestructible> It(World); It; ++It) { CheckZ(*It); }
		if (OffPlaneMarkers.Num() > 0)
		{
			Report += FString::Printf(
				TEXT("[경고] 아레나 바닥에서 %.0fcm 넘게 떨어진 마커 %d개: %s\n공중/지하 배치도 XY 판정이라 그대로 "
				     "래스터화됩니다 — 의도한 배치가 맞는지 확인하세요.\n"),
				ArenaAuthoringZToleranceCm, OffPlaneMarkers.Num(), *FString::Join(OffPlaneMarkers, TEXT(", ")));
		}

		if (Report.IsEmpty())
		{
			Report = TEXT("레벨 전체: 시작 아레나 1개 · 미소속 마커 없음 · 회전 이상 없음 · 바닥 정렬 이상 없음.\n");
		}
		return Report + TEXT("\n");
	}
}

void FFPSRArenaAuthoringTool::ProposeStartingLayout()
{
	UWorld* World = GetEditorWorld();
	TArray<AFPSRArenaActor*> Arenas;
	if (!FindAllArenasOrComplain(World, Arenas))
	{
		return;
	}

	// ONE transaction for the whole multi-arena sweep, so Ctrl+Z undoes every spawn across every arena in a single
	// step. Unlike the old single-arena tool, there is no "replace?" branch any more (see the skip check below) —
	// an already-authored arena is simply left alone, so there is nothing destructive here to weigh per arena.
	const FScopedTransaction Transaction(LOCTEXT("ProposeTransaction", "아레나 시작 배치 제안"));

	FString Report;
	TArray<FString> SkippedNames;

	for (AFPSRArenaActor* Arena : Arenas)
	{
		if (!Arena)
		{
			continue;
		}

		FFPSRArenaGenParams Params;
		FVector Origin;
		if (!Arena->GetGenParams(Params, Origin))
		{
			Report += FString::Printf(TEXT("[%s] 아레나 파라미터가 지정되지 않아 건너뜁니다.\n\n"), *Arena->GetName());
			continue;
		}

		FString ParamError;
		if (!Params.Validate(ParamError))
		{
			Report += FString::Printf(TEXT("[%s] 파라미터가 기하학적으로 성립하지 않아 건너뜁니다: %s\n\n"), *Arena->GetName(), *ParamError);
			continue;
		}

		// A blocker already inside THIS arena's own grid means a designer started authoring it by hand — the
		// multi-arena sweep must never overwrite that. This replaces the old single-arena tool's "replace?"
		// confirmation: with several arenas processed per click, silently SKIPPING an authored one (and saying so)
		// reads better than a modal per arena.
		bool bAlreadyAuthored = false;
		for (TActorIterator<AFPSRArenaBlocker> It(World); It; ++It)
		{
			const AFPSRArenaBlocker* Blocker = *It;
			if (IsValid(Blocker) && Arena->ContainsWorldLocation(Blocker->GetActorLocation()))
			{
				bAlreadyAuthored = true;
				break;
			}
		}
		if (bAlreadyAuthored)
		{
			UE_LOG(LogFPSR, Log, TEXT("[Arena] ProposeStartingLayout: '%s' already has authored blockers — skipped."), *Arena->GetName());
			SkippedNames.Add(Arena->GetName());
			continue;
		}

		TArray<FFPSRArenaCluster> Clusters;
		FIntPoint Lattice = FIntPoint::ZeroValue;
		if (!FFPSRArenaGenerator::ProposeLatticeClusters(Arena->GetInitialSeed(), Params, Clusters, Lattice) || Clusters.Num() == 0)
		{
			Report += FString::Printf(TEXT("[%s] 시작 배치를 만들지 못했습니다. 출력 로그의 [Arena] 항목을 확인하세요.\n\n"), *Arena->GetName());
			continue;
		}

		const float Height = Arena->GetClusterHeight();
		int32 Spawned = 0;
		for (const FFPSRArenaCluster& Cluster : Clusters)
		{
			// Round-tripped through the SAME cell->world conversion the rasteriser reads back, so what the tool
			// places and what the swarm believes are the same rectangle rather than two that agree by coincidence.
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
		FString ValidationSummary;
		if (Arena->BuildLayoutForSeed(Arena->GetInitialSeed(), Layout))
		{
			ValidationSummary = FFPSRArenaValidator::Summarize(FFPSRArenaValidator::Validate(Layout, Params));
		}
		Report += FString::Printf(TEXT("[%s] 격자 %d×%d · 블로커 %d개 · 시드 %d\n%s\n\n"),
			*Arena->GetName(), Lattice.X, Lattice.Y, Spawned, Arena->GetInitialSeed(), *ValidationSummary);
	}

	if (SkippedNames.Num() > 0)
	{
		Report += FString::Printf(TEXT("이미 블로커가 있어 건너뜀: %s"), *FString::Join(SkippedNames, TEXT(", ")));
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
		LOCTEXT("ProposeDone",
			"아레나 {0}개를 대상으로 시작 배치를 제안했습니다.\n\n{1}\n"
			"이제 손으로 옮기고 크기를 바꾸세요. 손댄 순간부터 순환·통로폭·오목은 검증기가 봅니다 — "
			"Tools > FPSR > '아레나 검증' 을 쓰세요.\n"
			"다른 배치를 보려면 아레나 액터의 '시작 시드' 를 바꾸고 다시 실행하면 됩니다."),
		FText::AsNumber(Arenas.Num()), FText::FromString(Report)));
}

void FFPSRArenaAuthoringTool::ValidateArenaInLevel()
{
	UWorld* World = GetEditorWorld();
	TArray<AFPSRArenaActor*> Arenas;
	if (!FindAllArenasOrComplain(World, Arenas))
	{
		return;
	}

	// Level-wide first: a wrong start-arena or an orphaned marker changes what every per-arena result below MEANS,
	// so it has to be read before them, not appended after.
	FString Body = CheckLevelWideArenaAuthoring(World, Arenas);

	for (AFPSRArenaActor* Arena : Arenas)
	{
		if (!Arena)
		{
			continue;
		}

		FFPSRArenaGenParams Params;
		FVector Origin;
		if (!Arena->GetGenParams(Params, Origin))
		{
			Body += FString::Printf(TEXT("[%s] 아레나 파라미터가 지정되지 않았습니다.\n\n"), *Arena->GetName());
			continue;
		}

		FFPSRArenaLayout Layout;
		if (!Arena->BuildLayoutForSeed(Arena->GetInitialSeed(), Layout))
		{
			Body += FString::Printf(TEXT("[%s] 레이아웃을 만들지 못했습니다. 출력 로그의 [Arena] 항목을 확인하세요.\n\n"), *Arena->GetName());
			continue;
		}

		const FFPSRArenaValidationResult Result = FFPSRArenaValidator::Validate(Layout, Params);

		// The arena's name goes IN the summary line itself, not only as a header above it — so a single line
		// copy-pasted out of a multi-arena report still says which arena it is about.
		Body += FString::Printf(TEXT("[%s] (시드 %d) %s\n"),
			*Arena->GetName(), Arena->GetInitialSeed(), *FFPSRArenaValidator::Summarize(Result));
		if (Result.Errors.Num() > 0)
		{
			Body += TEXT("[오류]\n") + FString::Join(Result.Errors, TEXT("\n")) + TEXT("\n");
		}
		if (Result.Warnings.Num() > 0)
		{
			Body += TEXT("[경고]\n") + FString::Join(Result.Warnings, TEXT("\n")) + TEXT("\n");
		}
		if (Result.Passed() && Result.Warnings.Num() == 0)
		{
			Body += TEXT("통과 — 순환 · 통로 폭 · 오목 없음 전부 만족합니다.\n");
		}
		Body += TEXT("\n");
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
		LOCTEXT("ValidateResult", "아레나 검증 ({0}개)\n\n{1}"),
		FText::AsNumber(Arenas.Num()), FText::FromString(Body)));
}

#undef LOCTEXT_NAMESPACE
