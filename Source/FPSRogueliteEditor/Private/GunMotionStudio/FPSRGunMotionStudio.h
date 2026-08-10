// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

// 스튜디오 공용 로그 카테고리 — 정의는 FPSRGunMotionStudio.cpp. (TU별 DEFINE_LOG_CATEGORY_STATIC 은 유니티 빌드가
// 두 .cpp 를 한 블롭으로 합치는 순간 재정의 충돌한다 — 실측 C2011.)
DECLARE_LOG_CATEGORY_EXTERN(LogFPSRGunMotionStudio, Log, All);

class AFPSRCharacter;
class APlayerController;
class UAnimSequence;
class UAnimMontage;
class UAnimInstance;
class UFPSRGunMotionStudioData;
class UFPSRWeaponDataAsset;
class UStaticMeshComponent;

/** GunMotionTool_Spec.md §5 selection targets — what the gizmo currently manipulates and the timeline currently
 *  shows keys for. */
enum class EFPSRGunMotionStudioSelection : uint8
{
	None,
	Gun,        // whole-gun mode (§5 "무기 전체 모드") — GunKeys track, camera-space keys.
	LeftHand,   // LeftHandKeys track, gun-space keys.
	RightHand,  // RightHandKeys track, gun-space keys.
	Part,       // PartTracks[SelectedPartSocket], socket-frame keys.
};

/** §6 studio mode — action-clip authoring (the whole §5 session) vs. state-pose authoring (DA fields only). */
enum class EFPSRGunMotionStudioMode : uint8
{
	ActionClip,
	StatePose,
};

/** §6 which DA state-pose field the whole-gun gizmo currently targets. */
enum class EFPSRGunMotionStatePoseTarget : uint8
{
	Slide,
	Airborne,
	Holster,
};

/**
 * Gun-motion studio session singleton (GunMotionTool_Spec.md §5) — owns every piece of state a studio session needs:
 * the PIE actor/weapon it is authoring against, the WIP transient clip + its studio keys, the paused slot montage
 * used to scrub, and the M/P transforms cached once at session entry (§3-2 "라이브 변환... 캐시 시점 = 저작 세션
 * 시작"). Toggled by the `FPSR.GunMotionStudio` console command (editor builds only — this whole module is excluded
 * from packaged builds by construction, §1). Ticks via FTSTicker (not an actor) so it keeps drawing/handling input
 * even while the PIE world itself is paused (SetGamePaused only stops Actor Tick, not the engine's core Slate/ImGui
 * loop) and costs nothing when inactive (§7 "세션 밖 틱 비용 0") — the ticker callback's first line is `if (!bActive)
 * return false-avoiding-work`, and no ticker is even registered until Activate() runs.
 */
class FFPSRGunMotionStudio
{
public:
	static FFPSRGunMotionStudio& Get();

	/** Bound to the `FPSR.GunMotionStudio` console command. */
	void Toggle();

	bool IsSessionActive() const { return bActive; }

	// --- Session state the UI layer (FPSRGunMotionStudioUI) reads/drives every frame. All raw pointers below are
	//     re-validated from their TWeakObjectPtr backing every Tick (§5 "전 참조 TWeakObjectPtr, PIE 종료 내성"). ---

	AFPSRCharacter* GetCharacter() const { return CachedCharacter.Get(); }
	UFPSRWeaponDataAsset* GetWeaponData() const { return CachedWeaponData.Get(); }
	UAnimSequence* GetWipClip() const { return WipClip.Get(); }
	UFPSRGunMotionStudioData* GetWipStudioData() const { return WipStudioData.Get(); }
	UAnimMontage* GetWipMontage() const { return WipMontage.Get(); }

	EFPSRGunMotionStudioMode GetMode() const { return Mode; }
	void SetMode(EFPSRGunMotionStudioMode NewMode) { Mode = NewMode; }

	EFPSRGunMotionStatePoseTarget GetStatePoseTarget() const { return StatePoseTarget; }
	/** Switching target reloads StatePoseOffset/StatePoseTilt FROM the DA's current value for that field, so
	 *  re-opening an already-authored pose starts from what is actually saved instead of silently resetting it. */
	void SetStatePoseTarget(EFPSRGunMotionStatePoseTarget NewTarget);

	EFPSRGunMotionStudioSelection GetSelection() const { return Selection; }
	FName GetSelectedPartSocket() const { return SelectedPartSocket; }
	void SetSelection(EFPSRGunMotionStudioSelection NewSelection, FName PartSocket = NAME_None);

	bool bWholeGunModeForced = false; // §5 "무기 전체 모드" toolbar toggle — forces Selection == Gun regardless of pick.

	float GetPlayheadSeconds() const { return PlayheadSeconds; }
	float GetClipLengthSeconds() const;
	/** Scrubs the paused WIP montage to Time (Montage_SetPosition + stays paused, §5). */
	void SetPlayheadSeconds(float Time);

	bool IsPlaying() const { return bIsPlaying; }
	void SetPlaying(bool bPlay);

	/** §5 "키 변경 → WIP 클립 재베이크(§3 전체, transient — ms 단위) → 몽타주 재시작(블렌드 0.05) 후 위치 복원" —
	 *  call after ANY key/span edit (gizmo commit, timeline drag, [IK 붙이기]/[떼기]). */
	void RebakeAndRestartMontage();

