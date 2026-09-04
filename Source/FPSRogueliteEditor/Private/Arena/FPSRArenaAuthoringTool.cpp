// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaAuthoringTool.h"

#include "Arena/FPSRArenaActor.h"
#include "Arena/FPSRArenaBakeDataAsset.h"
#include "Arena/FPSRArenaBakeHash.h"
#include "Arena/FPSRArenaDestructible.h"
#include "Arena/FPSRArenaGenerator.h"
#include "Arena/FPSRArenaCells.h"
#include "Arena/FPSRArenaMarkers.h"
#include "Arena/FPSRArenaTypes.h"
#include "Arena/FPSRArenaValidator.h"
#include "Enemy/FPSREnemySpawnPoint.h" // 스폰포인트 저작 검사
#include "Enemy/FPSRHoverWindowCore.h" // FPSRHoverWindow::OccupancyWords/SetOccupied — voxel bake (ADR 0009 S2)
#include "Core/FPSRLogChannels.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "ScopedTransaction.h"
#include "CollisionShape.h"
#include "CollisionQueryParams.h"

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

		// (2b) One arena per level (ADR 0012 invariant 4), and nothing in an arena's level that sits outside its
		// grid. Both are new consequences of arenas becoming streaming sublevels:
		//   - SetArenaActive now shows/hides EVERY actor in the arena's own ULevel, so a second arena in that
		//     level would be dragged in and out of visibility by its neighbour.
		//   - Shared lighting/post-process left in an arena sublevel goes dark the moment that arena is parked,
		//     and doubles up while two arenas are visible at once. It belongs in the persistent level.
		// Actors with no primitive (deferred spawners, notes) are ignored — they have no location that means
		// anything spatially and are not what this check is about.
		TMap<ULevel*, TArray<FString>> ArenasByLevel;
		for (const AFPSRArenaActor* Arena : Arenas)
		{
			if (!Arena) { continue; }
			if (ULevel* Level = Arena->GetLevel())
			{
				ArenasByLevel.FindOrAdd(Level).Add(Arena->GetName());
			}
		}
		for (const TPair<ULevel*, TArray<FString>>& Pair : ArenasByLevel)
		{
			if (Pair.Value.Num() > 1)
			{
				Report += FString::Printf(
					TEXT("[오류] 레벨 하나에 아레나가 %d개입니다(%s). ADR 0012 불변식 4 — 아레나 1개 = 서브레벨 1개입니다.\n"
					     "지금 상태로는 한 아레나를 숨기면 옆 아레나까지 같이 사라집니다.\n"),
					Pair.Value.Num(), *FString::Join(Pair.Value, TEXT(", ")));
			}
		}
		for (const AFPSRArenaActor* Arena : Arenas)
		{
			ULevel* Level = Arena ? Arena->GetLevel() : nullptr;
			if (!Level || ArenasByLevel.FindOrAdd(Level).Num() > 1)
			{
				continue; // the "two arenas in one level" error above already explains this level
			}
			// 겹침 판정이지 중심점 판정이 아니다 — 경계벽은 격자 모서리에 중심을 맞춰 저작되므로 중심으로
			// 재면 정상 저작물이 매번 걸린다(실측: 벽 4개 중 3개). 항상 켜져 있는 경고는 아무도 안 읽는다.
			TArray<FString> Outside;
			for (AActor* Actor : Level->Actors)
			{
				if (!IsValid(Actor) || Actor == Arena) { continue; }
				const USceneComponent* Root = Actor->GetRootComponent();
				if (!Root) { continue; }
				if (!Arena->OverlapsWorldBoundsXY(Root->Bounds))
				{
					Outside.Add(Actor->GetName());
				}
			}
			if (Outside.Num() > 0)
			{
				Report += FString::Printf(
					TEXT("[경고] [%s] 의 레벨에 아레나 격자에 전혀 닿지 않는 액터가 %d개 있습니다: %s\n"
					     "이 아레나가 예비로 대기하는 동안 같이 숨겨지고, 베이크 해시에도 들어가지 않습니다 — "
					     "공용 조명·포스트프로세스라면 지속 레벨로 옮기세요.\n"),
					*Arena->GetName(), Outside.Num(), *FString::Join(Outside, TEXT(", ")));
			}
		}

		// (3) Destructible yaw != 0. AFPSRArenaDestructible's footprint grows along the GRID's own +X/+Y regardless
		// of the actor's rotation (FFPSRArenaCells::ComputeDestructibleCells) — a yawed actor's mesh no longer
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
		if (FFPSRArenaGenerator::BuildLayoutForSeed(*Arena, Arena->GetInitialSeed(), Layout))
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

