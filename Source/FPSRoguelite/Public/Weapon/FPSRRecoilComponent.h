// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/CRRecoilSpreadComponent.h"
#include "Curves/CurveFloat.h"
#include "FPSRRecoilComponent.generated.h"

/**
 * Project recoil component (P1, CrystalRecoil adapter). Subclasses the plugin's pattern + heat-spread component so the
 * plugin owns the recoil KINEMATICS (uplift / recovery / player-input compensation) and the heat-spread model, while we
 * inject our run-state GATING through the ProcessDelta* hooks — the plugin's tick must not drift the camera during the
 * card-selection freeze (Game.MD §2-2) or while the owner is downed/dead (DBNO, U9).
 *
 * The owning UFPSRWeaponFireComponent drives it (owner-local): SetRecoilPattern on equip, StartShooting on trigger
 * press, ApplyShot per fired round, SetRecoilStrength for ADS scale + recoil-down cards. The plugin applies recoil to
 * the CONTROLLER's control rotation (ApplyInputToController → SetControlRotation), so the server's camera-viewpoint
 * fire trace stays consistent with the on-screen recoil — same authority model as the legacy procedural recoil.
 *
 * ChargeLaser keeps its own bespoke charge-ramp recoil in the fire component (no ApplyShot), so this component stays
 * idle for it. Melee has no recoil pattern.
 */
UCLASS(ClassGroup = (FPSR), meta = (BlueprintSpawnableComponent))
class FPSROGUELITE_API UFPSRRecoilComponent : public UCRRecoilSpreadComponent
{
	GENERATED_BODY()

public:
	/** The plugin's spread subclass narrows ApplyShot() to protected; re-expose it as public so the owning fire
	 *  component can drive each fired round. Virtual dispatch still runs the spread override (recoil uplift + heat). */
	using UCRRecoilSpreadComponent::ApplyShot;

	/** 장착 무기의 heat-확산 프로파일(곡선3 + 상한 + 냉각지연)을 컴포넌트에 주입(장착 시). 플러그인의 곡선은 컴포넌트
	 *  프로퍼티라 무기별 차등을 위해 매 장착 밀어넣는다. */
	void SetSpreadProfile(const FRuntimeFloatCurve& InShotToHeat, const FRuntimeFloatCurve& InHeatToSpread,
		const FRuntimeFloatCurve& InHeatToCooldown, float InMaxHeat, float InCooldownDelay);

	/** heat-확산 프로파일을 비운다(무프로파일 무기로 스왑 시). 플러그인 SetRecoilPattern(null)이 무시되는 것과 달리
	 *  곡선은 반드시 명시적으로 비워야 이전 무기 곡선이 새 무기로 새지 않는다. */
	void ClearSpreadProfile();

	/** 3곡선이 모두 저작(키>0)됐는지 — 동적 확산 활성 여부. 빈 FRuntimeFloatCurve도 GetRichCurveConst()는 non-null
	 *  이므로 ReadyToCalculateRecoil()로는 판정 불가, 키 개수로 판정한다. */
	bool HasSpreadCurves() const;

	/** 확산 단일 판독점: 프로파일이 저작됐으면 GetCurrentSpreadAngle()(heat→도), 아니면 0(무bloom, 순수 base).
	 *  HUD·발사 GA가 이것만 읽는다(base GetCurrentSpreadAngle 직접 호출 금지). */
	float GetHeatSpread() const;

	/** 누적 heat를 0으로(무기 스왑 시 새 무기가 차갑게 시작). */
	void ResetHeat();

	/** 반동 스프레이 패턴을 첫 발(ShotIndex 0)로 되돌린다(예: 재장전 시 새 탄창이 초반 패턴부터 다시 뿌리도록).
	 *  진행 중 uplift/recovery·heat는 건드리지 않음. */
	void ResetPattern();

	/** 서버 accepted-shot heat 갱신(발사 GA 서버 브랜치에서 원격 폰에만 호출) — 서버 authoritative 트레이스 확산이
	 *  원격 클라의 예측 확산과 일치하도록. heat 누적 + LastFireTime 갱신(grace 후 냉각) + tick 활성화까지 처리. */
	void AdvanceHeatForAcceptedShot();

protected:
	/** Skip applying the uplift delta while recoil is suppressed (freeze / downed). Base default returns true. */
	virtual bool ProcessDeltaRecoilRotation(FRotator& DeltaRecoilRotation) override;

	/** Skip applying the recovery delta while recoil is suppressed — otherwise auto-recovery would slowly pull the
	 *  view during the card-selection freeze or while downed. */
	virtual bool ProcessDeltaRecoveryRotation(FRotator& DeltaRecoveryRotation) override;

	/** True when recoil + recovery must be suppressed this frame: the global run is frozen for card selection, or the
	 *  owning player is not alive (DBNO / dead). Mirrors the fire component's own fire gates so the two never disagree. */
	bool IsRecoilSuppressed() const;
};
