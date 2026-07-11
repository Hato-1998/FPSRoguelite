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
	/** 변종번호 제거한 대표 파츠명 (SM_Wep_Mod_A_Barrel_01 -> Barrel). Part가 null이면 Part<Index>. 이 이름이 곧
	 *  BakeSockets가 굽는 소켓명(SOCKET_Mount_<이름>)이 되므로, 디자이너가 프리뷰 파츠 컴포넌트를 리네임해 슬롯을
	 *  제어할 수 있다(예: 배럴 변형들을 같은 소켓으로 합치기). */
	FString MakePartDisplayName(const TSoftObjectPtr<UStaticMesh>& Part, int32 Index);

	/** 스켈레탈 메시 루트본(bone0 ref pose)의 컴포넌트-공간 트랜스폼. 이 팩의 무기 바디 루트본은 90° roll을 가지므로,
	 *  본-상대 소켓을 조립 프리뷰에 배치하거나 반대로 프리뷰 배치를 소켓으로 구울 때 반드시 이 트랜스폼을 거쳐야
	 *  런타임과 동일한 위치가 나온다. */
	FTransform RootBoneComponentSpace(const USkeletalMesh* Mesh);

	/** PartComps[i]의 (BodyComp 대비) 상대 트랜스폼을 바디 메시 소켓으로 굽고, DA의 WeaponParts1P[i].Socket/Offset을
	 *  배선한 뒤 Body/DA 둘 다 저장한다. 각 PartComp의 (BodyComp 기준) 상대 트랜스폼을 루트본(90° roll) 기준으로
	 *  변환해 소켓에 넣으므로, 바디가 identity가 아니어도(예: "전체 이동"으로 조립품을 통째로 옮겼어도) 결과가
	 *  정합한다. 소켓명은 PartComp->GetName()(변종번호 제거된 대표명)에서 유도한다. 이 툴이 소유하는 SOCKET_Mount_*
	 *  네임스페이스의 구 소켓은 굽기 전에 전부 제거해 재구울때마다 고아가 남지 않게 한다. 생성/갱신한 소켓 수를 반환
	 *  (DA/BodyComp/메시 없으면 0). */
	int32 BakeSockets(UFPSRWeaponDataAsset* DA, USkeletalMeshComponent* BodyComp, const TArray<UStaticMeshComponent*>& PartComps);

	/** 조립품(바디+파츠) 전체의 월드 바운드 밑면을 프리뷰 바닥에 맞추기 위한 FAdvancedPreviewScene::SetFloorOffset 인자.
	 *  무기가 원점에 절반 파묻히지 않고 바닥에 얹혀 보이게 한다(엔진 SStaticMeshEditorViewport/SMaterialEditorViewport와
	 *  동일 관용구: -Origin.Z + Extent.Z → 바닥이 조립품 밑면에 놓임). 순수 프리뷰 프레이밍용이며 소켓 베이크(바디 상대)와
	 *  무관하다. 유효 바운드가 없으면 0(바닥 원점 유지)을 반환한다. */
	float ComputeFloorOffsetToRest(const USkeletalMeshComponent* BodyComp, const TArray<UStaticMeshComponent*>& PartComps);
}
