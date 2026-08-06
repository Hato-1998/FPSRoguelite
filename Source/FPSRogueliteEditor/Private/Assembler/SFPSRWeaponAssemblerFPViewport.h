// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"

class FFPSRWeaponAssemblerFPViewportClient;
class FPreviewScene;
class SFPSRWeaponAssemblerTab;

/** 1인칭 뷰 탭(SFPSRWeaponAssemblerFPTab)의 3D 뷰포트. 얇은 SEditorViewport 래퍼로, 구도/종횡비/잠금은 전부
 *  FFPSRWeaponAssemblerFPViewportClient 가 갖는다.
 *
 *  🚩 씬을 **소유하지 않는다** — 조립기 탭이 만든 FAdvancedPreviewScene 을 그대로 그린다(그래서 두 탭이 같은 무기·
 *  같은 팔을 본다). 씬의 수명은 탭 위젯이 TSharedPtr 공동 소유로 지킨다(SFPSRWeaponAssemblerFPTab 주석 참조);
 *  이 위젯은 그 씬이 살아 있는 동안에만 존재한다. 프리뷰 컴포넌트의 GC 보호도 씬(FPreviewScene::AddReferencedObjects)과
 *  조립기 쪽 뷰포트가 이미 하고 있으므로 여기서 FGCObject 를 또 달지 않는다. */
class SFPSRWeaponAssemblerFPViewport : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SFPSRWeaponAssemblerFPViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FPreviewScene>& InPreviewScene,
	               const TWeakPtr<SFPSRWeaponAssemblerTab>& InAssemblerTab);

	TSharedPtr<FFPSRWeaponAssemblerFPViewportClient> GetFPClient() const { return FPViewportClient; }

protected:
	// SEditorViewport interface
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	TSharedPtr<FPreviewScene> PreviewScene;
	TWeakPtr<SFPSRWeaponAssemblerTab> AssemblerTab;
	TSharedPtr<FFPSRWeaponAssemblerFPViewportClient> FPViewportClient;
};
