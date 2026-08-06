// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"

class FPreviewScene;
class SFPSRWeaponAssemblerViewport;
class UDebugSkelMeshComponent;
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
	 *  전체 파츠 위치의 평균을, InputWidgetDelta()는 그 평균을 피벗으로 삼아 바디를 이동/회전시킨다(파츠는 이제 바디의
	 *  진짜 자식이라 자동으로 따라온다 — B2/B5).
	 *
	 *  🔀 **의미 변화(B5)**: "팔 보기"가 켜져 있으면(HasGripFrame()) 바디는 팔에 붙어 있는 상태라, 이 토글로 바디를
	 *  옮기는 것은 사실상 **그립 오프셋을 편집하는 것과 같다** — GetGripTransform()이 바뀐 값을 그대로 반영한다.
	 *  즉 "전체 이동"과 "그립 편집"은 팔 보기 중엔 같은 조작의 두 이름이다. */
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
	 *  조립품(바디, 그리고 바디에 매달린 파츠 전체)을 팔의 무기 부착 소켓/뼈에 **실제로 attach** 한다(AttachAssemblyToHand,
	 *  P3 해결) — 그립을 **손에 든 상태**로, 그리고 애니를 재생·스크럽해도 손을 계속 따라가는 상태로 판정하기 위해서다.
	 *  끄면 팔을 걷어내고 조립품을 detach 해 원점으로 되돌린다(DetachAssemblyFromHand).
	 *
	 *  설정에 팔 메시가 비어 있으면 아무 일도 안 한다(false 로 되돌아감) — 콘텐츠가 없는 프로젝트에서 툴이 깨지지
	 *  않게. IsShowArms() 로 실제 상태를 되읽을 것. 부착 기준(소켓/뼈)을 못 찾은 경우도 마찬가지로 조용히 실패하니
	 *  GetArmsStatusMessage() 로 사유를 확인할 것. */
	void SetShowArms(bool bIn);
	bool IsShowArms() const { return bShowArms; }

	/** 설정(UFPSRWeaponAssemblerSettings)의 팔 메시/프리뷰 포즈를 다시 읽어 반영한다 — 프로젝트 설정 피커가 바뀌면
	 *  탭이 이걸 부른다. 팔 보기가 꺼져 있으면 아무 의미가 없으므로 no-op. 메시 자체가 바뀌면(스켈레톤이 다를 수
	 *  있으므로) 팔 컴포넌트를 통째로 새로 세우고, 그립 기준 뼈(GripBone)도 초기화한 뒤 다시 부착한다. */
	void RefreshArmsFromSettings();

	/** 팔/애니 관련 마지막 사유 — 부착 기준(소켓/뼈)을 못 찾았거나, 프리뷰 포즈가 팔 스켈레톤과 비호환이거나. 비어
	 *  있으면 정상. 두 사유가 동시에 있으면 " / "로 이어 붙여 반환한다(레퍼런스 반환용 내부 캐시를 매 호출 재조합). */
	const FString& GetArmsStatusMessage() const;

	// --- 애니 재생/스크럽 (B4) --------------------------------------------------------------------------------------
	// USkeletalMeshComponent 의 단일-노드 재생 편의 래퍼(Play/Stop/IsPlaying/SetPosition/GetPosition)와, 그 래퍼가
	// 없는 Looping/Length 는 GetSingleNodeInstance() 로 직접 처리한다(엔진 소스 확인 — Persona 의 애니메이션 에디터가
	// 같은 조합을 쓴다: AnimationEditorViewportClient.cpp, ThumbnailHelpers.cpp). 팔이 없거나 재생할 애니가 없으면
	// 전부 안전하게 no-op/기본값.

	/** 팔 프리뷰 애니를 재생/일시정지한다. */
	void SetPreviewPlaying(bool bPlay);
	bool IsPreviewPlaying() const;
	/** 재생 위치를 초 단위로 지정(스크럽) — 소켓/자식(바디+파츠)이 새 포즈를 이 프레임에 즉시 반영하도록 강제
	 *  갱신까지 포함한다(다음 Tick을 기다리지 않음). */
	void SetPreviewPosition(float Seconds);
	float GetPreviewPosition() const;
	/** 현재 재생 중인 애니의 길이(초). 팔이 없거나 애니가 없으면 0. */
	float GetPreviewLength() const;
	void SetPreviewLooping(bool bLoop);
	bool IsPreviewLooping() const;

	// --- 그립 (= BakeWeaponSocket 이 구울 값, 뼈 상대) (B3) -----------------------------------------------------------

	/** 팔이 서 있고(bShowArms) 부착 기준 뼈(GripBone)가 잡혀 있어 아래 그립 값들이 의미 있는 상태인가. false면
	 *  GetGripTransform()은 항등을 돌려준다 — "그립 없음"과 "그립이 항등"을 헷갈리지 않으려면 항상 이걸 먼저 볼 것. */
	bool HasGripFrame() const;
	/** 현재 그립을 GripBone **뼈 상대**로 반환한다 — BakeWeaponSocket이 굽는 값과 정확히 같은 식으로 계산한다
	 *  (BodyComp 월드를 ArmsComp의 GripBone 뼈 월드 기준 상대로 환산). HasGripFrame()이 false면 항등. */
	FTransform GetGripTransform() const;
	/** BoneRel(뼈 상대)을 그립 값으로 세팅 — GetGripTransform의 역연산(BodyComp를 BoneRel*뼈월드 로 SetWorldTransform).
	 *  바디가 이제 팔의 소켓/뼈에 실제로 attach돼 있으므로(B1) 이 호출은 그 attach 관계의 상대 트랜스폼을 갱신하는
	 *  것과 같다 — 파츠는 바디의 자식이라 자동으로 따라온다. HasGripFrame()이 false면 no-op. */
	void SetGripTransform(const FTransform& BoneRel);
	/** 그립의 현재 기준 뼈. 부착 소켓이 해석되면 그 소켓의 소유 뼈로 자동 갱신되고(AttachAssemblyToHand), 소켓이 없을
	 *  때는 이 값이 붙일 곳을 직접 지정하는 대체 앵커로 쓰인다(P5: 아직 소켓이 없는 새 무기도 뼈를 골라 그립을 잡고
	 *  베이크해 소켓을 처음 만들 수 있게). */
	FName GetGripBone() const { return GripBone; }
	/** 기준 뼈를 바꾸고 그 뼈로 재부착한다 — 단, **부착 소켓이 실제로 해석되는 동안은 무시된다**(AttachAssemblyToHand의
	 *  우선순위: 해석된 소켓이 있으면 항상 그 소켓의 뼈로 GripBone을 되돌린다 — 인가된 소켓을 뼈 선택으로 임의로
	 *  비껴갈 수는 없다). 소켓이 없을 때만 실질적인 효과가 있다. 뼈가 바뀌면 기준 프레임이 바뀌므로, 재부착 전
	 *  월드 트랜스폼을 떠 두고 재부착 후 되돌려 **그립 값(현재 보이는 무기 위치)은 유지**한다. */
	void SetGripBone(FName BoneName);

	// --- 부착 소켓 이름 (P2 해결) ------------------------------------------------------------------------------------

	/** 이 무기가 붙을 소켓 이름 — DA 값이 있으면 그것, 없으면 캐릭터 기본(AFPSRCharacter::ResolveWeaponAttachSocket과
	 *  동일 규칙, CDO로 조회). 실제로 ArmsComp에 이 소켓이 있는지는 보장하지 않는다(그건 AttachAssemblyToHand가
	 *  DoesSocketExist로 따로 확인) — 이건 순수하게 "DA가 뭐라고 말하는가"만 답한다. */
	FName GetResolvedAttachSocketName() const;
	/** DA의 WeaponAttachSocket이 비어 있어 캐릭터 공용 기본(SOCKET_Weapon)으로 폴백 중이면 true. UI가 "이걸 구우면
	 *  이 무기 하나가 아니라 공용 기본 소켓이 바뀌어 무기 여러 종이 같이 움직인다"고 경고하는 데 쓴다. */
	bool IsUsingSharedDefaultSocket() const;
	/** DA->WeaponAttachSocket에 NewName을 기록하고(+MarkPackageDirty) 새 이름으로 재부착한다. 인메모리 편집만 —
	 *  DA 애셋 저장은 여느 편집처럼 "조립→저장"이 담당. */
	void SetWeaponAttachSocketName(FName NewName);

	/** 🚨 "손 위치 저장"(BakeWeaponSocket) 호출부는 **굽기 직전 GetGripTransform() 을 떠 뒀다가, 성공 후 그 값으로
	 *  SetGripTransform 을 다시 걸어야 한다** — SFPSRWeaponAssemblerTab::OnBakeHandClicked 참조. 베이크가 무기를
	 *  움직이면 안 된다는 불변식이고, 안 지키면 저장을 누를 때마다 값이 누적된다(바디가 지금 굽는 그 소켓의 자식이라
	 *  소켓이 움직이면 같이 끌려가고, 다음 베이크가 그 끌려간 자리를 다시 굽는다). */

	// --- 1인칭 뷰 ---------------------------------------------------------------------------------------------------

	/** "1인칭 뷰" 토글. 켜면 프리뷰 카메라를 **플레이어 눈 위치**에 놓고 FOV 를 맞춘 뒤 카메라 조작을 잠근다 —
	 *  자유 시점으로는 "플레이어 눈에 총이 어떻게 걸리는가"를 판정할 수 없어서다(사용자 요청).
	 *
	 *  구도는 상수가 아니라 설정의 PreviewCharacterClass 를 프리뷰 씬에 **잠깐 스폰해서** 읽는다
	 *  (AFPSRCharacter::GetFirstPersonViewSetup → 카메라↔팔 상대 + FOV) 후 곧바로 파괴한다. 부착 위상을 가정하지
	 *  않으므로 BP 배선이 바뀌어도 따라간다. 읽기에 실패하면 켜지지 않고 사유를 GetArmsStatusMessage() 에 남긴다.
	 *
	 *  🚨 카메라를 잠그는 이유: 안 잠그면 드래그하는 순간 구도가 무너져 판정이 다시 불가능해진다. */
	void SetFirstPersonView(bool bIn);
	bool IsFirstPersonView() const { return bFirstPersonView; }
	/** 1인칭 뷰를 켤 수 있는 상태인가(설정에 PreviewCharacterClass 가 있고 팔이 서 있는가). UI 활성화 판정용. */
	bool CanUseFirstPersonView() const;

	const TArray<UStaticMeshComponent*>& GetPartComps() const { return PartComps; }
	USkeletalMeshComponent* GetBodyComp() const { return BodyComp; }
	/** 팔 프리뷰 컴포넌트. 실제 타입은 UDebugSkelMeshComponent(아래 멤버 주석 참조)지만 호출부가 필요로 하는 건
	 *  스켈레탈 메시 컴포넌트로서의 면(소켓/뼈/메시)뿐이라 베이스로 돌려준다 — 정의는 .cpp(업캐스트에 완전 타입 필요). */
	USkeletalMeshComponent* GetArmsComp() const;
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
	/** 1인칭 뷰일 때 화면 중앙에 **조준 축 기준선**을 그린다(십자 + 중심 점).
	 *  🚨 게임의 크로스헤어 위젯/UI 머티리얼을 렌더타깃에 그리지 않는다 — 이 프로젝트는 UI 머티리얼을 렌더타깃에
	 *  반복 렌더하다 GPU 가 물린 전례가 있다. 그립 판정에 필요한 정보는 "화면 중앙 = 조준 축"뿐이라 캔버스 선분
	 *  몇 개로 충분하고, 실제 크로스헤어가 아니라 기준선임을 라벨로 명시한다. */
	virtual void DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) override;

