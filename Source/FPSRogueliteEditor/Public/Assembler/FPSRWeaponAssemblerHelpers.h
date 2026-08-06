// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"
#include "Camera/CameraTypes.h"   // FMinimalViewInfo — 1인칭 구도는 FOV 하나가 아니라 뷰인포 통째로 다룬다(FFPSRFirstPersonSetup)

class UFPSRWeaponDataAsset;
class UWorld;
class USkeletalMesh;
class USkeletalMeshComponent;
class USkeletalMeshSocket;
class UStaticMesh;
class UStaticMeshComponent;

/** Free-function helpers backing the Weapon Part Assembler editor tool (Tools > FPSR > "무기 파츠 조립기…" nomad tab,
 *  see SFPSRWeaponAssemblerTab). Pure math/IO, no lifetime to manage — the tab widget and its viewport client own
 *  all the UObject/preview-scene state; these are plain statics shared by both. */
namespace FPSRWeaponAssemblerHelpers
{
	/** 변종번호 제거한 대표 파츠명 (SM_Wep_Mod_A_Barrel_01 -> Barrel). Part가 null이면 Part<Index>. 프리뷰 뷰포트의
	 *  파츠 컴포넌트 이름(내부 식별용)에만 쓰인다 — 소켓명(BakeSockets)은 이제 이 이름과 무관한 안정 id다. */
	FString MakePartDisplayName(const TSoftObjectPtr<UStaticMesh>& Part, int32 Index);

	/** 스켈레탈 메시 루트본(bone0 ref pose)의 컴포넌트-공간 트랜스폼. 이 팩의 무기 바디 루트본은 90° roll을 가지므로,
	 *  본-상대 소켓을 조립 프리뷰에 배치하거나 반대로 프리뷰 배치를 소켓으로 구울 때 반드시 이 트랜스폼을 거쳐야
	 *  런타임과 동일한 위치가 나온다. */
	FTransform RootBoneComponentSpace(const USkeletalMesh* Mesh);

	/** 프리뷰에서 잡아 둔 파츠 배치를 DA에 굽는다. 각 PartComp의 (BodyComp 기준) 상대 트랜스폼을 쓰므로, 바디가
	 *  identity가 아니어도(예: "전체 이동"으로 조립품을 옮겼거나, "팔 보기" 중이라 손에 매달려 스케일까지 걸린
	 *  상태여도) 결과가 정합한다.
	 *
	 *  🚨 **무엇에 굽는가**: 소켓이 아니라 **DA의 WeaponParts[i].Offset**이다. 런타임이 소켓을 "고정 마운트"로
	 *  다루기 때문이다 — `FPSRWeaponPartSelector.cpp:91` 이 진화 단계가 바뀌어도 Socket은 그대로 두고 Offset만
	 *  갈아끼우고, `FPSRCharacter.cpp:2015` 가 소켓에 붙인 뒤 Offset을 상대 트랜스폼으로 적용한다.
	 *  🚨 그래서 **이미 있는 소켓은 절대 건드리지 않는다.** 이 팩은 무기 7종이 바디 메시 하나를 공유하므로, 소켓을
	 *  고치는 것은 곧 다른 무기 6종의 마운트를 덮어쓰는 것이다(실사고: SMG 베이크가 Rifle 핸드가드를 10.47cm,
	 *  개머리판을 10.58cm 밀어냈다 — 변종 메시 크기가 달라 마운트가 달라야 했기 때문). 무기별 차이는 전부 Offset이
	 *  흡수한다.
	 *
	 *  소켓을 새로 만드는 경우는 그 슬롯에 마운트가 아직 없을 때뿐이며(새 파츠 등), 그때는 다른 무기와 겹치지 않도록
	 *  SOCKET_Mount_<GUID> 로 발급하고 Offset은 항등으로 둔다. 소켓 id는 메시/컴포넌트명과 무관한 안정값이라 메시를
	 *  교체해도 리네임되지 않는다(사용자 표시명은 DisplayLabel이 담당).
	 *
	 *  저장: DA는 항상, **바디 메시는 새 소켓을 만들었을 때만**(공유 애셋이라 불필요하게 더럽히지 않는다).
	 *  처리한 슬롯 수를 반환한다(DA/BodyComp/메시 없으면 0). */
	int32 BakeSockets(UFPSRWeaponDataAsset* DA, USkeletalMeshComponent* BodyComp, const TArray<UStaticMeshComponent*>& PartComps);

