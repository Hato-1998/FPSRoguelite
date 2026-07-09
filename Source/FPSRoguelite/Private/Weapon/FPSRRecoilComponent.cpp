// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRRecoilComponent.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"

#include "GameFramework/Pawn.h"
#include "Engine/World.h"

bool UFPSRRecoilComponent::IsRecoilSuppressed() const
{
	// Global run-freeze (card selection): no camera drift while the world is paused for a pick (Game.MD §2-2).
	if (const UWorld* World = GetWorld())
	{
		if (const AFPSRGameState* GS = World->GetGameState<AFPSRGameState>())
		{
			if (GS->IsRunPaused())
			{
				return true;
			}
		}
	}
	// Not alive (DBNO downed / dead): recoil + recovery are suppressed, mirroring the fire input/GA alive-gate (U9).
	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		if (const AFPSRPlayerState* PS = OwnerPawn->GetPlayerState<AFPSRPlayerState>())
		{
			if (!PS->IsAlive())
			{
				return true;
			}
		}
	}
	return false;
}

bool UFPSRRecoilComponent::ProcessDeltaRecoilRotation(FRotator& DeltaRecoilRotation)
{
	// false = the plugin skips applying this uplift delta this frame (base default is true).
	return !IsRecoilSuppressed();
}

bool UFPSRRecoilComponent::ProcessDeltaRecoveryRotation(FRotator& DeltaRecoveryRotation)
{
	return !IsRecoilSuppressed();
}

void UFPSRRecoilComponent::SetSpreadProfile(const FRuntimeFloatCurve& InShotToHeat, const FRuntimeFloatCurve& InHeatToSpread,
	const FRuntimeFloatCurve& InHeatToCooldown, float InMaxHeat, float InCooldownDelay)
{
	// 곡선은 UCRRecoilSpreadComponent의 protected 프로퍼티 — 서브클래스에서 값 대입(FRuntimeFloatCurve 복사).
	ShotToHeatCurve = InShotToHeat;
	HeatToSpreadAngleCurve = InHeatToSpread;
	HeatToCooldownPerSecondCurve = InHeatToCooldown;
	SetMaxRecoilHeat(InMaxHeat);                 // public 세터(>=0 클램프)
	SetRecoilHeatCoolDownDelay(InCooldownDelay); // public 세터
}

void UFPSRRecoilComponent::ClearSpreadProfile()
{
	// 기본 생성 곡선(키 0개, ExternalCurve null) 대입 → HasSpreadCurves()=false → 동적 확산 비활성.
	ShotToHeatCurve = FRuntimeFloatCurve();
	HeatToSpreadAngleCurve = FRuntimeFloatCurve();
	HeatToCooldownPerSecondCurve = FRuntimeFloatCurve();
}

bool UFPSRRecoilComponent::HasSpreadCurves() const
{
	return ShotToHeatCurve.GetRichCurveConst()->GetNumKeys() > 0
		&& HeatToSpreadAngleCurve.GetRichCurveConst()->GetNumKeys() > 0
		&& HeatToCooldownPerSecondCurve.GetRichCurveConst()->GetNumKeys() > 0;
}

float UFPSRRecoilComponent::GetHeatSpread() const
{
	return HasSpreadCurves() ? GetCurrentSpreadAngle() : 0.0f;
}

void UFPSRRecoilComponent::ResetHeat()
{
	SetRecoilHeat(0.0f); // protected(UCRRecoilSpreadComponent) — 서브클래스 접근
}

void UFPSRRecoilComponent::ResetPattern()
{
	// 스프레이를 첫 발로 되돌린다(재장전 시 새 탄창이 초반 패턴부터). 진행 중 uplift/recovery·heat는 불변.
	// CurrentShotIndex는 UCRRecoilComponent-protected → 서브클래스 접근. 다음 ApplyShot의 ConsumeShot이 0부터 소비.
	CurrentShotIndex = 0;
}

void UFPSRRecoilComponent::ClearRecoilPattern()
{
	// RecoilPattern은 UCRRecoilComponent-protected. 플러그인 SetRecoilPattern은 null을 무시하므로 여기서 직접 해제 →
	// base ApplyShot의 `!RecoilPattern` early-return이 살아 이전 무기 패턴의 stale 킥을 막는다.
	RecoilPattern = nullptr;
}

void UFPSRRecoilComponent::AdvanceHeatForAcceptedShot()
{
	if (!HasSpreadCurves())
	{
		return; // 동적 확산 없는 무기(근접/ChargeLaser/미저작): 서버 heat 갱신 불필요
	}
	// 읽고-나서-누적: 발사 GA는 이 호출 전에 GetHeatSpread()로 트레이스 확산을 이미 읽었다(클라의
	// GA-reads-before-ApplyShot 위상과 동일). ShotToHeat는 X=현재 heat.
	const float Add = ShotToHeatCurve.GetRichCurveConst()->Eval(CurrentRecoilHeat);
	SetRecoilHeat(CurrentRecoilHeat + Add); // 클램프[0,Max] + OnHeatChanged 브로드캐스트
	if (const UWorld* World = GetWorld())
	{
		LastFireTime = World->GetTimeSeconds(); // UCRRecoilComponent protected — 서버측 grace 냉각이 클라와 대칭
	}
	SetComponentTickEnabled(true); // SetRecoilHeat만으론 tick 재활성 안 됨 → 스프레드 TickComponent의 냉각 구동 보장
}
