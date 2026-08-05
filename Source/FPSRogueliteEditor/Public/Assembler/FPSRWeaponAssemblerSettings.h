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
	 *  The arms mesh is also the BAKE TARGET for "손 위치 저장" — that writes the weapon attach socket onto THIS mesh. */
	UPROPERTY(EditAnywhere, Config, Category = "First Person Preview", meta = (AllowedClasses = "/Script/Engine.SkeletalMesh"))
	TSoftObjectPtr<USkeletalMesh> PreviewArmsMesh;

	/** Pose/animation played on the preview arms. Judging a grip against the REFERENCE pose is misleading — the arms
	 *  stand spread-eagled and the weapon lands nowhere near where it does in play. Empty = reference pose.
	 *
	 *  ⚠ **정지 포즈를 넣어라**(idle/ADS). 무기는 "팔 보기"를 켜는 시점과 무기를 바꾸는 시점에 **한 번** 손 소켓
	 *  자리로 옮겨지고, 그 뒤로는 디자이너가 기즈모로 잡은 위치를 지킨다. 움직이는 애니(재장전 등)를 넣으면 손만
	 *  움직이고 총은 그 자리에 남는다. 매 틱 따라가게 만들면 기즈모 편집이 매 틱 덮여서 그립을 잡을 수 없다 —
	 *  이 툴의 목적이 정지 포즈에서의 그립 배치라서 이렇게 갈랐다. */
	UPROPERTY(EditAnywhere, Config, Category = "First Person Preview", meta = (AllowedClasses = "/Script/Engine.AnimationAsset"))
	TSoftObjectPtr<UAnimationAsset> PreviewArmsPose;
};
