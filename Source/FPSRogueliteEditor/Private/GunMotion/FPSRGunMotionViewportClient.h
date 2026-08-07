// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"
#include "Camera/CameraTypes.h"   // FMinimalViewInfo

class FPreviewScene;
class SFPSRGunMotionViewport;
class UAnimSequence;
class UDebugSkelMeshComponent;
class UStaticMeshComponent;
struct FFPSRGunMotionKey;

/** 키가 커밋됐을 때(§9 TrackingStopped) 탭에 알린다: 스크럽 시각 + 카메라 공간 오프셋/회전(§0 규약, X=앞/Y=오른쪽/Z=위). */
DECLARE_DELEGATE_ThreeParams(FOnFPSRGunMotionGizmoCommit, float /*Time*/, FVector /*CamOffset*/, FRotator /*CamRotation*/);

/**
 * 총 모션 저작 툴의 뷰포트 클라이언트(GunMotionTool_Spec.md 증보 v2 §7-§9) — 이 탭이 **단독 소유**하는
 * FAdvancedPreviewScene 위에 팔(UDebugSkelMeshComponent)+무기(UStaticMeshComponent)를 세우고, 1인칭 카메라를
 * AFPSRCharacter::GetFirstPersonViewSetup 기준으로 고정하며, 무기 컴포넌트를 대상으로 하는 이동/회전 기즈모를 제공한다.
 *
 * 어셈블러 뷰포트 클라이언트(FFPSRWeaponAssemblerViewportClient)의 기즈모 스택 패턴(GetWidgetLocation/
 * InputWidgetDelta/TrackingStarted·Stopped/SetWidgetMode, ModeTools 미경유)과 1인칭 프레이밍 패턴
 * (FFPSRWeaponAssemblerFPViewportClient — bUseControllingActorViewInfo + 매 Tick 카메라 되돌리기)을 답습한다.
 * 씬은 이 탭 전용이라 공유 수명주기 지뢰(SFPSRWeaponAssemblerFPTab 헤더 주석 참조)가 없다 — 공동소유/약참조가 필요 없다.
 */
class FFPSRGunMotionViewportClient : public FEditorViewportClient
{
public:
	FFPSRGunMotionViewportClient(FPreviewScene& InPreviewScene, const TSharedRef<SFPSRGunMotionViewport>& InViewport);

	/** 클립 선택(§8) — 원본 클립을 transient 패키지에 복제해 프리뷰 클립으로 삼고, 베이스라인(GunBase)을 다시 재고
	 *  InitialKeys 로 최초 굽기까지 실행한다. SourceClip==null 이면 프리뷰를 비운다. */
	void SetSourceClip(UAnimSequence* SourceClip, const TArray<FFPSRGunMotionKey>& InitialKeys);

	/** 키가 바뀔 때마다(기즈모 드래그 종료·숫자 편집·키 추가/삭제·총 고정화 재실행) 호출 — 프리뷰 클립을 다시 굽는다
	 *  (§8, "항상 구운 결과를 본다"). 기즈모로 만든 임시 델타는 여기서 해제된다(§9 마지막 문단). */
	void RebakePreview(const TArray<FFPSRGunMotionKey>& Keys);

	bool HasPreviewClip() const { return PreviewClip != nullptr; }
	/** 팔/카메라/무기 준비 상태에 대한 마지막 사유(없으면 정상). */
	const FString& GetIssue() const { return Issue; }

	// --- 타임라인(§8) — FFPSRWeaponAssemblerViewportClient의 B4 래퍼와 동일한 근거(GetSingleNodeInstance 기반). ---
	void SetPreviewPlaying(bool bPlay);
	bool IsPreviewPlaying() const;
	/** 스크럽 — PreviewInstance 를 해당 시각에 고정하고 소켓 부착 무기가 그 프레임의 포즈를 즉시 반영하도록 강제 갱신한다. */
	void SetPreviewPosition(float Seconds);
	float GetPreviewPosition() const;
	float GetPreviewLength() const;

	// --- 기즈모(§9) ---
	virtual void SetWidgetMode(UE::Widget::EWidgetMode InMode) override;
	FOnFPSRGunMotionGizmoCommit& OnGizmoCommit() { return GizmoCommitDelegate; }

