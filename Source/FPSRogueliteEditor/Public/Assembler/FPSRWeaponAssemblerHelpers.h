// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UFPSRWeaponDataAsset;
class AFPSRWeaponAssemblerActor;

/** Free-function helpers backing the Weapon Part Assembler editor tool (Tools > FPSR menu entries). No lifetime to
 *  manage — plain statics, callable straight from the module's menu-entry handlers. */
namespace FPSRWeaponAssemblerHelpers
{
	/** Content-browser에서 선택된 첫 UFPSRWeaponDataAsset (없으면 nullptr). */
	UFPSRWeaponDataAsset* GetSelectedWeaponDA();

	/** DA로 프리뷰 액터를 에디터 월드에 스폰+빌드 (기존 프리뷰가 있으면 먼저 제거). 스폰된 액터 반환. */
	AFPSRWeaponAssemblerActor* SpawnPreview(UFPSRWeaponDataAsset* DA);

	/** 에디터 월드의 첫 프리뷰 액터 (없으면 nullptr). */
	AFPSRWeaponAssemblerActor* FindPreview();

	/** 프리뷰의 각 파츠 컴포넌트 배치를 바디 소켓으로 캡처: 파츠별 소켓 생성/갱신 + DA Socket 배선 + Offset=Identity.
	 *  바디 메시·DA 저장. 생성/갱신한 소켓 수 반환. */
	int32 CaptureToSockets(AFPSRWeaponAssemblerActor* Preview);
}