	/** 조립품(바디+파츠, "팔 보기" 중이면 팔까지) 전체의 월드 바운드 밑면을 프리뷰 바닥에 맞추기 위한
	 *  FAdvancedPreviewScene::SetFloorOffset 인자. 무기가 원점에 절반 파묻히지 않고 바닥에 얹혀 보이게 한다(엔진
	 *  SStaticMeshEditorViewport/SMaterialEditorViewport와 동일 관용구: -Origin.Z + Extent.Z → 바닥이 조립품 밑면에
	 *  놓임). 순수 프리뷰 프레이밍용이며 소켓 베이크(바디 상대)와 무관하다. 유효 바운드가 없으면 0(바닥 원점 유지)을
	 *  반환한다.
	 *  @param ArmsComp  팔이 서 있을 때(= "팔 보기" 켜짐) 넘기면 바닥 계산에 팔까지 포함한다. 기본값 null =
	 *                   기존과 동일(무기만 기준) — 팔 프리뷰가 없던 시절의 호출부를 깨지 않기 위함. */
	float ComputeFloorOffsetToRest(const USkeletalMeshComponent* BodyComp, const TArray<UStaticMeshComponent*>& PartComps,
	                               const USkeletalMeshComponent* ArmsComp = nullptr);

	/** 소켓 '이름'이 실제로 어느 애셋에 사는지 찾는다(메시 자체 소켓인지, 폴백인 스켈레톤 소켓인지).
	 *  USkeletalMesh::FindSocketAndIndex(엔진 SkeletalMesh.cpp:4993)가 **메시 -> 스켈레톤 순으로** 찾는 것과 같은
	 *  우선순위를 따른다 — 이 팩처럼 소켓이 스켈레톤에만 있는 경우(P1)를 놓치지 않기 위해서다.
	 *  반환 = 소켓(없으면 null). OutOwner = 그 소켓을 고치고 나서 Modify()/MarkPackageDirty()/저장을 걸어야 할
	 *  객체(USkeletalMesh* 또는 USkeleton*) — 소켓을 못 찾으면 null. */
	USkeletalMeshSocket* FindWeaponSocket(USkeletalMesh* Mesh, FName SocketName, UObject*& OutOwner);

	/** "손 위치 저장"(BakeWeaponSocket) 결과 — "소켓을 만들었다"가 아니라 "어느 패키지가 실제로 저장됐다"를
	 *  보고하기 위한 것(P1: 예전엔 스켈레톤 소켓을 고쳐 놓고 메시 패키지만 저장해서, 저장됐다는 보고와 달리 디스크엔
	 *  아무것도 안 남았다). */
	struct FBakeHandResult
	{
		/** 값 반영 + 저장까지 전부 성공. false 면 Message 를 봐라(입력 오류/저장 실패 등 원인이 갈린다). */
		bool bOk = false;
		FName SocketName;
		/** 실제로 기준이 된 뼈(호출 시 넘긴 BoneName 이 None 이면 기존 소켓의 뼈로 채워진다). */
		FName BoneName;
		/** 소켓이 없어서 스켈레톤에 새로 만들었나(P5 해결). */
		bool bCreatedSocket = false;
		/** 기존 소켓이 있었고, 그 소켓의 뼈를 BoneName 으로 옮겼나. */
		bool bMovedBone = false;
		/** 실제로 저장에 성공한 패키지 이름(SavePackages 반환값으로 확인한 것만 — "만들었다"가 아니라 "저장됐다"). */
		TArray<FString> SavedPackages;
		/** 사용자 표시용 한국어 한 줄. 어느 애셋(팔 메시/팔 스켈레톤)에 저장됐는지를 반드시 담는다. */
		FString Message;
	};

