// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assembler/FPSRWeaponAssemblerViewportClient.h"

#include "Assembler/SFPSRWeaponAssemblerViewport.h"
#include "Assembler/FPSRWeaponAssemblerHelpers.h"
#include "Assembler/FPSRWeaponAssemblerSettings.h"
#include "Hero/FPSRCharacter.h"
#include "Weapon/FPSRWeaponDataAsset.h"

#include "Animation/AnimationAsset.h"
#include "Animation/AnimSequence.h"          // 애디티브 여부/기준 포즈 참조 검사(RefreshArmsFromSettings)
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/DebugSkelMeshComponent.h"   // 팔 프리뷰 컴포넌트 — 애디티브 기준 포즈를 까는 UAnimPreviewInstance 를 단다
#include "CanvasItem.h"          // FCanvasLineItem / FCanvasTextItem — 1인칭 조준 축 기준선
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"       // GEngine->GetTinyFont()
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

	// I-D: 자식을 먼저 떼고 부모를 지운다. 파츠는 이제 바디의 진짜 자식(B2)이라, 예전처럼 바디부터 지우면 바디가
	// (아직 살아 있는) 파츠의 AttachParent로 잠깐 남는 대신 파츠 쪽 참조가 끊긴 채 방치된다 — 순서를 바꿔 파츠부터 뗀다.
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

	if (BodyComp)
	{
		// 팔에 붙어 있었다면(bShowArms) 먼저 뗀다 — ArmsComp는 무기가 바뀌어도 살아남는 컴포넌트라, 은퇴할 바디를
		// 붙인 채로 지우면 ArmsComp의 AttachChildren에 낡은 항목이 남는다(다음 애니 갱신이 그 죽은 자식을 계속
		// 건드리게 된다). DetachAssemblyFromHand는 바디가 안 붙어 있어도 안전하게 no-op에 가깝다.
		DetachAssemblyFromHand();
		PreviewScene.RemoveComponent(BodyComp);
		RetireTransientComponent(BodyComp);
		BodyComp = nullptr;
	}

	WeaponDA = DA;
	GripBone = NAME_None; // 새 무기 = 새 부착 소켓/뼈일 수 있으므로 이전 해석을 버린다(아래 AttachAssemblyToHand가 다시 해석).
	bGripBoneUserSet = false;   // 새 무기의 애셋에 적힌 소켓이 진실이다 — 이전 무기에서 고른 뼈를 물고 가지 않는다.
	if (!DA)
	{
		Invalidate();
		return;
	}

	USkeletalMesh* Body = DA->WeaponMesh.IsNull() ? nullptr : DA->WeaponMesh.LoadSynchronous();
	BodyComp = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	BodyComp->SetSkeletalMeshAsset(Body);
	BodyComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewScene.AddComponent(BodyComp, FTransform::Identity);

	const FTransform RootBoneCS = FPSRWeaponAssemblerHelpers::RootBoneComponentSpace(Body);

	for (int32 i = 0; i < DA->WeaponParts.Num(); ++i)
	{
		const FFPSRWeaponPartAttachment& P = DA->WeaponParts[i];
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

		// B2: 파츠를 바디의 진짜 자식으로 붙인다(런타임 RebuildPartsFromSelection과 같은 관용구: register → attach
		// KeepRelativeTransform → SetRelativeTransform). Init은 이미 바디-상대 값이라 그대로 상대 트랜스폼에 넣으면
		// 되고, 상대 스케일은 Init의 스케일(항상 1 — 소켓/Offset 모두 스케일을 굽지 않는다, 불변식 I-B)이 되므로
		// 바디가 나중에 무기 스케일로 조정돼도(AttachAssemblyToHand) 자식이 자동으로 그 스케일을 상속한다.
		// KeepWorldTransform으로 붙였다면 그 시점 바디 스케일의 역수가 자식 상대 스케일에 실려버렸을 것이다.
		PreviewScene.AddComponent(PartComp, FTransform::Identity);
		PartComp->AttachToComponent(BodyComp, FAttachmentTransformRules::KeepRelativeTransform);
		PartComp->SetRelativeTransform(Init);
		PartComps.Add(PartComp);
	}

	// 무기를 바꿔도 팔에 든 상태를 유지한다 — 위에서 바디/파츠를 항등 기준으로 새로 세웠으므로 다시 얹어야 한다.
	// (부착 소켓·스케일은 무기 DA 마다 다를 수 있어 여기서 새 DA 기준으로 다시 계산된다.)
	if (bShowArms)
	{
		AttachAssemblyToHand();
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
	if (!NewMesh || !PartComps.IsValidIndex(SelectedPart) || !WeaponDA || !WeaponDA->WeaponParts.IsValidIndex(SelectedPart))
	{
		return;
	}

	// Component name (= the mount-socket BakeSockets derives it from) stays untouched — this is a variant swap on
	// the same slot, not a slot reassignment. DA save is BakeSockets'/"조립→저장"'s job; this only updates the
	// in-memory preview + the DA's part reference so a subsequent bake/save picks it up.
	PartComps[SelectedPart]->SetStaticMesh(NewMesh);
	WeaponDA->WeaponParts[SelectedPart].Part = NewMesh;

	Invalidate();
}

