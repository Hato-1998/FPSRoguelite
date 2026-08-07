// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"

class FFPSRGunMotionViewportClient;
class FPreviewScene;

/** 총 모션 저작 툴 탭(SFPSRGunMotionTab)의 3D 프리뷰 뷰포트(증보 v2 §7). 얇은 SEditorViewport 래퍼 — 모든 프리뷰
 *  상태(팔/무기 컴포넌트, 카메라 프레이밍, 기즈모)는 FFPSRGunMotionViewportClient 가 갖는다.
 *
 *  🚩 씬을 **이 탭이 단독 소유**한다(어셈블러 씬 공유 금지, §7) — SFPSRWeaponAssemblerFPViewport 와 달리 이 위젯은
 *  프리뷰 씬을 빌려 쓰는 게 아니라 탭이 만든 FAdvancedPreviewScene 을 그대로 넘겨받아 그린다. 저작 대상이 무기
 *  조립이 아니라 클립이라 공유할 이유가 없다. */
class SFPSRGunMotionViewport : public SEditorViewport
{
public:
	SLATE_BEGIN_ARGS(SFPSRGunMotionViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FPreviewScene>& InPreviewScene);

	TSharedPtr<FFPSRGunMotionViewportClient> GetGunMotionClient() const { return GunMotionViewportClient; }

protected:
	// SEditorViewport interface
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	TSharedPtr<FPreviewScene> PreviewScene;
	TSharedPtr<FFPSRGunMotionViewportClient> GunMotionViewportClient;
};
