// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "FPSRWeaponDataAsset.generated.h"

class UGameplayAbility;
class UFPSRCardDataAsset;
class AFPSRProjectile;
class USkeletalMesh;
class UStaticMesh;
class UAnimInstance;
class UAnimMontage;
class USoundBase;
class UParticleSystem;
class UTexture2D;
class UMaterialInterface;

/** One modular cosmetic part attached to the 1P skeletal weapon mesh at a named socket (U15). Purely visual: the
 *  part is a static mesh (barrel / forestock / magazine / sight from the pack) child-attached to the equipped
 *  skeletal weapon. Null Part = skipped (null-safe). Static/melee weapons and empty lists attach nothing. */
USTRUCT(BlueprintType)
struct FFPSRWeaponPartAttachment
{
	GENERATED_BODY()

	/** Static mesh of the part (soft ref; null = this entry is skipped). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<UStaticMesh> Part;

	/** Socket on the WEAPON mesh (SKEL_LPAMG_<W>) the part attaches to. NAME_None = weapon mesh root. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	FName Socket = NAME_None;

	/** Relative transform applied after attach (fine-tune the part's placement on the socket). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	FTransform Offset;
};

/** Data-driven weapon definition. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRWeaponDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FText DisplayName;

	/** Weapon archetype now lives in BaseStats so per-archetype stat fields can drive EditCondition visibility. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	EFPSRWeaponArchetype GetArchetype() const { return BaseStats.Archetype; }

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	FFPSRWeaponStatBlock BaseStats;

	/** Ability granted while this weapon is equipped (activated by the Fire input). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon")
	TSubclassOf<UGameplayAbility> FireAbility;

	/** Projectile actor class (AOE archetypes). Content assigns a BP with mesh/VFX; null falls back to AFPSRProjectile base. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Projectile")
	TSubclassOf<AFPSRProjectile> ProjectileClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Cards")
	TArray<TObjectPtr<UFPSRCardDataAsset>> WeaponCards;

	/** Feature-unlock cards (U18b): locked capabilities offered as weapon-unlock candidates on mission clear /
	 *  level milestones (reuse WeaponBehavior/WeaponStat effects). Behavior features are stack-gated by MaxStacks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Cards")
	TArray<TObjectPtr<UFPSRCardDataAsset>> UnlockableFeatures;

	/** Max number of DISTINCT behavior fragments this weapon can hold (U6). Reaching it makes a further new-fragment
	 *  pick a REPLACEMENT (drop one equipped fragment for the new one); stacking an already-held fragment is governed
	 *  by the fragment's own MaxStacks, not this cap. Per-weapon so e.g. a simple sidearm can allow fewer than a rifle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Cards", meta = (ClampMin = "1"))
	int32 MaxFragmentSlots = 3;

	/** DEPRECATED (U18b): was the mission-reward fragment pool. Fragments moved to WeaponCards (level-up); feature
	 *  unlocks now live in UnlockableFeatures. Retained only so the content resave can migrate off it — remove in a follow-up. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Cards")
	TArray<TObjectPtr<UFPSRCardDataAsset>> AvailableModifiers;

	/** Stat axes this weapon OPTS OUT of for AllWeapons-scope modifier cards (Game.MD §2-4-1 ①). A broad
	 *  "all weapons" card on a listed axis is skipped when this weapon resolves its stats — e.g. a ChargeLaser whose
	 *  recoil is a charge ramp can list RecoilVertical so a global "recoil down" card doesn't touch it. Per-weapon,
	 *  per-axis, AllWeapons-only: ThisWeapon cards (the player deliberately targeted this weapon) always apply. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Cards")
	TArray<EFPSRWeaponStat> AllWeaponsStatExclusions;

	// --- 1P visual / cosmetic (Game.MD §2-9, V0) — all soft refs, null = no visual (no gameplay effect) ---

	/** First-person weapon skeletal mesh (firearms). Attached to the character's FirstPersonArms on equip. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<USkeletalMesh> WeaponMesh1P;

	/** First-person weapon static mesh (e.g. melee knife). Used when WeaponMesh1P is unset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<UStaticMesh> WeaponMeshStatic1P;

	/** Optional per-weapon arms anim instance applied to FirstPersonArms on equip (the pack has per-weapon arm anims). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftClassPtr<UAnimInstance> ArmsAnimInstanceClass;

	/** Optional per-weapon anim instance applied to the 1P WEAPON mesh (WeaponMesh1P) on equip. The weapon has its OWN
	 *  skeleton (SKEL_LPAMG_<W>), so its bolt/magazine (A_FP_WEP_<W>_*) needs its own AnimBP to play WeaponFire/
	 *  ReloadMontage below. Only applied to skeletal weapons; null = no bolt animation (null-safe). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftClassPtr<UAnimInstance> WeaponAnimInstanceClass;

	/** Socket on FirstPersonArms the weapon mesh attaches to (NAME_None = arms component root). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	FName WeaponAttachSocket = NAME_None;

	/** Socket on the WEAPON mesh used as the muzzle-flash origin (cosmetic only; trace origin stays the camera). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	FName MuzzleSocket = NAME_None;

	/** Aim-down-sights (procedural ADS): socket whose transform is aligned to the camera's forward centre-line when aiming
	 *  — the 1P arms offset/rotate so this socket sits on the view axis (the fixed capsule camera does not follow a head
	 *  bone, so the sight is brought to the camera instead). Author it on the SIGHT PART (iron sight / optic) so swapping
	 *  the sight moves the aim reference — like the muzzle — else on the weapon receiver mesh; put it on the sight line at
	 *  sight height with +X forward, +Z up. NAME_None = no procedural alignment (only the ADS FOV zoom + spread run).
	 *  Gated by BaseStats.bHasADS. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	FName AimSocket = NAME_None;

	/** ADS: distance (cm) in front of the camera the AimSocket is placed on the view centre-line. Larger = the sight
	 *  sits further out. Tune together with AimSocket so the sights read centred. Ignored when AimSocket is NAME_None. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual", meta = (ClampMin = "1.0"))
	float ADSSightDistance = 25.0f;

	/** ADS rotation alignment: when true (default) aiming also ROTATES the arms so the AimSocket frame aligns with the
	 *  camera — removing the authored hip-pose cant so the sight reads level and centred (author the socket with +X down
	 *  the sight line, +Z up). Set false for translation-only ADS (keep the authored weapon tilt) if a weapon's AimSocket
	 *  rotation isn't authored down the sight line. Ignored when AimSocket is NAME_None or BaseStats.bHasADS is false. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	bool bADSAlignRotation = true;

	/** ADS full-frame alignment offset: an extra rotation applied to the AimSocket's frame (as if you rotated the socket
	 *  itself) so the gun points forward for packs whose socket axes are off-forward. This pack's weapon-forward is +Y
	 *  (the same reason the muzzle socket needs Yaw 90), so set Yaw 90 here ONCE per weapon instead of rotating every
	 *  sight part's socket — fine-tune pitch/roll in degrees. Only used when bADSAlignRotation is true. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual", meta = (EditConditionHides, EditCondition = "bADSAlignRotation"))
	FRotator ADSAimRotationOffset = FRotator::ZeroRotator;

	/** While aiming, suppress the fire recoil montages (arm kick + weapon bolt): they animate the arms/weapon and fight
	 *  the procedural ADS sight-centering (UpdateAimDownSights re-solves each frame), reading as sight shake. The shot
	 *  still reads via muzzle flash + sound + camera recoil. Default true. Only affects ADS weapons (BaseStats.bHasADS)
	 *  while aiming — hip fire and non-ADS weapons keep the full recoil montage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	bool bSuppressFireMontagesWhileADS = true;

	/** ADS stabilization knob: how much of the aim pose's animated positional bob is allowed through while aiming.
	 *  0 (default) pins the AimSocket exactly on the view centre-line every frame — steadiest reticle, no drift.
	 *  Raise slightly (e.g. 0.1–0.25) to let that fraction of the animated bob survive for a livelier ADS, at the cost
	 *  of a small sight drift off centre. ROTATION stays fully glued regardless. Only used when aiming an ADS weapon
	 *  with an AimSocket; ignored otherwise. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ADSPositionBobScale = 0.0f;

	/** While aiming, suppress the WEAPON bolt/action montage too. Default false: the bolt keeps cycling in ADS as fire
	 *  feedback (it animates the bolt bone, not the sight, so the reticle holds). Set true only for a weapon whose bolt
	 *  montage visibly disturbs the aimed sight. Independent of bSuppressFireMontagesWhileADS (which gates the ARM montage). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	bool bSuppressWeaponBoltWhileADS = false;

	/** ADS fire kick (degrees): a per-shot recoil kick that PIVOTS the weapon about the AimSocket while aiming — the
	 *  sight stays centred but the gun body/muzzle snaps, giving the shot a physical read without moving the reticle.
	 *  0 disables. Only applied while aiming an ADS weapon with an AimSocket. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual", meta = (ClampMin = "0.0"))
	float ADSFireKickDegrees = 1.5f;

	/** ADS fire-kick recovery speed (FInterpTo speed) — how fast the per-shot kick settles back to the aimed pose.
	 *  Higher = snappier recovery. Only used when ADSFireKickDegrees > 0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual", meta = (ClampMin = "0.1"))
	float ADSFireKickRecoveryRate = 12.0f;

	/** While aiming, don't spawn the muzzle flash. In ADS the muzzle sits right behind the sight, so a large flash
	 *  glow washes over the reticle (obscures the aim). Default true: hip fire keeps the flash, ADS drops it (the shot
	 *  still reads via bolt cycle + fire kick + sound + camera recoil). Set false to keep the flash while aiming. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	bool bSuppressMuzzleFlashWhileADS = true;

	/** Optional montage played on the arms when this weapon is equipped. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<UAnimMontage> EquipMontage;

	/** Optional montage played on the arms each shot (owner-client local feel). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<UAnimMontage> FireMontage;

	/** Optional montage played on the WEAPON mesh (WeaponMesh1P) each shot — the bolt/action cycle (A_FP_WEP_<W>_Fire).
	 *  Played on the same fire hook as FireMontage so the bolt syncs with the arm recoil. Needs WeaponAnimInstanceClass
	 *  set. Null = no bolt animation (null-safe). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<UAnimMontage> WeaponFireMontage;

	/** Optional montage played on the arms on reload start (owner-client 1P). Driven by UFPSRWeaponInstance's
	 *  OnRep_Reloading (server-confirmed edge), scaled so its play length matches the resolved ReloadTime. Null = none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<UAnimMontage> ReloadMontage;

	/** Optional montage played on the WEAPON mesh (WeaponMesh1P) on reload — the magazine/bolt action (A_FP_WEP_<W>_
	 *  Reload). Played on the same reload hook as ReloadMontage, rate-scaled to the ReloadTime. Needs
	 *  WeaponAnimInstanceClass set. Null = no weapon reload animation (null-safe). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<UAnimMontage> WeaponReloadMontage;

	/** Optional modular cosmetic parts child-attached to the 1P skeletal weapon mesh on equip (U15). Static/melee
	 *  weapons and empty lists attach nothing (null-safe). Parts inherit the weapon mesh's OnlyOwnerSee visibility. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TArray<FFPSRWeaponPartAttachment> WeaponParts1P;

	/** Cascade muzzle-flash particle spawned at MuzzleSocket each shot (owner-client local). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<UParticleSystem> MuzzleFlash;

	// --- 3P visual / cosmetic (U19 — teammate co-op visibility) — all soft refs, null = no 3P visual (no gameplay
	//     effect). Rendered on the 3P body mesh (GetMesh(), SetOwnerNoSee) for REMOTE observers; the owner sees only 1P. ---

	/** Third-person weapon skeletal mesh, attached to the 3P body hand socket on equip. Null = no 3P weapon. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual3P")
	TSoftObjectPtr<USkeletalMesh> WeaponMesh3P;

	/** Socket on the 3P BODY skeleton the WeaponMesh3P attaches to (NAME_None = body mesh root). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual3P")
	FName WeaponAttachSocket3P = NAME_None;

	/** Montage played on the 3P body each shot for remote observers (MulticastFireCosmetics remote branch). Null = none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual3P")
	TSoftObjectPtr<UAnimMontage> FireMontage3P;

	/** Montage played on the 3P body on reload start for remote observers (OnRep_Reloading remote branch). Null = none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual3P")
	TSoftObjectPtr<UAnimMontage> ReloadMontage3P;

	/** Crosshair style (preferred). When set, this style's Material + dynamic flag drive the HUD crosshair,
	 *  overriding the legacy CrosshairMaterial / bUseDynamicCrosshair below (those remain as a fallback used
	 *  only when CrosshairStyle is unset). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<class UFPSRCrosshairStyleDataAsset> CrosshairStyle;

	/** Per-weapon HUD crosshair material instance (a child MI of M_DynamicCrosshair; texture + spread tuning
	 *  baked into the MI). Null = HUD default crosshair MI. The HUD widget drives only the per-frame Spread
	 *  parameter on a dynamic copy of this material; all other tuning lives in the MI (designer-authored). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	TSoftObjectPtr<UMaterialInterface> CrosshairMaterial;

	/** HUD crosshair behaviour: true = dynamic (spread bloom widens the gap), false = static (no spread applied). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Visual")
	bool bUseDynamicCrosshair = true;

	/** Fire sound played each shot (owner-client local; multi-client cosmetic is a later unit). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Audio")
	TSoftObjectPtr<USoundBase> FireSound;

#if WITH_EDITOR
	/** Editor validation: missing FireAbility never fires (error); archetype/stat mismatches (AOE without an
	 *  AOERadius, ChargeLaser with ChargeTime 0, ranged with MagSize 0) silently misbehave at runtime (warn). */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