void FFPSRWeaponAssemblerViewportClient::AddPart(UStaticMesh* Mesh)
{
	if (!WeaponDA || !Mesh || !BodyComp)
	{
		return;
	}

	// Append a new modular part to the DA (in-memory; DA save is BakeSockets'/"조립→저장"'s job): Socket None, identity
	// Offset. The designer positions it with the gizmo, then a bake creates its SOCKET_Mount_<name> socket.
	FFPSRWeaponPartAttachment NewAttach;
	NewAttach.Part = Mesh;
	const int32 NewIndex = WeaponDA->WeaponParts.Add(NewAttach);

	// Preview component named after the mesh (same convention as SetWeapon). B2: 바디의 진짜 자식으로 붙이고 상대
	// 트랜스폼을 항등으로 둔다 — 바디가 지금 원점이든 손이든(bShowArms) 항상 "바디와 같은 자리에서 시작"이 되므로,
	// 예전의 "BodyComp->GetComponentTransform()을 월드 좌표로 흉내" 계산이 필요 없다. 디자이너가 여기서 기즈모로
	// 드래그해 위치를 잡는다.
	const FName CompName = MakeUniqueObjectName(GetTransientPackage(), UStaticMeshComponent::StaticClass(), FName(*FPSRWeaponAssemblerHelpers::MakePartDisplayName(NewAttach.Part, NewIndex)));
	UStaticMeshComponent* PartComp = NewObject<UStaticMeshComponent>(GetTransientPackage(), CompName, RF_Transient);
	PartComp->SetStaticMesh(Mesh);
	PartComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewScene.AddComponent(PartComp, FTransform::Identity);
	PartComp->AttachToComponent(BodyComp, FAttachmentTransformRules::KeepRelativeTransform);
	PartComp->SetRelativeTransform(FTransform::Identity);
	PartComps.Add(PartComp);

	SelectedPart = PartComps.Num() - 1;
	UpdatePartVisibility();
	Invalidate();
}

void FFPSRWeaponAssemblerViewportClient::RemoveSelectedPart()
{
	// 제거 전 단계 미리보기 중이면 정리(스테일 인덱스로 남는 것 방지 — 특히 지워질 슬롯을 미리보던 중이었을 경우).
	EndStagePreview();

	if (!PartComps.IsValidIndex(SelectedPart) || !WeaponDA || !WeaponDA->WeaponParts.IsValidIndex(SelectedPart))
	{
		return;
	}

	if (UStaticMeshComponent* PartComp = PartComps[SelectedPart])
	{
		// I-D: 부모(BodyComp)는 이 파츠 하나만 지워진 뒤에도 살아남으므로, 먼저 떼고 나서 씬에서 제거한다 — 안 그러면
		// BodyComp의 AttachChildren에 은퇴한 파츠를 가리키는 낡은 항목이 남는다.
		PartComp->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		PreviewScene.RemoveComponent(PartComp);
		RetireTransientComponent(PartComp);
	}
	PartComps.RemoveAt(SelectedPart);
	WeaponDA->WeaponParts.RemoveAt(SelectedPart);

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
		|| !WeaponDA->WeaponParts.IsValidIndex(SlotIndex) || !WeaponDA->WeaponParts[SlotIndex].Stages.IsValidIndex(StageIndex))
	{
		return;
	}

	UStaticMeshComponent* PC = PartComps[SlotIndex];

	// base 캡처(복원용) — B6: **바디 상대**로 캡처한다(PC의 실제 부모가 BodyComp이므로 GetRelativeTransform()이 곧
	// 바디 상대 값이다). 월드 트랜스폼으로 캡처하면 이제 바디가 손(ArmsComp)에 붙어 움직이므로(B1) Begin~End 사이에
	// 팔 애니가 재생/스크럽되는 것만으로 이 스냅샷이 낡아 stage.Offset이 틀어진다.
	PreviewStageBaseXf = PC->GetRelativeTransform();
	PreviewStageBaseMesh = PC->GetStaticMesh();

	const FFPSRWeaponPartStage& St = WeaponDA->WeaponParts[SlotIndex].Stages[StageIndex];
	UStaticMesh* StageMesh = St.Mesh.IsNull() ? nullptr : St.Mesh.LoadSynchronous();
	PC->SetStaticMesh(StageMesh); // null 허용 — "이 단계 선택 시 파츠 사라짐"과 동일 규약(FFPSRWeaponPartStage 주석)

	// 배치: 자식(바디 상대) = stage.Offset * base(바디 상대). 기존 규약(자식월드 = stage.Offset * base월드)과 수학적으로
	// 동치다(양변에 바디의 현재 월드 트랜스폼을 곱하면 그대로 나온다) — 다만 바디 상대끼리 계산하므로 바디가 그 뒤에
	// 움직여도(팔 애니) 자식이라 자동으로 따라온다. SetRelativeTransform 하나로 끝나 SetWorldTransform이 필요 없다.
	PC->SetRelativeTransform(St.Offset * PreviewStageBaseXf);

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
		&& WeaponDA->WeaponParts.IsValidIndex(PreviewStageSlot) && WeaponDA->WeaponParts[PreviewStageSlot].Stages.IsValidIndex(PreviewStageIndex))
	{
		UStaticMeshComponent* PC = PartComps[PreviewStageSlot];

		// 캡처: stage.Offset = 현재(기즈모로 옮겨진) 자식(바디 상대)을 base(바디 상대) 기준 상대로 환산
		// (GetRelativeTransform: this = Result * Other → Result = this.GetRelativeTransform(Other)). 월드가 아니라
		// 바디 상대끼리 계산하므로(B6) 그 사이 바디가 팔 애니를 따라 움직였어도 결과가 틀어지지 않는다.
		const FTransform NewOffset = PC->GetRelativeTransform().GetRelativeTransform(PreviewStageBaseXf);
		WeaponDA->WeaponParts[PreviewStageSlot].Stages[PreviewStageIndex].Offset = NewOffset;
		WeaponDA->MarkPackageDirty();

		// base 메시/위치 복원(바디 상대로).
		PC->SetStaticMesh(PreviewStageBaseMesh);
		PC->SetRelativeTransform(PreviewStageBaseXf);
	}

	PreviewStageSlot = INDEX_NONE;
	PreviewStageIndex = INDEX_NONE;
	PreviewStageBaseMesh = nullptr;

	UpdatePartVisibility();
	Invalidate();
}

