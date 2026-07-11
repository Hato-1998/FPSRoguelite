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
	 *  WeaponParts1P[SelectedPart].Part를 함께 갱신한다(인메모리만; DA 저장은 "조립→저장"(BakeSockets)이 담당).
	 *  컴포넌트 이름(=슬롯/소켓명)은 그대로 유지되므로 변종 교체이지 슬롯 재배치가 아니다. 선택 파츠가 없거나 DA가
	 *  없으면 아무 것도 하지 않는다. */
	void SwapSelectedPartMesh(UStaticMesh* NewMesh);

	/** 새 파츠를 무기에 추가: DA의 WeaponParts1P에 (Mesh, Socket=None, Offset=identity) 항목을 append하고 프리뷰
	 *  컴포넌트를 바디 위치에 생성·선택한다(인메모리만; DA 저장은 "조립→저장"이 담당). 디자이너가 기즈모로 위치를 잡은
	 *  뒤 베이크하면 소켓이 구워진다. DA/메시가 없으면 아무 것도 하지 않는다. */
	void AddPart(UStaticMesh* Mesh);

	/** 선택된 파츠를 제거: 프리뷰 컴포넌트와 DA의 WeaponParts1P[SelectedPart]를 함께 제거하고 선택을 해제한다(인덱스
	 *  정합 유지). 소켓 정리는 재베이크가 담당(BakeSockets가 SOCKET_Mount_*를 전부 지우고 다시 굽는다). */
	void RemoveSelectedPart();

	const TArray<UStaticMeshComponent*>& GetPartComps() const { return PartComps; }
	USkeletalMeshComponent* GetBodyComp() const { return BodyComp; }
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

	/** Index-aligned to WeaponDA->WeaponParts1P: one unparented StaticMeshComponent per part, added to the preview
	 *  scene at the part's initial world transform (component-space == Body-relative, since Body sits at identity). */
	TArray<UStaticMeshComponent*> PartComps;

	/** Index into PartComps currently targeted by the gizmo, or INDEX_NONE. */
	int32 SelectedPart = INDEX_NONE;

	UE::Widget::EWidgetMode WidgetMode = UE::Widget::WM_Translate;

	/** "전체 이동" 토글 상태. See SetMoveAll. */
	bool bMoveAll = false;

	/** "선택만 보기" 토글 상태. See SetIsolate/UpdatePartVisibility. */
	bool bIsolate = false;
};
