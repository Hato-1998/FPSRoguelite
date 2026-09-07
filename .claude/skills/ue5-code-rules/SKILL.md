---
name: ue5-code-rules
description: Unreal Engine 5 C++ coding conventions, GAS patterns, and Lyra architecture reference. Use when working on UE5 projects, writing C++ code for Unreal, editing Blueprints, or implementing Gameplay Ability System features.
---

# UE5 Code Rules & Architecture Reference

## Lyra Architecture Reference
- UE5 GAS 패턴 및 컴포넌트 아키텍처는 **Lyra Starter Game**을 참조한다.
- Composition over Inheritance 원칙을 따른다.

## Absolute Prohibitions (UE5-Specific)
- NEVER rename UCLASS, USTRUCT, or UENUM types (Blueprint asset references break).
- NEVER modify .uasset files directly — use MCP or editor tools.
- NEVER remove existing UPROPERTY/UFUNCTION without checking Blueprint references.

## Naming Conventions

### Class Prefixes
| Prefix | Type | Example |
|--------|------|---------|
| `A` | Actor | `AAuraCharacter` |
| `U` | UObject/Component | `UAuraAbilitySystemComponent` |
| `F` | Struct | `FAuraAttributeInfo` |
| `E` | Enum | `EAuraAbilityInputID` |
| `I` | Interface | `ICombatInterface` |

### Variable & Function
- bool: `bIsVisible` (b prefix)
- Count: `NumPlayers` (Num prefix)
- PascalCase only (no snake_case, no m_ prefix)
- Functions: verb-first (`GetPlayerLevel()`, `ApplyDamage()`)
- Bool returns: question form (`IsAlive()`, `CanActivate()`)

### Asset Naming
`BP_`, `DA_`, `GE_`, `GA_`, `SM_`, `T_`, `M_`, `MI_`, `ABP_`, `AM_`, `WBP_`

## UPROPERTY/UFUNCTION Quick Reference
- Editor-editable: `UPROPERTY(EditAnywhere, Category = "X")`
- BP read-only: `UPROPERTY(BlueprintReadOnly, Category = "X")`
- Runtime component: `UPROPERTY(VisibleAnywhere, Category = "X")`
- Internal: `UPROPERTY()` (no specifiers)
- BP callable: `UFUNCTION(BlueprintCallable, Category = "X")`
- BP overridable: `UFUNCTION(BlueprintNativeEvent, BlueprintCallable)`
- BP implement-only: `UFUNCTION(BlueprintImplementableEvent)`

## GAS Patterns (Lyra Reference)
- Player ASC ownership: PlayerState owns, Character inits with `InitAbilityActorInfo(PS, this)`
- Enemy ASC ownership: Character owns directly, inits with `InitAbilityActorInfo(this, this)`
- Attribute access: always use `ATTRIBUTE_ACCESSORS` macro
- Effect application: `MakeEffectContext() → MakeOutgoingSpec() → ApplyGameplayEffectSpecToTarget()`

## Architecture Principles
- Composition over Inheritance (components, not deep class hierarchies)
- Event Delegate over Polling (not Tick-based checks)
- `PrimaryActorTick.bCanEverTick = false` by default
- Cache `FindComponentByClass<>()` results in BeginPlay
- Use `TObjectPtr<>` instead of raw pointers (UE5 standard)
- `check()` for fatal, `ensure()` for debug, null-check for optional

## Detailed Code Rules
For comprehensive examples and patterns, see: @~/Desktop/UE5_Code_Rules_Default.md