USkeletalMeshComponent* FFPSRWeaponAssemblerViewportClient::GetArmsComp() const
{
	// 업캐스트에 완전 타입이 필요해 헤더가 아니라 여기 둔다(헤더는 전방선언만) — 헤더 주석 참조.
	return ArmsComp;
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
		// centre). 순수 위치 조회라 B2(파츠가 바디의 자식이 됨)의 영향을 받지 않는다 — GetComponentLocation()은 자식이든
		// 아니든 항상 현재 월드 위치를 돌려준다. InputWidgetDelta의 bMoveAll 분기(B5)만 이중 적용을 피하려고 바디
		// 하나만 옮기도록 바뀌었다; 여기 평균 계산은 일부러 그대로 뒀다.
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
		// B5: 파츠는 이제 바디의 진짜 자식(B2)이다 — 바디만 옮기면 부착 전파로 파츠가 자동으로 따라온다. 예전처럼
		// 파츠마다 같은 Drag/Rot을 또 적용하면 이중 적용이라 두 배로 움직인다(파츠는 바디 이동을 먼저 상속받고,
		// 거기에 자기 몫까지 더 얹는 꼴).
		//
		// 팔 보기가 켜져 있으면(HasGripFrame()) 바디는 팔의 소켓/뼈에 attach된 상태라, 이 조작은 사실상 그립 오프셋
		// 편집이다(SetMoveAll 헤더 주석 참조) — SetGripTransform과 동일한 컴포넌트(BodyComp)를 동일한 방식
		// (SetWorldTransform 계열)으로 움직이므로 서로 어긋나지 않는다.
		if (!BodyComp)
		{
			return FEditorViewportClient::InputWidgetDelta(InViewport, CurrentAxis, Drag, Rot, Scale);
		}

		// 이동 먼저...
		BodyComp->AddWorldOffset(Drag);

		// ...그다음 (이동 후의) 그룹 피벗 둘레로 회전 — 피벗은 여전히 "바디+파츠 전체 위치의 평균"(GetWidgetLocation(),
		// 바디 원점과 다를 수 있다)이라 AddWorldRotation 하나로는 부족하다(그건 바디 자기 원점 기준 회전이 되어 버린다).
		// 강체 부착이라(파츠가 바디의 자식) 바디를 이 피벗 둘레로 정확히 돌리면 파츠들도 수학적으로 같은 피벗을
		// 돈 것과 동일한 결과가 나온다 — 그래서 바디 하나만 계산하면 된다(기존 RotateAboutPivot 람다와 동일 수식,
		// 대상만 바디 하나로 축소).
		const FVector Pivot = GetWidgetLocation();
		const FVector NewLocation = Pivot + Rot.RotateVector(BodyComp->GetComponentLocation() - Pivot);
		BodyComp->SetWorldLocation(NewLocation);
		BodyComp->AddWorldRotation(Rot);

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

	// 1인칭 구도는 한 번만 어긋나도 판정이 무의미해지므로 매 프레임 되돌린다 — ApplyFirstPersonCamera 주석 참조.
	if (bFirstPersonView)
	{
		ApplyFirstPersonCamera();
	}
}

void FFPSRWeaponAssemblerViewportClient::SetShowArms(bool bIn)
{
	if (bIn == bShowArms)
	{
		return;
	}

	if (!bIn)
	{
		// 1인칭 구도는 팔 기준 상대값이라 팔이 사라지면 의미가 없다. 무엇보다 켜진 채로 두면 카메라가 잠긴 채
		// 남아 자유 시점으로 돌아갈 수 없다 — 먼저 끈다(SetFirstPersonView(false)가 이전 카메라를 복원한다).
		SetFirstPersonView(false);

		// I-D: 자식(바디, 그리고 바디에 매달린 파츠 전체)을 먼저 떼고 팔(부모)을 제거한다. 파츠는 바디의 자식일
		// 뿐 팔과는 직접 관계가 없으므로 바디만 떼면 충분하다(DetachAssemblyFromHand). ArmsComp는 무기가 바뀌어도
		// 살아남는 컴포넌트라, 붙은 채로 지우면 다음 애니 갱신이 은퇴한 자식을 계속 건드리게 된다.
		DetachAssemblyFromHand();
		if (ArmsComp)
		{
			PreviewScene.RemoveComponent(ArmsComp);
			RetireTransientComponent(ArmsComp);
			ArmsComp = nullptr;
		}
		bShowArms = false;
		GripBone = NAME_None; // 팔이 없어졌으니 뼈 선택도 무효 — 다음에 켜면 새로 해석한다.
		bGripBoneUserSet = false;
		ArmsAttachIssue.Reset();
		ArmsPoseIssue.Reset();
		Invalidate();
		return;
	}

	// 팔 메시는 **설정에서** 온다 — 경로를 C++ 에 박지 않는다(이 프로젝트는 이미 팔을 두 번 갈았다, 불변식 I-C).
	// 실제 컴포넌트 구성/포즈 적용/부착은 RefreshArmsFromSettings 하나로 통일한다 — "설정 피커가 바뀌었다"와
	// "지금 막 켰다"는 같은 절차여야 한다(둘 다 팔 컴포넌트를 설정 기준으로 맞추는 일이므로).
	const UFPSRWeaponAssemblerSettings* Settings = GetDefault<UFPSRWeaponAssemblerSettings>();
	if (!Settings || Settings->PreviewArmsMesh.IsNull())
	{
		// 콘텐츠가 없으면 조용히 꺼진 채로 둔다 — 탭이 IsShowArms() 로 되읽어 체크박스를 원복한다.
		bShowArms = false;
		return;
	}

	bShowArms = true;
	RefreshArmsFromSettings();
	Invalidate();
}

