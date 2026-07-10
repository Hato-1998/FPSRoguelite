// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Weapon/FPSRWeaponTypes.h"
#include "Curves/CurveFloat.h"
#include "FPSRWeaponDataAsset.generated.h"

class UGameplayAbility;
class UFPSRCardDataAsset;
class AFPSRProjectile;
class UCRRecoilPattern;
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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "파츠 메시"))
	TSoftObjectPtr<UStaticMesh> Part;

	/** Socket on the WEAPON mesh (SKEL_LPAMG_<W>) the part attaches to. NAME_None = weapon mesh root. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "부착 소켓"))
	FName Socket = NAME_None;

	/** Relative transform applied after attach (fine-tune the part's placement on the socket). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "오프셋(상대 트랜스폼)"))
	FTransform Offset;
};

/** 1P 절차 무기 모션(힙) 프로파일 — 정적 무기도 "살아있게" 만드는 owner-local 코스메틱 모션 파라미터 묶음(P1).
 *  룩스웨이(조준 지연)·걷기밥(속도 게이트)·발사킥. 값은 AFPSRCharacter::UpdateAimDownSights에서 힙 레이어로
 *  합성되며 ADS 진입 시 (1-알파)로 페이드아웃한다. 트레이스/조준에 영향 없음(순수 시각). */
USTRUCT(BlueprintType)
struct FFPSRProceduralWeaponMotionProfile
{
	GENERATED_BODY()

	/** 룩스웨이 강도: 프레임당 조준 회전 델타(도) 1도당 무기가 반대로 기우는 정도(도). 0 = 룩스웨이 없음. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "힙 절차모션|룩스웨이", meta = (DisplayName = "룩스웨이 강도", ClampMin = "0.0"))
	float LookSwayAmount = 0.35f;

	/** 룩스웨이 누적 최대각(도) — 빠르게 돌려도 이 각을 넘지 않는다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "힙 절차모션|룩스웨이", meta = (DisplayName = "룩스웨이 최대각(도)", ClampMin = "0.0"))
	float LookSwayMaxDegrees = 5.0f;

	/** 룩스웨이 복귀속도(FInterpTo) — 클수록 덜 지연되고 빨리 중앙 복귀. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "힙 절차모션|룩스웨이", meta = (DisplayName = "룩스웨이 복귀속도", ClampMin = "0.1"))
	float LookSwayReturnSpeed = 9.0f;

	/** 걷기밥 좌우 진폭(cm) — 이동 중 무기가 화면 좌우로 흔들리는 폭. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "힙 절차모션|걷기밥", meta = (DisplayName = "걷기밥 좌우(cm)", ClampMin = "0.0"))
	float WalkBobHorizontal = 1.2f;

	/** 걷기밥 상하 진폭(cm) — 좌우의 2배 주파수로 흔들려 8자 궤적을 만든다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "힙 절차모션|걷기밥", meta = (DisplayName = "걷기밥 상하(cm)", ClampMin = "0.0"))
	float WalkBobVertical = 0.8f;

	/** 걷기밥 주파수(사이클/초, 최대속도 기준) — 이동속도 0..1로 게이트된다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "힙 절차모션|걷기밥", meta = (DisplayName = "걷기밥 주파수", ClampMin = "0.0"))
	float WalkBobFrequency = 1.2f;

	/** 발사킥 피치(도) — 발사마다 총구가 순간적으로 위로 튀는 각(감쇠). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "힙 절차모션|발사킥", meta = (DisplayName = "발사킥 피치(도)", ClampMin = "0.0"))
	float FireKickPitchDegrees = 2.5f;

	/** 발사킥 후퇴(cm) — 발사마다 총이 카메라 쪽(-X)으로 밀리는 거리(감쇠). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "힙 절차모션|발사킥", meta = (DisplayName = "발사킥 후퇴(cm)", ClampMin = "0.0"))
	float FireKickBackwardCm = 2.0f;

	/** 발사킥 상승(cm) — 발사마다 총이 살짝 위로(+Z) 뜨는 거리(감쇠). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "힙 절차모션|발사킥", meta = (DisplayName = "발사킥 상승(cm)", ClampMin = "0.0"))
	float FireKickUpCm = 0.5f;

	/** 발사킥 복귀속도(FInterpTo) — 발사킥이 0으로 가라앉는 속도. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "힙 절차모션|발사킥", meta = (DisplayName = "발사킥 복귀속도", ClampMin = "0.1"))
	float FireKickRecoverySpeed = 11.0f;
};

/** Data-driven weapon definition. */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRWeaponDataAsset : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|기본", meta = (DisplayName = "무기 이름"))
	FText DisplayName;

