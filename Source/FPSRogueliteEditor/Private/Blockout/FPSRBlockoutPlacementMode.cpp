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
#include "CollisionQueryParams.h"         // FCollisionQueryParams / SCENE_QUERY_STAT (cursor-ray ghost trace)
#include "Textures/SlateIcon.h"
#include "InputCoreTypes.h"               // EKeys

#include "Engine/World.h"
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
	if (Ghost)
	{
		GhostActor = Ghost;
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
	FHitResult Hit;
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(FPSRBlockoutGhostTrace), /*bTraceComplex=*/false);
	TraceParams.AddIgnoredActor(Ghost);
	const FVector TraceStart = Cursor.GetOrigin();
	const FVector TraceEnd = TraceStart + Cursor.GetDirection() * 1000000.0f;
	bHasHit = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, TraceParams);
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

	// 3) Dominant surface axis from the hit normal — which face got hit (floor/ceiling = Z, wall = X or Y).
	const FVector N = Hit.ImpactNormal;
	int32 Axis = 0;
	if (FMath::Abs(N.Y) > FMath::Abs(N[Axis])) { Axis = 1; }
	if (FMath::Abs(N.Z) > FMath::Abs(N[Axis])) { Axis = 2; }
	const float Sign = FMath::Sign(N[Axis]);

	// 4) Target bbox CENTER: flush against the surface along the normal axis; grid-snap the two TANGENTIAL axes so
	//    same-size pieces tile edge-to-edge (snap the bbox MIN edge, not the pivot/cursor point — Minecraft-style).
	FVector TargetCenter = Hit.ImpactPoint;
	TargetCenter[Axis] = Hit.ImpactPoint[Axis] + Sign * GhostExtent[Axis];
	for (int32 T = 0; T < 3; ++T)
	{
		if (T == Axis || GridSize <= 0.0f)
		{
			continue;
		}
		const float MinEdge = Hit.ImpactPoint[T] - GhostExtent[T];
		TargetCenter[T] = FMath::GridSnap(MinEdge, GridSize) + GhostExtent[T];
	}

	// 5) Wall face (Axis == X/Y): grid-snapping Z (done above as a tangential axis) would float the piece off the
	//    ground. Override it instead — rest the ghost's bbox BOTTOM on the hit actor's bbox BOTTOM so wall pieces line
	//    up vertically with their neighbor rather than floating at cursor height.
	if (Axis != 2)
	{
		const float NeighborBottomZ = Hit.GetActor() ? Hit.GetActor()->GetComponentsBoundingBox().Min.Z : Hit.ImpactPoint.Z;
		TargetCenter.Z = NeighborBottomZ + GhostExtent.Z;
	}

	// 6) bbox center → pivot, then place.
	const FVector TargetPivot = TargetCenter - PivotToCenter;
	Ghost->SetActorLocation(TargetPivot);
	CurrentLocation = TargetPivot;

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