void FFPSRWeaponAssemblerViewportClient::RefreshArmsFromSettings()
{
	if (!bShowArms)
	{
		return; // 팔 보기가 꺼져 있으면 아무 의미 없음 — 헤더 주석 참조.
	}

	const UFPSRWeaponAssemblerSettings* Settings = GetDefault<UFPSRWeaponAssemblerSettings>();
	USkeletalMesh* Arms = Settings && !Settings->PreviewArmsMesh.IsNull() ? Settings->PreviewArmsMesh.LoadSynchronous() : nullptr;
	if (!Arms)
	{
		// 설정에서 팔 메시가 사라졌다 — SetShowArms(false)와 같은 경로로 완전히 끈다(자식 먼저 떼고 부모 제거).
		SetShowArms(false);
		return;
	}

	const bool bMeshChanged = !ArmsComp || ArmsComp->GetSkeletalMeshAsset() != Arms;
	if (bMeshChanged)
	{
		// 팔 메시 자체가 바뀌면(스켈레톤이 다를 수 있어) 컴포넌트를 통째로 다시 세운다. I-D: 바디를 먼저 옛 팔에서
		// 뗀 뒤(그 아래 파츠는 바디의 자식이라 함께 따라간다, B2) 옛 팔을 제거한다.
		DetachAssemblyFromHand();
		if (ArmsComp)
		{
			PreviewScene.RemoveComponent(ArmsComp);
			RetireTransientComponent(ArmsComp);
			ArmsComp = nullptr;
		}

		ArmsComp = NewObject<UDebugSkelMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		ArmsComp->SetSkeletalMeshAsset(Arms);
		ArmsComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PreviewScene.AddComponent(ArmsComp, FTransform::Identity);
		GripBone = NAME_None; // 새 팔 = 새 스켈레톤일 수 있으므로 이전 뼈 선택은 무효화 — AttachAssemblyToHand가 다시 해석.
		bGripBoneUserSet = false;   // 옛 스켈레톤의 뼈 이름을 새 팔에 물고 갈 수 없다.
	}

	// 🚨 레퍼런스 포즈로 판정하면 안 된다 — 팔이 벌어진 채로 서서 총이 인게임과 전혀 다른 자리에 온다. 포즈를 물려야
	//    "소총 자세로 든 손"을 기준으로 그립을 볼 수 있다. 포즈는 이제 재생·스크럽까지 되므로(B4, SetPreviewPlaying/
	//    SetPreviewPosition) "반드시 정지 포즈여야 한다"는 옛 제약은 더 이상 사실이 아니다 — 애니 어느 프레임에서든
	//    그립을 판정할 수 있다.
	ArmsPoseIssue.Reset();
	UAnimationAsset* Pose = Settings->PreviewArmsPose.IsNull() ? nullptr : Settings->PreviewArmsPose.LoadSynchronous();
	// P4 해결: 예전엔 Pose->GetSkeleton() == Arms->GetSkeleton() 로만 비교해서 스켈레톤이 안 맞으면 조용히 무시했다.
	// IsCompatibleForEditor는 ==비교와 달리 리타겟 호환 스켈레톤까지 인정한다(엔진 사실 4, Skeleton.h:707) — 그런데도
	// 실패하면 진짜 다른 스켈레톤이라는 뜻이라 사유를 남긴다.
	if (Pose && Arms->GetSkeleton() && Arms->GetSkeleton()->IsCompatibleForEditor(Pose->GetSkeleton()))
	{
		// 🚨 SetAnimationMode(AnimationSingleNode) 을 쓰지 않는다 — 그건 AnimScriptInstance 를 평범한
		// UAnimSingleNodeInstance 로 갈아치워, 애디티브 기준 포즈를 까는 UAnimPreviewInstance 를 도로 떼어낸다
		// (ArmsComp 멤버 주석의 bCanProcessAdditiveAnimations 참조). EnablePreview 가 UDebugSkelMeshComponent 의
		// 정식 경로다: AnimScriptInstance 를 PreviewInstance 로 세우고 애셋을 물린다(DebugSkelMeshComponent.cpp).
		// PreviewInstance 는 등록 시 InitAnim 이 만들어 두므로(메시를 AddComponent 前에 물려 둔 이유) 여기서 유효하다.
		ArmsComp->EnablePreview(true, Pose);
		// EnablePreview 는 이전 루프 상태를 물려줄 뿐 재생을 켜 주지 않는다 — 애니를 고르면 바로 도는 기존 동작을 유지한다.
		ArmsComp->Play(true);

		// 애디티브인데 기준 포즈를 가리키는 참조가 비어 있으면, 프리뷰 인스턴스라도 깔 게 없어 결국 레퍼런스 포즈
		// (A자) 위에 차분이 얹힌다 — 뒤틀린 포즈를 말없이 보여주느니 사유를 남긴다. 이 프로젝트의 FP_Rifle_Reload 는
		// RefPoseSeq=A_FP_Rifle_Pose / ABPT_AnimFrame 이 제대로 들어 있어 이 분기에 걸리지 않는다.
		if (const UAnimSequence* Seq = Cast<UAnimSequence>(Pose))
		{
			// ABPT_LocalAnimFrame 은 기준 포즈를 **자기 자신**에서 뽑으므로 RefPoseSeq 가 없어도 정상이다
			// (AnimSequence.cpp:3799 — `RefPoseType == ABPT_LocalAnimFrame ? this : RefPoseSeq.Get()`).
			// ABPT_RefPose 도 스켈레톤 레퍼런스 포즈가 곧 기준이라 참조가 필요 없다.
			const bool bNeedsRefSeq = Seq->RefPoseType == ABPT_AnimScaled || Seq->RefPoseType == ABPT_AnimFrame;
			if (Seq->IsValidAdditive() && (Seq->RefPoseType == ABPT_None || (bNeedsRefSeq && !Seq->RefPoseSeq)))
			{
				ArmsPoseIssue = FString::Printf(
					TEXT("'%s' 는 애디티브 애니인데 기준 포즈가 지정돼 있지 않습니다 — 레퍼런스 포즈(A자) 위에 얹혀 팔이 뒤틀려 보입니다. ")
					TEXT("애니 애셋의 Additive Settings 에서 Base Pose 를 지정하세요."),
					*Seq->GetName());
			}
		}
	}
	else
	{
		ArmsComp->EnablePreview(true, nullptr);
		if (Pose)
		{
			ArmsPoseIssue = FString::Printf(
				TEXT("프리뷰 포즈 '%s' 의 스켈레톤이 팔 메시 '%s' 와 호환되지 않습니다 — 포즈를 무시합니다(레퍼런스 포즈로 표시)."),
				*Pose->GetName(), *Arms->GetName());
		}
	}

	// 포즈가 한 프레임이라도 평가돼야 소켓이 제자리에 온다 — 안 그러면 첫 배치가 레퍼런스 포즈 기준이 된다.
	ArmsComp->RefreshBoneTransforms();
	AttachAssemblyToHand();
	Invalidate();
}

