// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FPSRAssetScripting.generated.h"

/** Result of one scripted asset-property read/write (UFPSRAssetScripting). Every failure path fills Message —
 *  headless callers (Python) only ever see this struct, never an engine exception, so silent success and silent
 *  failure are both bugs here. */
USTRUCT(BlueprintType)
struct FFPSRAssetEditResult
{
	GENERATED_BODY()

	/** True if the request completed without error. Includes the idempotent "already at target value" case for
	 *  SetAssetProperty, and every successful GetAssetProperty. */
	UPROPERTY(BlueprintReadOnly, Category="FPSR|Scripting")
	bool bOk = false;

	/** True only if a property value was actually written (or, under bDryRun, would have been). False for
	 *  GetAssetProperty, for an idempotent no-op SetAssetProperty, and for any failure (including a rolled-back
	 *  IsDataValid rejection). */
	UPROPERTY(BlueprintReadOnly, Category="FPSR|Scripting")
	bool bChanged = false;

	/** Property value before the call (GetAssetProperty: the value that was read). */
	UPROPERTY(BlueprintReadOnly, Category="FPSR|Scripting")
	FString OldValue;

	/** Property value after the call. Mirrors OldValue whenever nothing was written: GetAssetProperty, an
	 *  idempotent no-op, a dry-run preview target aside, or a failure that rolled the value back. */
	UPROPERTY(BlueprintReadOnly, Category="FPSR|Scripting")
	FString NewValue;

	/** Human-readable outcome, always set on both success and failure — see the class comment's contract. */
	UPROPERTY(BlueprintReadOnly, Category="FPSR|Scripting")
	FString Message;
};

/**
 * Headless DataAsset property scripting for offline authoring tools. Exists because the EditDefaultsOnly guard
 * Python hits ("cannot be edited on instances") is a DETAILS-PANEL guard, not a restriction on FProperty writes —
 * an offline authoring tool is not the details panel, so it is entitled to use a different layer: direct
 * reflection writes via the engine's own property-path system.
 *
 * Path resolution and string<->value conversion are NOT reimplemented here — both are delegated to
 * PropertyPathHelpers (Runtime/PropertyPath), which already understands dotted/indexed paths such as
 * "WeaponParts[0].Stages[0].StatValue" (FPropertyPathSegment parses the "[N]" array index itself). This class adds
 * no parsing of its own.
 *
 * SetAssetProperty's contract, in order:
 *   1. Read the current value (becomes OldValue).
 *   2. If it already equals the requested Value: idempotent no-op — bOk=true, bChanged=false, nothing touched.
 *   3. If bDryRun: report what WOULD happen (bOk=true, bChanged=true, NewValue=Value) and stop — no write.
 *   4-6. Modify() the object, write the value, PostEditChange().
 *   7. Validate with UEditorValidatorSubsystem::IsObjectValid. On Invalid: write OldValue back, PostEditChange()
 *      again, do NOT save, and fail (bOk=false) with the validation errors in Message.
 *   8. If bSave: UEditorAssetLibrary::SaveLoadedAsset. A save failure is also a hard failure (the in-memory value
 *      changed but disk did not, which the caller must know about).
 *
 * Editor-only by construction (this whole module is Editor-only) — never link this into a runtime target.
 */
UCLASS()
class UFPSRAssetScripting : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Write Value to PropertyPath on the asset at AssetPath (string form, converted via the property's own
	 *  ImportText — the same conversion the Details panel itself uses). See the class comment for the full
	 *  idempotent / dry-run / validate-or-rollback / save contract. */
	UFUNCTION(BlueprintCallable, Category="FPSR|Scripting")
	static FFPSRAssetEditResult SetAssetProperty(const FString& AssetPath, const FString& PropertyPath,
	                                             const FString& Value, bool bDryRun = false, bool bSave = true);

	/** Read PropertyPath from the asset at AssetPath. No write, no validation, no save — OldValue and NewValue
	 *  both carry the value read. */
	UFUNCTION(BlueprintCallable, Category="FPSR|Scripting")
	static FFPSRAssetEditResult GetAssetProperty(const FString& AssetPath, const FString& PropertyPath);
};
