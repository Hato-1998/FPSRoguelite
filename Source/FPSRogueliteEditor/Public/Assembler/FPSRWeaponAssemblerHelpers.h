// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"

class UFPSRWeaponDataAsset;
class USkeletalMesh;
class USkeletalMeshComponent;
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

	/** PartComps[i]의 (BodyComp 대비) 상대 트랜스폼을 바디 메시 소켓으로 굽고, DA의 WeaponParts[i].Socket/Offset을
	 *  배선한 뒤 Body/DA 둘 다 저장한다. 각 PartComp의 (BodyComp 기준) 상대 트랜스폼을 루트본(90° roll) 기준으로
	 *  변환해 소켓에 넣으므로, 바디가 identity가 아니어도(예: "전체 이동"으로 조립품을 통째로 옮겼어도) 결과가
	 *  정합한다. 소켓명은 메시/컴포넌트명과 무관한 안정 id — 슬롯에 이미 구운 소켓이 있으면 재사용, 없으면
	 *  SOCKET_Mount_<GUID>로 새로 발급한다(메시를 교체해도 소켓이 리네임되지 않음; 사용자 표시명은 DisplayLabel이
	 *  담당). 이 툴이 소유하는 SOCKET_Mount_* 네임스페이스의 구 소켓은 굽기 전에 전부 제거해 재구울때마다 고아가
	 *  남지 않게 한다(단, 안정 id라 재베이크 시 같은 이름으로 재생성됨). 생성/갱신한 소켓 수를 반환
	 *  (DA/BodyComp/메시 없으면 0). */
	int32 BakeSockets(UFPSRWeaponDataAsset* DA, USkeletalMeshComponent* BodyComp, const TArray<UStaticMeshComponent*>& PartComps);

	/** 조립품(바디+파츠) 전체의 월드 바운드 밑면을 프리뷰 바닥에 맞추기 위한 FAdvancedPreviewScene::SetFloorOffset 인자.
	 *  무기가 원점에 절반 파묻히지 않고 바닥에 얹혀 보이게 한다(엔진 SStaticMeshEditorViewport/SMaterialEditorViewport와
	 *  동일 관용구: -Origin.Z + Extent.Z → 바닥이 조립품 밑면에 놓임). 순수 프리뷰 프레이밍용이며 소켓 베이크(바디 상대)와
	 *  무관하다. 유효 바운드가 없으면 0(바닥 원점 유지)을 반환한다. */
	float ComputeFloorOffsetToRest(const USkeletalMeshComponent* BodyComp, const TArray<UStaticMeshComponent*>& PartComps);

	/** "손 위치 저장" — 프리뷰에서 손에 얹어 둔 조립품의 위치를 **팔 메시의 무기 부착 소켓**에 굽고 저장한다.
	 *  BakeSockets 가 파츠->바디를 굽는 것과 대칭이고, 굽는 **대상 메시가 다르다**(무기 바디가 아니라 팔).
	 *
	 *  🚨 스케일은 굽지 않는다. 런타임은 `SnapToTargetNotIncludingScale` 로 붙인 뒤 `SetRelativeScale3D(WeaponAttachScale)`
	 *     를 따로 건다(FPSRCharacter::AttachWeaponMeshes). 소켓에 스케일까지 넣으면 **두 번 곱해져** 총이 0.72배로
	 *     작아진다. 그래서 위치·회전만 쓰고 소켓 스케일은 1로 둔다.
	 *
	 *  🪤 소켓이 없으면 **만들지 않고 실패**한다 — 어느 뼈에 달지는 추측할 수 없다(손이 여럿이고, 이 프로젝트의 팔
	 *     스켈레톤은 이미 두 번 갈렸다). 있으면 그 소켓의 BoneName 을 그대로 기준 프레임으로 쓴다.
	 *
	 *  성공 시 true. ArmsComp 의 현재 **포즈된** 뼈 트랜스폼을 기준으로 재므로, 팔이 소총 자세로 서 있는 상태에서
	 *  불러야 인게임과 같은 값이 나온다. */
	bool BakeWeaponSocket(USkeletalMeshComponent* ArmsComp, const USkeletalMeshComponent* BodyComp, FName SocketName, FString& OutMessage);
}
