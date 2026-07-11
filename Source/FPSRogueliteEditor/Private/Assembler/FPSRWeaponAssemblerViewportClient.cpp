// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assembler/FPSRWeaponAssemblerViewportClient.h"

#include "Assembler/SFPSRWeaponAssemblerViewport.h"
#include "Assembler/FPSRWeaponAssemblerHelpers.h"
#include "Weapon/FPSRWeaponDataAsset.h"

#include "PreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"

namespace
{
	/** Gets a freshly-created preview component out of the way of the next NewObject with the same (representative,
	 *  variant-stripped) name at the same Outer (GetTransientPackage()) — Rename() with a null NewName generates a
	 *  fresh unique name immediately, instead of waiting for GC to reclaim the old one. Without this, re-selecting
	 *  the same weapon DA (or two DAs sharing a part's stripped name, e.g. two "Barrel" variants) could pick up a
	 *  "_1"-suffixed component name — which BakeSockets would then bake as the socket name too. Cosmetic, not a
	 *  correctness bug, but cheap to avoid outright. */
	void RetireTransientComponent(UActorComponent* Component)
	{
		if (Component)
		{
			Component->Rename(nullptr, nullptr, REN_DontCreateRedirectors | REN_NonTransactional);
		}
	}
}

FFPSRWeaponAssemblerViewportClient::FFPSRWeaponAssemblerViewportClient(FPreviewScene& InPreviewScene, const TSharedRef<SFPSRWeaponAssemblerViewport>& InViewport)
	: FEditorViewportClient(nullptr, &InPreviewScene, StaticCastSharedRef<SEditorViewport>(InViewport))
	, PreviewScene(InPreviewScene)
{
	SetRealtime(true);

	// Side-on framing of the weapon body sitting at the preview scene's origin (Body is always added at identity —
	// see SetWeapon). Derives the look-at rotation from the offset itself instead of hardcoding pitch/yaw degrees.
	const FVector CameraLocation(0.0f, 150.0f, 30.0f);
	SetViewLocation(CameraLocation);
	SetViewRotation((-CameraLocation).Rotation());
}

void FFPSRWeaponAssemblerViewportClient::SetWeapon(UFPSRWeaponDataAsset* DA)
{
	if (BodyComp)
	{
		PreviewScene.RemoveComponent(BodyComp);
		RetireTransientComponent(BodyComp);
		BodyComp = nullptr;
	}
	for (UStaticMeshComponent* PartComp : PartComps)
	{
		if (PartComp)
		{
			PreviewScene.RemoveComponent(PartComp);
			RetireTransientComponent(PartComp);
		}
	}
	PartComps.Reset();
	SelectedPart = INDEX_NONE;

	WeaponDA = DA;
	if (!DA)
	{
		Invalidate();
		return;
	}

	USkeletalMesh* Body = DA->WeaponMesh1P.IsNull() ? nullptr : DA->WeaponMesh1P.LoadSynchronous();
	BodyComp = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	BodyComp->SetSkeletalMeshAsset(Body);
	BodyComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewScene.AddComponent(BodyComp, FTransform::Identity);

	const FTransform RootBoneCS = FPSRWeaponAssemblerHelpers::RootBoneComponentSpace(Body);

	for (int32 i = 0; i < DA->WeaponParts1P.Num(); ++i)
	{
		const FFPSRWeaponPartAttachment& P = DA->WeaponParts1P[i];
		UStaticMesh* M = P.Part.IsNull() ? nullptr : P.Part.LoadSynchronous();

		// Name the component after its part (variant-stripped) so the designer can tell parts apart in the parts
		// list and rename it to control the mount-socket BakeSockets derives from that name.
		const FName CompName = MakeUniqueObjectName(GetTransientPackage(), UStaticMeshComponent::StaticClass(), FName(*FPSRWeaponAssemblerHelpers::MakePartDisplayName(P.Part, i)));
		UStaticMeshComponent* PartComp = NewObject<UStaticMeshComponent>(GetTransientPackage(), CompName, RF_Transient);
		PartComp->SetStaticMesh(M);
		PartComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		// Initial placement (component-space, Body sits at identity so this equals Body-relative). A wired socket is
		// BONE-relative — bring it into component space through the root bone (which carries a 90° roll here) so a
		// re-opened preview shows parts exactly where they attach at runtime; else fall back to the authored Offset.
		FTransform Init = P.Offset;
		if (!P.Socket.IsNone() && Body)
		{
			if (const USkeletalMeshSocket* Socket = Body->FindSocket(P.Socket))
			{
				const FTransform SocketRel(Socket->RelativeRotation, Socket->RelativeLocation, Socket->RelativeScale);
				Init = P.Offset * (SocketRel * RootBoneCS);
			}
		}

		PreviewScene.AddComponent(PartComp, Init);
		PartComps.Add(PartComp);
	}

	UpdatePartVisibility();
	Invalidate();
}

void FFPSRWeaponAssemblerViewportClient::SetSelectedPart(int32 Index)
{
	SelectedPart = PartComps.IsValidIndex(Index) ? Index : INDEX_NONE;
	UpdatePartVisibility();
	Invalidate();
}

void FFPSRWeaponAssemblerViewportClient::SetWidgetMode(UE::Widget::EWidgetMode InMode)
{
	WidgetMode = InMode;
	Invalidate();
}