	/** §5 [IK 붙이기]: validates AttachSocket exists on the weapon mesh (+ parts), opens an attach span on the
	 *  selected hand starting at the current playhead. Returns false (and logs via OutNotify) on missing socket. */
	bool AttachSelectedHand(FName AttachSocket, FString& OutNotify);
	/** §5 [떼기]: closes the currently-open attach span (if any) on the selected hand at the current playhead. */
	void DetachSelectedHand();
	/** True while the selected hand currently has an open (unterminated) attach span — gates showing [IK 붙이기] vs [떼기]. */
	bool IsSelectedHandAttached() const;

	/** §5 gizmo commit: ApplyDelta is a WORLD-space incremental delta (translation + rotation) already decomposed by
	 *  the UI from the drag-start snapshot; this converts it into the selected track's own space (§5 "공간별
	 *  역산") and writes/updates the key at the current playhead (±1 frame reuses the nearest existing key). */
	void CommitGizmoDelta(const FVector& WorldDeltaTranslation, const FQuat& WorldDeltaRotation);

	/** §5 whole-gun gizmo world transform for the CURRENT selection/mode — used both to seed ImGuizmo's matrix and,
	 *  in §6 state-pose mode, as the [DA에 저장] source. Returns false when nothing valid is selected/attached. */
	bool GetSelectionWorldTransform(FTransform& OutWorld) const;

	/** §6 "[DA에 저장]" — writes GetSelectionWorldTransform's current gizmo pose into the active state-pose target
	 *  field on the weapon DA (FScopedTransaction + MarkPackageDirty). */
	void SaveStatePoseToDataAsset();

	/** §3-1/§5 save flow: opens the engine's own asset-name/path dialog (AnimationEditorUtils::CreateAnimationAssets)
	 *  targeting the arms skeleton, then on confirm re-bakes into the newly created permanent asset and optionally
	 *  assigns it to the chosen weapon DA montage slot (§3-4). bAssignMontage/AssignTarget mirror the save dialog's
	 *  checkbox + combo. */
	void SaveClip(bool bAssignMontage, uint8 AssignTargetIndex, TArray<FString>& OutReport);

	// --- Live M/P (§3-2) and other session-entry caches the baker needs every re-bake. ---
	FQuat GetCachedCamToArmsRotation() const { return CachedCamToArmsRotation; }
	FQuat GetCachedLowerArmCompRotation() const { return CachedLowerArmCompRotation; }
	FTransform GetCachedHandRRefPose() const { return CachedHandRRefPose; }
	FName GetHandRBoneName() const { return HandRBoneName; }

private:
	FFPSRGunMotionStudio() = default;

	void Activate();
	void Deactivate(const TCHAR* Reason);
	bool Tick(float DeltaTime);
	/** Re-validates every weak/cached pointer; returns false (and deactivates) the moment the PIE session this
	 *  studio session is riding on has gone away — §5 "PIE 종료 내성". */
	bool ValidateSessionAlive();

	UAnimInstance* GetArmsAnimInstance() const;
	/** Returns the FFPSRStudioTrack* for the current Selection (Gun/LeftHand/RightHand/Part[SelectedPartSocket]) on
	 *  WipStudioData, creating the PartTracks entry on demand for Part selection. Null for Selection::None. */
	struct FFPSRStudioTrack* GetSelectedTrack() const;
	/** Attach-span array for LeftHand/RightHand selection (null for Gun/Part/None). */
	TArray<struct FFPSRStudioAttachSpan>* GetSelectedHandAttachSpans() const;

	bool bActive = false;
	FTSTicker::FDelegateHandle TickerHandle;

	TWeakObjectPtr<AFPSRCharacter> CachedCharacter;
	TWeakObjectPtr<APlayerController> CachedPlayerController;
	TWeakObjectPtr<UFPSRWeaponDataAsset> CachedWeaponData;

	TWeakObjectPtr<UAnimSequence> WipClip;
	TWeakObjectPtr<UFPSRGunMotionStudioData> WipStudioData;
	TWeakObjectPtr<UAnimMontage> WipMontage;

	EFPSRGunMotionStudioMode Mode = EFPSRGunMotionStudioMode::ActionClip;
	EFPSRGunMotionStatePoseTarget StatePoseTarget = EFPSRGunMotionStatePoseTarget::Slide;
	EFPSRGunMotionStudioSelection Selection = EFPSRGunMotionStudioSelection::None;
	FName SelectedPartSocket = NAME_None;

	// --- §6 state-pose mode edit buffer (session-local, relative to the weapon's own base rotation — "그립 기준" —
	//     NOT a WipStudioData key: state poses have no runtime consumer in THIS spec's scope, §6, so there is nothing
	//     to bake/preview beyond this buffer + a direct DA write on [DA에 저장]). ---
	FVector StatePoseOffset = FVector::ZeroVector;
	FRotator StatePoseTilt = FRotator::ZeroRotator;

	float PlayheadSeconds = 0.0f;
	bool bIsPlaying = false;

	// --- Restore-on-deactivate cache. ---
	bool bWasGamePausedBeforeSession = false;
	bool bWasShowingMouseCursorBeforeSession = false;

	// --- §3-2 live session-entry cache. ---
	FQuat CachedCamToArmsRotation = FQuat::Identity;
	FQuat CachedLowerArmCompRotation = FQuat::Identity;
	FTransform CachedHandRRefPose = FTransform::Identity;
	FName HandRBoneName = FName(TEXT("hand_r"));
	FName LowerArmBoneName = FName(TEXT("lowerarm_r"));

	/** Slot the WIP clip plays on as a dynamic montage (§5) — the arms AnimBP's default Slot node name. */
	FName StudioSlotName = FName(TEXT("DefaultSlot"));
};
