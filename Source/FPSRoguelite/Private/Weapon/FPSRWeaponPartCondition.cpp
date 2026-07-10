// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponPartCondition.h"
#include "Weapon/FPSRWeaponFragment.h"

#if WITH_EDITOR
#define LOCTEXT_NAMESPACE "FPSRWeaponPartCondition"
#endif

bool UFPSRPartCondition_StatThreshold::IsMet(const FFPSRWeaponStatBlock& Stats, const TArray<TObjectPtr<UFPSRWeaponFragment>>&) const
{
	const float V = Stats.GetAxisValue(Axis);
	switch (Cmp)
	{
	case EFPSRStatCompare::GreaterOrEqual: return V >= Value;
	case EFPSRStatCompare::Greater:        return V > Value;
	case EFPSRStatCompare::LessOrEqual:    return V <= Value;
	case EFPSRStatCompare::Less:           return V < Value;
	case EFPSRStatCompare::Equal:          return FMath::IsNearlyEqual(V, Value);
	default:                               return false;
	}
}

bool UFPSRPartCondition_HasFragment::IsMet(const FFPSRWeaponStatBlock&, const TArray<TObjectPtr<UFPSRWeaponFragment>>& Fragments) const
{
	if (!Fragment)
	{
		return false;
	}
	int32 Count = 0;
	for (const TObjectPtr<UFPSRWeaponFragment>& F : Fragments)
	{
		if (F == Fragment)
		{
			++Count;
		}
	}
	return Count >= FMath::Max(1, MinStacks);
}

#if WITH_EDITOR
FText UFPSRWeaponPartCondition::GetEditorDescription() const
{
	return GetClass()->GetDisplayNameText();
}

FText UFPSRPartCondition_Always::GetEditorDescription() const
{
	return LOCTEXT("Always", "항상");
}

FText UFPSRPartCondition_StatThreshold::GetEditorDescription() const
{
	const FString AxisName = StaticEnum<EFPSRWeaponStat>()
		? StaticEnum<EFPSRWeaponStat>()->GetDisplayNameTextByValue(static_cast<int64>(Axis)).ToString()
		: FString(TEXT("Stat"));
	const FString CmpStr = StaticEnum<EFPSRStatCompare>()
		? StaticEnum<EFPSRStatCompare>()->GetDisplayNameTextByValue(static_cast<int64>(Cmp)).ToString()
		: FString(TEXT("?"));
	return FText::FromString(FString::Printf(TEXT("%s %s %g"), *AxisName, *CmpStr, Value));
}

FText UFPSRPartCondition_HasFragment::GetEditorDescription() const
{
	return FText::Format(LOCTEXT("HasFragment", "{0} x{1}"),
		Fragment ? Fragment->DisplayName : LOCTEXT("HasFragment_NoFragment", "(없음)"),
		FText::AsNumber(MinStacks));
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
