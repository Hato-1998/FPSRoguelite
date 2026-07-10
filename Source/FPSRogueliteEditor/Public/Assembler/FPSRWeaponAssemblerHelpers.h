// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPtr.h"

class UFPSRWeaponDataAsset;
class USkeletalMesh;
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

	/** PartComps[i]의 (Body 대비) 상대 트랜스폼을 바디 메시 소켓으로 굽고, DA의 WeaponParts1P[i].Socket/Offset을
	 *  배선한 뒤 Body/DA 둘 다 저장한다. 조립기 프리뷰 씬에서는 Body가 identity로 배치되므로(부모 없음) 각
	 *  PartComp의 컴포넌트-공간(=world) 트랜스폼이 곧 Body-상대 트랜스폼이다. 소켓명은 PartComp->GetName()(변종번호
	 *  제거된 대표명)에서 유도한다. 이 툴이 소유하는 SOCKET_Mount_* 네임스페이스의 구 소켓은 굽기 전에 전부 제거해
	 *  재구울때마다 고아가 남지 않게 한다. 생성/갱신한 소켓 수를 반환(DA/Body 없으면 0). */
	int32 BakeSockets(UFPSRWeaponDataAsset* DA, USkeletalMesh* Body, const TArray<UStaticMeshComponent*>& PartComps);
}