const FString& FFPSRWeaponAssemblerViewportClient::GetArmsStatusMessage() const
{
	// 부착/포즈/1인칭 사유는 서로 독립적인 문제라 하나가 다른 하나를 덮어쓰면 안 된다 — 매 호출 재조합한다.
	CachedArmsStatusMessage.Reset();
	auto Append = [this](const FString& Issue)
	{
		if (!Issue.IsEmpty())
		{
			if (!CachedArmsStatusMessage.IsEmpty())
			{
				CachedArmsStatusMessage += TEXT(" / ");
			}
			CachedArmsStatusMessage += Issue;
		}
	};
	Append(ArmsAttachIssue);
	Append(ArmsPoseIssue);
	Append(FirstPersonIssue);
	return CachedArmsStatusMessage;
}

void FFPSRWeaponAssemblerViewportClient::AttachAssemblyToHand()
{
	ArmsAttachIssue.Reset();

	if (!ArmsComp || !BodyComp || !WeaponDA)
	{
		DetachAssemblyFromHand();
		return;
	}

	// 우선순위 1: AFPSRCharacter 가 이 무기에 대해 해석하는 소켓(런타임 AttachWeaponMeshes와 동일 규칙을 CDO로
	// 조회 — P2 해결: 예전엔 DA->WeaponAttachSocket 만 봐서 그 값이 박힌 무기 1종(Rifle) 외엔 팔 보기를 켜도 총이
	// 원점에 남았다). 소켓이 실제로 이 팔 메시에 있을 때만 채택하고, GripBone을 그 소켓의 소유 뼈로 맞춘다.
	//    단, 디자이너가 뼈 피커로 뼈를 직접 고른 뒤(bGripBoneUserSet)라면 그 선택이 이긴다 — 소켓의 뼈를 **옮기려는**
	//    중이기 때문이다(BakeWeaponSocket 의 bMovedBone 경로). 여기서 소켓 뼈로 되돌려 버리면 피커가 영영 무동작이 된다.
	FName AttachName = NAME_None;
	const FName ResolvedSocket = GetDefault<AFPSRCharacter>()->ResolveWeaponAttachSocket(WeaponDA);
	if (!bGripBoneUserSet && !ResolvedSocket.IsNone() && ArmsComp->DoesSocketExist(ResolvedSocket))
	{
		AttachName = ResolvedSocket;
		if (const USkeletalMeshSocket* Socket = ArmsComp->GetSocketByName(ResolvedSocket))
		{
			GripBone = Socket->BoneName;
		}
	}
	// 우선순위 2: 해석된 소켓이 없거나 이 팔 메시엔 아직 없으면(P5 시나리오 — 아직 한 번도 안 구운 새 무기, 또는
	// 새 팔 메시) 현재 선택된 뼈(GripBone — SetGripBone으로 사용자가 직접 고를 수 있다)로 붙인다. 실제로 이
	// 스켈레톤에 있는 뼈일 때만 채택한다.
	else if (!GripBone.IsNone() && ArmsComp->GetBoneIndex(GripBone) != INDEX_NONE)
	{
		AttachName = GripBone;
	}

	if (AttachName.IsNone())
	{
		// 🚨 둘 다 없으면 절대 붙이지 않는다(엔진 사실 1, 불변식 I-A) — NAME_None으로 붙이면 애니 갱신에서 조용히
		// 건너뛰어져(SceneComponent.cpp:2912) 손을 안 따라간다. 원점으로 정리하고 사유를 남긴다.
		DetachAssemblyFromHand();
		ArmsAttachIssue = FString::Printf(
			TEXT("무기 '%s' 를 붙일 곳이 없습니다 — 팔 메시 '%s' 에 소켓 '%s' 도, 선택된 뼈도 없습니다. 그립 탭에서 뼈를 먼저 고르세요."),
			*WeaponDA->GetName(), *ArmsComp->GetName(), *ResolvedSocket.ToString());
		return;
	}

	// 🚨 SnapToTargetNotIncludingScale + 명시적 SetRelativeScale3D — 런타임(FPSRCharacter::AttachWeaponMeshes)과
	// 정확히 같은 규약이라야 프리뷰 크기가 인게임과 일치한다. 소켓 자체엔 스케일을 굽지 않는다(불변식 I-B) — 크기는
	// 여기 하나(WeaponAttachScale)에서만 결정된다. 파츠는 바디의 자식이라(B2) 이 스케일을 자동으로 상속한다.
	BodyComp->AttachToComponent(ArmsComp, FAttachmentTransformRules::SnapToTargetNotIncludingScale, AttachName);
	BodyComp->SetRelativeScale3D(FVector(WeaponDA->WeaponAttachScale));

	Invalidate();
}

