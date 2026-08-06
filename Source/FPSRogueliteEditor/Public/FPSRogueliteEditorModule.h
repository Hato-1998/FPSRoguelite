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

	/** 지금 열려 있는 무기 파츠 조립기 탭(없으면 만료된 약참조).
	 *
	 *  1인칭 뷰 탭이 조립기의 프리뷰 씬·팔을 찾는 통로다. 조립기 탭은 **노매드 탭**이라 사용자가 언제든 닫고
	 *  다시 열 수 있고, 다시 열면 위젯도 씬도 새로 만들어진다 — 그래서 강참조로 들면 안 되고, 호출부는 매번
	 *  다시 물어봐야 한다. 탭 스포너가 위젯을 만들 때 여기에 등록한다. */
	static TWeakPtr<class SFPSRWeaponAssemblerTab> GetLiveWeaponAssemblerTab() { return LiveWeaponAssemblerTab; }

	/** 1인칭 뷰 탭 식별자 — 조립기 탭의 "1인칭 뷰 열기" 버튼이 TryInvokeTab 에 쓴다(이름을 두 곳에 적지 않기 위해). */
	static FName GetWeaponAssemblerFPTabName();

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

	/** Menu command handler: opens (or focuses) the FPSR Blockout tool nomad tab — a config-driven modular map
	 *  palette (Tools > FPSR > "블록아웃 툴…"). */
	static void OnOpenBlockoutMenuEntry();

	/** Menu command handler: opens (or focuses) the Weapon Assembler's first-person view nomad tab — draws the
	 *  assembler's own preview scene with the camera locked at the player's eye and the aspect ratio pinned to the
	 *  game's, which a docked viewport cannot do (its aspect differs, so the vertical framing differs). */
	static void OnOpenWeaponAssemblerFPMenuEntry();

	/** Nomad tab spawner for the FPSR Data Editor (P1) — registered in StartupModule, unregistered in
	 *  ShutdownModule. Returns a dock tab hosting a single SFPSRDataEditorWidget. */
	static TSharedRef<class SDockTab> SpawnDataEditorTab(const class FSpawnTabArgs& Args);

	/** Nomad tab spawner for the Weapon Part Assembler — registered in StartupModule, unregistered in
	 *  ShutdownModule. Returns a dock tab hosting a single SFPSRWeaponAssemblerTab. */
	static TSharedRef<class SDockTab> SpawnWeaponAssemblerTab(const class FSpawnTabArgs& Args);

	/** Nomad tab spawner for the FPSR Blockout tool — registered in StartupModule, unregistered in ShutdownModule.
	 *  Returns a dock tab hosting a single SFPSRBlockoutTab. */
	static TSharedRef<class SDockTab> SpawnBlockoutTab(const class FSpawnTabArgs& Args);

	/** Nomad tab spawner for the Weapon Assembler's first-person view — registered in StartupModule, unregistered
	 *  in ShutdownModule. Returns a dock tab hosting a single SFPSRWeaponAssemblerFPTab. */
	static TSharedRef<class SDockTab> SpawnWeaponAssemblerFPTab(const class FSpawnTabArgs& Args);

	/** Tab identifier for the FPSR Data Editor nomad tab (RegisterNomadTabSpawner / TryInvokeTab both key off this). */
	static const FName FPSRDataEditorTabName;

	/** Tab identifier for the Weapon Part Assembler nomad tab (RegisterNomadTabSpawner / TryInvokeTab both key off this). */
	static const FName FPSRWeaponAssemblerTabName;

	/** Tab identifier for the FPSR Blockout tool nomad tab (RegisterNomadTabSpawner / TryInvokeTab both key off this). */
	static const FName FPSRBlockoutTabName;

	/** Tab identifier for the Weapon Assembler first-person view nomad tab. */
	static const FName FPSRWeaponAssemblerFPTabName;

	/** Backing store for GetLiveWeaponAssemblerTab — set by SpawnWeaponAssemblerTab, expires on its own when the
	 *  user closes the tab (weak). See the accessor's comment for why it can't be a strong reference. */
	static TWeakPtr<class SFPSRWeaponAssemblerTab> LiveWeaponAssemblerTab;
};