	// FEditorViewportClient interface — 무기 컴포넌트 하나만을 대상으로 하는 기즈모. ModeTools 를 경유하지 않는다
	// (FFPSRWeaponAssemblerViewportClient 와 같은 이유 — 그 클래스 헤더 클래스 주석 참조: 단일 대상 기즈모에 별도
	// 에디트 모드가 필요 없다).
	virtual FVector GetWidgetLocation() const override;
	virtual FMatrix GetWidgetCoordSystem() const override { return FMatrix::Identity; }
	virtual ECoordSystem GetWidgetCoordSystemSpace() const override { return COORD_World; }
	virtual UE::Widget::EWidgetMode GetWidgetMode() const override { return WidgetMode; }
	virtual bool InputWidgetDelta(FViewport* InViewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale) override;
	virtual void TrackingStarted(const FInputEventState& InInputState, bool bIsDraggingWidget, bool bNudge) override;
	virtual void TrackingStopped() override;
	virtual void Tick(float DeltaSeconds) override;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FFPSRGunMotionViewportClient"); }

private:
	/** 설정(TargetCharacterBP/ArmsComponentName/PreviewWeaponMesh/PreviewWeaponAttachSocket)에서 팔+무기 컴포넌트를
	 *  (재)생성한다. 씬에 등록되므로 GC 보호는 FPreviewScene::AddReferencedObjects 가 담당(어셈블러와 동일 근거). */
	void RebuildArmsAndWeapon();

	/** §9 베이스라인: 프리뷰 클립을 오프셋 0 키 하나로 임시 굽고, 그 상태의 무기 월드 트랜스폼을 GunBase 로 캐시한다.
	 *  우측 체인이 상수라(총 고정화) 시각 무관 상수이므로 클립 선택 시 한 번만 계산하면 된다. */
	void RecomputeGunBase();

	/** §7 카메라: TargetCharacterBP 를 프리뷰 씬에 잠깐 스폰해 GetFirstPersonViewSetup 을 읽고 파괴한다
	 *  (FPSRWeaponAssemblerHelpers::ReadFirstPersonSetup 과 같은 패턴 — 설정 소스가 달라(TSoftObjectPtr<UBlueprint>
	 *  vs TSoftClassPtr<AFPSRCharacter>) 그 함수를 직접 재사용하지 않고 이 파일에 자체 구현한다). */
	void RefreshCameraComposition();

	/** 종횡비 고정 + FOV 파생 + 카메라 월드 되돌리기. 매 Tick 호출(엔진이 ControllingActorViewInfo 를 뷰포트 상태로
	 *  되쓰기 때문 — FFPSRWeaponAssemblerFPViewportClient 헤더 주석과 같은 근거). */
	void ApplyCameraComposition();

	FPreviewScene& PreviewScene;

	/** 팔 프리뷰. UDebugSkelMeshComponent 인 이유 = 애디티브 프리뷰(UAnimPreviewInstance, bCanProcessAdditiveAnimations)
	 *  — FFPSRWeaponAssemblerViewportClient::ArmsComp 멤버 주석 참조(같은 실사고 근거). */
	UDebugSkelMeshComponent* ArmsComp = nullptr;

	/** 무기 프리뷰 — 팔 소켓(PreviewWeaponAttachSocket)에 스냅 부착, 상대 트랜스폼 0(§7: hand_r 소켓 부착 =
	 *  인게임 gun-anchor 와 등가). 기즈모가 이 컴포넌트 하나만을 대상으로 한다(§9). */
	UStaticMeshComponent* WeaponComp = nullptr;

	/** 원본 클립을 transient 패키지에 복제한 프리뷰 클립(§8). 씬에 등록되지 않으므로 AddReferencedObjects 가 필요. */
	TObjectPtr<UAnimSequence> PreviewClip = nullptr;

	/** §9 베이스라인 — 오프셋 0 상태의 무기 월드 트랜스폼. */
	FTransform GunBase = FTransform::Identity;
	bool bHasGunBase = false;

	UE::Widget::EWidgetMode WidgetMode = UE::Widget::WM_Translate;

	/** §7 카메라 구도 캐시. */
	FTransform CameraRelativeToArms = FTransform::Identity;
	FMinimalViewInfo CameraView;
	bool bHasComposition = false;

	/** 팔/무기/카메라 준비 실패 사유(없으면 빈 문자열). */
	FString Issue;

	FOnFPSRGunMotionGizmoCommit GizmoCommitDelegate;
};
