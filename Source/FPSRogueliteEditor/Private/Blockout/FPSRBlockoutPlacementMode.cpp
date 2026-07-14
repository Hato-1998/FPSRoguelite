// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blockout/FPSRBlockoutPlacementMode.h"
#include "Blockout/FPSRBlockoutSettings.h"

#include "Editor.h"                       // GEditor, GLevelEditorModeTools, GLevelEditorModeToolsIsValid
#include "EditorModeManager.h"            // FEditorModeTools::DeactivateMode
#include "EditorViewportClient.h"         // FEditorViewportClient, FViewportCursorLocation, FViewportClick
#include "Editor/ActorPositioning.h"      // FActorPositioning::TraceWorldForPosition
#include "SceneView.h"                    // FSceneView / FSceneViewFamilyContext
#include "SceneManagement.h"              // FPrimitiveDrawInterface::DrawLine, SDPG_*
#include "PrimitiveDrawingUtils.h"        // DrawWireBox (moved out of SceneManagement.h in UE5)
#include "ScopedTransaction.h"
#include "CollisionQueryParams.h"         // FCollisionQueryParams / SCENE_QUERY_STAT (floor down-trace)
#include "Textures/SlateIcon.h"
#include "InputCoreTypes.h"               // EKeys

#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Blueprint.h"
#include "Components/StaticMeshComponent.h"
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

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transient;
	SpawnParams.bTemporaryEditorActor = true;   // excluded from outliner / save

	AActor* Ghost = nullptr;
	const bool bIsBP = (AssetToPlace.AssetClassPath == UBlueprint::StaticClass()->GetClassPathName());
	if (bIsBP)
	{
		UBlueprint* BP = Cast<UBlueprint>(AssetToPlace.GetAsset());
		if (BP && BP->GeneratedClass && BP->GeneratedClass->IsChildOf(AActor::StaticClass()))
		{
			Ghost = World->SpawnActor<AActor>(BP->GeneratedClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		}
	}
	else
	{
		UStaticMesh* Mesh = Cast<UStaticMesh>(AssetToPlace.GetAsset());
		if (Mesh)
		{
			AStaticMeshActor* MeshActor = World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
			if (MeshActor && MeshActor->GetStaticMeshComponent())
			{
				MeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			}
			Ghost = MeshActor;
		}
	}

	if (Ghost)
	{
		Ghost->bIsEditorPreviewActor = true;
		Ghost->SetActorEnableCollision(false);
		GhostActor = Ghost;
	}
}

FVector UFPSRBlockoutPlacementMode::SnapLocation(const FVector& In) const
{
	if (GridSize <= 0.0f)
	{
		return In;
	}
	return FVector(FMath::GridSnap(In.X, GridSize), FMath::GridSnap(In.Y, GridSize), In.Z);
}

bool UFPSRBlockoutPlacementMode::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y)
{
	if (!ViewportClient || !Viewport)
	{
		return false;
	}

	// Canonical mode deproject pattern (mirrors FEdModeFoliage::MouseMove): build a scene view, wrap the cursor, then
	// use the editor's own placement trace to find the floor position (+ surface normal).
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		Viewport, ViewportClient->GetScene(), ViewportClient->EngineShowFlags).SetRealtimeUpdate(ViewportClient->IsRealtime()));
	FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
	if (!View)
	{
		return false;
	}

	FViewportCursorLocation Cursor(View, ViewportClient, x, y);

	TArray<AActor*> IgnoreActors;
	if (GhostActor.IsValid())
	{
		IgnoreActors.Add(GhostActor.Get());
	}

	// 1) Cursor ray → the surface the cursor points at (which floor/static, and roughly where).
	const FActorPositionTraceResult Trace = FActorPositioning::TraceWorldForPosition(Cursor, *View, &IgnoreActors);
	bHasHit = (Trace.State == FActorPositionTraceResult::HitSuccess);
	if (bHasHit)
	{
		// 2) Grid-snap X/Y (Z passthrough).
		FVector Target = SnapLocation(Trace.Location);

		// 3) Re-trace straight DOWN at the snapped X/Y to find the true floor Z there, so a grid-snapped piece sits on
		//    the floor beneath its cell (END-key style) rather than at the slightly-off cursor Z. Falls back to the
		//    cursor hit Z if the down-trace finds nothing (e.g. snapped over a gap).
		float FloorZ = Trace.Location.Z;
		if (UWorld* World = GetEditorWorld())
		{
			FHitResult DownHit;
			const FVector DownStart(Target.X, Target.Y, Trace.Location.Z + 100000.0f);
			const FVector DownEnd(Target.X, Target.Y, Trace.Location.Z - 100000.0f);
			FCollisionQueryParams DownParams(SCENE_QUERY_STAT(FPSRBlockoutGhostFloor), /*bTraceComplex=*/false);
			if (GhostActor.IsValid())
			{
				DownParams.AddIgnoredActor(GhostActor.Get());
			}
			if (World->LineTraceSingleByChannel(DownHit, DownStart, DownEnd, ECC_WorldStatic, DownParams))
			{
				FloorZ = DownHit.ImpactPoint.Z;
			}
		}
		Target.Z = FloorZ;

		// 4) Lift so the ghost's (visual) bounding-box BOTTOM rests on the floor, not its pivot — a center-pivot mesh
		//    would otherwise sink halfway into the floor. bOnlyCollidingComponents=false so the ghost's disabled
		//    collision doesn't zero the bounds.
		if (AActor* Ghost = GhostActor.Get())
		{
			Ghost->SetActorLocation(Target);
			FVector BoundsOrigin, BoundsExtent;
			Ghost->GetActorBounds(/*bOnlyCollidingComponents=*/false, BoundsOrigin, BoundsExtent);
			const float BottomZ = BoundsOrigin.Z - BoundsExtent.Z;
			Target.Z += (FloorZ - BottomZ);
			Ghost->SetActorLocation(Target);
		}

		CurrentLocation = Target;
	}

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

	const bool bIsBP = (AssetToPlace.AssetClassPath == UBlueprint::StaticClass()->GetClassPathName());

	const FScopedTransaction Transaction(LOCTEXT("PlaceTx", "블록아웃 배치(뷰포트)"));

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transactional;

	AActor* NewActor = nullptr;
	if (bIsBP)
	{
		UBlueprint* BP = Cast<UBlueprint>(AssetToPlace.GetAsset());
		if (!BP || !BP->GeneratedClass || !BP->GeneratedClass->IsChildOf(AActor::StaticClass()))
		{
			return;
		}
		NewActor = World->SpawnActor<AActor>(BP->GeneratedClass, CurrentLocation, FRotator::ZeroRotator, SpawnParams);
	}
	else
	{
		UStaticMesh* Mesh = Cast<UStaticMesh>(AssetToPlace.GetAsset());
		if (!Mesh)
		{
			return;
		}
		AStaticMeshActor* MeshActor = World->SpawnActor<AStaticMeshActor>(CurrentLocation, FRotator::ZeroRotator, SpawnParams);
		if (MeshActor && MeshActor->GetStaticMeshComponent())
		{
			MeshActor->GetStaticMeshComponent()->Modify();
			MeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
			MeshActor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll")); // WorldStatic (K4=B / K14)
		}
		NewActor = MeshActor;
	}

	if (NewActor)
	{
		NewActor->Modify();
		NewActor->SetActorLabel(AssetToPlace.AssetName.ToString());
		NewActor->SetFolderPath(TEXT("Blockout"));
	}
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
