// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Card/FPSRCardTypes.h"
#include "FPSRCardSubsystem.generated.h"

class UFPSRCardDataAsset;
class UFPSRCardPoolDataAsset;
class UFPSRWeaponDataAsset;
class AController;

/** Server-authoritative card draw and application logic (P3-C).
 *  Cards define per-rarity magnitude tiers; the draw rolls a rarity (weighted by rarity base weight + player
 *  Luck) and returns offers carrying the rolled rarity + magnitude. Selection applies a GE to the ASC. */
UCLASS()
class FPSROGUELITE_API UFPSRCardSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/** Set the active card pool for this world. */
	void SetActivePool(UFPSRCardPoolDataAsset* InPool) { ActivePool = InPool; }

	/** Get the active card pool. */
	UFPSRCardPoolDataAsset* GetActivePool() const { return ActivePool; }

	/** Draw Count card offers for the given player, excluding any whose card is in the Exclude list
	 *  (server authority only). Each offer carries the rolled rarity and the magnitude to apply. */
	TArray<FFPSRCardDraw> DrawCards(AController* ForPlayer, int32 Count = 3, const TArray<UFPSRCardDataAsset*>& Exclude = TArray<UFPSRCardDataAsset*>());

	/** Apply a selected card offer to the given player (server authority only). Returns true if the selection
	 *  was accepted (so the offer flow can advance / unfreeze). Consume behavior depends on OfferType:
	 *   - OpeningSeed: applies, consumes nothing.
	 *   - LevelUp: requires & consumes a level-up pick (CardPicksPending).
	 *   - WeaponUnlock: requires & consumes a weapon-unlock pick (WeaponUnlockPicksPending).
	 *  Character-scope cards apply their GE now; weapon-scope (modifier) cards are accepted/consumed but their
	 *  effect application lands in P4-B (logged no-op here) so the freeze can never soft-lock. */
	bool ApplyCard(AController* ForPlayer, const FFPSRCardDraw& Draw, EFPSROfferType OfferType, int32 ReplaceFragmentIndex = INDEX_NONE);

	/** Build a single card draw from one card (used for mission-reward offers), rolling a rarity tier by
	 *  the player's luck. Returns an offer with a null Card if the card has no tiers. */
	FFPSRCardDraw BuildSingleDraw(UFPSRCardDataAsset* Card, AController* ForPlayer) const;

	/** Build a WeaponUnlock offer: new-weapon candidates from the pool's WeaponUnlockCards (gated on free slot +
	 *  not-already-owned, de-duped by granted weapon). U18b. (U18b2 adds feature-unlock candidates.) */
	TArray<FFPSRCardDraw> DrawWeaponUnlockOffer(AController* ForPlayer, int32 Count = 3);

	/** Try to consume a reroll charge from the player. Returns true if successful. */
	bool TryReroll(AController* ForPlayer);

	/** 시너지 계수의 **순수 산식**(CRIT2). 월드도 액터도 서브시스템 상태도 만지지 않으므로 헤드리스 단위테스트가
	 *  가능하다(G1 P1-1 — 리포 전례: AFPSRPlayerState::IsTopologyAckSatisfied, FPSRCombat::RollCrit).
	 *  = 1 + BonusPerCard × min(보유수, MaxStacks). 카드가 태그를 여러 개 달았으면 **가장 많이 투자한 태그 하나**만
	 *  본다(합산 아님) — 합산하면 저작이 "태그를 많이 달수록 유리"로 왜곡된다. 태그 없음/보유 0 → 1.0(현행 거동). */
	static float ComputeSynergyMultiplier(const TArray<FName>& CardTags, const TMap<FName, int32>& TagCounts,
		float BonusPerCard, int32 MaxStacks);

	/** 🔁 레벨업 풀에만 있던 비복원 가중 추출을 **공유 헬퍼로 추출**한다(CRIT2). 미션 풀이 균등 셔플 대신 이걸 쓴다.
	 *  현행 계약 승계: ① 매 선택마다 선택된 후보 자신은 **술어와 무관하게 항상 제거** ② 술어는 (Selected, Candidate)
	 *  순서로 받고 true 면 Candidate 도 제거 ③ 총 가중치 ≤ 0 이면 중단 ④ `Reserve(Min(Max(Count,0), Num))` —
	 *  `Max(0)` 는 장식이 아니다(`FPSR.DrawCards -1` 이 `TArray::Reserve` 에 음수를 넘겨 프로세스를 죽인다).
	 *
	 *  ⚠️ **public static** 이다(G1 P2-7) — §12-6 의 미션 풀 등가 판정을 자동화가 부를 수 있어야 한다.
	 *
	 *  **그룹 비중 보존 2단 추출 (사용자 결정 C)** — `GroupIds` 가 비어 있으면 종래의 단순 가중 추출
	 *  (레벨업 풀 경로, 거동 불변). 비어 있지 않으면 매 추출을 두 단계로 나눈다:
	 *    1단계 — 남은 그룹을 **`BaselineWeights` 합**에 비례해 고른다(= 시너지가 없었다면 가졌을 비중).
	 *    2단계 — 그 그룹 안에서 **`InOutWeights`**(시너지 포함)에 비례해 고른다.
	 *  이러면 시너지가 **그룹 안에서만 재분배**되고 새 무기 그룹의 몫 비중은 매 추출에서 정확히 보존된다.
	 *  모든 시너지가 1 이면 2단 추출은 종래 균등 추출과 **분포가 같다**(§12-6 회귀 기준). */
	static void WeightedSampleWithoutReplacement(
		TArray<FFPSRCardDraw>& InOutCandidates, TArray<float>& InOutWeights,
		TArray<float>& InOutBaselineWeights, TArray<int32>& InOutGroupIds, int32 Count,
		TFunctionRef<bool(const FFPSRCardDraw& Selected, const FFPSRCardDraw& Candidate)> ExclusionPredicate,
		TArray<FFPSRCardDraw>& OutPicked);

protected:
	/** Effective draw weight of a card at a specific rarity, given player luck. */
	float GetEffectiveWeight(const UFPSRCardDataAsset* Card, ECardRarity Rarity, float Luck) const;

	/** Gather candidate cards: the central pool (character / all-weapons) plus every owned weapon's WeaponCards.
	 *  OutSourceWeapons is index-aligned with OutCandidates — the weapon that contributed each card (null for the
	 *  central pool / character cards), used to set FFPSRCardDraw::TargetWeapon. */
	void GatherCandidatePool(AController* ForPlayer, TArray<UFPSRCardDataAsset*>& OutCandidates, TArray<UFPSRWeaponDataAsset*>& OutSourceWeapons) const;

private:
	/** 🔴 **시너지가 곱해지는 유일한 지점**(CRIT2). 미션/해금 풀 전용 — 레벨업(스탯) 풀은 사용자 결정에 따라
	 *  균등하게 두므로 `GetEffectiveWeight` 는 이 함수를 부르지 않고, 시너지 인자를 갖지도 않는다.
	 *  새 시너지 규칙은 반드시 여기 하나에만 들어간다(두 곳에 나뉘면 한 풀에서만 조용히 안 돈다). */
	float GetUnlockDrawWeight(const UFPSRCardDataAsset* Card, const TMap<FName, int32>& TagCounts) const;

	UPROPERTY()
	TObjectPtr<UFPSRCardPoolDataAsset> ActivePool;
};
