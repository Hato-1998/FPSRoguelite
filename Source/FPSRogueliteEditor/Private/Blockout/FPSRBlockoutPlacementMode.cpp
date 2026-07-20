// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blockout/FPSRBlockoutPlacementMode.h"
#include "Blockout/FPSRBlockoutSettings.h"
#include "Blockout/FPSRBlockoutSpawn.h"

#include "Editor.h"                       // GEditor, GLevelEditorModeTools, GLevelEditorModeToolsIsValid
#include "EditorModeManager.h"            // FEditorModeTools::DeactivateMode
#include "EditorViewportClient.h"         // FEditorViewportClient, FViewportCursorLocation, FViewportClick
#include "SceneView.h"                    // FSceneView / FSceneViewFamilyContext
#include "SceneManagement.h"              // FPrimitiveDrawInterface::DrawLine, SDPG_*
#include "PrimitiveDrawingUtils.h"        // DrawWireBox (moved out of SceneManagement.h in UE5)
#include "ScopedTransaction.h"
#include "CollisionQueryParams.h"         // FCollisionQueryParams / SCENE_QUERY_STAT (cursor-ray ghost trace + magnetic overlap)
#include "Engine/OverlapResult.h"         // FOverlapResult (magnetic neighbor sphere-overlap, R3b)
#include "Textures/SlateIcon.h"
#include "InputCoreTypes.h"               // EKeys

#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"       // AStaticMeshActor (magnetic neighbor filter, R3b)
#include "Components/MeshComponent.h"     // UMeshComponent (ghost material override, R3c)
#include "Materials/MaterialInterface.h"  // UMaterialInterface (ghost material override, R3c)
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "FPSRBlockoutPlacementMode"

const FEditorModeID UFPSRBlockoutPlacementMode::EM_BlockoutPlacement(TEXT("EM_FPSRBlockoutPlacement"));

UFPSRBlockoutPlacementMode::UFPSRBlockoutPlacementMode()
{
	// The asset-editor subsystem auto-registers every UEdMode by reading this Info off the CDO — no manual RegisterMode.
	Info = FEditorModeInfo(
		EM_BlockoutPlacement,
		LOCTEXT("ModeName", "FPSR 블록아웃 배치"),
		FSlateIcon(),
		/*bVisible=*/true);
}

UWorld* UFPSRBlockoutPlacementMode::GetEditorWorld() const
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

void UFPSRBlockoutPlacementMode::Enter()
{
	Super::Enter();
	const UFPSRBlockoutSettings* Settings = GetDefault<UFPSRBlockoutSettings>();
	GridSize = Settings ? Settings->PlacementGridSize : 100.0f;
	RotationSnapDegrees = Settings ? Settings->RotationSnapDegrees : 90.0f;
	SnapRadius = Settings ? Settings->SnapRadius : 0.0f;
	CurrentRotation = FRotator::ZeroRotator;
	RebuildGhost();
}

void UFPSRBlockoutPlacementMode::Exit()
{
	DestroyGhost();
	Super::Exit();
}

void UFPSRBlockoutPlacementMode::SetAssetToPlace(const FAssetData& InAsset)
{
	AssetToPlace = InAsset;
	RebuildGhost();
}

void UFPSRBlockoutPlacementMode::DestroyGhost()
{
	if (AActor* Ghost = GhostActor.Get())
	{
		if (UWorld* World = Ghost->GetWorld())
		{
			World->DestroyActor(Ghost);
		}
	}
	GhostActor.Reset();
}

void UFPSRBlockoutPlacementMode::RebuildGhost()
{
	DestroyGhost();

	UWorld* World = GetEditorWorld();
	if (!World || !AssetToPlace.IsValid())
	{
		return;
	}

	// FVector::ZeroVector here — MouseMove positions the ghost on the next tick; CurrentRotation carries over so a
	// rebuild (e.g. asset change) mid-placement doesn't reset the yaw the designer already dialed in.
	AActor* Ghost = FFPSRBlockoutSpawn::SpawnPiece(World, AssetToPlace, FTransform(CurrentRotation, FVector::ZeroVector), /*bTransientGhost=*/true);
	if (!Ghost)
	{
		return;
	}
	GhostActor = Ghost;

	// R3c: translucent preview material — override every material slot on every mesh component so the ghost reads as
	// a see-through preview instead of an opaque stand-in. Unset GhostMaterial (designer hasn't authored one yet, or
	// doesn't want the override) = leave the piece's own materials untouched (solid ghost, pre-R3c behavior).
	const UFPSRBlockoutSettings* Settings = GetDefault<UFPSRBlockoutSettings>();
	UMaterialInterface* GhostMaterial = Settings ? Settings->GhostMaterial.LoadSynchronous() : nullptr;
	if (GhostMaterial)
	{
		TArray<UMeshComponent*> MeshComponents;
		Ghost->GetComponents<UMeshComponent>(MeshComponents);
		for (UMeshComponent* MeshComp : MeshComponents)
		{
			if (!MeshComp)
			{
				continue;
			}
			for (int32 SlotIndex = 0; SlotIndex < MeshComp->GetNumMaterials(); ++SlotIndex)
			{
				MeshComp->SetMaterial(SlotIndex, GhostMaterial);
			}
		}
	}
}

