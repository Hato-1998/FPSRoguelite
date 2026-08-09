// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"
#include "Camera/CameraTypes.h"   // FMinimalViewInfo
#include "GunMotion/FPSRGunMotionChannelId.h"   // v3 §20-4: 활성 채널 id/종류

class FPreviewScene;
class SFPSRGunMotionViewport;
class UAnimSequence;
class UDebugSkelMeshComponent;
class UStaticMeshComponent;
class USphereComponent;
class UPrimitiveComponent;
class UFPSRGunMotionAuthoringData;
struct FFPSRGunMotionKey;

/** v3 §20-4: 기즈모 드래그 종료(TrackingStopped) 커밋 — 채널 id + 그 채널 공간의 시각/loc/rot(§18: 총=카메라 공간,
 *  손=총 공간, 파츠=파츠 로컬). §20-3 "기존 MutateKey 단일 훅을 채널 id 로 파라미터화"의 뷰포트 쪽 절반. */
DECLARE_DELEGATE_FourParams(FOnFPSRGunMotionChannelGizmoCommit, FName /*ChannelId*/, float /*Time*/, FVector /*Loc*/, FRotator /*Rot*/);

/** v3 §20-4: 뷰포트에서 히트 프록시 클릭으로 대상(총/손/파츠)을 골랐을 때 탭에 알린다 — 탭이 SelectedChannelId 를
 *  갱신하고 SetActiveChannel 로 되비춘다(진짜 소유자는 탭, 클라이언트는 탭이 미는 상태를 그대로 반영). */
