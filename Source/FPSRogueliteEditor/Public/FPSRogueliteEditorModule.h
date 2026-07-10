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

	/** Menu command handler: spawns a transient Weapon Part Assembler preview actor in the editor level from the
	 *  weapon DA selected in the Content Browser, so a designer can gizmo-move its modular parts. */
	static void OnSpawnWeaponAssembler();

	/** Menu command handler: bakes the current Weapon Part Assembler preview's part placements into body-mesh
	 *  sockets, wires the weapon DA's WeaponParts1P sockets to them, and saves both assets. */
	static void OnCaptureWeaponAssembler();

	/** Nomad tab spawner for the FPSR Data Editor (P1) — registered in StartupModule, unregistered in
	 *  ShutdownModule. Returns a dock tab hosting a single SFPSRDataEditorWidget. */
	static TSharedRef<class SDockTab> SpawnDataEditorTab(const class FSpawnTabArgs& Args);

	/** Tab identifier for the FPSR Data Editor nomad tab (RegisterNomadTabSpawner / TryInvokeTab both key off this). */
	static const FName FPSRDataEditorTabName;
};