	/** "손 위치 저장" — 프리뷰에서 손에 얹어 둔 조립품의 위치를 팔의 무기 부착 소켓으로 굽고 저장한다. BakeSockets 가
	 *  파츠->바디를 굽는 것과 대칭이고, 굽는 **대상 메시가 다르다**(무기 바디가 아니라 팔).
	 *
	 *  - 소켓이 이미 있으면: FindWeaponSocket 으로 찾은 **실제 주인**(메시 or 스켈레톤 — 이 팩은 P1 때문에 스켈레톤)
	 *    패키지를 Modify()/MarkPackageDirty()/저장한다. 소켓 객체 자신도 Modify() 해서 실행취소가 필드 변경을
	 *    잡게 한다(예전엔 이걸 안 해서 재베이크가 실행취소에 안 잡혔다).
	 *  - 소켓이 없으면(P5 해결): BoneName 에 **스켈레톤 소켓**을 새로 만든다 — 엔진 정식 경로
	 *    FEditableSkeleton::HandleAddSocket(EditableSkeleton.cpp:542) 그대로. 🚨 bAddToSkeleton=true 인자로
	 *    USkeletalMesh::AddSocket 을 쓰지 않는다 — 메시와 스켈레톤 양쪽에 만들어 버려 FindSocketAndIndex(메시 우선)가
	 *    메시 사본에 가려 스켈레톤 사본이 영원히 안 보이게 된다. BoneName 이 None 이거나 팔 스켈레톤의 레퍼런스
	 *    스켈레톤에 없는 뼈면 만들지 않고 실패(Message 에 사유).
	 *  - BoneName 이 기존 소켓의 뼈와 다르면 그 소켓을 옮기고 bMovedBone=true.
	 *  - 기준 프레임 = ArmsComp 에서 (최종적으로 정해진) 대상 뼈의 현재 **포즈된** 월드 트랜스폼(GetSocketTransform
	 *    은 뼈 이름도 푼다). 소켓 자신을 기준으로 재면 지금 값이 상쇄돼 항상 항등이 나온다 — 그래서 팔이 소총 자세로
	 *    서 있는 상태에서 불러야 인게임과 같은 값이 나온다.
	 *
	 *  🚨 스케일은 굽지 않는다(항상 1). 런타임은 `SnapToTargetNotIncludingScale` 로 붙인 뒤
	 *     `SetRelativeScale3D(WeaponAttachScale)` 를 따로 건다(FPSRCharacter::AttachWeaponMeshes). 소켓에 스케일까지
	 *     넣으면 **두 번 곱해져** 총이 작아진다.
	 *
	 *  SavePackages 의 반환값을 실제로 확인해서 SavedPackages/bOk 를 채운다 — "소켓 만들었다"가 아니라 "무엇이
	 *  저장됐다"가 핵심(불변식 I-E). */
	FBakeHandResult BakeWeaponSocket(USkeletalMeshComponent* ArmsComp, const USkeletalMeshComponent* BodyComp,
	                                 FName SocketName, FName BoneName);

	// --- 1인칭 구도 -----------------------------------------------------------------------------------------------

	/** 플레이어 캐릭터에서 읽어온 1인칭 구도. "카메라가 팔에 대해 어디에 있는가" + 그 카메라의 뷰인포 전부. */
	struct FFPSRFirstPersonSetup
	{
		/** AFPSRCharacter::GetFirstPersonViewSetup 이 준 값. 프리뷰 씬의 팔 월드에 곱하면 카메라를 놓을 자리가 된다.
		 *  🚩 부호 주의: 이건 **카메라 기준이 아니라 팔 기준**이다(= BP 의 팔 상대 트랜스폼의 역행렬). BP 에 적힌
		 *     팔 값 (-21, 10, -165)/yaw −95 를 그대로 기대하면 안 된다 — 그 역인 (8.13, 21.79, 165)/yaw +95 가 맞다. */
		FTransform CameraRelativeToArms = FTransform::Identity;

