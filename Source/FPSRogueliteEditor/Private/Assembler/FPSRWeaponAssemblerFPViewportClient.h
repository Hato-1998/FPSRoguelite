// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"
#include "Assembler/FPSRWeaponAssemblerHelpers.h"   // FFPSRFirstPersonSetup

class FPreviewScene;
class SFPSRWeaponAssemblerFPViewport;
class SFPSRWeaponAssemblerTab;
class USkeletalMeshComponent;

/** 무기 어셈블러의 **1인칭 뷰 전용** 뷰포트 클라이언트(별도 도킹 탭, SFPSRWeaponAssemblerFPTab).
 *
 *  존재 이유: 도킹된 조립기 뷰포트는 화면비가 게임과 달라, FOV 가 같아도 **세로 구도가 다르다**. 실측으로 거의
 *  정사각(≈1.14)인 뷰포트에서 세로 82.3° 를 보고 있었는데 16:9 플레이어는 58.7° 를 본다 — 1.40배다. 그 상태로
 *  잡은 그립은 인게임에서 어긋난다(사용자가 PIE 로 확인한 그 증상).
 *
 *  ## 두 가지를 서로 다른 방법으로 고정한다 — 하나로는 안 된다
 *
 *  **투영(종횡비·FOV)** = `bUseControllingActorViewInfo` + `ControllingActorViewInfo.bConstrainAspectRatio`.
 *  이게 에디터 뷰포트에서 종횡비를 고정하는 **유일한** 경로다(EditorViewportClient.cpp:1158). 엔진이 레터박스
 *  검은 띠까지 그려 준다(같은 파일 4746: `Canvas->Clear(Black)`).
 *
 *  **카메라 위치·회전** = 매 프레임 되돌리기(Tick). 🚨 종횡비 고정이 이것까지 해 준다고 착각하면 안 된다 —
 *  엔진은 뷰 위치/회전을 **뷰포트 자기 트랜스폼**에서 읽고(EditorViewportClient.cpp:1081-1082, ViewOrigin 은 1109),
 *  우리가 `ControllingActorViewInfo.Location/Rotation` 에 넣은 값은 매 프레임 **거꾸로 덮어쓴다**(1102-1106).
 *  즉 그 필드로는 카메라가 안 잠긴다. 처음 켤 땐 자리가 맞아 정상으로 보이다가 드래그하면 무너지는, 알아채기
 *  어려운 형태로 깨진다. 그래서 Tick 에서 SetViewLocation/SetViewRotation 으로 되돌린다.
 *  (입력 경로를 하나씩 막지 않는 이유: 궤도/팬/돌리/북마크/포커스 등 갈래가 많아 반드시 새는 길이 남는다.)
 *
 *  ## 씬은 빌려 쓴다
 *  조립기 탭이 소유한 FAdvancedPreviewScene 을 **공동 소유**하는 것은 탭 위젯(SFPSRWeaponAssemblerFPTab)이고,
 *  이 클라이언트는 베이스 클래스에 넘긴 참조로만 쓴다. 팔 컴포넌트는 조립기 쪽이 만들고 없애므로 약참조로 들고
 *  매 Tick 다시 확인한다(팔 보기를 끄면 사라진다). */
class FFPSRWeaponAssemblerFPViewportClient : public FEditorViewportClient
{
public:
	FFPSRWeaponAssemblerFPViewportClient(FPreviewScene& InPreviewScene,
	                                     const TSharedRef<SFPSRWeaponAssemblerFPViewport>& InViewport,
	                                     const TWeakPtr<SFPSRWeaponAssemblerTab>& InAssemblerTab);

	/** 설정의 프리뷰 캐릭터를 프리뷰 씬에 잠깐 스폰해 구도를 다시 읽는다(스폰→읽기→파괴). 구도는 한 번 읽어 캐시하고
	 *  카메라 월드는 매 Tick 살아 있는 팔 트랜스폼으로 다시 합성한다 — 매 프레임 액터를 스폰하지 않기 위해서다.
	 *  실패하면 사유가 GetIssue() 에 남고 IsCompositionValid() 가 false 가 된다. */
	void RefreshComposition();

	/** 화면비 프리셋 적용. 🚨 종횡비만 바꾸면 안 된다 — 게임은 세로 FOV 를 유지하므로 가로 FOV 를 다시 구해야
	 *  같은 구도가 나온다(FPSRWeaponAssemblerHelpers::DeriveFOVForAspect 주석 참조). */
	void SetTargetAspect(float InAspect);
	float GetTargetAspect() const { return TargetAspect; }