void FFPSRWeaponAssemblerViewportClient::DetachAssemblyFromHand()
{
	if (!BodyComp)
	{
		return;
	}

	if (BodyComp->GetAttachParent())
	{
		BodyComp->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	}

	// 원래 자리(항등 — 스케일도 1로 함께 리셋됨)로. 파츠는 이제 바디의 진짜 자식(B2)이라 따로 손대지 않아도
	// 자동으로 같이 원점으로 돌아온다 — 예전처럼 파츠별 바디-상대 오프셋을 떠 뒀다가 다시 적용할 필요가 없다.
	BodyComp->SetWorldTransform(FTransform::Identity);
}

// --- 애니 재생/스크럽 (B4) -----------------------------------------------------------------------------------------

void FFPSRWeaponAssemblerViewportClient::SetPreviewPlaying(bool bPlay)
{
	if (!ArmsComp)
	{
		return;
	}
	if (UAnimSingleNodeInstance* Instance = ArmsComp->GetSingleNodeInstance())
	{
		Instance->SetPlaying(bPlay);
	}
	Invalidate();
}

bool FFPSRWeaponAssemblerViewportClient::IsPreviewPlaying() const
{
	UAnimSingleNodeInstance* Instance = ArmsComp ? ArmsComp->GetSingleNodeInstance() : nullptr;
	return Instance && Instance->IsPlaying();
}

void FFPSRWeaponAssemblerViewportClient::SetPreviewPosition(float Seconds)
{
	if (!ArmsComp)
	{
		return;
	}
	// USkeletalMeshComponent::SetPosition은 GetSingleNodeInstance()->SetPosition으로 넘겨주는 편의 래퍼다(엔진 소스
	// 확인 — SkeletalMeshComponent.cpp:4107, ThumbnailHelpers.cpp의 애니 썸네일 스크럽도 이 래퍼를 쓴다).
	ArmsComp->SetPosition(Seconds);

	// 스크럽 직후 소켓/자식(바디+파츠)이 새 포즈를 이 프레임에 바로 반영하도록 강제 갱신 — SetShowArms의 최초 배치와
	// 같은 이유(Tick을 기다리면 한 프레임 늦게 보인다).
	ArmsComp->RefreshBoneTransforms();
	Invalidate();
}

float FFPSRWeaponAssemblerViewportClient::GetPreviewPosition() const
{
	return ArmsComp ? ArmsComp->GetPosition() : 0.0f;
}

float FFPSRWeaponAssemblerViewportClient::GetPreviewLength() const
{
	// USkeletalMeshComponent엔 Play/Stop/IsPlaying/SetPosition/GetPosition 래퍼만 있고 Length는 없다(엔진 소스
	// 확인 — SkeletalMeshComponent.h:1160-1241) — GetSingleNodeInstance()로 직접 받는다. GetLength()는 non-const라
	// Instance를 non-const 포인터로 받아야 한다.
	UAnimSingleNodeInstance* Instance = ArmsComp ? ArmsComp->GetSingleNodeInstance() : nullptr;
	return Instance ? Instance->GetLength() : 0.0f;
}

void FFPSRWeaponAssemblerViewportClient::SetPreviewLooping(bool bLoop)
{
	if (!ArmsComp)
	{
		return;
	}
	// USkeletalMeshComponent엔 SetLooping 래퍼가 없다 — GetSingleNodeInstance()로 직접 처리(위 GetPreviewLength와
	// 같은 이유). Play(bool)을 다시 부르면 재생 여부까지 같이 바뀌어 SetPreviewPlaying과 충돌하므로 쓰지 않는다.
	if (UAnimSingleNodeInstance* Instance = ArmsComp->GetSingleNodeInstance())
	{
		Instance->SetLooping(bLoop);
	}
}

bool FFPSRWeaponAssemblerViewportClient::IsPreviewLooping() const
{
	UAnimSingleNodeInstance* Instance = ArmsComp ? ArmsComp->GetSingleNodeInstance() : nullptr;
	return Instance && Instance->IsLooping();
}

// --- 그립 (B3) ------------------------------------------------------------------------------------------------

bool FFPSRWeaponAssemblerViewportClient::HasGripFrame() const
{
	return bShowArms && ArmsComp && BodyComp && !GripBone.IsNone();
}

FTransform FFPSRWeaponAssemblerViewportClient::GetGripTransform() const
{
	if (!HasGripFrame())
	{
		return FTransform::Identity;
	}
	// FPSRWeaponAssemblerHelpers::BakeWeaponSocket이 굽는 값과 정확히 같은 식이어야 한다 — 헤더 주석 참조.
	const FTransform BoneWorld = ArmsComp->GetSocketTransform(GripBone, RTS_World);
	return BodyComp->GetComponentTransform().GetRelativeTransform(BoneWorld);
}

void FFPSRWeaponAssemblerViewportClient::SetGripTransform(const FTransform& BoneRel)
{
	if (!HasGripFrame())
	{
		return;
	}
	// GetGripTransform의 역연산. 바디가 이미 팔의 소켓/뼈에 attach돼 있으므로(AttachAssemblyToHand) SetWorldTransform은
	// 그 attach 관계의 상대 트랜스폼을 갱신하는 것과 같다 — 파츠는 바디의 자식이라(B2) 자동으로 따라온다.
	const FTransform BoneWorld = ArmsComp->GetSocketTransform(GripBone, RTS_World);
	BodyComp->SetWorldTransform(BoneRel * BoneWorld);
	Invalidate();
}

