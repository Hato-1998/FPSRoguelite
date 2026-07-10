// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"

class FPreviewScene;
class SFPSRWeaponAssemblerViewport;
class UFPSRWeaponDataAsset;
class USkeletalMeshComponent;
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
	 *  GetWidgetLocation then returns the origin and the gizmo effectively has nothing to grab. */
	void SetSelectedPart(int32 Index);
	int32 GetSelectedPart() const { return SelectedPart; }

	/** Toggles the gizmo between move/rotate (tab toolbar's "이동"/"회전" buttons). Overrides the base class's own
	 *  SetWidgetMode (which routes through FEditorModeTools) since this client owns WidgetMode/GetWidgetMode()
	 *  itself and never touches ModeTools — see the class comment. */
	virtual void SetWidgetMode(UE::Widget::EWidgetMode InMode) override;

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
};
