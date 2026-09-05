// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FPSRCardTypes.generated.h"

class UFPSRCardDataAsset;
class UFPSRWeaponDataAsset;

/** v2 card group (§2-3-2): the draw pool / trigger / UI filter a card belongs to. Orthogonal to per-effect
 *  application scope (UCardEffect_WeaponStat::bThisWeaponOnly). Character = character + all-weapons effects
 *  (no target weapon). Weapon = this-weapon stat / behavior cards (TargetWeapon set).
 *
 *  WeaponUnlock = the weapon-unlock cards (DA_CardUnlock_*). No C++ branch reads this value — the unlock flow is
 *  routed by UFPSRCardPoolDataAsset::WeaponUnlockCards + EFPSROfferType::WeaponUnlock — but it IS the authored
 *  classification on those assets, so it is deliberately kept (it is dead CODE, not dead DATA).
 *  ⚠️ Do NOT "tidy" an unlock card to Weapon: GatherCandidatePool only sets a draw's TargetWeapon when
 *  Group == Weapon (FPSRCardSubsystem.cpp), so re-tagging would change real behaviour. Character/WeaponUnlock
 *  both mean "no target weapon" to the code; WeaponUnlock additionally says *why*. */
UENUM(BlueprintType)
enum class ECardGroup : uint8
{
	Character    UMETA(DisplayName = "Character"),
	Weapon       UMETA(DisplayName = "Weapon"),
	WeaponUnlock UMETA(DisplayName = "Weapon Unlock")
};

/** What a presented card offer represents — drives the draw pool and the consume/gate behavior. */
UENUM(BlueprintType)
enum class EFPSROfferType : uint8
{
	OpeningSeed  UMETA(DisplayName = "Opening Seed"),  // run-start seed; applies without consuming a pick
	LevelUp      UMETA(DisplayName = "Level Up"),      // consumes a level-up pick (CardPicksPending)
	WeaponUnlock UMETA(DisplayName = "Weapon Unlock") // WeaponUnlock: requires & consumes a weapon-unlock pick; new-weapon unlock (mission clear + level milestones). Not rerollable.
};

/** Closed set of card DRAW ROUTES = which membership array a card lives in (each has different draw semantics).
 *  Closed by schema (the arrays are fixed C++ members); the OPEN axis is card effects, which declare which of
 *  these routes they permit via UFPSRCardEffect::GetEditorEligibleRoutes(). Editor-tool preflight uses this. */
UENUM()
enum class EFPSRCardRoute : uint8
{
	LevelUpGlobal             UMETA(DisplayName = "Level-Up: Global Pool (Pool.Cards)"),
	MissionClearNewWeapon     UMETA(DisplayName = "Mission-Clear: New Weapon (Pool.WeaponUnlockCards)"),
	LevelUpWeapon             UMETA(DisplayName = "Level-Up: Weapon Card (Weapon.WeaponCards)"),
	MissionClearWeaponFeature UMETA(DisplayName = "Mission-Clear: Weapon Feature (Weapon.UnlockableFeatures)")
};

UENUM(BlueprintType)
enum class ECardRarity : uint8
{
	Common    UMETA(DisplayName = "Common"),
	Rare      UMETA(DisplayName = "Rare"),
	Epic      UMETA(DisplayName = "Epic"),
	Legendary UMETA(DisplayName = "Legendary")
};

/** One rarity variant of a card: the rarity it can roll at and the SetByCaller magnitude applied at that rarity.
 *  A card may define several tiers so it can be offered at any of them (e.g. MaxHealth +15 Common .. +100 Legendary). */
USTRUCT(BlueprintType)
struct FFPSRCardRarityTier
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	ECardRarity Rarity = ECardRarity::Common;

	/** Magnitude injected into the card's AppliedEffect via SetByCaller (tag SetByCaller.CardMagnitude). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Card")
	float Magnitude = 0.0f;
};

/** A single drawn card offer: the card and the rarity it rolled at. Per-effect magnitude is no longer carried on
 *  the draw (v2) — each effect resolves its own magnitude from its RarityTiers at this rolled Rarity, both on the
 *  server (UFPSRCardEffect::Apply) and on the client (UI reads Card->Effects locally). */
USTRUCT(BlueprintType)
struct FFPSRCardDraw
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Card")
	TObjectPtr<UFPSRCardDataAsset> Card = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Card")
	ECardRarity Rarity = ECardRarity::Common;

	/** Weapon this offer applies to (the weapon whose pool contributed the card). null = character / all-weapons
	 *  target. Set server-side at draw time so weapon-scope cards apply to their SOURCE weapon — owned but not
	 *  necessarily equipped — instead of whatever is currently held. */
	UPROPERTY(BlueprintReadOnly, Category = "Card")
	TObjectPtr<UFPSRWeaponDataAsset> TargetWeapon = nullptr;
};

/**
 * 이 플레이어가 이번 런에 획득한 카드 1장 (CRIT2). **복제된다** — Tab 정보창이 남의 빌드까지 보여주기 때문이다
 * (그래서 서버 전용으로 두지 않았다. 나중에 복제로 바꾸는 것은 임시 구조 금지에 걸린다).
 * 획득 순서 = 배열 인덱스. 같은 카드를 두 번 고르면 원소가 둘 생긴다(스택형 카드의 투자도 투자다).
 */
USTRUCT(BlueprintType)
struct FFPSRAcquiredCard
{
	GENERATED_BODY()

	/** 획득한 카드. 카드 DA 포인터 복제는 이미 ClientPresentCards 가 쓰는 검증된 경로다(CRIT2 C0 실측 7). */
	UPROPERTY(BlueprintReadOnly, Category = "Card")
	TObjectPtr<UFPSRCardDataAsset> Card = nullptr;

	/** 굴린 레어도 — 정보창이 "치명타 확률 (에픽)"처럼 보여주려면 필요하다. */
	UPROPERTY(BlueprintReadOnly, Category = "Card")
	ECardRarity Rarity = ECardRarity::Common;

	/** 이 카드가 적용된 무기(캐릭터·전체무기 오퍼는 null). **나중에 넣을 수 없다** —
	 *  복제 struct 를 나중에 바꾸는 것은 이 유닛이 애초에 복제형을 택한 이유(임시 구조 금지)와 정면 충돌한다(CRIT2 G1 P2-6).
	 *  출처 풀 라벨(§2-4-1, 2026-08-13 확정)이 카드 표시에 `TargetWeapon->DisplayName` 을 쓰므로,
	 *  정보창이 "연사 속도(라이플, 레어)" 를 그리려면 지금 있어야 한다. */
	UPROPERTY(BlueprintReadOnly, Category = "Card")
	TObjectPtr<UFPSRWeaponDataAsset> TargetWeapon = nullptr;
};