DECLARE_DELEGATE_OneParam(FOnFPSRGunMotionChannelPicked, FName /*ChannelId*/);

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
	 *  InitialData 로 최초 굽기까지 실행한다. SourceClip==null 이면 프리뷰를 비운다. v3 §20-4: InitialData 전체를
	 *  받는 이유는 레거시 Keys(hand_r 본 트랙) 뿐 아니라 §18 채널 트랙도 여기서부터 직접 평가로 적용해야 해서다. */
	void SetSourceClip(UAnimSequence* SourceClip, const UFPSRGunMotionAuthoringData* InitialData);

	/** 키가 바뀔 때마다(기즈모 드래그 종료·숫자 편집·키 추가/삭제·총 고정화 재실행) 호출 — 프리뷰 클립을 다시 굽는다
	 *  (§8, "항상 구운 결과를 본다"). 기즈모로 만든 임시 델타는 여기서 해제된다(§9 마지막 문단). v3 §20-4: 레거시
	 *  hand_r 굽기가 실패해도(예: bSanitized=false) §18 채널 직접 평가는 계속 적용한다 — v3 신규 클립은 [새 액션
	 *  클립]에서 bSanitized=true 로 시작하지만, 이미 있던 v2 클립을 손대는 도중에도 채널 저작이 막히면 안 된다. */
	void RebakePreview(const UFPSRGunMotionAuthoringData* Data);

	bool HasPreviewClip() const { return PreviewClip != nullptr; }
	/** 지금 프리뷰 클립(§14 PIE 라이브 링크가 미러링할 대상 — RebakePreview 가 in-place 로 덮어쓰는 바로 그
	 *  객체). 없으면 null. */
	UAnimSequence* GetPreviewClip() const { return PreviewClip; }
	/** 팔/카메라/무기 준비 상태에 대한 마지막 사유(없으면 정상). */
	const FString& GetIssue() const { return Issue; }

	/** 증보 v2.1 §11: [PIE 구도 캡처] 버튼이 설정(UFPSRGunMotionSettings)에 값을 쓴 뒤 호출 — bHasCapturedComposition
	 *  이면 캡처값을, 아니면 기존 프로브 경로(§7, CDO 스폰)로 구도를 다시 읽는다. 생성자도 이걸 호출한다(원래
	 *  private 였던 §7 경로를 이번 증보에서 재사용 가능하도록 public 으로 올렸다). */
	void RefreshCameraComposition();

	// --- 타임라인(§8) — FFPSRWeaponAssemblerViewportClient의 B4 래퍼와 동일한 근거(GetSingleNodeInstance 기반). ---
	void SetPreviewPlaying(bool bPlay);
	bool IsPreviewPlaying() const;
	/** 스크럽 — PreviewInstance 를 해당 시각에 고정하고 소켓 부착 무기가 그 프레임의 포즈를 즉시 반영하도록 강제 갱신한다. */
	void SetPreviewPosition(float Seconds);
	float GetPreviewPosition() const;
	float GetPreviewLength() const;

	// --- 기즈모(§9) ---
	virtual void SetWidgetMode(UE::Widget::EWidgetMode InMode) override;

	// --- v3 §20-4: 채널 선택 + 손/파츠 프리뷰 ---------------------------------------------------------------------
	// (기즈모 커밋 델리게이트는 이제 채널 id 를 함께 나르는 FOnFPSRGunMotionChannelGizmoCommit 하나뿐이다 — v2 시절
	// "무기 컴포넌트 고정 대상" 3-param 델리게이트는 폐기했다. Gun 채널로 커밋되면 §9/§12 수학은 그대로다.)

	/** 지금 기즈모가 조작하는 채널(§20-3 "기즈모는 활성 채널의 대상을 조작"). 소유자는 탭(SFPSRGunMotionTab) — 이
	 *  세터는 탭이 미는 상태를 그대로 반영만 한다(레인 클릭·드롭다운·히트 프록시 클릭 전부 탭을 거쳐 여기로 온다). */
	void SetActiveChannel(FName ChannelId);
	FName GetActiveChannel() const { return ActiveChannelId; }

	/** §20-3 파츠 레인 자동 생성 — 지금 프리뷰에 붙어 있는 파츠들의 채널 id(=안정 부착소켓 id) 목록. */
	const TArray<FName>& GetPartChannelIds() const { return PartSocketIds; }

	/** v3 §20-4 "뷰포트 포즈 반영 = 평가값 직접 적용" — 스크럽/키 변경마다 호출해 §3-3(채널 무관 일반화) 평가 결과를
	 *  총/파츠/손 프리뷰 컴포넌트 상대 트랜스폼에 가산 적용한다(런타임과 같은 수학 — 근사 시각화 금지). Data 가
	 *  null 이거나 파츠/손 트랙이 비었으면 그 컴포넌트는 베이스라인(오프셋 0)으로 남는다. */
	void ApplyChannelEvaluationAtTime(const UFPSRGunMotionAuthoringData* Data, float Time);

	FOnFPSRGunMotionChannelGizmoCommit& OnChannelGizmoCommit() { return ChannelGizmoCommitDelegate; }
	FOnFPSRGunMotionChannelPicked& OnChannelPicked() { return ChannelPickedDelegate; }

	// --- 증보 v2.1 §12: 자유시점 토글 ------------------------------------------------------------------------------

	/** ON 이면 Tick 이 ApplyCameraComposition 을 건너뛴다(표준 에디터 뷰포트 네비게이션 — 회전/이동/줌). 카메라
	 *  자체는 건드리지 않고 스위치만 바꾸므로 진입 시 지금 구도 위치에서 그대로 시작한다(§12 "뷰가 튀지 않게").
	 *  OFF 로 돌아오면 다음 Tick 을 기다리지 않고 즉시 구도로 스냅한다. */
	void SetFreeLook(bool bEnable);
	bool IsFreeLook() const { return bFreeLook; }

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

	/** v3 §20-4: 히트 프록시 클릭으로 대상(총/파츠/손) 선택 — 어셈블러 뷰포트 클라이언트가 답습하는 것과 같은
	 *  "HActor 계열 패턴"(엔진 기본 UPrimitiveComponent::CreateHitProxies 가 액터 없는 프리뷰 컴포넌트에도
	 *  `HActor(nullptr, this)`를 붙여 준다 — Actor 는 null 이어도 PrimComponent 는 유효하다). 대상이 아닌 클릭은
	 *  Super 로 넘긴다. ModeTools 미경유 원칙(§9 클래스 주석)과 별개 경로 — 위젯 드래그는 이 함수를 타지 않는다. */
	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;

	/** §12: F 키로 자유시점 토글(SCSEditorViewportClient::InputKey 와 같은 오버라이드 패턴 — 이 프로젝트 뷰포트
	 *  클라이언트 중 InputKey 를 직접 잡는 첫 사례. F 는 FEditorViewportClient 기본 구현이 소비하지 않는다). */
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FFPSRGunMotionViewportClient"); }

