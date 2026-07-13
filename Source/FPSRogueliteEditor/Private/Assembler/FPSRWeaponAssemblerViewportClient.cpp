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
	// 무기 자체가 바뀌므로 단계 미리보기는 캡처 없이 그냥 리셋한다 — EndStagePreview()를 부르면 옛 DA(WeaponDA, 곧 교체될)에
	// 오프셋을 써버리는 꼴이라 잘못됐다. 프리뷰 컴포넌트도 바로 아래서 전부 재생성되므로 base 메시 참조도 의미가 없다.
	PreviewStageSlot = INDEX_NONE;
	PreviewStageIndex = INDEX_NONE;
	PreviewStageBaseMesh = nullptr;

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

void FFPSRWeaponAssemblerViewportClient::AddPart(UStaticMesh* Mesh)
{
	if (!WeaponDA || !Mesh)
	{
		return;
	}

	// Append a new modular part to the DA (in-memory; DA save is BakeSockets'/"조립→저장"'s job): Socket None, identity
	// Offset. The designer positions it with the gizmo, then a bake creates its SOCKET_Mount_<name> socket.
	FFPSRWeaponPartAttachment NewAttach;
	NewAttach.Part = Mesh;
	const int32 NewIndex = WeaponDA->WeaponParts1P.Add(NewAttach);

	// Preview component named after the mesh (same convention as SetWeapon), spawned at the body's transform so it
	// starts on the weapon and framed; the designer then drags it into place.
	const FName CompName = MakeUniqueObjectName(GetTransientPackage(), UStaticMeshComponent::StaticClass(), FName(*FPSRWeaponAssemblerHelpers::MakePartDisplayName(NewAttach.Part, NewIndex)));
	UStaticMeshComponent* PartComp = NewObject<UStaticMeshComponent>(GetTransientPackage(), CompName, RF_Transient);
	PartComp->SetStaticMesh(Mesh);
	PartComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	const FTransform Init = BodyComp ? BodyComp->GetComponentTransform() : FTransform::Identity;
	PreviewScene.AddComponent(PartComp, Init);
	PartComps.Add(PartComp);

	SelectedPart = PartComps.Num() - 1;
	UpdatePartVisibility();
	Invalidate();
}

void FFPSRWeaponAssemblerViewportClient::RemoveSelectedPart()
{
	// 제거 전 단계 미리보기 중이면 정리(스테일 인덱스로 남는 것 방지 — 특히 지워질 슬롯을 미리보던 중이었을 경우).
	EndStagePreview();

	if (!PartComps.IsValidIndex(SelectedPart) || !WeaponDA || !WeaponDA->WeaponParts1P.IsValidIndex(SelectedPart))
	{
		return;
	}

	if (UStaticMeshComponent* PartComp = PartComps[SelectedPart])
	{
		PreviewScene.RemoveComponent(PartComp);
		RetireTransientComponent(PartComp);
	}
	PartComps.RemoveAt(SelectedPart);
	WeaponDA->WeaponParts1P.RemoveAt(SelectedPart);

	SelectedPart = INDEX_NONE;
	UpdatePartVisibility();
	Invalidate();
}

void FFPSRWeaponAssemblerViewportClient::BeginStagePreview(int32 SlotIndex, int32 StageIndex)
{
	// 이미 다른 단계를 미리보는 중이면 먼저 그 오프셋을 캡처·복원(순서 보장 — 미리보기는 한 번에 하나만).
	if (PreviewStageSlot != INDEX_NONE)
	{
		EndStagePreview();
	}

	if (!PartComps.IsValidIndex(SlotIndex) || !PartComps[SlotIndex] || !WeaponDA
		|| !WeaponDA->WeaponParts1P.IsValidIndex(SlotIndex) || !WeaponDA->WeaponParts1P[SlotIndex].Stages.IsValidIndex(StageIndex))
	{
		return;
	}

	UStaticMeshComponent* PC = PartComps[SlotIndex];

	// base 캡처(복원용) — 현재 컴포넌트 월드 트랜스폼이 stage.Offset의 기준 프레임이 된다.
	PreviewStageBaseXf = PC->GetComponentTransform();
	PreviewStageBaseMesh = PC->GetStaticMesh();

	const FFPSRWeaponPartStage& St = WeaponDA->WeaponParts1P[SlotIndex].Stages[StageIndex];
	UStaticMesh* StageMesh = St.Mesh.IsNull() ? nullptr : St.Mesh.LoadSynchronous();
	PC->SetStaticMesh(StageMesh); // null 허용 — "이 단계 선택 시 파츠 사라짐"과 동일 규약(FFPSRWeaponPartStage 주석)

	// 배치: 자식월드 = stage.Offset * base월드(기존 SetWeapon의 Init = P.Offset * (SocketRel * RootBoneCS)와 동일 규약).
	PC->SetWorldTransform(St.Offset * PreviewStageBaseXf);

	PreviewStageSlot = SlotIndex;
	PreviewStageIndex = StageIndex;
	SelectedPart = SlotIndex;

	UpdatePartVisibility();
	Invalidate();
}

void FFPSRWeaponAssemblerViewportClient::EndStagePreview()
{
	if (PreviewStageSlot == INDEX_NONE)
	{
		return;
	}

	if (PartComps.IsValidIndex(PreviewStageSlot) && PartComps[PreviewStageSlot] && WeaponDA
		&& WeaponDA->WeaponParts1P.IsValidIndex(PreviewStageSlot) && WeaponDA->WeaponParts1P[PreviewStageSlot].Stages.IsValidIndex(PreviewStageIndex))
	{
		UStaticMeshComponent* PC = PartComps[PreviewStageSlot];

		// 캡처: stage.Offset = 현재(기즈모로 옮겨진) 자식월드를 base월드 기준 상대로 환산(GetRelativeTransform:
		// this = Result * Other → Result = this.GetRelativeTransform(Other)).
		const FTransform NewOffset = PC->GetComponentTransform().GetRelativeTransform(PreviewStageBaseXf);
		WeaponDA->WeaponParts1P[PreviewStageSlot].Stages[PreviewStageIndex].Offset = NewOffset;
		WeaponDA->MarkPackageDirty();

		// base 메시/위치 복원.
		PC->SetStaticMesh(PreviewStageBaseMesh);
		PC->SetWorldTransform(PreviewStageBaseXf);
	}

	PreviewStageSlot = INDEX_NONE;
	PreviewStageIndex = INDEX_NONE;
	PreviewStageBaseMesh = nullptr;

	UpdatePartVisibility();
	Invalidate();
}

void FFPSRWeaponAssemblerViewportClient::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEditorViewportClient::AddReferencedObjects(Collector);
	Collector.AddReferencedObject(WeaponDA);
	Collector.AddReferencedObject(PreviewStageBaseMesh);
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
