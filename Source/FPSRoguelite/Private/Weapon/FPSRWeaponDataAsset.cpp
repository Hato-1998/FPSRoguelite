// Copyright Epic Games, Inc. All Rights Reserved.

#include "Weapon/FPSRWeaponDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "AbilitySystem/Abilities/FPSRGA_WeaponFire_Projectile.h"
#include "Weapon/FPSRWeaponFragment.h"

#define LOCTEXT_NAMESPACE "FPSRWeaponDataAsset"

namespace
{
	/** Does this mesh expose the named attachment point? Runtime attach (UPrimitiveComponent::DoesSocketExist) treats
	 *  BONES and SOCKETS alike, and this pack names many attach bones "SOCKET_*", so we accept either. NAME_None is the
	 *  component root (always valid). A null/unloaded mesh returns true so validation never false-positives on a soft
	 *  ref that simply isn't resolvable here. */
	bool SkeletalMeshHasAttachPoint(const USkeletalMesh* Mesh, FName Name)
	{
		if (Name.IsNone() || Mesh == nullptr)
		{
			return true;
		}
		if (Mesh->FindSocket(Name) != nullptr)
		{
			return true;
		}
		return Mesh->GetRefSkeleton().FindBoneIndex(Name) != INDEX_NONE;
	}
}

