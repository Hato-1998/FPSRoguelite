// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AFPSRArenaActor;

/** What a source scan found. Kept as a value so the five checks of ADR 0012 can compare digests without each
 *  re-deriving what "the source" means. */
struct FFPSRArenaBakeSourceDigest
{
	/** Hex SHA1 over the canonical description of every contributing component. Empty = the scan failed. */
	FString Hash;

	/** How many components went into the hash. Surfaced separately because "0 or 1" is the signature of a
	 *  collision-setup mistake, and a bare hash cannot say that. */
	int32 ComponentCount = 0;

	/** How many distinct actors those components belong to — the number stored on the bake asset. */
	int32 ActorCount = 0;

	bool IsValid() const { return !Hash.IsEmpty(); }
};

/**
 * The one definition of "what the arena bake reads" (ADR 0012).
 *
 * This lives in the RUNTIME module on purpose even though only the editor bakes. ADR 0011 E4 settled that a rule
 * with two implementations is a rule that will disagree with itself, and ADR 0012 puts five checks on this hash —
 * the editor baker, the save-time warning, the manual validate tool, the PreSubmit validator, the CI commandlet,
 * and the world-start alarm. The last one runs in a shipped-ish runtime, so an editor-only implementation would
 * force a second copy, and the copy that drifts is always the one nobody is looking at.
 *
 * WHAT COUNTS: components whose collision object type is ECC_WorldStatic with query collision enabled, whose
 * bounds fall inside the arena. That is not a taste call — it is exactly what UFPSRFlowFieldComputer's obstacle
 * probe traces (FCollisionObjectQueryParams::AddObjectTypesToQuery(ECC_WorldStatic)). If the two ever diverge the
 * hash starts guarding the wrong set and reports "fresh" for a mask that is stale, which is worse than no check.
 * Change one, change the other.
 *
 * Destructibles and randomly-placed props are excluded for free: ADR 0010 D7 already requires them NOT to be
 * WorldStatic, and ADR 0012 invariant 6 turns that into a hard rule. They reach the mask through
 * StampCellBlocked at runtime, so they must not move this hash.
 */
/** 베이크와 레벨이 일치하는가. 다섯 검사가 같은 어휘를 쓰도록 값으로 뽑아 둔다. */
enum class EFPSRArenaBakeFreshness : uint8
{
	/** 아레나에 베이크 에셋 참조가 없다 — 아직 저작 중일 수 있으므로 스테일과 구분한다. */
	NoAsset,
	/** 참조는 있는데 내용이 비었다(굽지 않았거나 저장이 날아갔다). */
	NotBaked,
	/** 해시가 다르다 — 레벨을 고치고 다시 굽지 않았다. */
	Stale,
	/** 일치. */
	Fresh
};

/** 한 아레나의 신선도 판정 결과. */
struct FFPSRArenaBakeCheck
{
	EFPSRArenaBakeFreshness Freshness = EFPSRArenaBakeFreshness::NoAsset;

	/** 사람이 읽는 한 줄. 다섯 검사 지점이 **같은 문장**을 쓴다 — 같은 사고를 두 가지 말로 설명하면
	 *  받는 사람이 다른 문제로 오해한다. */
	FString Message;

	/** 지금 레벨에서 다시 계산한 것. 리포트에 소스 개수를 띄우는 데 쓴다. */
	FFPSRArenaBakeSourceDigest Current;

	/** 마스크를 믿을 수 없는 상태인가. Fresh 만 false. */
	bool IsProblem() const { return Freshness != EFPSRArenaBakeFreshness::Fresh; }
};

class FPSROGUELITE_API FFPSRArenaBakeHash
{
public:
	/**
	 * 베이크가 현재 레벨과 일치하는지 판정한다 — ADR 0012 (나)안이 (가)안 대신 성립하려면 반드시 있어야
	 * 하는 검사다.
	 *
	 * (가)안(서브레벨 인라인 저장)을 접고 (나)안(별도 DataAsset)을 택한 이유가 저장 훅을 안 만드는 것이었고,
	 * 그 대가가 "사람이 다시 굽는 걸 잊는다"였다. 그 대가를 갚는 것이 이 함수 하나이며, 다섯 지점
	 * (저장 경고 · 검증 툴 · PreSubmit · 커맨드렛 · 월드 시작 알람)이 **전부 이것을 부른다.** 판정 로직이
	 * 두 벌이 되면 "에디터는 괜찮다는데 런타임은 아니라고 한다"가 나온다(0011 E4).
	 */
	static FFPSRArenaBakeCheck CheckFreshness(const AFPSRArenaActor& Arena);

	/**
	 * Scan Arena's level for the geometry the bake would see and hash it.
	 *
	 * Transforms are hashed RELATIVE TO THE ARENA (ADR 0012 invariant 8), so parking the arena somewhere else —
	 * or streaming its sublevel in at a different LevelTransform — does not read as "the level changed".
	 */
	static bool Compute(const AFPSRArenaActor& Arena, FFPSRArenaBakeSourceDigest& Out);
};
