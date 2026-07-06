// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/**
 * Editor-only module for FPSRoguelite designer tooling (data validation seam, P0).
 * Holds cross-asset UEditorValidatorBase validators, the anchored-validation service, the
 * headless validation commandlet, and the Tools > FPSR menu entry point. No Slate editing UI (P0).
 */
class FFPSRogueliteEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Registers the Tools > FPSR > "Validate Anchored Data" menu entry. Deferred via
	 *  UToolMenus::RegisterStartupCallback (menus aren't ready to register at module-load time). */
	void RegisterMenus();

	/** Menu command handler: runs the same anchors+reachable validation as the commandlet, in-editor, then opens
	 *  the AssetCheck message log so results are visible without digging through Output Log. */
	static void OnValidateAnchoredDataMenuEntry();
};
