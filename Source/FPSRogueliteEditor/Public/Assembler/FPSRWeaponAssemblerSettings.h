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
};
