// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "UObject/GCObject.h"

class FFPSRWeaponAssemblerViewportClient;
class FPreviewScene;

/** The 3D preview viewport for the Weapon Part Assembler tool (Tools > FPSR > "무기 파츠 조립기…", see
 *  SFPSRWeaponAssemblerTab). Thin SEditorViewport wrapper — all weapon-preview state (body/part components, gizmo
 *  selection, widget mode) lives on FFPSRWeaponAssemblerViewportClient; this widget only owns the FPreviewScene
 *  reference needed to construct that client and builds it in MakeEditorViewportClient(). */
class SFPSRWeaponAssemblerViewport : public SEditorViewport, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SFPSRWeaponAssemblerViewport) {}
	SLATE_END_ARGS()

	/** InPreviewScene is the tab's shared FAdvancedPreviewScene (also handed to the parts-list side of the tab so
	 *  both panels agree on the same body/part components). */
	void Construct(const FArguments& InArgs, const TSharedRef<FPreviewScene>& InPreviewScene);

	TSharedPtr<FFPSRWeaponAssemblerViewportClient> GetAssemblerClient() const { return AssemblerViewportClient; }

	// FGCObject interface — defense-in-depth on top of FPreviewScene's own component GC protection (see
	// FFPSRWeaponAssemblerViewportClient's class comment): re-registers the client's current BodyComp/PartComps here
	// too, so this widget doesn't silently depend on an implementation detail of a scene object it doesn't own.
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SFPSRWeaponAssemblerViewport"); }

protected:
	// SEditorViewport interface
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	TSharedPtr<FPreviewScene> PreviewScene;
	TSharedPtr<FFPSRWeaponAssemblerViewportClient> AssemblerViewportClient;
};