		/** 캐릭터 카메라의 뷰인포(FOV·AspectRatio·축 제약·1인칭 파라미터). 게임과 같은 호출로 채워진다. */
		FMinimalViewInfo View;
	};

	/** 설정(UFPSRWeaponAssemblerSettings::PreviewCharacterClass)의 캐릭터를 World 에 **잠깐 스폰해서** 1인칭 구도를
	 *  읽고 곧바로 파괴한다. 성공하면 true. 실패 사유는 OutIssue 에 한국어 한 줄.
	 *
	 *  🚨 CDO 가 아니라 인스턴스라야 컴포넌트 월드 트랜스폼이 의미를 갖는다(엔진도 FBlueprintEditor 가 프리뷰 씬에
	 *  BP 를 스폰한다). World 는 **프리뷰 씬의 월드**를 넘길 것 — 에디터 레벨에 스폰하면 레벨이 더러워진다. */
	bool ReadFirstPersonSetup(UWorld* World, FFPSRFirstPersonSetup& OutSetup, FString& OutIssue);

	/** 목표 종횡비에서 **게임과 같은 구도**가 나오도록 가로 FOV 를 다시 구한다.
	 *
	 *  🚨 종횡비만 바꾸면 틀린다. 에디터 뷰포트에서 종횡비를 고정하는 유일한 경로(bConstrainAspectRatio)는 켜지는
	 *  순간 축 제약을 **무시하고** FOV+AspectRatio 로 직접 투영을 만든다(CameraStackTypes.cpp:263). 반면 게임은
	 *  축 제약(기본 MaintainYFOV — BaseEngine.ini `[/Script/Engine.LocalPlayer]`)에 따라 **세로 FOV 를 유지**하며
	 *  가로를 넓힌다. 그래서 세로를 고정한 채 가로를 다시 계산해야 21:9 플레이어가 실제로 보는 화면이 나온다
	 *  (예: 세로 58.72° 고정 → 21:9 의 가로는 90 이 아니라 105.4).
	 *
	 *  게임이 가로를 고정하는 설정(MaintainXFOV, 또는 가로가 긴 화면에서의 MajorAxisFOV)이면 재계산 없이 원본
	 *  FOV 를 그대로 돌려준다 — 그때는 그게 게임의 동작이기 때문이다(CameraStackTypes.cpp:287).
	 *
	 *  @param View          캐릭터 카메라 뷰인포(원본 FOV·원본 종횡비·카메라가 축 제약을 덮었는지)
	 *  @param TargetAspect  프리셋 종횡비
	 *  @return 그 종횡비에서 쓸 가로 FOV(도). 16:9 원본을 16:9 로 맞추면 정확히 원본 값이 나온다(자기검증). */
	float DeriveFOVForAspect(const FMinimalViewInfo& View, float TargetAspect);

	/** 가로 FOV + 종횡비 → 세로 FOV(도). 화면에 "지금 세로로 몇 도를 보고 있는가"를 숫자로 띄우는 용도. */
	float ComputeVerticalFOV(float HorizontalFOVDegrees, float Aspect);

	/** 프리뷰 월드를 **프레임당 한 번만** 틱한다. 이미 이 프레임에 누가 틱했으면 아무것도 안 하고 false.
	 *
	 *  🚨 왜 필요한가: 조립기 탭과 1인칭 탭은 **같은 프리뷰 씬을 공유**하고, 뷰포트 클라이언트는 각자 Tick 에서
	 *  월드를 돌린다(FAdvancedPreviewScene::Tick 은 캡처/라이팅만 갱신하지 월드를 안 돌린다). 두 탭을 나란히
	 *  띄우는 게 이 기능의 정상 사용법인데, 그러면 한 프레임에 월드가 두 번 돌아 **팔 애니가 2배속**이 된다.
	 *  그렇다고 한쪽만 틱하게 하면 그 탭이 가려지거나 닫혔을 때 다른 쪽 애니가 멈춘다 — 그래서 "누구든 먼저,
	 *  단 한 번" 규칙으로 둔다. */
	bool TickPreviewWorldOnce(UWorld* World, float DeltaSeconds);
}
