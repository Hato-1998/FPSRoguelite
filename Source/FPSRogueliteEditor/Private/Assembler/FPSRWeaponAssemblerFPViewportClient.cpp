// Copyright Epic Games, Inc. All Rights Reserved.

#include "Assembler/FPSRWeaponAssemblerFPViewportClient.h"

#include "Assembler/SFPSRWeaponAssemblerFPViewport.h"
#include "Assembler/SFPSRWeaponAssemblerTab.h"
#include "Assembler/FPSRWeaponAssemblerViewportClient.h"

#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"      // GEngine->GetTinyFont()
#include "PreviewScene.h"
#include "SceneView.h"          // FSceneView::CameraConstrainedViewRect
#include "Engine/World.h"

FFPSRWeaponAssemblerFPViewportClient::FFPSRWeaponAssemblerFPViewportClient(
	FPreviewScene& InPreviewScene,
	const TSharedRef<SFPSRWeaponAssemblerFPViewport>& InViewport,
	const TWeakPtr<SFPSRWeaponAssemblerTab>& InAssemblerTab)
	: FEditorViewportClient(nullptr, &InPreviewScene, StaticCastSharedRef<SEditorViewport>(InViewport))
	, AssemblerTab(InAssemblerTab)
{
	SetRealtime(true);

	// 종횡비 고정 경로를 켠다. 이 플래그가 꺼져 있으면 bConstrainAspectRatio 는 아예 읽히지도 않는다
	// (EditorViewportClient.cpp:1158 — 둘의 AND 다).
	bUseControllingActorViewInfo = true;

	// 축 제약을 명시해 둔다. bConstrainAspectRatio 가 켜져 있는 동안엔 엔진이 이 값을 쓰지 않지만
	// (CameraStackTypes.cpp:263 에서 제약 분기로 빠진다), 비워 두면 그 값이 **사용자의 에디터 환경설정**
	// (BaseEditorPerProjectUserSettings.ini = MaintainXFOV)에서 온다(EditorViewportClient.cpp:1168).
	// 툴의 구도가 사람마다 달라질 수 있는 변수는 애초에 남기지 않는다.
	ControllingActorAspectRatioAxisConstraint = AspectRatio_MaintainYFOV;

	RefreshComposition();
}

void FFPSRWeaponAssemblerFPViewportClient::RefreshComposition()
{
	Issue.Reset();
	bHasComposition = false;

	UWorld* World = GetWorld();
	if (!World)
	{
		Issue = TEXT("프리뷰 씬 월드가 없어 1인칭 구도를 읽지 못했습니다.");
		return;
	}

	bHasComposition = FPSRWeaponAssemblerHelpers::ReadFirstPersonSetup(World, Composition, Issue);
	if (bHasComposition)
	{
		SetTargetAspect(TargetAspect);   // 새 구도로 FOV 를 다시 파생
	}
}

void FFPSRWeaponAssemblerFPViewportClient::SetTargetAspect(float InAspect)
{
	TargetAspect = FMath::Max(UE_KINDA_SMALL_NUMBER, InAspect);
	AppliedFOV = bHasComposition
		? FPSRWeaponAssemblerHelpers::DeriveFOVForAspect(Composition.View, TargetAspect)
		: Composition.View.FOV;
	Invalidate();
}

float FFPSRWeaponAssemblerFPViewportClient::GetAppliedVerticalFOV() const
{
	return FPSRWeaponAssemblerHelpers::ComputeVerticalFOV(AppliedFOV, TargetAspect);
}

void FFPSRWeaponAssemblerFPViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);

	// 팔 애니를 진행시킨다. 조립기 탭도 같은 씬을 돌리므로 프레임당 한 번으로 문지기를 둔다(안 그러면 두 탭을
	// 나란히 띄웠을 때 2배속이 된다 — TickPreviewWorldOnce 주석 참조).
	FPSRWeaponAssemblerHelpers::TickPreviewWorldOnce(GetWorld(), DeltaSeconds);

	// 팔은 조립기 쪽이 만들고 없앤다("팔 보기" 토글) — 매 프레임 다시 받는다.
	ArmsComp = nullptr;
	if (const TSharedPtr<SFPSRWeaponAssemblerTab> Tab = AssemblerTab.Pin())
	{
		if (const TSharedPtr<FFPSRWeaponAssemblerViewportClient> Client = Tab->GetAssemblerViewportClient())
		{
			ArmsComp = Client->GetArmsComp();
		}
	}

	ApplyComposition();
}

void FFPSRWeaponAssemblerFPViewportClient::ApplyComposition()
{
	// 투영: 종횡비 고정 + 그 종횡비에 맞게 다시 구한 가로 FOV. 매 프레임 다시 넣는 이유는 엔진이 그리는 동안
	// ControllingActorViewInfo 를 뷰포트 상태로 되쓰기 때문이다(EditorViewportClient.cpp:1102-1106).
	ControllingActorViewInfo = Composition.View;
	ControllingActorViewInfo.AspectRatio = TargetAspect;
	ControllingActorViewInfo.bConstrainAspectRatio = true;
	ControllingActorViewInfo.FOV = AppliedFOV;
	ViewFOV = AppliedFOV;   // 계기/폴백 경로가 같은 값을 보게 맞춰 둔다

	if (!bHasComposition)
	{
		return;
	}

	// 카메라: 팔의 **살아 있는** 월드에 캐시한 상대값을 곱한다. 팔을 항등에 세운다는 전제에 기대지 않는다 —
	// 팔 배치가 바뀌어도 구도가 따라가야 한다.
	const FTransform ArmsWorld = ArmsComp.IsValid() ? ArmsComp->GetComponentTransform() : FTransform::Identity;
	const FTransform CameraWorld = Composition.CameraRelativeToArms * ArmsWorld;

	// 🚨 여기가 잠금이다. ControllingActorViewInfo.Location/Rotation 에 넣어도 소용없다(헤더 주석 참조) —
	// 엔진이 읽는 것은 뷰포트 자기 트랜스폼이므로 그걸 되돌려야 한다.
	SetViewLocation(CameraWorld.GetLocation());
	SetViewRotation(CameraWorld.GetRotation().Rotator());
}

void FFPSRWeaponAssemblerFPViewportClient::DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);

	// 게임 화면의 중앙 = 레터박스 검은 띠를 뺀 안쪽 사각형의 중앙. 제약이 걸려 있지 않은 경우(구도를 못 읽어
	// 폴백 중일 때)만 뷰 전체 사각형으로 떨어진다.
	const FIntRect Rect = View.CameraConstrainedViewRect.Area() > 0 ? View.CameraConstrainedViewRect : View.UnscaledViewRect;
	const float CX = Rect.Min.X + Rect.Width() * 0.5f;
	const float CY = Rect.Min.Y + Rect.Height() * 0.5f;

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