private:
	/** 바디를 팔의 무기 부착 소켓/뼈에 **실제로 attach** 한다(P3 해결 — 예전엔 한 번 스냅만 하고 그 뒤 월드에 떠
	 *  있었다). 파츠는 이제 바디의 진짜 자식(B2)이라 따로 손대지 않아도 자동으로 따라온다.
	 *
	 *  부착 이름 결정 우선순위(런타임 AttachWeaponMeshes와 같은 규칙을 CDO로 조회):
	 *   1. AFPSRCharacter::ResolveWeaponAttachSocket(WeaponDA)가 돌려준 소켓이 ArmsComp에 실제로 있으면 그 이름으로
	 *      붙이고, GripBone을 그 소켓의 소유 뼈로 갱신한다.
	 *   2. 없으면 현재 GripBone(사용자가 SetGripBone으로 고른 뼈, 또는 이전 해석의 잔재)으로 붙인다 — 단
	 *      ArmsComp의 스켈레톤에 실제로 있는 뼈일 때만.
	 *   3. 🚨 **둘 다 없으면 절대 붙이지 않는다**(DetachAssemblyFromHand로 원점 복귀) — 엔진 사실: AttachSocketName
	 *      이 NAME_None인 자식은 애니 갱신에서 건너뛰어져 손을 안 따라간다(SceneComponent.cpp:2912). 사유는
	 *      ArmsAttachIssue에 남겨 GetArmsStatusMessage()로 노출한다.
	 *
	 *  🚨 바디 스케일을 무기 DA 의 WeaponAttachScale 로 맞춘다(SetRelativeScale3D — 소켓 자체엔 스케일을 굽지 않는다,
	 *     불변식 I-B). 런타임이 그렇게 붙이기 때문(FPSRCharacter::AttachWeaponMeshes)이고, 프리뷰가 1.0 이면 화면에서
	 *     총이 인게임보다 커 보여 그립 판정이 어긋난다. 파츠는 바디의 자식이라 스케일이 자동으로 상속된다(B2).
	 *
	 *  팔/바디/DA 중 하나라도 없으면 DetachAssemblyFromHand로 정리하고 끝낸다. */
	void AttachAssemblyToHand();

	/** 바디를 팔에서 detach 하고 원래 자리(바디=항등, 스케일도 1)로 되돌린다. AttachAssemblyToHand 의 역. 파츠는
	 *  바디의 자식이라(B2) 따로 손대지 않아도 같이 원점으로 돌아온다. 이미 붙어 있지 않으면 트랜스폼만 원점으로
	 *  맞추고 끝(안전하게 반복 호출 가능). */
	void DetachAssemblyFromHand();

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

	/** Preview body (SkeletalMeshComponent). Added to the preview scene at identity with no attach parent; when
	 *  "팔 보기" 가 켜지면 AttachAssemblyToHand 가 이걸 ArmsComp 의 부착 소켓/뼈에 실제로 attach 한다(B1) — 그래서
	 *  이 컴포넌트의 부모는 상태에 따라 null(팔 꺼짐)이거나 ArmsComp(팔 켜짐, 그립 중)다. */
	USkeletalMeshComponent* BodyComp = nullptr;

	/** 1인칭 팔 프리뷰. 씬에 등록되므로 BodyComp/PartComps 와 같은 이유로 별도 GC 항목이 필요 없다(클래스 주석 참조).
	 *  null = "팔 보기" 꺼짐.
	 *
	 *  🚨 평범한 USkeletalMeshComponent 가 아니라 **UDebugSkelMeshComponent** 다 — 엔진의 애니메이션 에디터 프리뷰가
	 *  전부 쓰는 그 컴포넌트이고, 유일한 이유는 **애디티브 애니**다. 애디티브 클립(이 프로젝트의 `FP_Rifle_Reload` =
	 *  `AAT_LOCAL_SPACE_BASE`)은 절대 포즈가 아니라 "기준 포즈에 얹는 차분"이라, 기준 포즈를 깔지 않으면 스켈레톤
	 *  레퍼런스 포즈(A자) 위에 차분이 얹혀 팔이 뒤틀린 채로 조용히 재생된다(실측: 두 손 간격 26cm → 131cm).
	 *  기준 포즈를 까는 분기는 `FAnimSingleNodeInstanceProxy::Evaluate` 의 `bCanProcessAdditiveAnimations` 로 갈리는데
	 *  (`AnimSingleNodeInstanceProxy.cpp:331-350` 의 `GetAdditiveBasePose`), 그 플래그가
	 *  `FAnimSingleNodeInstanceProxy` 생성자에선 **false**(`AnimSingleNodeInstanceProxy.h:55,79`),
	 *  `FAnimPreviewInstanceProxy` 생성자에선 **true**(`AnimPreviewInstance.h:39,48`)다. 즉 애디티브를 제대로 보려면
	 *  `UAnimPreviewInstance` 를 다는 이 컴포넌트라야 한다(RefreshArmsFromSettings 의 EnablePreview 참조).
	 *  `UAnimPreviewInstance : UAnimSingleNodeInstance` 이므로 GetSingleNodeInstance() 기반 재생/스크럽 래퍼는 그대로 쓴다. */
	UDebugSkelMeshComponent* ArmsComp = nullptr;

	/** "팔 보기" 토글 상태. See SetShowArms. */
	bool bShowArms = false;

	/** "1인칭 뷰" 토글 상태. See SetFirstPersonView. */
	bool bFirstPersonView = false;

	/** 1인칭 뷰가 유지해야 할 카메라(월드) + FOV. Tick 이 매 프레임 이 값으로 되돌린다 — 아래 이유 참조. */
	FTransform FirstPersonCameraWorld = FTransform::Identity;
	float FirstPersonFOV = 90.0f;

	/** 1인칭 뷰를 못 켠 사유(설정 비어 있음/스폰 실패/컴포넌트 없음). GetArmsStatusMessage() 로 노출된다. */
	FString FirstPersonIssue;

	/** 저장해 둔 1인칭 카메라/FOV 를 지금 뷰포트에 건다. Tick 이 매 프레임 부른다 — 구현부 주석에 이유. */
	void ApplyFirstPersonCamera();

	/** 1인칭 뷰를 켜기 직전의 카메라(위치/회전/FOV) — 끄면 그대로 되돌려 준다. 자유 시점에서 잡아 둔 각도를
	 *  1인칭 한 번 봤다고 잃어버리면 저작 흐름이 끊긴다. */
	FVector SavedViewLocation = FVector::ZeroVector;
	FRotator SavedViewRotation = FRotator::ZeroRotator;
	float SavedViewFOV = 90.0f;

	/** 설정의 PreviewCharacterClass 를 프리뷰 씬에 잠깐 스폰해 1인칭 구도를 읽는다(스폰 → 읽기 → 파괴).
	 *  성공하면 true 이고 출력값이 채워진다. 실패 사유는 OutIssue 에 한국어 한 줄로 남긴다. */
	bool ReadFirstPersonSetupFromCharacter(FTransform& OutCameraRelativeToArms, float& OutFOV, FString& OutIssue) const;

	/** 그립의 현재 기준 뼈 — AttachAssemblyToHand/SetGripBone 참조. NAME_None = 아직 해석된 적 없음(팔이 꺼져 있거나,
	 *  부착 기준을 못 찾음). */
	FName GripBone = NAME_None;

	/** 디자이너가 뼈 피커로 GripBone 을 **직접 고른** 상태인가. 이게 없으면 뼈 피커가 무동작이 된다: 소켓이 이미
	 *  있는 무기(대부분)에서는 AttachAssemblyToHand 가 매번 GripBone 을 그 소켓의 뼈로 되돌리므로, 사용자가 고른
	 *  값이 곧바로 지워지고 BakeWeaponSocket 의 뼈 재배치(bMovedBone) 경로에 영원히 도달하지 못한다.
	 *
	 *  그래서 "고른 적 있음" 을 따로 기억한다. 켜져 있으면 부착도 소켓이 아니라 그 뼈로 하는데, 그래야 프리뷰가
	 *  **구워질 프레임 그대로** 보인다(그립 숫자는 뼈 상대값이라 기준 뼈가 바뀌면 값도 달라진다 — 화면과 저장될
	 *  값이 갈리면 안 된다).
	 *
	 *  로드성 이벤트(무기 교체·팔 메시 교체·팔 끄기)에서는 해제한다 — 그때는 애셋에 적힌 소켓이 진실이다. */
	bool bGripBoneUserSet = false;

	/** 팔 부착 프레임(소켓/뼈)을 못 찾았을 때의 사유 — AttachAssemblyToHand 가 채우고(실패 시) 비운다(성공 시). */
	FString ArmsAttachIssue;

	/** 프리뷰 포즈가 팔 스켈레톤과 비호환일 때의 사유 — RefreshArmsFromSettings 가 채우고/비운다. ArmsAttachIssue 와
	 *  별도로 유지하는 이유: 부착은 멀쩡한데 포즈만 안 맞는 경우(또는 그 반대)가 서로를 덮어쓰면 안 되기 때문. */
	FString ArmsPoseIssue;

	/** GetArmsStatusMessage() 가 참조로 반환할 결합 문자열의 저장소 — 매 호출마다 위 두 사유를 재조합해 채운다
	 *  (const 게터에서 갱신해야 해서 mutable). */
	mutable FString CachedArmsStatusMessage;

	/** Index-aligned to WeaponDA->WeaponParts: one StaticMeshComponent per part, attached as a real child of BodyComp
	 *  (B2 — "modular parts ride the weapon mesh", 런타임과 같은 관용구) at a Body-relative transform. Moving BodyComp
	 *  (원점↔손) 이나 그 컴포넌트 트리 전체를 매 프레임 다시 계산할 필요 없이 자동으로 따라온다. */
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
	/** 미리보기 시작 시 캡처한 base 컴포넌트의 **바디 상대** 트랜스폼(PC->GetRelativeTransform(), B6) — stage.Offset은
	 *  이 프레임 기준 상대값이다. 🚨 월드 트랜스폼이 아니라 바디 상대로 캡처한다: 이제 바디가 팔에 붙어 움직이므로
	 *  (B1), Begin~End 사이에 팔 애니가 스크럽되는 등으로 바디가 움직이면 월드 스냅샷은 그 순간 낡아버린다 — 바디
	 *  상대는 바디가 움직여도 유효하다(파츠가 바디의 자식이라 자동으로 따라오는 것과 같은 이유). */
	FTransform PreviewStageBaseXf = FTransform::Identity;
	/** 미리보기 시작 시 슬롯 컴포넌트에 물려 있던 base 메시(복원용). */
	TObjectPtr<UStaticMesh> PreviewStageBaseMesh = nullptr;
};