/**
 * 검증 리포트의 **본문만** 만든다(다이얼로그를 띄우지 않는다).
 *
 * 베이크 직후 같은 다이얼로그에 이어 붙이려고 분리했다(사용자 결정 2026-08-29). 굽기와 검증을 두 번의
 * 메뉴 클릭으로 나눠 두면 "굽고 검증은 안 한" 상태가 만들어지는데, 그게 정확히 ADR 0012 가 막으려는
 * 상태다 — 화면과 적이 서로 다른 마스크를 보고, 그 증상은 실행해 보기 전까지 아무 데도 안 드러난다.
 */
static FString BuildArenaValidationBody(UWorld* World, const TArray<AFPSRArenaActor*>& Arenas)
{
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

		// ADR 0012: 베이크가 있으면 **베이크를 검사한다.** 이게 핵심이다 — 생성기 레이아웃을 검사하면
		// 실제로 런타임이 쓰는 마스크와 다른 것을 보게 되고, 실측으로 그 사고가 났다(2026-08-18: 베이크는
		// 25,600셀 중 6,931셀이 바닥 없음이었는데 검증기는 "전부 열림, PASS"를 찍었다 — 블로커 0개인 빈
		// 생성기 레이아웃을 보고 있었기 때문). 통과 신호가 틀리는 것은 검사가 없는 것보다 나쁘다.
		FFPSRArenaLayout Layout;
		const bool bUsedBake = Arena->BuildValidationLayoutFromBake(Layout);
		if (!bUsedBake && !FFPSRArenaGenerator::BuildLayoutForSeed(*Arena, Arena->GetInitialSeed(), Layout))
		{
			Body += FString::Printf(TEXT("[%s] 검사할 것이 없습니다 — 베이크도 없고 절차 레이아웃도 만들지 못했습니다. 출력 로그의 [Arena] 항목을 확인하세요.\n\n"), *Arena->GetName());
			continue;
		}

		const FFPSRArenaValidationResult Result = FFPSRArenaValidator::Validate(Layout, Params);

		// ── 적 스폰포인트 저작 검사 ──────────────────────────────────────────────────────────────────────
		// 스웜은 스폰포인트가 없으면 **폴백 없이 0마리**다(보스만 플레이어 앞 폴백이 있다). 그런데 런타임은
		// 부적격 점을 조용히 떨어뜨릴 뿐 아무 말도 하지 않으므로, 잘못 놓은 것은 플레이해 보기 전엔 안 보인다.
		// 여기가 그 침묵을 메우는 자리다.
		//
		// 좌표는 반드시 GetSpawnLocation() — 액터 원점이 아니다. 구조형 스포너(Enemy.md C1)는 SpawnAnchor 를
		// 메시 공동 안으로 옮겨 두므로 원점으로 재면 엉뚱한 셀을 본다.
		{
			TArray<FString> OutsideGrid;   // 이 아레나 레벨에 있는데 격자 밖 — 경계 게이트가 영원히 탈락시킨다
			TArray<FString> TrappedNoPath; // 막힌 셀인데 탈출 경로가 없다 — 구조물 안에 갇힌다
			TArray<FString> PathEndsBlocked; // 탈출 경로 끝이 막힌 셀 — 다 따라 나와도 갇힌다
			int32 InArenaCount = 0;

			for (TActorIterator<AFPSREnemySpawnPoint> It(World); It; ++It)
			{
				const AFPSREnemySpawnPoint* Point = *It;
				if (!IsValid(Point))
				{
					continue;
				}

				const bool bInThisLevel = (Point->GetLevel() == Arena->GetLevel());
				const FVector SpawnLoc = Point->GetSpawnLocation();
				const bool bInGrid = Arena->ContainsWorldLocation(SpawnLoc);

				if (!bInGrid)
				{
					// 다른 아레나 소속이면 그 아레나 차례에 검사된다. 이 아레나 레벨 안인데 격자 밖일 때만 지적.
					if (bInThisLevel)
					{
						OutsideGrid.Add(Point->GetName());
					}
					continue;
				}
				++InArenaCount;

				TArray<FVector> ExitPath;
				Point->GetExitPathWorldPoints(ExitPath);

				// 막힌 셀 자체는 오류가 아니다 — 구조형 스포너는 **일부러** 메시 공동(=막힌 셀) 안에서
				// 스폰시킨다(C1 이 정확히 그걸 위해 만들어졌다). 오류인 것은 나올 수단이 없는 것이다.
				const FIntPoint SpawnCell = FFPSRArenaCells::WorldToCell(Layout, SpawnLoc);
				if (!FFPSRArenaCells::IsCellOpen(Layout, SpawnCell.X, SpawnCell.Y) && ExitPath.Num() == 0)
				{
					TrappedNoPath.Add(Point->GetName());
				}

				// 마지막 웨이포인트가 플로우필드 추적으로 넘기는 지점이다. 거기가 막혀 있으면 경로를 다 따라
				// 나와도 갇힌 채로 인계된다.
				if (ExitPath.Num() > 0)
				{
					const FIntPoint EndCell = FFPSRArenaCells::WorldToCell(Layout, ExitPath.Last());
					if (!FFPSRArenaCells::IsCellOpen(Layout, EndCell.X, EndCell.Y))
					{
						PathEndsBlocked.Add(Point->GetName());
					}
				}
			}

			if (InArenaCount == 0)
			{
				Body += FString::Printf(
					TEXT("  [오류] [%s] 격자 안에 적 스폰포인트가 0개입니다 — 런은 돌지만 적이 한 마리도 안 나옵니다(적 스폰엔 폴백이 없습니다).\n"),
					*Arena->GetName());
			}
			if (OutsideGrid.Num() > 0)
			{
				Body += FString::Printf(
					TEXT("  [오류] 이 아레나 레벨에 있는데 격자 밖인 스폰포인트 %d개: %s\n"
					     "  아레나 경계 게이트가 조용히 탈락시켜 영원히 쓰이지 않습니다.\n"),
					OutsideGrid.Num(), *FString::Join(OutsideGrid, TEXT(", ")));
			}
			if (TrappedNoPath.Num() > 0)
			{
				Body += FString::Printf(
					TEXT("  [오류] 막힌 셀에 있으면서 탈출 경로가 없는 스폰포인트 %d개: %s\n"
					     "  적이 구조물 안에 갇힙니다. 구조형 스포너라면 ExitPathRoot 아래에 웨이포인트를, 배치 하나만 고치려면\n"
					     "  그 스폰포인트의 MovePoint(ExitPathPoints)를 쓰세요(Enemy.md C1). 모퉁이 너머로 보낼 때는 '탈출 중 지오메트리 통과'를 끄세요.\n"),
					TrappedNoPath.Num(), *FString::Join(TrappedNoPath, TEXT(", ")));
			}
			if (PathEndsBlocked.Num() > 0)
			{
				Body += FString::Printf(
					TEXT("  [오류] 탈출 경로의 **마지막** 웨이포인트가 막힌 셀인 스폰포인트 %d개: %s\n"
					     "  마지막 점이 플로우필드 추적 인계 지점입니다 — 경로를 다 따라 나와도 갇힙니다.\n"),
					PathEndsBlocked.Num(), *FString::Join(PathEndsBlocked, TEXT(", ")));
			}
			if (InArenaCount > 0)
			{
				Body += FString::Printf(TEXT("  적 스폰포인트 %d개.\n"), InArenaCount);
			}
		}

		// ── 플레이어 진입 지점 저작 검사 (Phase A: 잔존 적 이월) ──────────────────────────────────────────
		// 진입 지점이 막힌 셀에 있어도 런타임(AFPSRArenaActor::GetPlayerEntryTransforms)은 크래시하지 않는다 —
		// 조용히 가장 가까운 열린 셀로 스냅한다(P3 redteam fix). 그러니 여기서 지적하는 것은 "터진다"가 아니라
		// "저작한 위치와 실제로 플레이어가 서는 위치가 다르다"는 것이다(문 앞·엄폐물 뒤 같은 의도된 자리를
		// 벗어날 수 있다). GetPlayerEntryTransforms를 **그대로 호출**해 같은 소스·같은 열거(APlayerStart 이름순
		// 정렬, 없으면 중심 오프셋 폴백)를 그대로 쓴다 — 런타임 스냅이 읽는 Arena->Layout은 에디터에선 아직
		// AdoptArenaField(구 AdoptArenaSurface)가 돈 적이 없어 항상 비어 있으므로(IsValid()==false) 그 함수 안의 SnapToOpenCells는
		// no-op이고, 반환된 좌표는 저작 그대로다. 그 좌표를 이 아레나 검증에 쓰는 (베이크/절차) Layout으로
		// 판정한다 — 스폰포인트 검사와 같은 기준.
		{
			TArray<FTransform> EntryTransforms;
			const bool bHasAuthoredStarts = Arena->GetPlayerEntryTransforms(EntryTransforms);

			TArray<FString> BlockedEntries;
			for (const FTransform& EntryXf : EntryTransforms)
			{
				const FIntPoint EntryCell = FFPSRArenaCells::WorldToCell(Layout, EntryXf.GetLocation());
				if (!FFPSRArenaCells::IsCellOpen(Layout, EntryCell.X, EntryCell.Y))
				{
					BlockedEntries.Add(EntryXf.GetLocation().ToString());
				}
			}
			if (BlockedEntries.Num() > 0)
			{
				Body += FString::Printf(
					TEXT("  [오류] 플레이어 진입 지점 %d개가 막힌 셀에 있습니다: %s\n"
					     "  런타임은 조용히 가장 가까운 열린 셀로 스냅합니다 — 저작한 자리가 아닐 수 있으니 %s 를 열린 셀 위로 옮기세요.\n"),
					BlockedEntries.Num(), *FString::Join(BlockedEntries, TEXT(", ")),
					bHasAuthoredStarts ? TEXT("해당 APlayerStart") : TEXT("아레나(진입점이 없어 중심 폴백을 쓰는 중입니다 — APlayerStart를 놓으세요)"));
			}
		}

		// The arena's name goes IN the summary line itself, not only as a header above it — so a single line
		// copy-pasted out of a multi-arena report still says which arena it is about. WHAT was checked goes in
		// too: a PASS means something completely different depending on whether it read the shipped mask or a
		// procedural layout nothing will ever use.
		Body += FString::Printf(TEXT("[%s] %s %s\n"),
			*Arena->GetName(),
			bUsedBake ? TEXT("(베이크)") : *FString::Printf(TEXT("(절차 · 시드 %d)"), Arena->GetInitialSeed()),
			*FFPSRArenaValidator::Summarize(Result));
		if (!bUsedBake)
		{
			Body += TEXT("  ⚠ 베이크 데이터가 없어 절차 레이아웃을 검사했습니다 — 런타임이 실제로 쓰는 마스크가 아닙니다. 먼저 '아레나 베이크' 를 돌리세요.\n");
		}

		// ADR 0012 5겹 검사 ② — 스테일 대조. 위의 통과/실패보다 이것이 먼저 읽혀야 한다: 베이크가 레벨과
		// 다르면 위 결과는 **지금 레벨이 아니라 옛 레벨**에 대한 판정이라, PASS 든 FAIL 이든 의미가 없다.
		const FFPSRArenaBakeCheck Freshness = FFPSRArenaBakeHash::CheckFreshness(*Arena);
		Body += FString::Printf(TEXT("  %s %s\n"),
			Freshness.IsProblem() ? TEXT("🔴") : TEXT("✔"), *Freshness.Message);
		if (Freshness.Freshness == EFPSRArenaBakeFreshness::Stale && bUsedBake)
		{
			Body += TEXT("  → 위 검사 결과는 지금 레벨이 아니라 마지막으로 구웠던 레벨에 대한 것입니다. 다시 굽기 전에는 통과든 실패든 믿지 마세요.\n");
		}
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

	return Body;
}