private:
	/** 설정(TargetCharacterBP/ArmsComponentName/PreviewWeaponMesh/PreviewWeaponAttachSocket)에서 팔+무기 컴포넌트를
	 *  (재)생성한다. 씬에 등록되므로 GC 보호는 FPreviewScene::AddReferencedObjects 가 담당(어셈블러와 동일 근거). */
	void RebuildArmsAndWeapon();

	/** §9 베이스라인: 프리뷰 클립을 오프셋 0 키 하나로 임시 굽고, 그 상태의 무기 월드 트랜스폼을 GunBase 로 캐시한다.
	 *  우측 체인이 상수라(총 고정화) 시각 무관 상수이므로 클립 선택 시 한 번만 계산하면 된다. */
	void RecomputeGunBase();

	/** 종횡비 고정 + FOV 파생 + 카메라 월드 되돌리기. 매 Tick 호출(엔진이 ControllingActorViewInfo 를 뷰포트 상태로
	 *  되쓰기 때문 — FFPSRWeaponAssemblerFPViewportClient 헤더 주석과 같은 근거). */
	void ApplyCameraComposition();

	/** §9 역산의 CamRot 원본 — 항상 "잠금 구도"의 카메라 회전이다(§12: 자유시점 여부와 무관). GetViewRotation() 은
	 *  자유시점일 때 그 자유 카메라 회전을 돌려주므로 여기선 쓰지 않고, ApplyCameraComposition 이 잠금 모드에서
	 *  카메라에 넣는 것과 같은 식(CameraRelativeToArms * ArmsWorld)을 항상 다시 계산한다. */
	FQuat GetLockedCompositionCameraRotation() const;

	/** RebuildArmsAndWeapon 의 §20-4 확장 — Settings->PreviewWeaponData 에서 파츠 목록(WeaponParts)과 그립 소켓
	 *  (LeftHandSocket/RightHandSocket, AFPSRCharacter::ComputeGripInGunFrame 이 읽는 것과 같은 필드)을 읽어
	 *  PartComps/PartSocketIds/PartSocketRelative/PartAuthoredOffset 을 (재)구성하고 손 IK 타깃 프록시(구체) 2개를 세운다. 그립/부착
	 *  소켓이 WeaponComp 의 스태틱 메시에 없으면 무기 원점 폴백 + Issue 한 줄(§20-4, 조작엔 지장 없음). */
	void RebuildPartsAndHandProxies();

	/** ProcessClick 히트 프록시 → 채널 id(§20-4). 대상이 아니면 NAME_None. */
	FName ResolveChannelForComponent(const UPrimitiveComponent* Component) const;

	/** GetWidgetLocation/InputWidgetDelta/TrackingStopped 가 공유하는 "지금 기즈모가 물고 있는 실제 컴포넌트"
	 *  (§9 를 WeaponComp 하나에서 채널 4종으로 일반화한 것). ActiveChannelId 가 가리키는 대상이 없으면 null. */
	UPrimitiveComponent* GetActiveTargetComponent() const;

	/** TrackingStopped 의 손/파츠 채널 역산(§20-4 "손=총 공간 / 파츠=파츠 로컬") — Gun 채널은 기존 §9·§12 카메라
	 *  공간 역산을 그대로 쓴다(안 건드림). 부모 회전은 채널 종류로 스스로 고른다(손=WeaponComp 의 지금 월드 회전
	 *  ="총 공간", 파츠=그 파츠 자신의 베이스(오프셋 0) 월드 회전="파츠 로컬"). TrackingStarted 스냅샷
	 *  (DragBaseWorld) 대비 지금 월드 트랜스폼의 델타를 그 부모 회전 기준으로 구해, 그 채널이 지금 시각에 이미
	 *  평가 중이던 값에 가산한 뒤 커밋한다 — Gun 채널의 "GunBase 로부터 절대량 재계산"과 다른 설계(보고서에 해석
	 *  근거 명시: 손/파츠는 부모 프레임 자체가 Gun 채널 오프셋에 따라 매 시각 달라질 수 있어 시간불변 절대 기준을
	 *  못 잡는다). */
	void CommitHandOrPartChannel(FName ChannelId, UPrimitiveComponent* TargetComp);

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

	/** §12: ON 이면 Tick 이 ApplyCameraComposition 을 건너뛴다(표준 에디터 뷰포트 네비게이션). */
	bool bFreeLook = false;

	/** §7 카메라 구도 캐시. */
	FTransform CameraRelativeToArms = FTransform::Identity;
	FMinimalViewInfo CameraView;
	bool bHasComposition = false;

	/** 팔/무기/카메라 준비 실패 사유(없으면 빈 문자열). */
	FString Issue;

	// --- v3 §20-4: 파츠 + 손 IK 타깃 프록시 ----------------------------------------------------------------------

	/** 파츠 프리뷰 — WeaponComp 자식(§20-4 "무기 메시의 Socket+Offset 부착, DA 값 그대로"). 소켓이 없으면 무기
	 *  원점 폴백(Issue 한 줄, "저작값은 오프셋이라 조작엔 지장 없음"). */
	TArray<UStaticMeshComponent*> PartComps;
	/** PartComps 와 병렬 배열 — 채널 id(=그 파츠의 안정 부착소켓 id, FFPSRWeaponPartAttachment::Socket). */
	TArray<FName> PartSocketIds;
	/** 소켓 프레임(WeaponComp 기준 상대, 소켓 폴백 시 Identity)과 DA 저작 오프셋(FFPSRWeaponPartAttachment::Offset)을
	 *  분리 보관한다 — 런타임 ApplyWeaponPartCurves 의 합성(이동=소켓 프레임 가산, 회전=저작 오프셋에 좌곱)을 그대로
	 *  재현하려면 합성된 Base 하나로는 부족하다(오프셋 회전이 섞여 커브 델타의 기준 프레임이 어긋난다 — 검증에서
	 *  잡은 결함). 베이스(오프셋 0) 상대 트랜스폼은 필요 시 PartAuthoredOffset * PartSocketRelative 로 합성. */
	TArray<FTransform> PartSocketRelative;
	TArray<FTransform> PartAuthoredOffset;

	/** 손 IK 타깃 프록시(구체) — WeaponComp 자식("총 공간"이 곧 WeaponComp 기준 상대 공간이라 부모를 그대로 쓴다).
	 *  그립 소켓이 없으면 무기 원점 폴백. */
	USphereComponent* LeftHandProxy = nullptr;
	USphereComponent* RightHandProxy = nullptr;

	/** PreviewWeaponData 에서 읽은 그립 소켓 — AFPSRCharacter::ComputeGripInGunFrame 이 읽는 것과 같은 필드
	 *  (Weapon->LeftHandSocket/RightHandSocket, §20-4 "그립 기준점은 캐릭터 그립 캐시와 같은 소스"). */
	FName LeftHandGripSocket = NAME_None;
	FName RightHandGripSocket = NAME_None;

	/** 지금 기즈모가 조작하는 채널(§20-3). 기본값 Gun — 탭을 열면 총 채널부터 저작하는 게 자연스러운 시작점. */
	FName ActiveChannelId = FPSRGunMotionChannelIds::Gun;

	/** TrackingStarted 스냅샷 — 손/파츠 채널 역산의 기준(CommitHandOrPartChannel 주석 참조). Gun 채널은 안 쓴다
	 *  (기존 GunBase 를 그대로 쓴다). */
	FTransform DragBaseWorld = FTransform::Identity;

	/** 이번 트래킹이 기즈모(위젯) 드래그였는가 — TrackingStarted 의 bIsDraggingWidget 스냅샷. 카메라 궤도/클릭도
	 *  같은 TrackingStarted/Stopped 쌍을 타므로, 이 가드 없이는 자유시점 궤도 조작마다 활성 채널에 무점프 키가
	 *  하나씩 쌓인다(§9 의 "드래그 종료 시 커밋"은 위젯 드래그 얘기다). */
	bool bWidgetDragActive = false;

	/** GetCamToCompRotation()(CDO 어태치체인 워크 + LoadSynchronous) 결과 캐시 — ApplyChannelEvaluationAtTime 이
	 *  매 틱 돌므로 매번 다시 계산하지 않는다. SetSourceClip 에서 무효화(설정 변경은 클립 재선택으로 반영). */
	FQuat CachedCamToComp = FQuat::Identity;
	bool bHasCachedCamToComp = false;

	FOnFPSRGunMotionChannelGizmoCommit ChannelGizmoCommitDelegate;
	FOnFPSRGunMotionChannelPicked ChannelPickedDelegate;

	/** 마지막으로 RebakePreview/SetSourceClip 에 전달된 저작 데이터 — Tick 이 매 프레임 ApplyChannelEvaluationAtTime
	 *  을 다시 부를 때(재생 중 스크럽 위치가 계속 바뀌므로) 쓴다. AssetUserData 는 클립(Seq)의 서브오브젝트라
	 *  TWeakObjectPtr(프로젝트 관례 — 탭도 클립을 약참조로만 든다). */
	TWeakObjectPtr<const UFPSRGunMotionAuthoringData> LastAppliedAuthoringData;
};
