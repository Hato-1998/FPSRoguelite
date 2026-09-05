// Copyright Epic Games, Inc. All Rights Reserved.

#include "Scripting/FPSRAssetScripting.h"

#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "EditorValidatorSubsystem.h"
#include "Misc/DataValidation.h"
#include "PropertyPathHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPSRAssetScripting, Log, All);

namespace
{
	/** Flatten IsObjectValid's TArray<FText> into one Message-sized line. Callers only need a human-readable
	 *  summary (this is surfaced through FFPSRAssetEditResult::Message, not consumed programmatically). */
	FString JoinValidationText(const TArray<FText>& Messages)
	{
		TArray<FString> Lines;
		Lines.Reserve(Messages.Num());
		for (const FText& Message : Messages)
		{
			Lines.Add(Message.ToString());
		}
		return FString::Join(Lines, TEXT(" | "));
	}
}

FFPSRAssetEditResult UFPSRAssetScripting::SetAssetProperty(const FString& AssetPath, const FString& PropertyPath,
                                                           const FString& Value, bool bDryRun, bool bSave)
{
	FFPSRAssetEditResult Result;

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		Result.Message = FString::Printf(TEXT("Asset not found or failed to load: %s"), *AssetPath);
		UE_LOG(LogFPSRAssetScripting, Error, TEXT("SetAssetProperty: %s"), *Result.Message);
		return Result;
	}

	// (1) Read-old — also doubles as the path-resolution check: a bad path fails here, before anything is touched.
	FString OldValue;
	if (!PropertyPathHelpers::GetPropertyValueAsString(Asset, PropertyPath, OldValue))
	{
		Result.Message = FString::Printf(TEXT("Property path did not resolve for read: '%s' on %s"), *PropertyPath, *AssetPath);
		UE_LOG(LogFPSRAssetScripting, Error, TEXT("SetAssetProperty: %s"), *Result.Message);
		return Result;
	}
	Result.OldValue = OldValue;
	Result.NewValue = OldValue;

	// (2) Idempotent no-op — already at the target value, nothing to write/validate/save.
	if (OldValue == Value)
	{
		Result.bOk = true;
		Result.bChanged = false;
		Result.Message = FString::Printf(TEXT("No-op: '%s' on %s already equals '%s'."), *PropertyPath, *AssetPath, *Value);
		return Result;
	}

	// (3) Dry-run stops here — report the intended change without writing anything.
	if (bDryRun)
	{
		Result.bOk = true;
		Result.bChanged = true;
		Result.NewValue = Value;
		Result.Message = FString::Printf(TEXT("DRY-RUN: would set '%s' on %s from '%s' to '%s'."), *PropertyPath, *AssetPath, *OldValue, *Value);
		return Result;
	}

	// (4)-(5) Modify + write. A failed SetPropertyValueFromString should not have mutated anything; either way
	// there is nothing for PostEditChange to react to, so it is skipped on this path.
	Asset->Modify();
	if (!PropertyPathHelpers::SetPropertyValueFromString(Asset, PropertyPath, Value))
	{
		Result.Message = FString::Printf(TEXT("Property path did not resolve for write: '%s' = '%s' on %s"), *PropertyPath, *Value, *AssetPath);
		UE_LOG(LogFPSRAssetScripting, Error, TEXT("SetAssetProperty: %s"), *Result.Message);
		return Result;
	}
	Asset->MarkPackageDirty();

	// (6)
	Asset->PostEditChange();

	// (7) Validate. Refuse to save (and roll the value back) on anything IsDataValid flags as Invalid — Valid and
	// NotValidated (no registered validator for this type) both proceed, matching how ValidateAssetsWithSettings
	// elsewhere in this module only counts Invalid as a failure.
	UEditorValidatorSubsystem* Validator = GEditor ? GEditor->GetEditorSubsystem<UEditorValidatorSubsystem>() : nullptr;
	if (!Validator)
	{
		// No validation subsystem available (should not happen inside a real editor/-editor commandlet session,
		// but this is a scripting entry point that must never write-then-save unvalidated data on the strength of
		// an assumption) — roll back and fail loudly rather than silently skipping step 7 of the contract.
		PropertyPathHelpers::SetPropertyValueFromString(Asset, PropertyPath, OldValue);
		Asset->PostEditChange();
		Result.NewValue = OldValue;
		Result.Message = TEXT("UEditorValidatorSubsystem unavailable (no GEditor?) — rolled back, refusing to save an unvalidated write.");
		UE_LOG(LogFPSRAssetScripting, Error, TEXT("SetAssetProperty: %s"), *Result.Message);
		return Result;
	}

	TArray<FText> ValidationErrors;
	TArray<FText> ValidationWarnings;
	const EDataValidationResult ValidationResult = Validator->IsObjectValid(Asset, ValidationErrors, ValidationWarnings, EDataValidationUsecase::Script);
	if (ValidationResult == EDataValidationResult::Invalid)
	{
		PropertyPathHelpers::SetPropertyValueFromString(Asset, PropertyPath, OldValue);
		Asset->PostEditChange();
		Result.NewValue = OldValue;
		Result.Message = FString::Printf(TEXT("Rejected by IsDataValid, rolled back to '%s': %s"), *OldValue, *JoinValidationText(ValidationErrors));
		UE_LOG(LogFPSRAssetScripting, Error, TEXT("SetAssetProperty: %s"), *Result.Message);
		return Result;
	}

	// (8) Save.
	Result.bOk = true;
	Result.bChanged = true;
	Result.NewValue = Value;
	if (bSave)
	{
		if (!UEditorAssetLibrary::SaveLoadedAsset(Asset, /*bOnlyIfIsDirty=*/false))
		{
			// The write itself already happened and passed validation — a save failure (checked out by someone
			// else, read-only file, etc.) still leaves the caller with a stale disk copy, so this is a hard
			// failure, not a warning.
			Result.bOk = false;
			Result.Message = FString::Printf(TEXT("Set '%s' to '%s' but SaveLoadedAsset failed (checked out elsewhere / read-only?): %s"), *PropertyPath, *Value, *AssetPath);
			UE_LOG(LogFPSRAssetScripting, Error, TEXT("SetAssetProperty: %s"), *Result.Message);
			return Result;
		}
	}

	Result.Message = FString::Printf(TEXT("Set '%s' on %s from '%s' to '%s'%s."), *PropertyPath, *AssetPath, *OldValue, *Value,
		bSave ? TEXT(" and saved") : TEXT(" (bSave=false, not saved)"));
	return Result;
}

FFPSRAssetEditResult UFPSRAssetScripting::GetAssetProperty(const FString& AssetPath, const FString& PropertyPath)
{
	FFPSRAssetEditResult Result;

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset)
	{
		Result.Message = FString::Printf(TEXT("Asset not found or failed to load: %s"), *AssetPath);
		UE_LOG(LogFPSRAssetScripting, Error, TEXT("GetAssetProperty: %s"), *Result.Message);
		return Result;
	}

	FString Value;
	if (!PropertyPathHelpers::GetPropertyValueAsString(Asset, PropertyPath, Value))
	{
		Result.Message = FString::Printf(TEXT("Property path did not resolve for read: '%s' on %s"), *PropertyPath, *AssetPath);
		UE_LOG(LogFPSRAssetScripting, Error, TEXT("GetAssetProperty: %s"), *Result.Message);
		return Result;
	}

	Result.bOk = true;
	Result.bChanged = false;
	Result.OldValue = Value;
	Result.NewValue = Value;
	Result.Message = FString::Printf(TEXT("'%s' on %s = '%s'"), *PropertyPath, *AssetPath, *Value);
	return Result;
}