void FFPSRWeaponAssemblerViewportClient::SetGripBone(FName BoneName)
{
	if (!ArmsComp || !BodyComp)
	{
		GripBone = BoneName;
		bGripBoneUserSet = !BoneName.IsNone();
		return;
	}

	// 뼈가 바뀌면 기준 프레임이 바뀐다 — 그립 값(현재 보이는 무기 위치)은 유지한 채 재부착해야 하므로, 바꾸기 전
	// 월드 트랜스폼을 떠 두고 재부착 후 되돌린다.
	//
	// bGripBoneUserSet 을 세우는 이유는 헤더에 적었다: 이걸 안 세우면 소켓이 이미 있는 무기에서 AttachAssemblyToHand
	// 가 곧바로 소켓의 뼈로 되돌려, 피커가 무동작이 되고 베이크의 뼈 재배치 경로에 도달하지 못한다. 화면에 보이는
	// 기준 프레임과 저장될 값이 갈리지 않도록 부착도 이 뼈로 간다.
	const FTransform WorldBefore = BodyComp->GetComponentTransform();
	GripBone = BoneName;
	bGripBoneUserSet = !BoneName.IsNone();
	AttachAssemblyToHand();
	if (BodyComp->GetAttachParent())
	{
		// 재부착 성공했을 때만 복원 — 실패했으면(붙일 곳 없음) AttachAssemblyToHand가 이미 원점으로 보냈다.
		BodyComp->SetWorldTransform(WorldBefore);
	}
	Invalidate();
}

// --- 부착 소켓 이름 (P2) ----------------------------------------------------------------------------------------

FName FFPSRWeaponAssemblerViewportClient::GetResolvedAttachSocketName() const
{
	return WeaponDA ? GetDefault<AFPSRCharacter>()->ResolveWeaponAttachSocket(WeaponDA) : NAME_None;
}

bool FFPSRWeaponAssemblerViewportClient::IsUsingSharedDefaultSocket() const
{
	if (!WeaponDA)
	{
		return false;
	}

	// 🚨 "DA 필드가 비었나" 로 판정하면 안 된다 — 그건 폴백 여부일 뿐이고, 정작 위험한 건 **결과 소켓 이름이
	//    캐릭터 기본과 같은가** 이다. 실제로 DA_Weapon_Rifle 은 WeaponAttachSocket 에 `SOCKET_Weapon` 을 **명시**해
	//    두는데, 그 이름이 곧 캐릭터 기본값이라 결국 다른 무기 전부와 같은 소켓을 가리킨다. 필드만 봤을 땐
	//    "이 무기 전용" 으로 보여서, 저장하면 다른 무기까지 움직이는데도 경고가 안 떴다.
	//
	// 기본 이름은 리졸버에 null 을 넣어 얻는다(무기 없음 = 캐릭터 기본) — 규칙을 여기서 복제하지 않기 위해서다.
	const AFPSRCharacter* CDO = GetDefault<AFPSRCharacter>();
	const FName DefaultSocket = CDO->ResolveWeaponAttachSocket(nullptr);
	return CDO->ResolveWeaponAttachSocket(WeaponDA) == DefaultSocket;
}

void FFPSRWeaponAssemblerViewportClient::SetWeaponAttachSocketName(FName NewName)
{
	if (!WeaponDA)
	{
		return;
	}
	WeaponDA->WeaponAttachSocket = NewName;
	WeaponDA->MarkPackageDirty();
	AttachAssemblyToHand(); // 인메모리 재부착 — 애셋 저장은 여느 편집처럼 "조립→저장"이 담당(헤더 주석 참조).
	Invalidate();
}

// --- 1인칭 뷰 -------------------------------------------------------------------------------------------------------

bool FFPSRWeaponAssemblerViewportClient::ReadFirstPersonSetupFromCharacter(FTransform& OutCameraRelativeToArms, float& OutFOV, FString& OutIssue) const
{
	const UFPSRWeaponAssemblerSettings* Settings = GetDefault<UFPSRWeaponAssemblerSettings>();
	if (!Settings || Settings->PreviewCharacterClass.IsNull())
	{
		OutIssue = TEXT("프로젝트 설정 > FPSR > 무기 어셈블러 에 '프리뷰 캐릭터 클래스'가 비어 있어 1인칭 구도를 읽을 수 없습니다.");
		return false;
	}

	UClass* CharClass = Settings->PreviewCharacterClass.LoadSynchronous();
	UWorld* World = PreviewScene.GetWorld();
	if (!CharClass || !World)
	{
		OutIssue = TEXT("프리뷰 캐릭터 클래스를 불러오지 못했습니다.");
		return false;
	}

	// 🚨 CDO 가 아니라 **인스턴스**라야 컴포넌트 월드 트랜스폼이 의미를 갖는다. 엔진도 FBlueprintEditor 가
	// 프리뷰 씬에 BP 를 그대로 스폰한다(BlueprintEditor.cpp). 프리뷰 월드는 BeginPlay 를 걸지 않으므로
	// AFPSRCharacter::BeginPlay/PossessedBy 같은 게임플레이 초기화는 돌지 않는다 — 값만 읽고 즉시 파괴한다.
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags |= RF_Transient;
	AFPSRCharacter* Probe = World->SpawnActor<AFPSRCharacter>(CharClass, FTransform::Identity, SpawnParams);
	if (!Probe)
	{
		OutIssue = TEXT("프리뷰 캐릭터를 프리뷰 씬에 스폰하지 못했습니다.");
		return false;
	}

	const bool bOk = Probe->GetFirstPersonViewSetup(OutCameraRelativeToArms, OutFOV);
	Probe->Destroy();

	if (!bOk)
	{
		OutIssue = TEXT("프리뷰 캐릭터에 1인칭 카메라 또는 1인칭 팔 컴포넌트가 없어 구도를 읽지 못했습니다.");
	}
	return bOk;
}