EDataValidationResult UFPSRWeaponDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// Every weapon fires by activating its FireAbility — a missing one means the weapon does nothing.
	if (!FireAbility)
	{
		Context.AddError(LOCTEXT("NoFireAbility", "Weapon has no FireAbility — it will never fire. Assign the matching GA (Hitscan / Projectile / ChargeLaser / Melee)."));
		Result = EDataValidationResult::Invalid;
	}

	const EFPSRWeaponArchetype Archetype = BaseStats.Archetype;

	// AOE archetype with no blast radius silently degrades to a single-target projectile (no explosion).
	if (Archetype == EFPSRWeaponArchetype::AOE && BaseStats.AOERadius <= 0.0f)
	{
		Context.AddWarning(LOCTEXT("AOENoRadius", "AOE weapon has AOERadius <= 0 — it will only hit a single target (no explosion). Set AOERadius > 0 for a blast."));
	}

	// ChargeLaser with ChargeTime 0 fires the full-power beam instantly on click (no warm-up sequence).
	if (Archetype == EFPSRWeaponArchetype::ChargeLaser && BaseStats.ChargeTime <= 0.0f)
	{
		Context.AddWarning(LOCTEXT("ChargeLaserNoChargeTime", "ChargeLaser has ChargeTime <= 0 — a click fires the full-power beam instantly with no warm-up. Set ChargeTime > 0 for a charge-up sequence."));
	}

	// Ranged weapons consume ammo; a 0 magazine can never fire past the empty-mag gate.
	if (Archetype != EFPSRWeaponArchetype::Melee && BaseStats.MagSize <= 0)
	{
		Context.AddWarning(LOCTEXT("NoMagSize", "Ranged weapon has MagSize <= 0 — it will be permanently empty and unable to fire. Set MagSize > 0."));
	}

	// --- "Dead value" sanity (P0 data-validation seam): archetype-gated stat fields that are structurally required
	//     for that archetype to do ANYTHING. These are ERRORS (not warnings) — unlike the softer archetype-mismatch
	//     warnings above (AOE radius / charge time / mag size, which degrade to a lesser-but-functional weapon), a
	//     zero here means the weapon cannot function as its declared archetype at all. ---
	if (BaseStats.Damage <= 0.0f)
	{
		Context.AddError(LOCTEXT("NoDamage", "Weapon has Damage <= 0 — every hit will deal zero damage. Set Damage > 0."));
		Result = EDataValidationResult::Invalid;
	}
	if (BaseStats.FireRate <= 0.0f)
	{
		Context.AddError(LOCTEXT("NoFireRate", "Weapon has FireRate <= 0 — the fire loop's rate-of-fire timer would never re-arm. Set FireRate > 0."));
		Result = EDataValidationResult::Invalid;
	}
	if (Archetype != EFPSRWeaponArchetype::Melee)
	{
		if (BaseStats.MagSize <= 0)
		{
			Context.AddError(LOCTEXT("MagSizeNotPositive", "Non-melee weapon has MagSize <= 0 — it can never load a round. Set MagSize > 0 (or switch Archetype to Melee)."));
			Result = EDataValidationResult::Invalid;
		}
		if (BaseStats.ReloadTime <= 0.0f)
		{
			Context.AddError(LOCTEXT("ReloadTimeNotPositive", "Non-melee weapon has ReloadTime <= 0 — the reload timer would never complete. Set ReloadTime > 0."));
			Result = EDataValidationResult::Invalid;
		}
	}
	if (Archetype == EFPSRWeaponArchetype::Shotgun && BaseStats.PelletCount <= 0)
	{
		Context.AddError(LOCTEXT("PelletCountNotPositive", "Shotgun has PelletCount <= 0 — firing would spawn no pellets at all. Set PelletCount > 0."));
		Result = EDataValidationResult::Invalid;
	}
	// Projectile weapons are identified by their FireAbility (the Projectile GA), NOT the archetype — a FullAuto rifle
	// or a Shotgun can now fire projectiles. Validate the projectile stats whenever the weapon uses the Projectile GA.
	const bool bIsProjectileWeapon = FireAbility && FireAbility->IsChildOf(UFPSRGA_WeaponFire_Projectile::StaticClass());
	if (bIsProjectileWeapon)
	{
		if (BaseStats.ProjectileSpeed <= 0.0f)
		{
			Context.AddError(LOCTEXT("ProjectileSpeedNotPositive", "Projectile weapon has ProjectileSpeed <= 0 — the projectile would never travel toward its target. Set ProjectileSpeed > 0."));
			Result = EDataValidationResult::Invalid;
		}
		if (BaseStats.ProjectileLifetime <= 0.0f)
		{
			Context.AddError(LOCTEXT("ProjectileLifetimeNotPositive", "Projectile weapon has ProjectileLifetime <= 0 — the projectile would auto-release before it can ever hit anything. Set ProjectileLifetime > 0."));
			Result = EDataValidationResult::Invalid;
		}
		// Replicated-projectile budget guard (Game.MD §5, cap 64 with FIFO eviction). Peak in-flight per player for
		// a sustained-fire projectile weapon ≈ FireRate × Lifetime × PelletCount. Warn when that alone could crowd the
		// shared player cap (×4 players): a fast-firing weapon MUST use a SHORT lifetime + high speed so its rounds
		// clear quickly. (The default Lifetime 5s is fine for a slow launcher but far too long for a rifle.)
		const int32 Pellets = FMath::Max(1, BaseStats.PelletCount);
		const float PeakInFlight = BaseStats.FireRate * FMath::Max(0.0f, BaseStats.ProjectileLifetime) * Pellets;
		if (PeakInFlight > 12.0f)
		{
			Context.AddWarning(FText::Format(LOCTEXT("ProjectileBudgetHigh",
				"Projectile weapon's estimated peak in-flight rounds (FireRate {0} × Lifetime {1}s × Pellets {2} ≈ {3}) is high — with 4 players this can hit the ≤64 replicated-projectile cap and FIFO-evict teammates' projectiles (Game.MD §5). Shorten ProjectileLifetime and/or raise ProjectileSpeed so rounds clear fast."),
				FText::AsNumber(BaseStats.FireRate), FText::AsNumber(BaseStats.ProjectileLifetime),
				FText::AsNumber(Pellets), FText::AsNumber(FMath::RoundToInt(PeakInFlight))));
		}
	}
	if (Archetype == EFPSRWeaponArchetype::Melee)
	{
		if (BaseStats.MeleeRadius <= 0.0f)
		{
			Context.AddError(LOCTEXT("MeleeRadiusNotPositive", "Melee weapon has MeleeRadius <= 0 — the attack's hit sweep would find nothing. Set MeleeRadius > 0."));
			Result = EDataValidationResult::Invalid;
		}
		if (BaseStats.MeleeAttackDelay <= 0.0f)
		{
			Context.AddError(LOCTEXT("MeleeAttackDelayNotPositive", "Melee weapon has MeleeAttackDelay <= 0 — rapid clicks would never be rate-limited (or the attack loop may never re-arm, depending on the ability). Set MeleeAttackDelay > 0."));
			Result = EDataValidationResult::Invalid;
		}
	}

	// --- 확산 heat 프로파일(P2): 3곡선은 all-or-nothing이다. 부분 저작(예: ShotToHeat만 있고 HeatToCooldown 없음)은
	//     heat가 냉각 없이 상한에 붙어 확산이 최대로 고정되는 침묵 버그를 낳는다. 근접/ChargeLaser는 3곡선을 모두
	//     비워 두는 것이 정상(동적 블룸 없음). FRuntimeFloatCurve::GetRichCurveConst()는 빈 곡선도 non-null이므로
	//     키 개수로 저작 여부를 판정한다. ---
	{
		const int32 ShotKeys = ShotToHeatCurve.GetRichCurveConst()->GetNumKeys();
		const int32 SpreadKeys = HeatToSpreadAngleCurve.GetRichCurveConst()->GetNumKeys();
		const int32 CoolKeys = HeatToCooldownPerSecondCurve.GetRichCurveConst()->GetNumKeys();
		const int32 Authored = (ShotKeys > 0 ? 1 : 0) + (SpreadKeys > 0 ? 1 : 0) + (CoolKeys > 0 ? 1 : 0);
		if (Authored != 0 && Authored != 3)
		{
			Context.AddError(LOCTEXT("HeatProfilePartial", "확산 heat 프로파일은 3개 곡선(ShotToHeat / HeatToSpreadAngle / HeatToCooldownPerSecond)을 전부 채우거나 전부 비워야 합니다 — 일부만 채우면 heat가 냉각 없이 상한에 붙어 확산이 고장납니다."));
			Result = EDataValidationResult::Invalid;
		}
		if (Authored == 3)
		{
			if (MaxRecoilHeat <= 0.0f)
			{
				Context.AddError(LOCTEXT("HeatProfileMaxZero", "확산 heat 프로파일이 있는데 MaxRecoilHeat <= 0 — heat가 항상 0으로 클램프되어 동적 확산이 발생하지 않습니다. MaxRecoilHeat > 0 으로 설정하세요."));
				Result = EDataValidationResult::Invalid;
			}
			// heat=0 → spread=0 앵커: 무heat는 순수 base SpreadDegrees여야 한다(아니면 base 이중 계산).
			const float SpreadAtZero = HeatToSpreadAngleCurve.GetRichCurveConst()->Eval(0.0f);
			if (FMath::Abs(SpreadAtZero) > KINDA_SMALL_NUMBER)
			{
				Context.AddWarning(FText::Format(LOCTEXT("HeatProfileSpreadAnchor", "HeatToSpreadAngleCurve의 heat=0 값이 {0}° (0이 아님) — 발사 전에도 base SpreadDegrees에 더해져 확산이 넓어집니다. heat=0 → 0° 키를 앵커로 두세요."), FText::AsNumber(SpreadAtZero)));
			}
			// 음수 냉각은 heat를 오히려 증가시켜 폭주. 정의역을 샘플링 검사.
			const FRichCurve* CoolCurve = HeatToCooldownPerSecondCurve.GetRichCurveConst();
			bool bNegCooldown = false;
			const float Step = FMath::Max(1.0f, MaxRecoilHeat * 0.1f);
			for (float H = 0.0f; H <= MaxRecoilHeat + KINDA_SMALL_NUMBER; H += Step)
			{
				if (CoolCurve->Eval(H) < 0.0f) { bNegCooldown = true; break; }
			}
			if (bNegCooldown)
			{
				Context.AddError(LOCTEXT("HeatProfileNegCooldown", "HeatToCooldownPerSecondCurve가 음수 값을 가집니다 — 냉각이 heat를 오히려 증가시켜 확산이 폭주합니다. 전 구간 >= 0(권장 > 0)으로 설정하세요."));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	// --- Socket / attach-point validation. A referenced socket that doesn't exist on the target mesh (a typo such as a
	//     space instead of '_', or the wrong socket) makes the part / muzzle flash silently fall back to the mesh
	//     origin — the exact class of bug that is invisible until you look at the assembled weapon in-game. Warnings
	//     (not errors): the game still runs, but the visual is wrong. ---
	USkeletalMesh* SkelWeapon = WeaponMesh1P.IsNull() ? nullptr : WeaponMesh1P.LoadSynchronous();

	// Modular cosmetic parts attach to the skeletal weapon mesh (WeaponMesh1P) at their Socket.
	for (const FFPSRWeaponPartAttachment& Part : WeaponParts1P)
	{
		if (Part.Part.IsNull() || Part.Socket.IsNone())
		{
			continue;
		}
		if (SkelWeapon && !SkeletalMeshHasAttachPoint(SkelWeapon, Part.Socket))
		{
			Context.AddWarning(FText::Format(LOCTEXT("PartSocketMissing",
				"파츠 슬롯 '{0}' references socket '{1}' that does not exist on WeaponMesh1P — it will attach at the mesh origin. Check for a typo (e.g. a space instead of '_')."),
				FText::FromString(Part.Part.GetAssetName()), FText::FromName(Part.Socket)));
		}
	}

	// --- W-U1b 파츠별 스택 진화 검증(폴리모픽 PartRules 대체). 단계 메시는 슬롯의 고정 소켓(Entry.Socket)에 그대로
	//     붙으므로 별도 소켓검사는 불필요하다 — 위 WeaponParts1P 소켓검사가 슬롯당 1회로 이미 커버한다. Stages 항목은
	//     '데이터'일 뿐이라 순수 struct — 조건 없이 MinStacks 하나로만 승자가 갈린다(§2-A: 파츠는 스택을 읽기만 함). ---
	for (int32 i = 0; i < WeaponParts1P.Num(); ++i)
	{
		const FFPSRWeaponPartAttachment& Entry = WeaponParts1P[i];
		if (!Entry.EvolutionFragment.IsNull() && Entry.Stages.Num() == 0)
		{
			Context.AddWarning(FText::Format(LOCTEXT("PartStageNoStages",
				"파츠 슬롯 [{0}]에 진화 프래그먼트가 지정됐지만 진화 단계(Stages)가 비어 있습니다 — 항상 기본 파츠만 표시됩니다."),
				FText::AsNumber(i)));
		}

		UFPSRWeaponFragment* Frag = Entry.EvolutionFragment.LoadSynchronous();
		TArray<int32> SeenMinStacks;
		for (int32 s = 0; s < Entry.Stages.Num(); ++s)
		{
			const FFPSRWeaponPartStage& Stage = Entry.Stages[s];
			if (Stage.MinStacks < 1)
			{
				Context.AddError(FText::Format(LOCTEXT("PartStageMinStacksInvalid",
					"파츠 슬롯 [{0}] 단계 [{1}]의 필요 스택이 {2} — 1 이상이어야 합니다(0/음수는 기본 파츠를 항상 가립니다)."),
					FText::AsNumber(i), FText::AsNumber(s), FText::AsNumber(Stage.MinStacks)));
				Result = EDataValidationResult::Invalid;
			}
			else if (SeenMinStacks.Contains(Stage.MinStacks))
			{
				Context.AddError(FText::Format(LOCTEXT("PartStageDuplicateMinStacks",
					"파츠 슬롯 [{0}]에 필요 스택 {1}인 단계가 중복됩니다 — 승자가 결정되지 않습니다. 하나만 남기거나 필요 스택을 다르게 하세요."),
					FText::AsNumber(i), FText::AsNumber(Stage.MinStacks)));
				Result = EDataValidationResult::Invalid;
			}
			else
			{
				SeenMinStacks.Add(Stage.MinStacks);
			}

			if (Stage.Mesh.IsNull())
			{
				Context.AddWarning(FText::Format(LOCTEXT("PartStageMeshMissing",
					"파츠 슬롯 [{0}] 단계 [{1}]에 메시가 없습니다 — 이 단계가 선택되면 파츠가 사라집니다."),
					FText::AsNumber(i), FText::AsNumber(s)));
			}

			if (Frag && Stage.MinStacks > Frag->MaxStacks)
			{
				Context.AddWarning(FText::Format(LOCTEXT("PartStageUnreachable",
					"파츠 슬롯 [{0}] 단계 [{1}]의 필요 스택({2})이 프래그먼트 '{3}'의 MaxStacks({4})보다 큽니다 — 이 단계는 절대 도달할 수 없습니다."),
					FText::AsNumber(i), FText::AsNumber(s), FText::AsNumber(Stage.MinStacks),
					Frag->DisplayName, FText::AsNumber(Frag->MaxStacks)));
			}
		}
	}

	// 소켓 중복: 서로 다른 슬롯이 같은(None 아닌) 소켓을 쓰면 두 파츠가 같은 지점에 겹쳐 붙는다.
	for (int32 i = 0; i < WeaponParts1P.Num(); ++i)
	{
		if (WeaponParts1P[i].Socket.IsNone())
		{
			continue;
		}
		for (int32 j = i + 1; j < WeaponParts1P.Num(); ++j)
		{
			if (WeaponParts1P[j].Socket == WeaponParts1P[i].Socket)
			{
				Context.AddError(FText::Format(LOCTEXT("PartSlotDuplicateSocket",
					"파츠 슬롯 [{0}]과 [{1}]이 같은 소켓 '{2}'을 사용합니다 — 두 파츠가 같은 지점에 겹쳐 붙습니다. 서로 다른 소켓을 지정하세요."),
					FText::AsNumber(i), FText::AsNumber(j), FText::FromName(WeaponParts1P[i].Socket)));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	// The muzzle socket may live on the weapon mesh OR on a modular part (barrel / forestock carry SOCKET_Muzzle in
	// this pack). Only validate for firearms (a skeletal weapon mesh or at least one part is present).
	if (!MuzzleSocket.IsNone() && (SkelWeapon != nullptr || WeaponParts1P.Num() > 0))
	{
		bool bMuzzleFound = SkeletalMeshHasAttachPoint(SkelWeapon, MuzzleSocket) && SkelWeapon != nullptr;
		for (const FFPSRWeaponPartAttachment& Part : WeaponParts1P)
		{
			if (bMuzzleFound)
			{
				break;
			}
			if (!Part.Part.IsNull())
			{
				if (const UStaticMesh* PartMesh = Part.Part.LoadSynchronous())
				{
					bMuzzleFound = PartMesh->FindSocket(MuzzleSocket) != nullptr;
				}
			}
			// A muzzle-carrying variant may only appear once the slot has evolved (e.g. an evolved barrel stage) —
			// scan the slot's stage meshes too so the search matches SelectParts' actual resolved output.
			for (const FFPSRWeaponPartStage& Stage : Part.Stages)
			{
				if (bMuzzleFound)
				{
					break;
				}
				if (Stage.Mesh.IsNull())
				{
					continue;
				}
				if (const UStaticMesh* StageMesh = Stage.Mesh.LoadSynchronous())
				{
					bMuzzleFound = StageMesh->FindSocket(MuzzleSocket) != nullptr;
				}
			}
		}
		if (!bMuzzleFound)
		{
			Context.AddWarning(FText::Format(LOCTEXT("MuzzleSocketMissing",
				"MuzzleSocket '{0}' is not found on WeaponMesh1P or any modular part — the muzzle flash will spawn at the mesh origin. Put the muzzle socket on the barrel/forestock part (or the weapon mesh) and check for typos."),
				FText::FromName(MuzzleSocket)));
		}
	}

	// The procedural-ADS AimSocket may live on the weapon receiver OR on a sight part (iron sight / optic), same as the
	// muzzle. A typo makes DoesSocketExist fail at runtime (ADS silently stays at hip). Validate across receiver + parts.
	if (BaseStats.bHasADS && !AimSocket.IsNone() && (SkelWeapon != nullptr || WeaponParts1P.Num() > 0))
	{
		bool bAimFound = SkelWeapon != nullptr && SkeletalMeshHasAttachPoint(SkelWeapon, AimSocket);
		for (const FFPSRWeaponPartAttachment& Part : WeaponParts1P)
		{
			if (bAimFound)
			{
				break;
			}
			if (!Part.Part.IsNull())
			{
				if (const UStaticMesh* PartMesh = Part.Part.LoadSynchronous())
				{
					bAimFound = PartMesh->FindSocket(AimSocket) != nullptr;
				}
			}
			// A sight-carrying variant may only appear once the slot has evolved — scan the slot's stage meshes too
			// so this "does an aim socket exist ANYWHERE" search matches SelectParts' actual resolved output.
			for (const FFPSRWeaponPartStage& Stage : Part.Stages)
			{
				if (bAimFound)
				{
					break;
				}
				if (Stage.Mesh.IsNull())
				{
					continue;
				}
				if (const UStaticMesh* StageMesh = Stage.Mesh.LoadSynchronous())
				{
					bAimFound = StageMesh->FindSocket(AimSocket) != nullptr;
				}
			}
		}
		if (!bAimFound)
		{
			Context.AddWarning(FText::Format(LOCTEXT("AimSocketMissing",
				"AimSocket '{0}' is not found on WeaponMesh1P or any modular part — procedural ADS will not align (it stays at hip). Put the aim socket on the sight part (iron sight / optic) or the weapon mesh (+X forward, +Z up), and check for typos (e.g. a space instead of '_')."),
				FText::FromName(AimSocket)));
		}
	}

	// --- W-U1b 조준감 회귀 차단(중요): 슬롯이 진화로 사이트가 '될 수 있으면'(베이스 또는 임의 단계 메시가 AimSocket을
	//     보유하거나, 베이스/단계 스코프 중 하나라도 bScopeOverlay=true) 베이스 + 모든 non-null 단계 메시가 전부
	//     AimSocket을 보유해야 한다 — 하나라도 없으면 그 스택으로 진화한 순간 조준이 힙으로 회귀한다(ERROR). ---
	if (BaseStats.bHasADS && !AimSocket.IsNone())
	{
		for (int32 i = 0; i < WeaponParts1P.Num(); ++i)
		{
			const FFPSRWeaponPartAttachment& Entry = WeaponParts1P[i];
			const UStaticMesh* BaseMesh = Entry.Part.IsNull() ? nullptr : Entry.Part.LoadSynchronous();

			bool bMightBeSight = Entry.Scope.bScopeOverlay || (BaseMesh && BaseMesh->FindSocket(AimSocket) != nullptr);
			for (int32 s = 0; !bMightBeSight && s < Entry.Stages.Num(); ++s)
			{
				const FFPSRWeaponPartStage& Stage = Entry.Stages[s];
				if (Stage.Scope.bScopeOverlay)
				{
					bMightBeSight = true;
					break;
				}
				const UStaticMesh* StageMesh = Stage.Mesh.IsNull() ? nullptr : Stage.Mesh.LoadSynchronous();
				if (StageMesh && StageMesh->FindSocket(AimSocket) != nullptr)
				{
					bMightBeSight = true;
				}
			}
			if (!bMightBeSight)
			{
				continue;
			}

			bool bAllVariantsHaveAim = (BaseMesh == nullptr) || BaseMesh->FindSocket(AimSocket) != nullptr;
			for (int32 s = 0; bAllVariantsHaveAim && s < Entry.Stages.Num(); ++s)
			{
				const UStaticMesh* StageMesh = Entry.Stages[s].Mesh.IsNull() ? nullptr : Entry.Stages[s].Mesh.LoadSynchronous();
				if (StageMesh == nullptr)
				{
					continue; // null stage mesh already warned separately (PartStageMeshMissing)
				}
				bAllVariantsHaveAim = StageMesh->FindSocket(AimSocket) != nullptr;
			}
			if (!bAllVariantsHaveAim)
			{
				Context.AddError(FText::Format(LOCTEXT("PartStageAimSocketRegression",
					"파츠 슬롯 [{0}]이 진화로 사이트가 될 수 있는데(스코프 오버레이 또는 AimSocket 보유) 일부 단계 메시에 AimSocket '{1}'이 없습니다 — 그 단계로 진화하면 조준이 힙으로 회귀합니다. 모든 단계 메시에 같은 AimSocket을 부여하세요."),
					FText::AsNumber(i), FText::FromName(AimSocket)));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	// W-U2: a scope-overlay sight only ever activates through ADS (the scope shows while aiming with that sight active).
	// A scope part (base OR any evolution stage) on a weapon with no ADS / no AimSocket can never trigger — warn.
	for (int32 i = 0; i < WeaponParts1P.Num(); ++i)
	{
		const FFPSRWeaponPartAttachment& Part = WeaponParts1P[i];
		bool bHasScope = Part.Scope.bScopeOverlay;
		if (!bHasScope)
		{
			for (const FFPSRWeaponPartStage& Stage : Part.Stages)
			{
				if (Stage.Scope.bScopeOverlay)
				{
					bHasScope = true;
					break;
				}
			}
		}
		if (!bHasScope)
		{
			continue;
		}
		if (!BaseStats.bHasADS || AimSocket.IsNone())
		{
			Context.AddWarning(FText::Format(LOCTEXT("ScopeNoADS",
				"파츠 슬롯 [{0}](기본 또는 진화 단계)이 스코프 오버레이를 사용하지만, 무기에 ADS/AimSocket이 없어 스코프가 절대 켜지지 않습니다 — 스코프는 그 사이트로 조준할 때만 활성화됩니다. BaseStats.bHasADS와 AimSocket을 설정하세요."),
				FText::AsNumber(i)));
		}
	}

	// The fire-part recoil bone (bolt / charging handle driven by UFPSRWeaponAnimInstance) must exist on the weapon mesh
	// — a typo means the AnimBP's ModifyBone targets nothing and the part never kicks. Only validate when a bone is set
	// (None = no moving part = no-op).
	if (!FirePartRecoilBone.IsNone() && SkelWeapon != nullptr && !SkeletalMeshHasAttachPoint(SkelWeapon, FirePartRecoilBone))
	{
		Context.AddWarning(FText::Format(LOCTEXT("FirePartRecoilBoneMissing",
			"FirePartRecoilBone '{0}' does not exist on WeaponMesh1P — the fire-part recoil (bolt / charging handle) will drive nothing. Check the bone name for typos."),
			FText::FromName(FirePartRecoilBone)));
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