void FFPSRWeaponAssemblerViewportClient::SwapSelectedPartMesh(UStaticMesh* NewMesh)
{
	// Null NewMesh ignored — a failed catalog load must never blank the selected part (the caller reports the failure).
	if (!NewMesh || !PartComps.IsValidIndex(SelectedPart) || !WeaponDA || !WeaponDA->WeaponParts1P.IsValidIndex(SelectedPart))
	{
		return;
	}

	// Component name (= the mount-socket BakeSockets derives it from) stays untouched — this is a variant swap on
	// the same slot, not a slot reassignment. DA save is BakeSockets'/"조립→저장"'s job; this only updates the
	// in-memory preview + the DA's part reference so a subsequent bake/save picks it up.
	PartComps[SelectedPart]->SetStaticMesh(NewMesh);
	WeaponDA->WeaponParts1P[SelectedPart].Part = NewMesh;

	Invalidate();
}

void FFPSRWeaponAssemblerViewportClient::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEditorViewportClient::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(WeaponDA);
}

FVector FFPSRWeaponAssemblerViewportClient::GetWidgetLocation() const
{
	if (bMoveAll)
	{
		// Whole-assembly pivot = average of the body + every part (all move together, so the gizmo sits at the group
		// centre).
		FVector Sum = FVector::ZeroVector;
		int32 ValidCount = 0;
		if (BodyComp)
		{
			Sum += BodyComp->GetComponentLocation();
			++ValidCount;
		}
		for (const UStaticMeshComponent* PartComp : PartComps)
		{
			if (PartComp)
			{
				Sum += PartComp->GetComponentLocation();
				++ValidCount;
			}
		}
		return ValidCount > 0 ? (Sum / ValidCount) : FVector::ZeroVector;
	}

	if (PartComps.IsValidIndex(SelectedPart) && PartComps[SelectedPart])
	{
		return PartComps[SelectedPart]->GetComponentLocation();
	}
	return FVector::ZeroVector;
}

bool FFPSRWeaponAssemblerViewportClient::InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale)
{
	if (CurrentAxis == EAxisList::None)
	{
		return FEditorViewportClient::InputWidgetDelta(InViewport, CurrentAxis, Drag, Rot, Scale);
	}

	if (bMoveAll)
	{
		// Move the WHOLE assembly — body + every part — together. Translate first...
		if (BodyComp)
		{
			BodyComp->AddWorldOffset(Drag);
		}
		for (UStaticMeshComponent* PartComp : PartComps)
		{
			if (PartComp)
			{
				PartComp->AddWorldOffset(Drag);
			}
		}

		// ...then rotate everything around the (now-translated) group pivot, so a combined drag+rotate on the widget
		// doesn't fight itself: GetWidgetLocation() picks up the post-translate average (body included).
		const FVector Pivot = GetWidgetLocation();
		auto RotateAboutPivot = [&Pivot, &Rot](USceneComponent* Comp)
		{
			const FVector L = Comp->GetComponentLocation();
			Comp->SetWorldLocation(Pivot + Rot.RotateVector(L - Pivot));
			Comp->AddWorldRotation(Rot);
		};
		if (BodyComp)
		{
			RotateAboutPivot(BodyComp);
		}
		for (UStaticMeshComponent* PartComp : PartComps)
		{
			if (PartComp)
			{
				RotateAboutPivot(PartComp);
			}
		}

		Invalidate();
		return true;
	}

	if (PartComps.IsValidIndex(SelectedPart))
	{
		if (UStaticMeshComponent* PartComp = PartComps[SelectedPart])
		{
			PartComp->AddWorldOffset(Drag);
			PartComp->AddWorldRotation(Rot);
			Invalidate();
			return true;
		}
	}
	return FEditorViewportClient::InputWidgetDelta(InViewport, CurrentAxis, Drag, Rot, Scale);
}

void FFPSRWeaponAssemblerViewportClient::TrackingStarted(const FInputEventState& InInputState, bool bIsDraggingWidget, bool bNudge)
{
	Invalidate();
}

void FFPSRWeaponAssemblerViewportClient::TrackingStopped()
{
	Invalidate();
}

void FFPSRWeaponAssemblerViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);

	// Tick the preview world so the skeletal mesh component (ref pose) and any attached ticking state stay current
	// (FAdvancedPreviewScene::Tick only handles capture/lighting-rig updates, not the world itself — see
	// FStaticMeshEditorViewportClient::Tick for the same pattern).
	if (UWorld* World = PreviewScene.GetWorld())
	{
		World->Tick(LEVELTICK_All, DeltaSeconds);
	}
}

void FFPSRWeaponAssemblerViewportClient::UpdatePartVisibility()
{
	for (int32 i = 0; i < PartComps.Num(); ++i)
	{
		if (PartComps[i])
		{
			PartComps[i]->SetVisibility(!bIsolate || i == SelectedPart);
		}
	}
}

UStaticMesh* FFPSRWeaponAssemblerViewportClient::GetSelectedPartMesh() const
{
	if (PartComps.IsValidIndex(SelectedPart) && PartComps[SelectedPart])
	{
		return PartComps[SelectedPart]->GetStaticMesh();
	}
	return nullptr;
}
