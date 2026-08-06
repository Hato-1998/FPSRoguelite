// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "FPSRWeaponAssemblerSettings.generated.h"

/**
 * Weapon Part Assembler tool configuration (editor-only). Config = Editor + DefaultConfig so the value lives in
 * Config/DefaultEditor.ini — checked in, shared by designers, and changeable in Project Settings > FPSR with NO C++
 * rebuild. The default mirrors the current content layout; a later content reorg (e.g. U22 Synty) can repoint it
 * without touching C++. Editor module only — nothing here is read at runtime.
 */
UCLASS(Config = Editor, DefaultConfig, meta = (DisplayName = "FPSR Weapon Assembler"))
class UFPSRWeaponAssemblerSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Groups this under the "FPSR" section in Project Settings, alongside the other FPSR editor tools. */
	virtual FName GetCategoryName() const override { return FName(TEXT("FPSR")); }

	/** Name of the sibling folder (next to a weapon's own parts folder) that holds shared attachments — sights, grips,
	 *  muzzle devices, lasers, and a Scopes/ subfolder. The assembler's catalog scan looks in "<weaponFolder>/../<this>"
	 *  recursively. Default "Attachments" matches the current layout; change it here (no rebuild) if content is reorganised. */
	UPROPERTY(EditAnywhere, Config, Category = "Catalog")
	FString AttachmentsFolderName = TEXT("Attachments");

	/** First-person arms shown in the assembler's "팔 보기" mode, so a weapon's grip can be judged **in the hand**
	 *  instead of floating at the origin. Empty = the toggle stays off and the tool behaves exactly as before.
	 *  Soft ref + Config so swapping the arms mesh (this project has already changed it twice) needs no rebuild.
	 *  The arms mesh is also where the "손 위치 저장" bake starts from — but the socket it writes may land on THIS
	 *  mesh or on its Skeleton asset, whichever actually owns the weapon attach socket
	 *  (FPSRWeaponAssemblerHelpers::FindWeaponSocket resolves it; this pack's SOCKET_Weapon lives on the Skeleton). */
	UPROPERTY(EditAnywhere, Config, Category = "First Person Preview", meta = (AllowedClasses = "/Script/Engine.SkeletalMesh"))
	TSoftObjectPtr<USkeletalMesh> PreviewArmsMesh;

	/** Pose/animation played on the preview arms. Judging a grip against the REFERENCE pose is misleading — the arms
	 *  stand spread-eagled and the weapon lands nowhere near where it does in play. Empty = reference pose.
	 *
	 *  무기는 이제 팔의 무기 부착 소켓(또는 지정한 뼈)에 실제로 **attach** 되어 그 뼈를 계속 따라간다(런타임
	 *  AttachWeaponMeshes 와 동일한 부착 규칙 — AFPSRCharacter::ResolveWeaponAttachSocket 참조). 그래서 여기 넣은
	 *  애니가 정지 포즈일 필요가 없다 — 재장전처럼 움직이는 애니를 넣어도 총이 손을 그대로 따라간다. 툴에서
	 *  재생/스크럽하며 애니가 진행되는 동안 그립이 미끄러지지 않는지 눈으로 판정하는 것이 오히려 이 필드의 목적. */
	UPROPERTY(EditAnywhere, Config, Category = "First Person Preview", meta = (AllowedClasses = "/Script/Engine.AnimationAsset"))
	TSoftObjectPtr<UAnimationAsset> PreviewArmsPose;
};
