// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"

class FPreviewScene;
class SFPSRWeaponAssemblerViewport;
class UFPSRWeaponDataAsset;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** Viewport client for the Weapon Part Assembler tool (Tools > FPSR > "무기 파츠 조립기…", see SFPSRWeaponAssemblerTab).
 *  Owns the currently-loaded weapon's preview components (body + modular parts) inside the tab's shared preview
 *  scene — nothing is ever spawned into an editor level. Also owns the transform-gizmo plumbing that lets a
 *  designer drag the selected part in the viewport (matches the FEditorViewportClient contract used by
 *  FStaticMeshEditorViewportClient / FPersonaViewportClient etc: GetWidgetLocation/InputWidgetDelta overrides, no
 *  separate edit mode needed for a single-selection gizmo).
 *
 *  GC: BodyComp/PartComps are UActorComponents registered with the (shared, tab-owned) FPreviewScene via
 *  AddComponent — FPreviewScene::AddReferencedObjects already keeps everything it holds alive for as long as the
 *  scene itself lives, so these raw pointers don't need their own collector entries (this mirrors the engine's own
 *  FStaticMeshEditorViewportClient, which keeps a raw UStaticMeshComponent* for exactly the same reason). WeaponDA is
 *  NOT scene-registered (it's a DataAsset, not a scene component), so it DOES need an explicit AddReferencedObjects
 *  override here. */
class FFPSRWeaponAssemblerViewportClient : public FEditorViewportClient
{
public:
	FFPSRWeaponAssemblerViewportClient(FPreviewScene& InPreviewScene, const TSharedRef<SFPSRWeaponAssemblerViewport>& InViewport);

	/** Tears down the previous weapon's preview components (if any), then rebuilds body + parts from DA. Null DA
	 *  just clears the preview. Loads soft refs synchronously (editor-only tool). */
	void SetWeapon(UFPSRWeaponDataAsset* DA);

	/** Selects PartComps[Index] as the gizmo target. INDEX_NONE (or an out-of-range index) clears the selection —
	 *  GetWidgetLocation then returns the origin and the gizmo effectively has nothing to grab. Also refreshes part
	 *  visibility (isolate mode follows the selection). */
	void SetSelectedPart(int32 Index);
	int32 GetSelectedPart() const { return SelectedPart; }

	/** Toggles the gizmo between move/rotate (tab toolbar's "이동"/"회전" buttons). Overrides the base class's own
	 *  SetWidgetMode (which routes through FEditorModeTools) since this client owns WidgetMode/GetWidgetMode()
	 *  itself and never touches ModeTools — see the class comment. */
	virtual void SetWidgetMode(UE::Widget::EWidgetMode InMode) override;

	/** "전체 이동" 토글(탭 툴바 체크박스). true면 선택 파츠 대신 모든 파츠가 동시에 기즈모를 따라간다: GetWidgetLocation()은
	 *  전체 파츠 위치의 평균을, InputWidgetDelta()는 그 평균을 피벗으로 삼아 전체 파츠를 이동/회전시킨다. */
	void SetMoveAll(bool bIn) { bMoveAll = bIn; Invalidate(); }
	bool IsMoveAll() const { return bMoveAll; }

	/** "선택만 보기" 토글(탭 툴바 체크박스). true면 선택된 파츠만 보이고 나머지 파츠는 숨겨진다(바디는 항상 보임,
	 *  건드리지 않음). */
	void SetIsolate(bool bIn) { bIsolate = bIn; UpdatePartVisibility(); Invalidate(); }
	bool IsIsolate() const { return bIsolate; }

	/** 선택된 파츠(PartComps[SelectedPart])의 스태틱 메시를 NewMesh로 교체 — 프리뷰 컴포넌트와 DA의
	 *  WeaponParts[SelectedPart].Part를 함께 갱신한다(인메모리만; DA 저장은 "조립→저장"(BakeSockets)이 담당).
	 *  컴포넌트 이름(=슬롯/소켓명)은 그대로 유지되므로 변종 교체이지 슬롯 재배치가 아니다. 선택 파츠가 없거나 DA가
	 *  없으면 아무 것도 하지 않는다. */
	void SwapSelectedPartMesh(UStaticMesh* NewMesh);

	/** 새 파츠를 무기에 추가: DA의 WeaponParts에 (Mesh, Socket=None, Offset=identity) 항목을 append하고 프리뷰
	 *  컴포넌트를 바디 위치에 생성·선택한다(인메모리만; DA 저장은 "조립→저장"이 담당). 디자이너가 기즈모로 위치를 잡은
	 *  뒤 베이크하면 소켓이 구워진다. DA/메시가 없으면 아무 것도 하지 않는다. */
	void AddPart(UStaticMesh* Mesh);

	/** 선택된 파츠를 제거: 프리뷰 컴포넌트와 DA의 WeaponParts[SelectedPart]를 함께 제거하고 선택을 해제한다(인덱스
	 *  정합 유지). 소켓 정리는 재베이크가 담당(BakeSockets가 SOCKET_Mount_*를 전부 지우고 다시 굽는다). */
	void RemoveSelectedPart();

	/** 슬롯 SlotIndex의 진화 단계 StageIndex를 뷰포트에서 미리본다: 그 슬롯 컴포넌트의 메시를 stage 메시로 바꾸고
	 *  base 위치(현재 컴포넌트 트랜스폼) 기준 stage.Offset 만큼 배치, 기즈모를 그 슬롯에 맞춘다. 이미 다른 단계를
	 *  미리보는 중이면 먼저 EndStagePreview()로 이전 오프셋을 캡처·복원한다. 인덱스 무효면 아무 것도 안 함. */
	void BeginStagePreview(int32 SlotIndex, int32 StageIndex);

	/** 단계 미리보기 종료: 현재 기즈모 위치를 stage.Offset으로 캡처(base 기준 상대) 후 base 메시/위치 복원. 미리보기
	 *  중이 아니면 no-op. */
	void EndStagePreview();
	bool IsPreviewingStage() const { return PreviewStageSlot != INDEX_NONE; }

	/** "팔 보기" 토글(탭 툴바 체크박스). 켜면 설정(UFPSRWeaponAssemblerSettings)의 1인칭 팔을 프리뷰 씬에 세우고,
	 *  조립품 전체를 팔의 무기 부착 소켓 자리로 옮긴다 — 그립을 **손에 든 상태**로 판정하기 위해서다. 끄면 팔을
	 *  걷어내고 조립품을 원점으로 되돌린다(툴의 원래 동작).
	 *
	 *  설정에 팔 메시가 비어 있으면 아무 일도 안 한다(false 로 되돌아감) — 콘텐츠가 없는 프로젝트에서 툴이 깨지지
	 *  않게. IsShowArms() 로 실제 상태를 되읽을 것. */
	void SetShowArms(bool bIn);
	bool IsShowArms() const { return bShowArms; }

	const TArray<UStaticMeshComponent*>& GetPartComps() const { return PartComps; }
	USkeletalMeshComponent* GetBodyComp() const { return BodyComp; }
	USkeletalMeshComponent* GetArmsComp() const { return ArmsComp; }
	UFPSRWeaponDataAsset* GetWeaponDA() const { return WeaponDA; }

	// FGCObject interface (FEditorViewportClient already derives FGCObject) — see the class comment above for why
	// only WeaponDA needs an entry here.
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FFPSRWeaponAssemblerViewportClient"); }

	// FEditorViewportClient interface — single-part transform gizmo. This client owns gizmo state directly (Selected
	// Part / WidgetMode) instead of routing through FEditorModeTools (constructed with InModeTools=nullptr), so
	// every one of these overrides is self-contained and never calls into ModeTools.
	virtual FVector GetWidgetLocation() const override;
	virtual FMatrix GetWidgetCoordSystem() const override { return FMatrix::Identity; }
	virtual ECoordSystem GetWidgetCoordSystemSpace() const override { return COORD_World; }
	virtual UE::Widget::EWidgetMode GetWidgetMode() const override { return WidgetMode; }
	virtual bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale) override;
	virtual void TrackingStarted(const FInputEventState& InInputState, bool bIsDraggingWidget, bool bNudge) override;
	virtual void TrackingStopped() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	/** 조립품(바디+파츠)을 팔의 무기 부착 소켓 자리로 통째로 옮긴다. 파츠는 **바디 상대 배치를 유지**한 채 따라간다
	 *  — 그래야 기존 BakeSockets(파츠를 바디 기준으로 굽는다)가 그대로 유효하다.
	 *
	 *  🚨 바디 스케일을 무기 DA 의 WeaponAttachScale 로 맞춘다. 런타임이 그렇게 붙이기 때문(FPSRCharacter::
	 *     AttachWeaponMeshes)이고, 프리뷰가 1.0 이면 화면에서 총이 인게임보다 커 보여 그립 판정이 어긋난다.
	 *     파츠는 바디 상대라 스케일이 자동으로 따라온다.
	 *
	 *  팔/바디/소켓 중 하나라도 없으면 아무 것도 안 한다. */
	void PlaceAssemblyAtHand();

	/** 팔 프리뷰를 끌 때 조립품을 원래 자리(바디=항등)로 되돌린다. PlaceAssemblyAtHand 의 역이며, 파츠의 바디 상대
	 *  배치는 여기서도 유지된다. */
	void ResetAssemblyToOrigin();

	/** Applies bIsolate to part visibility: bIsolate=false shows every part, true shows only PartComps[SelectedPart]
	 *  and hides the rest (BodyComp is never touched). Called from SetSelectedPart, SetIsolate, and SetWeapon (after
	 *  rebuilding the part components) so all three paths that can change "what should be visible" stay in sync. */
	void UpdatePartVisibility();

	/** The currently-selected part's static mesh, or null if nothing is selected. */
	UStaticMesh* GetSelectedPartMesh() const;

	/** The tab's shared preview scene (owns the render world BodyComp/PartComps live in). Reference, not pointer —
	 *  outlives this client (the tab constructs the scene before the viewport/client). */
	FPreviewScene& PreviewScene;

	/** Source weapon DA the current preview was built from (null = no weapon selected). Not scene-registered, see
	 *  the class comment — kept alive via AddReferencedObjects above. */
	TObjectPtr<UFPSRWeaponDataAsset> WeaponDA = nullptr;

	/** Preview body (SkeletalMeshComponent), added to the preview scene at identity with no attach parent. */
	USkeletalMeshComponent* BodyComp = nullptr;

	/** 1인칭 팔 프리뷰. 씬에 등록되므로 BodyComp/PartComps 와 같은 이유로 별도 GC 항목이 필요 없다(클래스 주석 참조).
	 *  null = "팔 보기" 꺼짐. */
	USkeletalMeshComponent* ArmsComp = nullptr;

	/** "팔 보기" 토글 상태. See SetShowArms. */
	bool bShowArms = false;

	/** Index-aligned to WeaponDA->WeaponParts: one unparented StaticMeshComponent per part, added to the preview
	 *  scene at the part's initial world transform (component-space == Body-relative, since Body sits at identity). */
	TArray<UStaticMeshComponent*> PartComps;

	/** Index into PartComps currently targeted by the gizmo, or INDEX_NONE. */
	int32 SelectedPart = INDEX_NONE;

	UE::Widget::EWidgetMode WidgetMode = UE::Widget::WM_Translate;

	/** "전체 이동" 토글 상태. See SetMoveAll. */
	bool bMoveAll = false;

	/** "선택만 보기" 토글 상태. See SetIsolate/UpdatePartVisibility. */
	bool bIsolate = false;

	/** 단계 미리보기 중인 슬롯(PartComps/WeaponParts 인덱스). INDEX_NONE = 미리보기 중이 아님. See BeginStagePreview/EndStagePreview. */
	int32 PreviewStageSlot = INDEX_NONE;
	/** PreviewStageSlot의 진화 단계 인덱스(WeaponParts[PreviewStageSlot].Stages 기준). */
	int32 PreviewStageIndex = INDEX_NONE;
	/** 미리보기 시작 시 캡처한 base 컴포넌트의 월드 트랜스폼 — stage.Offset은 이 프레임 기준 상대값이다. */
	FTransform PreviewStageBaseXf = FTransform::Identity;
	/** 미리보기 시작 시 슬롯 컴포넌트에 물려 있던 base 메시(복원용). */
	TObjectPtr<UStaticMesh> PreviewStageBaseMesh = nullptr;
};