bool UFPSRBlockoutPlacementMode::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	if (!ViewportClient || !Viewport)
	{
		return false;
	}

	// Canonical mode deproject pattern (mirrors FEdModeFoliage::MouseMove): build a scene view, wrap the cursor.
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Viewport, ViewportClient->GetScene(), ViewportClient->EngineShowFlags).SetRealtimeUpdate(ViewportClient->IsRealtime()));
	FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
	if (!View)
	{
		return false;
	}

	FViewportCursorLocation Cursor(View, ViewportClient, x, y);

	UWorld* World = GetEditorWorld();
	AActor* Ghost = GhostActor.Get();
	if (!World || !Ghost)
	{
		bHasHit = false;
		ViewportClient->Invalidate();
		return false;
	}

	// 1) Manual line trace along the cursor ray — a Minecraft-style "snap to the pointed SURFACE" needs the actual hit
	//    actor + face normal (FActorPositioning::TraceWorldForPosition only ever returns a floor point, never who/where
	//    on a wall was hit), so we trace ourselves instead of using the editor's placement-trace helper.
	FHitResult CursorHit;
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(FPSRBlockoutGhostTrace), /*bTraceComplex=*/false);
	TraceParams.AddIgnoredActor(Ghost);
	const FVector TraceStart = Cursor.GetOrigin();
	const FVector TraceEnd = TraceStart + Cursor.GetDirection() * 1000000.0f;
	bHasHit = World->LineTraceSingleByChannel(CursorHit, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams);
	if (!bHasHit)
	{
		ViewportClient->Invalidate();
		return false;
	}

	// 2) Rotate FIRST, then read the ghost's WORLD AABB at a KNOWN location (origin) so GhostExtent / PivotToCenter
	//    below are rotation-correct but position-invariant (Synty pieces have corner/edge pivots, not center pivots —
	//    e.g. SM_Bld_Base_Wall_01 pivots at its X-min end — so the raw hit point can't be used as the spawn pivot).
	Ghost->SetActorRotation(CurrentRotation);
	Ghost->SetActorLocation(FVector::ZeroVector);
	FVector BoundsOrigin, GhostExtent;
	Ghost->GetActorBounds(/*bOnlyCollidingComponents=*/false, BoundsOrigin, GhostExtent);
	const FVector PivotToCenter = BoundsOrigin; // pivot(origin) → bbox center offset, world space, this rotation

	// 3) MAGNETIC neighbor search (R3b) — sphere-overlap around the cursor hit point looking for an ALREADY-PLACED
	//    Blockout piece to snap against, even when the cursor ray is pointing at open floor NEAR the piece rather than
	//    directly at one of its faces. Only tool-placed pieces count (outliner "Blockout" folder) — floor/vendor
	//    geometry never magnet-snaps. SnapRadius 0 (unset in settings) falls back to GridSize as the search radius.
	const float EffectiveSnapRadius = SnapRadius > 0.0f ? SnapRadius : FMath::Max(GridSize, 1.0f);
	AActor* NeighborActor = nullptr;
	FBox NeighborBox;
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams OverlapParams(SCENE_QUERY_STAT(FPSRBlockoutGhostSnapOverlap), /*bTraceComplex=*/false);
		OverlapParams.AddIgnoredActor(Ghost);
		if (World->OverlapMultiByChannel(Overlaps, CursorHit.ImpactPoint, FQuat::Identity, ECC_WorldStatic,
				FCollisionShape::MakeSphere(EffectiveSnapRadius), OverlapParams))
		{
			float BestDistSq = TNumericLimits<float>::Max();
			for (const FOverlapResult& Overlap : Overlaps)
			{
				// 배치도구가 만든 조각만 자석 스냅 대상(바닥/벤더 지오메트리 제외). AStaticMeshActor뿐 아니라 하베스트된
				// BP_* 프리팹(AActor 파생, StaticMeshActor 아님)도 포함 — SpawnPiece가 둘 다 "Blockout" 폴더에 넣으므로
				// 클래스가 아니라 폴더로 판정한다(Codex P2: 캐스트가 BP 프리팹을 놓쳐 자석 스냅 대상서 빠지던 문제).
				AActor* Candidate = Overlap.GetActor();
				if (!Candidate || !Candidate->GetFolderPath().ToString().StartsWith(TEXT("Blockout")))
				{
					continue;
				}
				const FBox CandidateBox = Candidate->GetComponentsBoundingBox();
				if (!CandidateBox.IsValid)
				{
					continue; // 메시 바운드가 없는 액터(순수 볼륨 등)는 자석 대상 아님
				}
				const float DistSq = FVector::DistSquared(CandidateBox.GetCenter(), CursorHit.ImpactPoint);
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					NeighborActor = Candidate;
					NeighborBox = CandidateBox;
				}
			}
		}
	}

	FVector TargetCenter;
	if (NeighborActor)
	{
		// 4) Magnetic face-snap: which SIDE of the neighbor the cursor is on decides the attach face — the dominant
		//    axis of the cursor→neighbor-center vector. This reads more robustly than the raw ray normal when the
		//    cursor is near-but-not-on the neighbor (the whole point of magnetic snapping).
		// 자석 스냅은 "이웃 옆에 붙이는"(커서가 이웃 근처 바닥을 가리키는) 제스처라 붙일 면은 항상 HORIZONTAL 측면 —
		// 지배축을 X/Y 중에서만 고르고 Z는 배제한다. 안 그러면 높은 이웃(벽은 중심이 바닥보다 한참 위)에서 |Dir.Z|가
		// 이겨 조각이 옆이 아니라 위/아래로 붙는다(Codex P2). 위에 쌓기는 아래 direct-hit 경로(커서 레이가 윗면 히트
		// → 노멀 +Z)가 담당하지, 자석 경로가 아니다.
		const FVector Dir = CursorHit.ImpactPoint - NeighborBox.GetCenter();
		const int32 Axis = (FMath::Abs(Dir.Y) > FMath::Abs(Dir.X)) ? 1 : 0;
		const float Sign = FMath::Sign(Dir[Axis]);

		const float NeighborFace = (Sign > 0.0f) ? NeighborBox.Max[Axis] : NeighborBox.Min[Axis];
		TargetCenter = CursorHit.ImpactPoint;
		TargetCenter[Axis] = NeighborFace + Sign * GhostExtent[Axis];

		// Tangential axes: grid-snap the ghost bbox MIN edge using the cursor hit as reference (same Minecraft-style
		// tiling as the floor-snap fallback below).
		for (int32 T = 0; T < 3; ++T)
		{
			if (T == Axis || GridSize <= 0.0f)
			{
				continue;
			}
			const float MinEdge = CursorHit.ImpactPoint[T] - GhostExtent[T];
			TargetCenter[T] = FMath::GridSnap(MinEdge, GridSize) + GhostExtent[T];
		}

		// Horizontal attach face (wall-to-wall, Axis == X/Y): grid-snapping Z as a tangential axis would float the
		// piece off the ground. Override it — rest the ghost's bbox BOTTOM on the neighbor's bbox BOTTOM so wall
		// pieces line up vertically with their neighbor instead of floating at cursor height.
		if (Axis != 2)
		{
			TargetCenter.Z = NeighborBox.Min.Z + GhostExtent.Z;
		}
	}
	else
	{
		// 4-Fallback) No Blockout neighbor within SnapRadius — flush/grid snap on whatever surface the cursor ray
		//    actually hit (floor, vendor geometry, …), exactly as before magnetic snapping existed.
		const FVector N = CursorHit.ImpactNormal;
		int32 Axis = 0;
		if (FMath::Abs(N.Y) > FMath::Abs(N[Axis])) { Axis = 1; }
		if (FMath::Abs(N.Z) > FMath::Abs(N[Axis])) { Axis = 2; }
		const float Sign = FMath::Sign(N[Axis]);

		TargetCenter = CursorHit.ImpactPoint;
		TargetCenter[Axis] = CursorHit.ImpactPoint[Axis] + Sign * GhostExtent[Axis];
		for (int32 T = 0; T < 3; ++T)
		{
			if (T == Axis || GridSize <= 0.0f)
			{
				continue;
			}
			const float MinEdge = CursorHit.ImpactPoint[T] - GhostExtent[T];
			TargetCenter[T] = FMath::GridSnap(MinEdge, GridSize) + GhostExtent[T];
		}

		if (Axis != 2)
		{
			const float NeighborBottomZ = CursorHit.GetActor() ? CursorHit.GetActor()->GetComponentsBoundingBox().Min.Z : CursorHit.ImpactPoint.Z;
			TargetCenter.Z = NeighborBottomZ + GhostExtent.Z;
		}
	}

	// 5) bbox center → pivot, then place.
	const FVector TargetPivot = TargetCenter - PivotToCenter;
	Ghost->SetActorLocation(TargetPivot);
	CurrentLocation = TargetPivot;
	bHasHit = true;

	ViewportClient->Invalidate();
	return false; // don't consume — let camera navigation keep working
}

bool UFPSRBlockoutPlacementMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (Key == EKeys::Escape && Event == IE_Pressed)
	{
		// Inside an active mode's own InputKey the global mode manager is guaranteed valid (the deprecated
		// GLevelEditorModeToolsIsValid() check is unnecessary here).
		GLevelEditorModeTools().DeactivateMode(EM_BlockoutPlacement);
		return true;
	}

	if (Event == IE_Pressed && (Key == EKeys::RightBracket || Key == EKeys::LeftBracket))
	{
		// SimCity-style quick-rotate: [ / ] step the ghost's (and next spawn's) yaw by RotationSnapDegrees.
		CurrentRotation.Yaw += (Key == EKeys::RightBracket) ? RotationSnapDegrees : -RotationSnapDegrees;
		CurrentRotation.Yaw = FRotator::NormalizeAxis(CurrentRotation.Yaw);
		if (AActor* Ghost = GhostActor.Get())
		{
			Ghost->SetActorRotation(CurrentRotation);
		}
		if (ViewportClient)
		{
			ViewportClient->Invalidate();
		}
		return true;
	}

	return false;
}

bool UFPSRBlockoutPlacementMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (Click.GetKey() == EKeys::LeftMouseButton && bHasHit)
	{
		SpawnAtCurrent();
		return true; // consume — place instead of select/deselect
	}
	return false;
}