	/** Weapon archetype now lives in BaseStats so per-archetype stat fields can drive EditCondition visibility. */
	UFUNCTION(BlueprintPure, Category = "Weapon")
	EFPSRWeaponArchetype GetArchetype() const { return BaseStats.Archetype; }

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|기본", meta = (DisplayName = "기본 스탯"))
	FFPSRWeaponStatBlock BaseStats;

	/** Ability granted while this weapon is equipped (activated by the Fire input). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|기본", meta = (DisplayName = "발사 어빌리티(GA)"))
	TSubclassOf<UGameplayAbility> FireAbility;

	/** Projectile actor class (AOE archetypes). Content assigns a BP with mesh/VFX; null falls back to AFPSRProjectile base. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|발사체", meta = (DisplayName = "발사체 클래스"))
	TSubclassOf<AFPSRProjectile> ProjectileClass;

	/** CrystalRecoil recoil pattern (per-shot coordinate deltas + recovery tuning) applied by UFPSRRecoilComponent while
	 *  this weapon is equipped (P1 adapter). Null = no pattern recoil — melee (no recoil) and ChargeLaser (uses its own
	 *  charge-ramp recoil in the fire component) leave this unset. Authored in the CrystalRecoil pattern editor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|반동", meta = (DisplayName = "반동 패턴(CrystalRecoil)"))
	TObjectPtr<UCRRecoilPattern> RecoilPattern;

	/** 확산 heat 프로파일(P2) — 무기별 동적 확산. 레거시 스칼라 블룸(P4에서 제거됨)을 대체한다:
	 *  수락된 발사마다 ShotToHeatCurve(X=현재 heat, Y=더할 heat)로 heat가 쌓이고, HeatToSpreadAngleCurve(X=heat,
	 *  Y=확산 반각 도; heat=0→0으로 저작해 무heat=순수 base SpreadDegrees)로 동적 확산각을 만들고,
	 *  HeatToCooldownPerSecondCurve(X=heat, Y=heat/초, >0)로 식는다. 장착 시 반동 컴포넌트에 주입된다.
	 *  동적 블룸이 없는 무기(근접·ChargeLaser)는 3곡선 모두 비워 둔다. RecoilPattern(반동 kinematics)과 독립 —
	 *  패턴 없이 확산만, 확산 없이 패턴만도 가능. ⚠️ X축은 '샷 인덱스'가 아니라 '현재 heat'(자기참조): 발당 고정
	 *  증가를 원하면 ShotToHeat를 상수 곡선으로 저작. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|반동", meta = (DisplayName = "확산 heat: 발당 heat증가(X=현재heat, Y=증가량)"))
	FRuntimeFloatCurve ShotToHeatCurve;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|반동", meta = (DisplayName = "확산 heat: heat→확산각(X=heat, Y=도, heat0→0 앵커)"))
	FRuntimeFloatCurve HeatToSpreadAngleCurve;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|반동", meta = (DisplayName = "확산 heat: heat→냉각(X=heat, Y=heat/초, >0)"))
	FRuntimeFloatCurve HeatToCooldownPerSecondCurve;

	/** heat 상한(확산 곡선의 정의역 X=[0..이 값]). 동적 확산 프로파일을 쓸 때만 유효. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|반동", meta = (DisplayName = "확산 heat: 최대 heat", ClampMin = "0.0"))
	float MaxRecoilHeat = 100.0f;

	/** 마지막 발사 후 이 시간(초)이 지나야 heat 냉각이 시작된다(연사 중 확산 유지용 grace). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|반동", meta = (DisplayName = "확산 heat: 냉각 지연(초)", ClampMin = "0.0"))
	float RecoilHeatCooldownDelay = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|카드", meta = (DisplayName = "무기 카드(레벨업 풀)"))
	TArray<TObjectPtr<UFPSRCardDataAsset>> WeaponCards;

	/** Feature-unlock cards (U18b): locked capabilities offered as weapon-unlock candidates on mission clear /
	 *  level milestones (reuse WeaponBehavior/WeaponStat effects). Behavior features are stack-gated by MaxStacks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|카드", meta = (DisplayName = "언락 피처(미션클리어)"))
	TArray<TObjectPtr<UFPSRCardDataAsset>> UnlockableFeatures;

	/** Max number of DISTINCT behavior fragments this weapon can hold (U6). Reaching it makes a further new-fragment
	 *  pick a REPLACEMENT (drop one equipped fragment for the new one); stacking an already-held fragment is governed
	 *  by the fragment's own MaxStacks, not this cap. Per-weapon so e.g. a simple sidearm can allow fewer than a rifle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|카드", meta = (DisplayName = "최대 프래그먼트 슬롯", ClampMin = "1"))
	int32 MaxFragmentSlots = 3;

	/** DEPRECATED (U18b→U6): 구 미션보상 프래그먼트 풀. U6/H2 라우팅에서 행동 프래그먼트와 stat 피처 언락 모두
	 *  UnlockableFeatures(미션/마일스톤)에 위치; 레벨업 무기 카드(WeaponCards)=순수 stat만. 콘텐츠 리세이브
	 *  마이그레이션용으로만 잔존 — 후속에서 제거. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|카드", meta = (DisplayName = "모디파이어 풀(폐기예정)"))
	TArray<TObjectPtr<UFPSRCardDataAsset>> AvailableModifiers;

	/** Stat axes this weapon OPTS OUT of for AllWeapons-scope modifier cards (Game.MD §2-4-1 ①). A broad
	 *  "all weapons" card on a listed axis is skipped when this weapon resolves its stats — e.g. a ChargeLaser whose
	 *  recoil is a charge ramp can list RecoilVertical so a global "recoil down" card doesn't touch it. Per-weapon,
	 *  per-axis, AllWeapons-only: ThisWeapon cards (the player deliberately targeted this weapon) always apply. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|카드", meta = (DisplayName = "전체무기 카드 제외 스탯"))
	TArray<EFPSRWeaponStat> AllWeaponsStatExclusions;

	// --- 1P visual / cosmetic (Game.MD §2-9, V0) — all soft refs, null = no visual (no gameplay effect) ---

	/** First-person weapon skeletal mesh (firearms). Attached to the character's FirstPersonArms on equip. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|1인칭 메시", meta = (DisplayName = "1P 무기 메시(스켈)"))
	TSoftObjectPtr<USkeletalMesh> WeaponMesh1P;

	/** First-person weapon static mesh (e.g. melee knife). Used when WeaponMesh1P is unset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|1인칭 메시", meta = (DisplayName = "1P 무기 메시(스태틱/근접)"))
	TSoftObjectPtr<UStaticMesh> WeaponMeshStatic1P;

	/** Optional per-weapon arms anim instance applied to FirstPersonArms on equip (the pack has per-weapon arm anims). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|1인칭 메시", meta = (DisplayName = "팔 AnimBP"))
	TSoftClassPtr<UAnimInstance> ArmsAnimInstanceClass;

	/** Optional per-weapon anim instance applied to the 1P WEAPON mesh (WeaponMesh1P) on equip. The weapon has its OWN
	 *  skeleton (SKEL_LPAMG_<W>), so its bolt/magazine (A_FP_WEP_<W>_*) needs its own AnimBP to play WeaponFire/
	 *  ReloadMontage below. Only applied to skeletal weapons; null = no bolt animation (null-safe). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|1인칭 메시", meta = (DisplayName = "무기 AnimBP"))
	TSoftClassPtr<UAnimInstance> WeaponAnimInstanceClass;

	/** Socket on FirstPersonArms the weapon mesh attaches to (NAME_None = arms component root). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|1인칭 메시", meta = (DisplayName = "무기 부착 소켓(팔)"))
	FName WeaponAttachSocket = NAME_None;

	/** Socket on the WEAPON mesh used as the muzzle-flash origin (cosmetic only; trace origin stays the camera). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|발사 연출", meta = (DisplayName = "총구 소켓(화염 원점)"))
	FName MuzzleSocket = NAME_None;

	/** Aim-down-sights (procedural ADS): socket whose transform is aligned to the camera's forward centre-line when aiming
	 *  — the 1P arms offset/rotate so this socket sits on the view axis (the fixed capsule camera does not follow a head
	 *  bone, so the sight is brought to the camera instead). Author it on the SIGHT PART (iron sight / optic) so swapping
	 *  the sight moves the aim reference — like the muzzle — else on the weapon receiver mesh; put it on the sight line at
	 *  sight height with +X forward, +Z up. NAME_None = no procedural alignment (only the ADS FOV zoom + spread run).
	 *  Gated by BaseStats.bHasADS. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|조준(ADS)", meta = (DisplayName = "조준 소켓(사이트)"))
	FName AimSocket = NAME_None;

	/** ADS: distance (cm) in front of the camera the AimSocket is placed on the view centre-line. Larger = the sight
	 *  sits further out. Tune together with AimSocket so the sights read centred. Ignored when AimSocket is NAME_None. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|조준(ADS)", meta = (DisplayName = "조준경-카메라 거리(cm)", ClampMin = "1.0"))
	float ADSSightDistance = 25.0f;

	/** ADS rotation alignment: when true (default) aiming also ROTATES the arms so the AimSocket frame aligns with the
	 *  camera — removing the authored hip-pose cant so the sight reads level and centred (author the socket with +X down
	 *  the sight line, +Z up). Set false for translation-only ADS (keep the authored weapon tilt) if a weapon's AimSocket
	 *  rotation isn't authored down the sight line. Ignored when AimSocket is NAME_None or BaseStats.bHasADS is false. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|조준(ADS)", meta = (DisplayName = "조준 회전 정렬"))
	bool bADSAlignRotation = true;

	/** ADS full-frame alignment offset: an extra rotation applied to the AimSocket's frame (as if you rotated the socket
	 *  itself) so the gun points forward for packs whose socket axes are off-forward. This pack's weapon-forward is +Y
	 *  (the same reason the muzzle socket needs Yaw 90), so set Yaw 90 here ONCE per weapon instead of rotating every
	 *  sight part's socket — fine-tune pitch/roll in degrees. Only used when bADSAlignRotation is true. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|조준(ADS)", meta = (DisplayName = "조준 정렬 회전 오프셋", EditConditionHides, EditCondition = "bADSAlignRotation"))
	FRotator ADSAimRotationOffset = FRotator::ZeroRotator;

	/** While aiming, suppress the fire recoil montages (arm kick + weapon bolt): they animate the arms/weapon and fight
	 *  the procedural ADS sight-centering (UpdateAimDownSights re-solves each frame), reading as sight shake. The shot
	 *  still reads via muzzle flash + sound + camera recoil. Default true. Only affects ADS weapons (BaseStats.bHasADS)
	 *  while aiming — hip fire and non-ADS weapons keep the full recoil montage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|조준(ADS)", meta = (DisplayName = "조준 중 발사몽타주 억제(팔)"))
	bool bSuppressFireMontagesWhileADS = true;

	/** ADS stabilization knob: how much of the aim pose's animated positional bob is allowed through while aiming.
	 *  0 (default) pins the AimSocket exactly on the view centre-line every frame — steadiest reticle, no drift.
	 *  Raise slightly (e.g. 0.1–0.25) to let that fraction of the animated bob survive for a livelier ADS, at the cost
	 *  of a small sight drift off centre. ROTATION stays fully glued regardless. Only used when aiming an ADS weapon
	 *  with an AimSocket; ignored otherwise. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|조준(ADS)", meta = (DisplayName = "조준 위치흔들림 허용량", ClampMin = "0.0", ClampMax = "1.0"))
	float ADSPositionBobScale = 0.0f;

	/** While aiming, suppress the WEAPON bolt/action montage too. Default false: the bolt keeps cycling in ADS as fire
	 *  feedback (it animates the bolt bone, not the sight, so the reticle holds). Set true only for a weapon whose bolt
	 *  montage visibly disturbs the aimed sight. Independent of bSuppressFireMontagesWhileADS (which gates the ARM montage). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|조준(ADS)", meta = (DisplayName = "조준 중 노리쇠몽타주 억제"))
	bool bSuppressWeaponBoltWhileADS = false;

	/** ADS fire kick (degrees): a per-shot recoil kick that PIVOTS the weapon about the AimSocket while aiming — the
	 *  sight stays centred but the gun body/muzzle snaps, giving the shot a physical read without moving the reticle.
	 *  0 disables. Only applied while aiming an ADS weapon with an AimSocket. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|조준(ADS)", meta = (DisplayName = "조준 발사킥(도)", ClampMin = "0.0"))
	float ADSFireKickDegrees = 1.5f;

	/** ADS fire-kick recovery speed (FInterpTo speed) — how fast the per-shot kick settles back to the aimed pose.
	 *  Higher = snappier recovery. Only used when ADSFireKickDegrees > 0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|조준(ADS)", meta = (DisplayName = "조준 발사킥 복귀속도", ClampMin = "0.1"))
	float ADSFireKickRecoveryRate = 12.0f;

	/** ADS idle sway — LEFT-RIGHT (yaw) amplitude in degrees. While aiming, the weapon gently wanders about the sight
	 *  pivot to add a handheld "breathing" life: the gun body sways but the reticle stays centred (same pivot trick as
	 *  ADSFireKickDegrees). 0 disables the yaw sway. Only applied while aiming an ADS weapon with an AimSocket; faded
	 *  in/out by the ADS blend. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|조준(ADS)", meta = (DisplayName = "조준 흔들림 좌우(도)", ClampMin = "0.0"))
	float ADSSwayYawDegrees = 0.4f;

	/** ADS idle sway — subtle UP-DOWN (pitch) amplitude in degrees, out of phase with the yaw so the wander reads
	 *  organic rather than a straight line. 0 = yaw-only sway. Only used while aiming when a sway amplitude is > 0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|조준(ADS)", meta = (DisplayName = "조준 흔들림 상하(도)", ClampMin = "0.0"))
	float ADSSwayPitchDegrees = 0.15f;

	/** ADS idle sway speed (oscillation frequency, rad/s). Lower = slower, calmer breathing; higher = jitterier.
	 *  Only used when a sway amplitude is > 0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|조준(ADS)", meta = (DisplayName = "조준 흔들림 속도", ClampMin = "0.0"))
	float ADSSwaySpeed = 1.2f;

	/** ADS muzzle-flash scale (0..1): how big the muzzle flash is while aiming, relative to hip fire. In ADS the muzzle
	 *  sits right behind the sight, so a full-size flash washes over the reticle — shrink it so the shot still reads (a
	 *  smaller flash) without obscuring the aim. 0 = no flash while aiming; 1 = full hip-fire size. Hip fire is always
	 *  full size. Only affects ADS weapons while aiming. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|조준(ADS)", meta = (DisplayName = "조준 중 총구화염 크기(0=끔)", ClampMin = "0.0", ClampMax = "1.0"))
	float ADSMuzzleFlashScale = 0.35f;

	/** 1P 절차 무기 모션(힙) — 살아있는 총 코스메틱(룩스웨이·걷기밥·발사킥). ADS 진입 시 페이드아웃. owner-local·복제0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|1인칭 절차 무기모션(힙)", meta = (DisplayName = "힙 절차 무기모션 프로파일"))
	FFPSRProceduralWeaponMotionProfile ProceduralWeaponMotion;

	/** Optional montage played on the arms when this weapon is equipped. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|애니 몽타주", meta = (DisplayName = "장착 몽타주(팔)"))
	TSoftObjectPtr<UAnimMontage> EquipMontage;

	/** Optional montage played on the arms each shot (owner-client local feel). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|애니 몽타주", meta = (DisplayName = "발사 몽타주(팔)"))
	TSoftObjectPtr<UAnimMontage> FireMontage;

	/** Optional montage played on the WEAPON mesh (WeaponMesh1P) each shot — the bolt/action cycle (A_FP_WEP_<W>_Fire).
	 *  Played on the same fire hook as FireMontage so the bolt syncs with the arm recoil. Needs WeaponAnimInstanceClass
	 *  set. Null = no bolt animation (null-safe). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|애니 몽타주", meta = (DisplayName = "발사 몽타주(무기/노리쇠)"))
	TSoftObjectPtr<UAnimMontage> WeaponFireMontage;

	/** Optional montage played on the arms on reload start (owner-client 1P). Driven by UFPSRWeaponInstance's
	 *  OnRep_Reloading (server-confirmed edge), scaled so its play length matches the resolved ReloadTime. Null = none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|애니 몽타주", meta = (DisplayName = "재장전 몽타주(팔)"))
	TSoftObjectPtr<UAnimMontage> ReloadMontage;

	/** Optional montage played on the WEAPON mesh (WeaponMesh1P) on reload — the magazine/bolt action (A_FP_WEP_<W>_
	 *  Reload). Played on the same reload hook as ReloadMontage, rate-scaled to the ReloadTime. Needs
	 *  WeaponAnimInstanceClass set. Null = no weapon reload animation (null-safe). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|애니 몽타주", meta = (DisplayName = "재장전 몽타주(무기)"))
	TSoftObjectPtr<UAnimMontage> WeaponReloadMontage;

	/** --- Fire-part recoil (bolt / charging handle), data-driven via UFPSRWeaponAnimInstance ---
	 *  The bone the weapon AnimBP's ModifyBone targets (bolt / charging handle). None = no moving fire part (no-op).
	 *  NOTE: the ModifyBone target bone is authored in the AnimBP (not runtime-settable) — this field is for IsDataValid
	 *  verification + documentation of which bone the AnimBP should drive. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|발사파츠 반동", meta = (DisplayName = "반동 파츠 본(노리쇠/장전손잡이)"))
	FName FirePartRecoilBone = NAME_None;

	/** Fire-part recoil travel distance (cm) at the recoil curve's peak. 0 disables the effect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|발사파츠 반동", meta = (DisplayName = "반동 거리(cm)", ClampMin = "0.0"))
	float FirePartRecoilDistanceCm = 3.5f;

	/** Fire-part recoil direction in the weapon mesh's COMPONENT space (unit vector). This pack's charging handle travels
	 *  along component -Y, so (0, -1, 0). Scaled by FirePartRecoilDistanceCm x the curve value each shot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|발사파츠 반동", meta = (DisplayName = "반동 방향(컴포넌트)"))
	FVector FirePartRecoilAxis = FVector(0.0f, -1.0f, 0.0f);

	/** Anim curve (on the weapon fire montage, e.g. CHRecoil) driving the recoil 0..1 over the shot. None disables. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|발사파츠 반동", meta = (DisplayName = "반동 커브"))
	FName FirePartRecoilCurve = FName("CHRecoil");

	/** Optional modular cosmetic parts child-attached to the 1P skeletal weapon mesh on equip (U15). Static/melee
	 *  weapons and empty lists attach nothing (null-safe). Parts inherit the weapon mesh's OnlyOwnerSee visibility. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "모듈 파츠 목록(1P)"))
	TArray<FFPSRWeaponPartAttachment> WeaponParts1P;

	/** Cascade muzzle-flash particle spawned at MuzzleSocket each shot (owner-client local). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|발사 연출", meta = (DisplayName = "총구 화염 파티클"))
	TSoftObjectPtr<UParticleSystem> MuzzleFlash;

	// --- 3P visual / cosmetic (U19 — teammate co-op visibility) — all soft refs, null = no 3P visual (no gameplay
	//     effect). Rendered on the 3P body mesh (GetMesh(), SetOwnerNoSee) for REMOTE observers; the owner sees only 1P. ---

	/** Third-person weapon skeletal mesh, attached to the 3P body hand socket on equip. Null = no 3P weapon. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|3인칭", meta = (DisplayName = "3P 무기 메시"))
	TSoftObjectPtr<USkeletalMesh> WeaponMesh3P;

	/** Socket on the 3P BODY skeleton the WeaponMesh3P attaches to (NAME_None = body mesh root). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|3인칭", meta = (DisplayName = "3P 부착 소켓(바디)"))
	FName WeaponAttachSocket3P = NAME_None;

	/** Montage played on the 3P body each shot for remote observers (MulticastFireCosmetics remote branch). Null = none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|3인칭", meta = (DisplayName = "3P 발사 몽타주"))
	TSoftObjectPtr<UAnimMontage> FireMontage3P;

	/** Montage played on the 3P body on reload start for remote observers (OnRep_Reloading remote branch). Null = none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|3인칭", meta = (DisplayName = "3P 재장전 몽타주"))
	TSoftObjectPtr<UAnimMontage> ReloadMontage3P;

	/** Crosshair style (preferred). When set, this style's Material + dynamic flag drive the HUD crosshair,
	 *  overriding the legacy CrosshairMaterial / bUseDynamicCrosshair below (those remain as a fallback used
	 *  only when CrosshairStyle is unset). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|크로스헤어", meta = (DisplayName = "크로스헤어 스타일"))
	TSoftObjectPtr<class UFPSRCrosshairStyleDataAsset> CrosshairStyle;

	/** Per-weapon HUD crosshair material instance (a child MI of M_DynamicCrosshair; texture + spread tuning
	 *  baked into the MI). Null = HUD default crosshair MI. The HUD widget drives only the per-frame Spread
	 *  parameter on a dynamic copy of this material; all other tuning lives in the MI (designer-authored). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|크로스헤어", meta = (DisplayName = "크로스헤어 머티리얼(레거시)"))
	TSoftObjectPtr<UMaterialInterface> CrosshairMaterial;

	/** HUD crosshair behaviour: true = dynamic (spread bloom widens the gap), false = static (no spread applied). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|크로스헤어", meta = (DisplayName = "다이내믹 크로스헤어"))
	bool bUseDynamicCrosshair = true;

	/** Fire sound played each shot (owner-client local; multi-client cosmetic is a later unit). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|발사 연출", meta = (DisplayName = "발사 사운드"))
	TSoftObjectPtr<USoundBase> FireSound;

#if WITH_EDITOR
	/** Editor validation: missing FireAbility never fires (error); archetype/stat mismatches (AOE without an
	 *  AOERadius, ChargeLaser with ChargeTime 0, ranged with MagSize 0) silently misbehave at runtime (warn). */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