void FFPSRArenaAuthoringTool::ValidateArenaInLevel()
{
	UWorld* World = GetEditorWorld();
	TArray<AFPSRArenaActor*> Arenas;
	if (!FindAllArenasOrComplain(World, Arenas))
	{
		return;
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
		LOCTEXT("ValidateResult", "아레나 검증 ({0}개)\n\n{1}"),
		FText::AsNumber(Arenas.Num()), FText::FromString(BuildArenaValidationBody(World, Arenas))));
}

namespace
{
	/**
	 * Editor-only budget guard for the voxel bake (ADR 0011 E1 / 0012's authoring-refusal spirit, adapted). The
	 * voxel cell size is FIXED at 150cm (FFPSRArenaVoxelData::VoxelCellSizeCm) by the P1 plan — unlike the 2D bake
	 * there is no "grow the cell size" escape valve on overflow, so this is a hard refusal, not a coarsen. The
	 * plan's own default footprint (160x160 cells @ 100cm 2D = 16000x16000cm) voxelizes to 107x107x24 = ~275K
	 * probes; these caps leave generous headroom above that before calling an arena "wildly oversized".
	 */
	constexpr int32 MaxVoxelDimPerAxis = 512;
	constexpr int64 MaxVoxelTotal = 4000000;

	/**
	 * Voxelizes WorldStatic collision into a WORLD-space FFPSRArenaVoxelData (ADR 0009 결정 3, S2). Mirrors the 2D
	 * bake's obstacle probe deliberately — same object type (ECC_WorldStatic), same overlap-test shape of query —
	 * so this rides the SAME SourceHash as the 2D bake without redefining "what counts": FPSRArenaBakeHash.h's
	 * ContributesToBake predicate already matches this probe's query set 1:1.
	 *
	 * WorldGridOrigin/Params are the SAME values BakeArenasInLevel already computed for the 2D Request
	 * (Request.GridOrigin / Params.ArenaSizeCells/CellSize) — the two bakes share one arena footprint, just
	 * discretized at two different FIXED cell sizes (100cm 2D, 150cm 3D).
	 *
	 * Returns false (with a human-readable OutError) on a budget overflow (see MaxVoxelDimPerAxis/MaxVoxelTotal) —
	 * refused, never silently coarsened.
	 */
	bool VoxelizeArenaWorld(UWorld* World, const FVector& WorldGridOrigin, const FFPSRArenaGenParams& Params,
		FFPSRArenaVoxelData& OutWorld, FString& OutError)
	{
		const float VoxelSize = FFPSRArenaVoxelData::VoxelCellSizeCm;
		const float SizeX = Params.ArenaSizeCells.X * Params.CellSize;
		const float SizeY = Params.ArenaSizeCells.Y * Params.CellSize;
		const int32 DimX = FMath::Max(1, FMath::CeilToInt(SizeX / VoxelSize));
		const int32 DimY = FMath::Max(1, FMath::CeilToInt(SizeY / VoxelSize));
		const int32 DimZ = FMath::Max(1, FMath::CeilToInt(
			(FFPSRArenaVoxelData::VoxelBelowOriginCm + FFPSRArenaVoxelData::VoxelAboveOriginCm) / VoxelSize));

		const int64 TotalVoxels = static_cast<int64>(DimX) * DimY * DimZ;
		if (DimX > MaxVoxelDimPerAxis || DimY > MaxVoxelDimPerAxis || TotalVoxels > MaxVoxelTotal)
		{
			OutError = FString::Printf(
				TEXT("복셀 격자 %dx%dx%d (150cm 고정 셀) 이 예산을 넘는다(축<=%d, 총<=%lld) — 아레나 XY 크기를 줄일 것. ")
				TEXT("복셀 셀 크기는 고정값이라 2D 베이크처럼 성기게 굽는 대신 거부한다."),
				DimX, DimY, DimZ, MaxVoxelDimPerAxis, MaxVoxelTotal);
			return false;
		}

		OutWorld.VoxelOrigin = FVector(WorldGridOrigin.X, WorldGridOrigin.Y,
			WorldGridOrigin.Z - FFPSRArenaVoxelData::VoxelBelowOriginCm);
		OutWorld.VoxelSize = VoxelSize;
		OutWorld.DimX = DimX;
		OutWorld.DimY = DimY;
		OutWorld.DimZ = DimZ;
		OutWorld.Occupancy.Init(0, FPSRHoverWindow::OccupancyWords(static_cast<int32>(TotalVoxels)));
		OutWorld.FreeVoxels = 0;

		FCollisionObjectQueryParams ObjParams;
		ObjParams.AddObjectTypesToQuery(ECC_WorldStatic);
		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(FPSRArenaVoxelBake), false);
		const FCollisionShape Box = FCollisionShape::MakeBox(FVector(VoxelSize * 0.5f)); // half-extent, tiles exactly

