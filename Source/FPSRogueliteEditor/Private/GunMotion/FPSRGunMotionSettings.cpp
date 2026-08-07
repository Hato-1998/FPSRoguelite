// Copyright Epic Games, Inc. All Rights Reserved.

#include "GunMotion/FPSRGunMotionSettings.h"

UFPSRGunMotionSettings::UFPSRGunMotionSettings()
{
	// Config 프로퍼티의 "처음 한 번의 시드" — Config/DefaultEditor.ini 에 값이 이미 있으면 그쪽이 이긴다
	// (UFPSRWeaponAssemblerSettings::AspectPresets 와 같은 관례, GunMotionTool_Spec.md §1).
	TargetCharacterBP = TSoftObjectPtr<UBlueprint>(FSoftObjectPath(TEXT("/Game/Character/Player/BP_FPSRPlayer.BP_FPSRPlayer")));

	RightChainBones = {
		TEXT("clavicle_r"),
		TEXT("upperarm_r"),
		TEXT("lowerarm_r"),
		TEXT("hand_r"),
		TEXT("upperarm_twist_01_r"),
		TEXT("lowerarm_twist_01_r")
	};

	// 증보 v2 §7 기본값.
	PreviewWeaponMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
		TEXT("/Game/PolygonMilitary/Meshes/Weapons/Modular/Weapon_A/SM_Wep_Mod_A_Body_01.SM_Wep_Mod_A_Body_01")));
}
