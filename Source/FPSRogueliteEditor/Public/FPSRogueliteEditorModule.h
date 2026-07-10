// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/**
 * Editor-only module for FPSRoguelite designer tooling. P0 shipped the data-validation seam (cross-asset
 * UEditorValidatorBase validators, the anchored-validation service, the headless validation commandlet, and the
 * Tools > FPSR > "Validate Anchored Data" menu entry). P1 adds the FPSR Data Editor — a Slate tool tab (Tools > FPSR
 * > "Data Editor...") for wiring/orphan-fixing and card per-rarity magnitude editing, reusing the P0 validation
 * service for discovery.
 */
class FFPSRogueliteEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	/** Registers the Tools > FPSR menu entries. Deferred via UToolMenus::RegisterStartupCallback (menus aren't
	 *  ready to register at module-load time). */
	void RegisterMenus();

	/** Menu command handler: runs the same anchors+reachable validation as the commandlet, in-editor, then opens
	 *  the AssetCheck message log so results are visible without digging through Output Log. */
	static void OnValidateAnchoredDataMenuEntry();

	/** Menu command handler (P1): opens (or focuses) the FPSR Data Editor nomad tab. */
	static void OnOpenDataEditorMenuEntry();

	/** Menu command handler: opens (or focuses) the Weapon Part Assembler nomad tab — a fully embedded-viewport
	 *  tool (weapon DA picker + own 3D preview + parts list + gizmo + bake-to-socket button). Nothing is ever
	 *  spawned into an editor level. */
	static void OnOpenWeaponAssemblerMenuEntry();

	/** Nomad tab spawner for the FPSR Data Editor (P1) — registered in StartupModule, unregistered in
	 *  ShutdownModule. Returns a dock tab hosting a single SFPSRDataEditorWidget. */
	static TSharedRef<class SDockTab> SpawnDataEditorTab(const class FSpawnTabArgs& Args);

	/** Nomad tab spawner for the Weapon Part Assembler — registered in StartupModule, unregistered in
	 *  ShutdownModule. Returns a dock tab hosting a single SFPSRWeaponAssemblerTab. */
	static TSharedRef<class SDockTab> SpawnWeaponAssemblerTab(const class FSpawnTabArgs& Args);

	/** Tab identifier for the FPSR Data Editor nomad tab (RegisterNomadTabSpawner / TryInvokeTab both key off this). */
	static const FName FPSRDataEditorTabName;

	/** Tab identifier for the Weapon Part Assembler nomad tab (RegisterNomadTabSpawner / TryInvokeTab both key off this). */
	static const FName FPSRWeaponAssemblerTabName;
};
