// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "FPSRGunMotionStudioData.generated.h"

/**
 * One authoring key on a studio track (GunMotionTool_Spec.md §2-2). The space Loc/Rot are expressed in is decided by
 * WHICH track holds the key, not by this struct — gun keys are camera space, hand keys are gun space, part-track keys
 * are the part's socket frame (§2-2 "공간은 대상이 정한다").
 */
USTRUCT()
struct FFPSRStudioKey
{
	GENERATED_BODY()

	/** Clip playback time (seconds). */
	UPROPERTY(EditAnywhere)
	float Time = 0.0f;

	UPROPERTY(EditAnywhere)
	FVector Loc = FVector::ZeroVector;

	UPROPERTY(EditAnywhere)
	FRotator Rot = FRotator::ZeroRotator;
};

/** Independent-timing key track for one channel (§2-2) — the gun, either hand, or one attached part each own one of
 *  these, and keys on different tracks are never forced onto the same timestamps. */
USTRUCT()
struct FFPSRStudioTrack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	TArray<FFPSRStudioKey> Keys;
};

/** One "hand attached to a part" interval (§2-2), authored by [IK 붙이기]/[떼기] in the studio session. The baker
 *  turns a span's boundaries into a Blend-curve ramp (±BlendTime, default 0.1s) rather than a hard cut. */
USTRUCT()
struct FFPSRStudioAttachSpan
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	float Start = 0.0f;

	UPROPERTY(EditAnywhere)
	float End = 0.0f;

	/** Stable attach-socket id the hand rebases onto for this span (FFPSRWeaponPartAttachment::Socket). */
	UPROPERTY(EditAnywhere)
	FName PartSocket = NAME_None;
};

/**
 * Gun-motion studio authoring keys, attached to a clip asset as AssetUserData (GunMotionTool_Spec.md §2-2) — a
 * sidecar file would not follow the clip when it is moved/duplicated, so the keys live on the asset itself instead.
 *
 * §2-3 draws a hard line: at RUNTIME, only the curves baked from these keys (FPSRGunMotionCurves.h) plus the two
 * per-hand attach-socket fields below are ever read. Everything else here (the raw keys, the spans) is studio-only —
 * the baker consumes them once at save time and nothing at play time re-derives from them.
 */
UCLASS()
class FPSROGUELITE_API UFPSRGunMotionStudioData : public UAssetUserData
{
	GENERATED_BODY()

public:
	/** Whole-gun loc/rot offset track (camera space, §2-2/§3-2) — baked to the hand_r bone track, never a runtime
	 *  curve (§0: "총 전체 = hand_r 애디티브 본 트랙(런타임 0)"). Studio-only. */
	UPROPERTY(EditAnywhere)
	FFPSRStudioTrack GunKeys;

	/** Left-hand IK target offset track (gun space, §2-1 FPGM_HL_*). Studio-only source the baker samples into
	 *  runtime curves. */
	UPROPERTY(EditAnywhere)
	FFPSRStudioTrack LeftHandKeys;

	/** Right-hand mirror of LeftHandKeys above. */
	UPROPERTY(EditAnywhere)
	FFPSRStudioTrack RightHandKeys;

	/** Per-part additional relative-transform track (part socket frame, §2-1 FPGM_P_<소켓>_*) — key = stable attach
	 *  socket id (FFPSRWeaponPartAttachment::Socket, e.g. "SOCKET_Mount_3"). Studio-only. */
	UPROPERTY(EditAnywhere)
	TMap<FName, FFPSRStudioTrack> PartTracks;

	/** Left-hand attach spans authored by [IK 붙이기]/[떼기] (§2-2). Studio-only — the baker collapses these into the
	 *  FPGM_HL_Blend curve ramp and, separately, into LeftHandAttachPartSocket below. */
	UPROPERTY(EditAnywhere)
	TArray<FFPSRStudioAttachSpan> LeftHandAttachSpans;

	/** Right-hand mirror of LeftHandAttachSpans above. */
	UPROPERTY(EditAnywhere)
	TArray<FFPSRStudioAttachSpan> RightHandAttachSpans;

	/** §2-3 runtime-consumed metadata: this clip's LEFT-hand representative attach-socket id, reduced at bake time
	 *  from LeftHandAttachSpans to ONE socket per clip (a clip attaches at most one part per hand — sequential
	 *  multi-part attachment within a single clip is a later extension, §2-2). None = this clip never rebases the
	 *  left hand onto a part (Blend curve, if authored at all, stays meaningless with no frame to rebase onto).
	 *  UFPSRFirstPersonArmsAnimInstance::ComputeHandIKTarget reads this — and ONLY this — from studio data at runtime. */
	UPROPERTY(EditAnywhere)
	FName LeftHandAttachPartSocket = NAME_None;

	/** Right-hand mirror of LeftHandAttachPartSocket above. */
	UPROPERTY(EditAnywhere)
	FName RightHandAttachPartSocket = NAME_None;
};