		FScopedSlowTask SlowTask(static_cast<float>(DimZ), LOCTEXT("VoxelizeArenaSlowTask", "아레나 복셀 베이크 중..."));
		SlowTask.MakeDialog();

		for (int32 Z = 0; Z < DimZ; ++Z)
		{
			SlowTask.EnterProgressFrame(1.0f);
			const float CenterZ = OutWorld.VoxelOrigin.Z + (Z + 0.5f) * VoxelSize;
			for (int32 Y = 0; Y < DimY; ++Y)
			{
				const float CenterY = OutWorld.VoxelOrigin.Y + (Y + 0.5f) * VoxelSize;
				for (int32 X = 0; X < DimX; ++X)
				{
					const float CenterX = OutWorld.VoxelOrigin.X + (X + 0.5f) * VoxelSize;
					const int32 Idx = OutWorld.VoxelIndex(X, Y, Z);
					if (World->OverlapAnyTestByObjectType(FVector(CenterX, CenterY, CenterZ), FQuat::Identity, ObjParams, Box, QueryParams))
					{
						FPSRHoverWindow::SetOccupied(OutWorld.Occupancy, Idx);
					}
					else
					{
						++OutWorld.FreeVoxels;
					}
				}
			}
		}
		return true;
	}
}

void FFPSRArenaAuthoringTool::BakeArenasInLevel()
{
	UWorld* World = GetEditorWorld();
	TArray<AFPSRArenaActor*> Arenas;
	if (!FindAllArenasOrComplain(World, Arenas))
	{
		return;
	}

	FString Body;
	int32 NumBaked = 0;
	int32 NumFailed = 0;

	for (AFPSRArenaActor* Arena : Arenas)
	{
		UFPSRArenaBakeDataAsset* Asset = Arena->GetBakeData();
		if (!Asset)
		{
			// 조용히 건너뛰지 않는다. "구웠는데 왜 안 바뀌지"의 가장 흔한 원인이 참조 미설정이고, 그건
			// 리포트에 한 줄 없으면 알아낼 방법이 없다.
			Body += FString::Printf(TEXT("[%s] 건너뜀 — '베이크 데이터' 가 비어 있습니다. UFPSRArenaBakeDataAsset 을 만들어 액터에 물리세요.\n\n"),
				*Arena->GetName());
			++NumFailed;
			continue;
		}

		FFPSRArenaGenParams Params;
		FVector Origin;
		if (!Arena->GetGenParams(Params, Origin))
		{
			Body += FString::Printf(TEXT("[%s] 실패 — 아레나 파라미터를 읽을 수 없습니다('아레나 파라미터' 미설정 또는 유효하지 않음).\n\n"),
				*Arena->GetName());
			++NumFailed;
			continue;
		}

		FFPSRFlowFieldBakeRequest Request;
		Request.GridOrigin = Origin;
		Request.SizeX = Params.ArenaSizeCells.X * Params.CellSize;
		Request.SizeY = Params.ArenaSizeCells.Y * Params.CellSize;
		Request.CellSize = Params.CellSize;
		Request.ClimbableStepHeight = Params.ClimbableStepHeight;
		Request.ProbeApexAboveOrigin = UFPSRFlowFieldComputer::GetDefaultProbeApexAboveOrigin();
		Request.bFailOnBudgetOverflow = true; // 저작 베이크는 조용히 성기게 굽지 않는다 (ADR 0011 E1 · 0012)
		Request.SourceLabel = TEXT("arena editor bake");

		// 트랜지언트 임시 컴퓨터. 레벨의 살아 있는 필드를 건드리지 않고 결과 구조체만 뽑아 온다.
		UFPSRFlowFieldComputer* Temp = NewObject<UFPSRFlowFieldComputer>(GetTransientPackage());
		if (!Temp->BuildFromTraceRequest(World, Request))
		{
			Body += FString::Printf(TEXT("[%s] 실패 — 베이크가 거부됐습니다. 출력 로그의 [FlowField] 줄을 보세요(대개 셀 예산 초과).\n\n"),
				*Arena->GetName());
			++NumFailed;
			continue;
		}

		FFPSRFlowFieldSurfaceData WorldSurface;
		Temp->ExtractSurfaceData(WorldSurface);

		// ADR 0009 S2: voxelize the SAME arena footprint right after the 2D surface, before anything is written —
		// a voxel-budget refusal must abort this arena's WHOLE bake (2D included), not leave Voxels stale while
		// Surface updates (the half-write ADR 0012's write block below exists to prevent).
		FFPSRArenaVoxelData WorldVoxels;
		FString VoxelError;
		if (!VoxelizeArenaWorld(World, Origin, Params, WorldVoxels, VoxelError))
		{
			Body += FString::Printf(TEXT("[%s] 실패 — 복셀 베이크가 거부됐습니다: %s\n\n"), *Arena->GetName(), *VoxelError);
			++NumFailed;
			continue;
		}

		FFPSRFlowFieldSurfaceData LocalSurface;
		if (!UFPSRArenaBakeDataAsset::LocalizeSurface(WorldSurface, Arena->GetActorTransform(), LocalSurface))
		{
			Body += FString::Printf(TEXT("[%s] 실패 — 아레나 트랜스폼을 로컬로 접을 수 없습니다(회전/스케일). 출력 로그 참조.\n\n"),
				*Arena->GetName());
			++NumFailed;
			continue;
		}

		FFPSRArenaVoxelData LocalVoxels;
		if (!UFPSRArenaBakeDataAsset::LocalizeVoxels(WorldVoxels, Arena->GetActorTransform(), LocalVoxels))
		{
			Body += FString::Printf(TEXT("[%s] 실패 — 복셀 데이터를 로컬로 접을 수 없습니다(회전/스케일). 출력 로그 참조.\n\n"),
				*Arena->GetName());
			++NumFailed;
			continue;
		}

		FFPSRArenaBakeSourceDigest Digest;
		if (!FFPSRArenaBakeHash::Compute(*Arena, Digest))
		{
			Body += FString::Printf(TEXT("[%s] 실패 — 소스 해시를 계산할 수 없습니다.\n\n"), *Arena->GetName());
			++NumFailed;
			continue;
		}

		// 열린 셀 수: 검증기와 같은 정의(랭크 0 에 표면이 있고 막히지 않음)를 쓴다. 사람이 리포트 한 줄로
		// "이게 그럴듯한가"를 판단하는 유일한 수치다 — 벌크 배열은 눈으로 볼 수 없다.
		int32 OpenCells = 0;
		const int32 NumCells = LocalSurface.GridDimX * LocalSurface.GridDimY;
		for (int32 Cell = 0; Cell < NumCells; ++Cell)
		{
			const int32 Surf = Cell * UFPSRFlowFieldComputer::NumLayers;
			if (LocalSurface.CellFloorZ.IsValidIndex(Surf)
				&& LocalSurface.CellFloorZ[Surf] != MAX_flt
				&& !LocalSurface.BlockedField[Surf])
			{
				++OpenCells;
			}
		}

		Asset->Modify();
		Asset->Surface = MoveTemp(LocalSurface);
		Asset->Voxels = MoveTemp(LocalVoxels);
		Asset->SourceHash = Digest.Hash;
		Asset->SourceActorCount = Digest.ActorCount;
		Asset->SourceLevel = FSoftObjectPath(Arena->GetTypedOuter<UWorld>());
		Asset->SourceArenaName = Arena->GetFName();
		Asset->BakedAtUtc = FDateTime::UtcNow();
		Asset->OpenCells = OpenCells;
		Asset->MarkPackageDirty();

		// ADR 0009 S2 리포트 수치: 복셀은 51,200 표면 엔트리보다도 커서(107x107x24 ≈ 27만) 더더욱 눈으로 볼
		// 수 없다 — FreeVoxels/총수/점유율/바이트가 유일한 "이게 그럴듯한가" 창이다.
		const int32 NumVoxels = Asset->Voxels.NumVoxels();
		const int32 OccupiedVoxels = NumVoxels - Asset->Voxels.FreeVoxels;
		const float OccupiedPct = (NumVoxels > 0) ? (100.0f * OccupiedVoxels / static_cast<float>(NumVoxels)) : 0.0f;
		const int64 VoxelBytes = static_cast<int64>(Asset->Voxels.Occupancy.Num()) * static_cast<int64>(sizeof(uint64));

		++NumBaked;
		Body += FString::Printf(
			TEXT("[%s] → %s\n  격자 %dx%d (셀 %.0fcm) · 열린 셀 %d / %d\n  복셀 %dx%dx%d (셀 %.0fcm) · 자유 %d / %d (점유 %.1f%%) · %lld바이트\n  소스 액터 %d개 (컴포넌트 %d) · 해시 %s\n\n"),
			*Arena->GetName(), *Asset->GetName(),
			Asset->Surface.GridDimX, Asset->Surface.GridDimY, Asset->Surface.CellSize,
			OpenCells, NumCells,
			Asset->Voxels.DimX, Asset->Voxels.DimY, Asset->Voxels.DimZ, Asset->Voxels.VoxelSize,
			Asset->Voxels.FreeVoxels, NumVoxels, OccupiedPct, VoxelBytes,
			Digest.ActorCount, Digest.ComponentCount, *Digest.Hash.Left(12));

		// 같은 내용을 로그에도 남긴다. 다이얼로그는 닫는 순간 사라지므로, 나중에 "그때 몇 개가 잡혔더라"를
		// 물으면 답할 방법이 없다 — 베이크가 옳았는지는 이 숫자들로만 판정할 수 있다.
		UE_LOG(LogFPSR, Log,
			TEXT("[Arena] BAKE %s -> %s: grid %dx%d cell=%.0f, open %d/%d cells, sources %d actor(s) / %d component(s), hash %s"),
			*Arena->GetName(), *Asset->GetName(),
			Asset->Surface.GridDimX, Asset->Surface.GridDimY, Asset->Surface.CellSize,
			OpenCells, NumCells, Digest.ActorCount, Digest.ComponentCount, *Digest.Hash);
		UE_LOG(LogFPSR, Log,
			TEXT("[Arena] BAKE %s -> %s: voxels %dx%dx%d cell=%.0f, free %d/%d (%.1f%% occupied), %lld bytes"),
			*Arena->GetName(), *Asset->GetName(),
			Asset->Voxels.DimX, Asset->Voxels.DimY, Asset->Voxels.DimZ, Asset->Voxels.VoxelSize,
			Asset->Voxels.FreeVoxels, NumVoxels, OccupiedPct, VoxelBytes);

		// 액터가 거의 안 잡혔다는 것은 대개 콜리전 설정 사고다(불변식 2: 콜리전이 곧 마스크). 에셋
		// 검증에서도 잡지만, 방금 구운 사람 눈앞에 띄우는 편이 훨씬 빠르다.
		if (Digest.ComponentCount <= 1)
		{
			Body += FString::Printf(
				TEXT("  ⚠ 소스가 %d개뿐입니다. 벽 메시가 WorldStatic + Query 콜리전인지 확인하세요 — 아닌 것은 베이크에 안 들어갑니다.\n\n"),
				Digest.ComponentCount);
		}
	}

	Body += TEXT("에셋은 더티 상태로 두었습니다 — 저장(Ctrl+S)해야 반영됩니다.\n");

	// 굽고 나면 **곧바로** 검증까지 돌린다(사용자 결정 2026-08-29). 종전에는 "다음: '아레나 검증' 을
	// 실행하세요" 라는 안내 한 줄이었는데, 안내는 안 지켜도 아무도 모른다 — 그리고 안 지킨 상태가 정확히
	// ADR 0012 가 막으려는 것이다(화면과 적이 다른 마스크를 본다). 방금 구운 해시로 검사하므로 스테일
	// 대조는 여기서 항상 초록이고, 그래서 남는 지적은 전부 **지금 레벨의 진짜 문제**다.
	// 하나도 못 구웠으면 붙이지 않는다 — 참조 미설정 같은 선행 문제를 검증 리포트가 덮어 버린다.
	if (NumBaked > 0)
	{
		Body += TEXT("\n──────────── 이어서 아레나 검증 ────────────\n\n");
		Body += BuildArenaValidationBody(World, Arenas);
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
		LOCTEXT("BakeResult", "아레나 베이크 — 성공 {0} / 실패·건너뜀 {1}\n\n{2}"),
		FText::AsNumber(NumBaked), FText::AsNumber(NumFailed), FText::FromString(Body)));
}

#undef LOCTEXT_NAMESPACE