void UFPSRBlockoutPlacementMode::SpawnAtCurrent()
{
	UWorld* World = GetEditorWorld();
	if (!World || !AssetToPlace.IsValid())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("PlaceTx", "블록아웃 배치(뷰포트)"));
	FFPSRBlockoutSpawn::SpawnPiece(World, AssetToPlace, FTransform(CurrentRotation, CurrentLocation), /*bTransientGhost=*/false);
}

void UFPSRBlockoutPlacementMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	Super::Render(View, Viewport, PDI);

	if (!bHasHit || !PDI)
	{
		return;
	}

	// Snap box at the placement point.
	const float BoxHalf = FMath::Max(GridSize, 50.0f) * 0.5f;
	const FVector BoxExtent(BoxHalf, BoxHalf, BoxHalf);
	DrawWireBox(PDI, FBox(CurrentLocation - BoxExtent, CurrentLocation + BoxExtent), FLinearColor::Yellow, SDPG_Foreground, 2.0f);

	// Grid lines around the snap point (only when snapping is on).
	if (GridSize > 0.0f)
	{
		const int32 NumLines = 6;
		const float Span = GridSize * NumLines;
		const FVector Base(FMath::GridSnap(CurrentLocation.X, GridSize), FMath::GridSnap(CurrentLocation.Y, GridSize), CurrentLocation.Z + 1.0f);
		const FLinearColor GridColor(0.3f, 0.7f, 1.0f, 0.5f);
		for (int32 i = -NumLines; i <= NumLines; ++i)
		{
			const float Offset = i * GridSize;
			PDI->DrawLine(Base + FVector(Offset, -Span, 0.0f), Base + FVector(Offset, Span, 0.0f), GridColor, SDPG_World, 0.5f);
			PDI->DrawLine(Base + FVector(-Span, Offset, 0.0f), Base + FVector(Span, Offset, 0.0f), GridColor, SDPG_World, 0.5f);
		}
	}
}

#undef LOCTEXT_NAMESPACE