	// --- 숫자 계기 (탭 상단 표시용) ---------------------------------------------------------------------------------
	// 🚩 이 게터들이 있는 이유: v1 은 읽어온 값을 어디에도 표시하지 않아서, 맞는지 확인하려면 매번 에디터 밖에서
	//    따로 재야 했다. "판정은 숫자로" 를 실제로 할 수 있으려면 툴이 자기가 쓰는 값을 화면에 내놔야 한다.

	bool IsCompositionValid() const { return bHasComposition; }
	const FString& GetIssue() const { return Issue; }
	/** 카메라가 팔에 대해 어디에 있는가(= 캐릭터 BP 의 팔 상대 트랜스폼의 역행렬). */
	const FTransform& GetCameraRelativeToArms() const { return Composition.CameraRelativeToArms; }
	/** 캐릭터 카메라의 원본 FOV(가로, 도). */
	float GetSourceFOV() const { return Composition.View.FOV; }
	/** 캐릭터 카메라의 원본 종횡비(세로 FOV 를 결정하는 기준). */
	float GetSourceAspect() const { return Composition.View.AspectRatio; }
	/** 지금 프리셋에서 실제로 적용 중인 가로 FOV(도). */
	float GetAppliedFOV() const { return AppliedFOV; }
	/** 지금 보고 있는 세로 FOV(도) — 프리셋이 바뀌어도 이 값이 안 변해야 게임과 같은 구도다. */
	float GetAppliedVerticalFOV() const;
	/** 팔이 서 있는가(= 조립기 탭에서 "팔 보기"가 켜져 있는가). 꺼져 있으면 구도의 기준이 없다. */
	bool HasArms() const { return ArmsComp.IsValid(); }

	// FEditorViewportClient interface
	virtual void Tick(float DeltaSeconds) override;
	/** 화면 중앙에 조준 축 기준선을 그린다.
	 *  🚨 중심은 뷰포트 한가운데가 아니라 **레터박스 안쪽 사각형**(FSceneView::CameraConstrainedViewRect)의 한가운데다 —
	 *  게임 화면의 중앙은 검은 띠를 뺀 영역의 중앙이기 때문. (지금 엔진은 그 사각형을 가운데 정렬로 만들어서
	 *  UnrealClient.cpp:2204 결과가 같지만, 뜻이 다른 두 값을 우연히 같다는 이유로 바꿔 쓰면 나중에 조용히 틀어진다.)
	 *  🚨 게임 크로스헤어 위젯/UI 머티리얼을 렌더타깃에 그리지 않는다 — 이 프로젝트는 그러다 GPU 가 물린 전례가 있다. */
	virtual void DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) override;

private:
	/** 캐시한 구도(카메라↔팔 상대 + 캐릭터 카메라 뷰인포). RefreshComposition 이 채운다. */
	FPSRWeaponAssemblerHelpers::FFPSRFirstPersonSetup Composition;
	bool bHasComposition = false;
	FString Issue;

	/** 프리셋 종횡비. 기본 16:9 — 캐릭터 카메라의 종횡비를 첫 프리셋으로 삼는 게 아니라 여기서 시작하고, 탭이
	 *  설정의 프리셋 목록으로 곧바로 덮는다. */
	float TargetAspect = 16.0f / 9.0f;

	/** TargetAspect 에서 실제로 적용 중인 가로 FOV. DeriveFOVForAspect 의 결과이며 계기에도 이 값을 띄운다. */
	float AppliedFOV = 90.0f;

	/** 조립기 탭(씬·팔의 주인). 노매드 탭이라 사용자가 언제든 닫을 수 있으므로 약참조. */
	TWeakPtr<SFPSRWeaponAssemblerTab> AssemblerTab;

	/** 조립기 쪽 팔 컴포넌트. "팔 보기"를 끄면 은퇴시키므로 raw 로 들면 매달린다 — 약참조로 매 Tick 다시 받는다. */
	TWeakObjectPtr<USkeletalMeshComponent> ArmsComp;

	/** ControllingActorViewInfo 에 종횡비·FOV·잠금값을 반영하고, 카메라를 팔 기준 제자리로 되돌린다. Tick 이 부른다. */
	void ApplyComposition();
};