void FFPSRWeaponAssemblerViewportClient::DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);

	if (!bFirstPersonView)
	{
		return;
	}

	// 화면 중앙 = 카메라의 조준 축. 뷰 크기에서 직접 구한다(하드코딩한 해상도를 쓰면 뷰포트를 리사이즈하는
	// 순간 기준선이 축에서 벗어나 판정이 조용히 틀어진다).
	const float CX = Canvas.GetRenderTarget()->GetSizeXY().X * 0.5f;
	const float CY = Canvas.GetRenderTarget()->GetSizeXY().Y * 0.5f;
	const FLinearColor LineColor(1.0f, 0.35f, 0.0f, 0.9f);
	const float Gap = 4.0f;    // 중심을 비워 둬야 총구가 축에 겹칠 때 가려지지 않는다
	const float Len = 12.0f;

	FCanvasLineItem Line;
	Line.SetColor(LineColor);
	Line.LineThickness = 1.0f;
	auto DrawSeg = [&Canvas, &Line](float X0, float Y0, float X1, float Y1)
	{
		Line.Origin = FVector(X0, Y0, 0.0f);
		Line.EndPos = FVector(X1, Y1, 0.0f);
		Canvas.DrawItem(Line);
	};
	DrawSeg(CX - Gap - Len, CY, CX - Gap, CY);
	DrawSeg(CX + Gap, CY, CX + Gap + Len, CY);
	DrawSeg(CX, CY - Gap - Len, CX, CY - Gap);
	DrawSeg(CX, CY + Gap, CX, CY + Gap + Len);
	DrawSeg(CX, CY, CX + 1.0f, CY);   // 중심 점

	// 실제 게임 크로스헤어가 아니라는 것을 화면에 명시한다 — 이걸로 크로스헤어 모양/크기를 판정하면 안 된다.
	FCanvasTextItem Label(FVector2D(CX + Gap + Len + 6.0f, CY - 7.0f),
		NSLOCTEXT("FPSRWeaponAssembler", "AimAxisGuide", "조준 축 기준선 (실제 크로스헤어 아님)"),
		GEngine->GetTinyFont(), LineColor);
	Label.EnableShadow(FLinearColor::Black);
	Canvas.DrawItem(Label);
}

void FFPSRWeaponAssemblerViewportClient::ApplyFirstPersonCamera()
{
	// 🚨 잠금 방식: 입력 경로를 막는 대신 **매 프레임 되돌린다.** 에디터 뷰포트에서 카메라를 움직이는 경로는
	// 궤도/팬/돌리/북마크/포커스 등 여러 갈래라 하나씩 막으면 반드시 새는 길이 남는데, 1인칭 구도는 한 번만
	// 어긋나도 그립 판정이 무의미해진다(이 기능 자체가 "자유 시점으로는 판정이 안 된다"는 지적에서 나왔다).
	// 기즈모 드래그는 카메라를 건드리지 않으므로 이 방식으로도 파츠·그립 편집은 그대로 된다.
	SetViewLocation(FirstPersonCameraWorld.GetLocation());
	SetViewRotation(FirstPersonCameraWorld.GetRotation().Rotator());
	ViewFOV = FirstPersonFOV;
}

bool FFPSRWeaponAssemblerViewportClient::CanUseFirstPersonView() const
{
	const UFPSRWeaponAssemblerSettings* Settings = GetDefault<UFPSRWeaponAssemblerSettings>();
	// 팔이 서 있어야 의미가 있다 — 1인칭 구도는 "팔 기준" 상대값이라, 팔이 없으면 놓을 기준 자체가 없다.
	return bShowArms && ArmsComp && Settings && !Settings->PreviewCharacterClass.IsNull();
}

void FFPSRWeaponAssemblerViewportClient::SetFirstPersonView(bool bIn)
{
	if (bIn == bFirstPersonView)
	{
		return;
	}

	if (!bIn)
	{
		// 켜기 직전의 자유 시점을 그대로 되돌려 준다 — 1인칭 한 번 봤다고 잡아 둔 각도를 잃으면 저작이 끊긴다.
		bFirstPersonView = false;
		SetViewLocation(SavedViewLocation);
		SetViewRotation(SavedViewRotation);
		ViewFOV = SavedViewFOV;
		Invalidate();
		return;
	}

	FTransform CameraRelArms;
	float FOV = 90.0f;
	FString Issue;
	if (!ReadFirstPersonSetupFromCharacter(CameraRelArms, FOV, Issue))
	{
		// 조용히 꺼진 채로 둔다 — 탭이 IsFirstPersonView() 로 되읽어 체크박스를 원복하고, 사유는 상태줄에 뜬다.
		FirstPersonIssue = Issue;
		bFirstPersonView = false;
		return;
	}
	FirstPersonIssue.Reset();

	SavedViewLocation = GetViewLocation();
	SavedViewRotation = GetViewRotation();
	SavedViewFOV = ViewFOV;

	// 팔은 프리뷰 씬에서 항등에 서 있지만(RefreshArmsFromSettings 의 AddComponent), 그 전제에 기대지 않고 실제
	// 월드를 곱한다 — 팔 배치가 나중에 바뀌어도 구도가 따라가야 한다.
	FirstPersonCameraWorld = ArmsComp ? CameraRelArms * ArmsComp->GetComponentTransform() : CameraRelArms;
	FirstPersonFOV = FOV;

	bFirstPersonView = true;
	ApplyFirstPersonCamera();
	Invalidate();
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
