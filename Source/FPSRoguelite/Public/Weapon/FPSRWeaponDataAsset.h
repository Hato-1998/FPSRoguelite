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
class UAnimSequence;
class USoundBase;
class UParticleSystem;
class UMaterialInterface;
class UFPSRWeaponFragment;
class UUserWidget;

/** Sniper-scope descriptor (W-U2) for a SIGHT part. Purely OWNER-LOCAL cosmetic: when this sight is active and the
 *  player aims, it drives a full-screen scope — strong FOV zoom + HUD reticle/vignette overlay + 1P weapon hidden.
 *  Only meaningful on the part whose mesh carries the weapon's AimSocket. Never touches trace/damage/replication/save
 *  (§2-A isolation contract). */
USTRUCT(BlueprintType)
struct FFPSRWeaponScopeDescriptor
{
	GENERATED_BODY()

	/** Master switch: aiming with this sight active drives the full-screen scope. false = ordinary ADS (iron sight / reddot). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "스코프", meta = (DisplayName = "스코프 오버레이 사용"))
	bool bScopeOverlay = false;

	/** This sight's ADS magnification — the camera FOV (deg) it zooms to while aiming. Applies to ANY sight (iron /
	 *  reddot / low-power scope / sniper scope), so each sight part carries its own zoom. <=0 = use the weapon's
	 *  BaseStats.ADSFieldOfView (this sight adds no extra zoom). Owner-local camera FOV only; never affects
	 *  trace/spread (trace origin stays the camera). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "스코프", meta = (DisplayName = "조준 배율 FOV(도, ≤0=무기 기본)", ClampMin = "0.0"))
	float AimFieldOfView = 0.0f;

	/** 이 사이트가 활성일 때 HUD가 켜는 풀스크린 스코프 오버레이 위젯 BP(사이트/단계별). null = HUD 폴백
	 *  (UFPSRRunHUDWidget::ScopeOverlayWidgetClass) 사용. 오너-로컬 코스메틱(복제0). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "스코프", meta = (DisplayName = "스코프 오버레이 위젯(WBP)", EditCondition = "bScopeOverlay", EditConditionHides))
	TSoftClassPtr<UUserWidget> ScopeOverlayWidgetClass;

	/** Show the dark scope-edge vignette around the reticle while scoped. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "스코프", meta = (DisplayName = "스코프 비네트", EditCondition = "bScopeOverlay", EditConditionHides))
	bool bScopeVignette = true;

	/** Hide the 1P weapon/arms while this scope is active (non-PiP full-screen scope). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "스코프", meta = (DisplayName = "스코프 시 1P총 숨김", EditCondition = "bScopeOverlay", EditConditionHides))
	bool bHideWeaponWhileScoped = true;
};

/** 진화 단계 트리거 종류: 슬롯 프래그먼트의 스택 수 / 무기 해결스탯 임계. */
UENUM(BlueprintType)
enum class EFPSRPartStageTrigger : uint8
{
	FragmentStacks UMETA(DisplayName = "프래그먼트 스택"),
	StatThreshold  UMETA(DisplayName = "스탯 임계"),
};

/** One evolution stage of a part slot (W-U1b 재설계): when the slot's trigger condition is met, this stage's
 *  mesh/offset/scope replaces the slot's base part — sharing the slot's FIXED socket. Winner among met stages =
 *  the LAST one satisfied in list order (author stages base→강한 순). 순수 데이터: 폴리모픽 조건 없이 '단일
 *  프래그먼트 스택 임계' 또는 '무기 해결스탯 임계'만으로 진화(§2-A 격리계약 유지 — 파츠는 스택/스탯을 읽기만 함). */
USTRUCT(BlueprintType)
struct FFPSRWeaponPartStage
{
	GENERATED_BODY()

	/** 이 단계를 켜는 조건 종류. 기본=프래그먼트 스택(기존 동작). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "트리거 종류"))
	EFPSRPartStageTrigger Trigger = EFPSRPartStageTrigger::FragmentStacks;

	/** 이 단계가 켜지는 최소 스택(슬롯 EvolutionFragment 기준). 1 이상. Base(0단계)는 슬롯의 Part 필드. Trigger=프래그먼트 스택일 때만 사용. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "필요 스택", ClampMin = "1", EditCondition = "Trigger == EFPSRPartStageTrigger::FragmentStacks", EditConditionHides))
	int32 MinStacks = 1;

	/** 스탯 임계 트리거: 비교할 무기 해결스탯 축. Trigger=스탯 임계일 때만 사용. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "스탯 축", EditCondition = "Trigger == EFPSRPartStageTrigger::StatThreshold", EditConditionHides))
	EFPSRWeaponStat StatAxis = EFPSRWeaponStat::FireRate;

	/** 스탯 임계 트리거: 비교 연산자. Trigger=스탯 임계일 때만 사용. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "비교", EditCondition = "Trigger == EFPSRPartStageTrigger::StatThreshold", EditConditionHides))
	EFPSRStatCompare StatCompare = EFPSRStatCompare::GreaterOrEqual;

	/** 스탯 임계 트리거: 비교 기준값. Trigger=스탯 임계일 때만 사용. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "기준값", EditCondition = "Trigger == EFPSRPartStageTrigger::StatThreshold", EditConditionHides))
	float StatValue = 0.0f;

	/** 이 단계에서 교체되는 파츠 메시. null = 이 단계 선택 시 파츠 사라짐(null-safe). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "단계 메시"))
	TSoftObjectPtr<UStaticMesh> Mesh;

	/** 슬롯 고정 소켓 기준 상대 트랜스폼(Synty 파츠는 비공유원점이라 단계마다 오프셋이 다를 수 있음). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "오프셋(상대 트랜스폼)"))
	FTransform Offset;

	/** 이 단계가 사이트일 때의 스코프 디스크립터(예: 저격 스코프 단계는 bScopeOverlay=true). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "스코프(사이트 단계)"))
	FFPSRWeaponScopeDescriptor Scope;
};

/** One modular cosmetic part slot attached to the 1P skeletal weapon mesh at a named socket (U15). Purely visual: the
 *  part is a static mesh (barrel / forestock / magazine / sight from the pack) child-attached to the equipped
 *  skeletal weapon. Null Part = skipped (null-safe). Static/melee weapons and empty lists attach nothing. A slot may
 *  be purely structural (no evolution) or may evolve (W-U1b): the base Part below is stage 0, and Stages lists
 *  higher stack-gated replacements — the slot's Socket stays FIXED across every stage. */
USTRUCT(BlueprintType)
struct FFPSRWeaponPartAttachment
{
	GENERATED_BODY()

	/** Static mesh of the part (soft ref; null = this entry is skipped). Base/stage-0 mesh when this slot evolves. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "파츠 메시"))
	TSoftObjectPtr<UStaticMesh> Part;

	/** Socket on the WEAPON mesh (SKEL_LPAMG_<W>) the part attaches to. NAME_None = weapon mesh root. FIXED mount —
	 *  the tool bakes this as a stable SOCKET_Mount_<자동id> and it stays unchanged even as the slot evolves. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "부착 소켓"))
	FName Socket = NAME_None;

	/** Relative transform applied after attach (fine-tune the part's placement on the socket). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "오프셋(상대 트랜스폼)"))
	FTransform Offset;

	/** Sniper-scope descriptor — only meaningful when this part is the active sight (its mesh owns the weapon's
	 *  AimSocket). bScopeOverlay=false (default) = an ordinary structural part / iron sight, no scope. (W-U2) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "스코프(사이트 파츠)"))
	FFPSRWeaponScopeDescriptor Scope;

	/** 슬롯 표시 이름(툴/Details 표시용, 사용자 지정). 실제 소켓명이 아님 — 소켓은 위 Socket(툴이 안정 id로 관리). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "슬롯 이름(표시용)"))
	FText DisplayLabel;

	/** 이 슬롯을 진화시키는 프래그먼트(카드) — 이 프래그먼트의 획득 스택 수가 Stages의 단계를 결정한다. null = 순수
	 *  구조 파츠(진화 없음). 최종 단계 MinStacks = 프래그먼트 MaxStacks로 두면 카드가 자동 소진된다(기존 카드풀 로직). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "진화 프래그먼트(카드)"))
	TSoftObjectPtr<UFPSRWeaponFragment> EvolutionFragment;

	/** 진화 단계 목록(약→강 순 권장). 목록 순서상 마지막으로 조건이 충족된 단계가 기본 Part를 교체(스택 트리거와
	 *  스탯 임계 트리거를 한 목록에 섞어도 됨). 빈 목록 = 기본만. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "진화 단계"))
	TArray<FFPSRWeaponPartStage> Stages;
};

/** 상태 포즈 한 개(GunMotionTool_Spec.md §6) — 무기 전체에 적용되는 오프셋+틸트. 저작 = 총모션 스튜디오의 "상태
 *  포즈 모드"(§5 범위 밖, 이 명세는 스키마까지)에서 무기 전체 기즈모로 잡은 뒤 [DA에 저장]으로 기록한다. 소비는
 *  홀스터 행(UpdateAimDownSights 상태 레이어·홀스터 게이트) 범위 — 이 파일은 데이터 계약만 정의한다. */
USTRUCT(BlueprintType)
struct FFPSRWeaponStatePose
{
	GENERATED_BODY()

	/** 무기 전체 위치 오프셋(cm, 그립 기준). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|상태 포즈", meta = (DisplayName = "위치 오프셋(cm)"))
	FVector Offset = FVector::ZeroVector;

	/** 무기 전체 회전 오프셋(도). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|상태 포즈", meta = (DisplayName = "회전(틸트)"))
	FRotator Tilt = FRotator::ZeroRotator;
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

	// --- 상태 포즈(§6, 통합 스키마 — 저작만 이 명세, 소비는 홀스터 행) ---

	/** 슬라이드 상태 무기 포즈. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|상태 포즈", meta = (DisplayName = "슬라이드 포즈"))
	FFPSRWeaponStatePose SlidePose;

	/** 슬라이드 중 밥(bob) 허용량 — 0=포즈 고정, 기본 0(§6 명세값). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|상태 포즈", meta = (DisplayName = "슬라이드 밥 배율", ClampMin = "0.0"))
	float SlideBobScale = 0.0f;

	/** 공중(에어본) 상태 무기 포즈. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|상태 포즈", meta = (DisplayName = "공중 포즈"))
	FFPSRWeaponStatePose AirbornePose;

	/** 공중 중 밥(bob) 허용량 — 기본 1(§6 명세값, 힙 밥을 그대로 통과). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|상태 포즈", meta = (DisplayName = "공중 밥 배율", ClampMin = "0.0"))
	float AirborneBobScale = 1.0f;

	/** 공중 블렌드 전환 소요 시간(초). 슬라이드는 이미 스무드 소스가 있고(GetSlideBlend), 홀스터도 이미 있지만
	 *  (HolsterDuration), 공중은 IsFalling()이라는 유일한 이진(binary) 소스뿐이라 스무딩 시간을 데이터로 쥐어줘야
	 *  한다. 0 = 즉시 스냅(이 필드가 생기기 전의 동작과 동일). 기본 0.15는 §6 명세엔 없던 값이다 — 소비 구현
	 *  (홀스터 행)이 정한 제안 기본값. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|상태 포즈", meta = (DisplayName = "공중 블렌드 시간(초)", ClampMin = "0.0"))
	float AirborneBlendDuration = 0.15f;

	/** 홀스터(수납) 상태 포즈. ⚠️ **위 슬라이드/공중과 공간이 다르다.** 슬라이드·공중은 총의 부착 기준값에
	 *  더해지는 총-공간 델타(디테일 패널 WeaponMesh 숫자와 1:1 대응)지만, 홀스터는 **1인칭 팔 전체**에 얹히는
	 *  카메라 공간 오프셋이다(+X 전방 / +Y 우 / +Z 상 — 아래로 내리려면 Z에 음수).
	 *
	 *  왜 홀스터만 다른가: 수납은 "화면에 아무것도 안 남기기"가 목표인데, 총만 내리면 어깨가 카메라에 고정돼 있어
	 *  팔이 닿는 거리를 넘는 순간 손 IK가 총을 놓고 **팔만 화면에 남는다**. 팔을 움직이면 총은 팔에 붙어 있으니
	 *  같이 빠진다. 팔을 아예 숨기는 안은 기각 — 나중에 진짜 1인칭 홀스터 애니메이션이 들어오면 되돌려야 한다.
	 *
	 *  그래서 이 값은 패널 숫자로 저작할 수 없다(팔 트랜스폼은 매 프레임 덮어써진다). 값을 넣고 실행해 확인하는
	 *  방식으로 잡는다 — 정밀 포즈가 아니라 "충분히 화면 밖으로"라 몇 번이면 수렴한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|상태 포즈", meta = (DisplayName = "홀스터 포즈"))
	FFPSRWeaponStatePose HolsterPose;

	/** 홀스터 전환 소요 시간(초) — 기본 0.25(§6 명세값). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|상태 포즈", meta = (DisplayName = "홀스터 소요시간(초)", ClampMin = "0.0"))
	float HolsterDuration = 0.25f;
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

	/** 이 무기를 든 동안의 걷기 속도 상한(cm/s). **0 = 캐릭터 기본값**(`AFPSRCharacter::BaseWalkSpeed`, 600).
	 *  근접/맨손이 700인 것이 이 필드의 첫 용도다. 카드 이동속도 배수는 이 값에 곱해지므로, 무기 속도를 준다고
	 *  카드가 무효화되지 않는다(합성은 `UFPSRCharacterMovementComponent::RefreshWalkSpeedCap`).
	 *
	 *  ⚠️ 슬라이드 속도는 여기서 저작하지 않는다 — 슬라이드 진입 임펄스가 *그 순간 속도 × 1.5*에서 파생되므로
	 *  걷기 600이면 900, 700이면 1000이 자동으로 나온다. 따로 두면 두 값을 계속 맞춰야 하고, 진입 속도가
	 *  "장착이 언제 복제됐나"에 좌우돼 재현 안 되는 편차가 생긴다. (`PlayerFeel §2-13`) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|이동", meta = (DisplayName = "걷기 속도(0=기본 600)", ClampMin = "0.0"))
	float WalkSpeed = 0.0f;

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

	/** 이 무기를 "보유 무기"로 세지 않는다 — 슬롯의 **기본 무기(맨손)** 전용 플래그.
	 *  맨손은 플레이어가 획득한 무기가 아니라 빈 칸을 막는 기본값이라, 보유 목록에 섞이면
	 *  ① 맨손을 대상으로 한 무기 카드가 풀에 뜨고 ② 아무 무기도 안 주웠는데 "무기 보유"로 판정되고
	 *  ③ 로비 시작 무기 후보로 고를 수 있게 된다. `GetOwnedWeapons()`가 여기서 걸러 낸다.
	 *  일반 무기는 절대 켜지 말 것. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|카드", meta = (DisplayName = "진행(카드/해금)에서 제외 — 맨손 전용"))
	bool bExcludeFromProgression = false;

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

	/** Stat axes this weapon OPTS OUT of for AllWeapons-scope modifier cards (Game.MD §2-4-1 ①). A broad
	 *  "all weapons" card on a listed axis is skipped when this weapon resolves its stats — e.g. a ChargeLaser whose
	 *  recoil is a charge ramp can list RecoilVertical so a global "recoil down" card doesn't touch it. Per-weapon,
	 *  per-axis, AllWeapons-only: ThisWeapon cards (the player deliberately targeted this weapon) always apply. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|카드", meta = (DisplayName = "전체무기 카드 제외 스탯"))
	TArray<EFPSRWeaponStat> AllWeaponsStatExclusions;

	// --- Weapon visual / cosmetic (ADR 0002 True First Person) — all soft refs, null = no visual (no gameplay effect).
	//     ONE set of meshes serves both the owner and remote observers: the weapon hangs off the single body mesh at
	//     WeaponAttachSocket, so there is no 1P/3P duplication to keep in sync (the old 3P block was never authored). ---

	/** Weapon skeletal mesh (firearms). Attached to the body mesh at WeaponAttachSocket on equip. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|메시", meta = (DisplayName = "무기 메시(스켈)"))
	TSoftObjectPtr<USkeletalMesh> WeaponMesh;

	/** Weapon static mesh (e.g. melee knife). Used when WeaponMesh is unset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|메시", meta = (DisplayName = "무기 메시(스태틱/근접)"))
	TSoftObjectPtr<UStaticMesh> WeaponMeshStatic;

	/** Optional per-weapon anim instance applied to the WEAPON mesh on equip. The weapon has its OWN skeleton, so its
	 *  bolt/magazine animation needs its own AnimBP to play WeaponFire/ReloadMontage below. Only applied to skeletal
	 *  weapons; null = no bolt animation (null-safe). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|메시", meta = (DisplayName = "무기 AnimBP"))
	TSoftClassPtr<UAnimInstance> WeaponAnimInstanceClass;

	/** 이 무기를 들었을 때 바디에 링크되는 애니 레이어(ADR 0002 4단계). 바디 AnimBP는 하나뿐이고, 무기별로 달라지는
	 *  포즈(로코모션·에임오프셋)만 이 레이어가 담당한다 — 루트 요 오프셋·왼손 IK·몽타주 슬롯은 무기와 무관하므로
	 *  바디 쪽에 남는다. 같은 애니 세트를 쓰는 무기들은 같은 레이어를 가리키면 되고, 그때 코드는 건드리지 않는다.
	 *  null = 레이어 없음(바디 기본 포즈만) — 리타게팅된 애니가 아직 없는 무기의 정상 상태다.
	 *  ⚠ 반드시 `UFPSRCharacterAnimInstance` 파생이어야 한다. 그게 아니면 링크는 되지만 로코모션·조준·IK 값을 받지
	 *  못해 기본값으로 렌더된다(런타임이 그 타입으로만 값을 밀어 넣는다). 그래서 피커를 경로 메타로 제한했다 —
	 *  타입으로 제한하면 이 헤더가 AnimInstance 헤더를 끌고 와 전 프로젝트 컴파일이 무거워진다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|메시",
		meta = (DisplayName = "바디 애니 레이어", MetaClass = "/Script/FPSRoguelite.FPSRCharacterAnimInstance"))
	TSoftClassPtr<UAnimInstance> BodyAnimLayerClass;

	/** 이 무기를 들었을 때 **1인칭 팔**에 링크되는 애니 레이어(ADR 0003). 바디 레이어와 같은 구조·같은 이유지만
	 *  **완전히 별개**다 — 팔은 카메라에 붙어 자기 스켈레톤에서 자기 그래프를 돌린다(3인칭 애니를 눈높이에서 재생하면
	 *  팔이 낮고 카메라를 감싼다는 것이 이 분리의 출발점이다). 이쪽이 담당하는 것은 무기군별 포즈뿐이고, 몽타주 슬롯과
	 *  왼손 IK는 무기와 무관하므로 팔 본체 그래프에 남는다.
	 *  null = 레이어 없음(팔 기본 포즈) — 팔 애니가 아직 없는 무기의 정상 상태다.
	 *  ⚠ 반드시 `UFPSRFirstPersonArmsAnimInstance` 파생이어야 한다(바디 레이어와 같은 이유 — 런타임이 그 타입으로만
	 *  값을 밀어 넣는다). 바디 레이어를 여기에 넣으면 링크는 되지만 값을 못 받는다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|메시",
		meta = (DisplayName = "1인칭 팔 애니 레이어", MetaClass = "/Script/FPSRoguelite.FPSRFirstPersonArmsAnimInstance"))
	TSoftClassPtr<UAnimInstance> ArmsAnimLayerClass;

	/** Socket on the BODY skeleton the weapon attaches to (NAME_None = body mesh root). Authored on the character's
	 *  skeleton at the grip hand — see ADR 0002 (Blu: SOCKET_Weapon on hand_R). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|메시", meta = (DisplayName = "무기 부착 소켓(바디)"))
	FName WeaponAttachSocket = NAME_None;

	/** Uniform scale applied to the weapon when attached to the body. The animation pack is authored for realistic
	 *  human proportions, so a stylised character needs the weapon shrunk to stay in reach of BOTH hands — the value is
	 *  a property of the weapon/character pairing, not of the mesh (ADR 0002: Blu + Synty rifle = 0.85, measured against
	 *  the tightest animation). 1.0 = no scaling. The attach uses SnapToTargetNotIncludingScale, so the socket's own
	 *  scale is ignored and this is the single place the size is decided. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|메시", meta = (DisplayName = "무기 부착 스케일", ClampMin = "0.1", ClampMax = "3.0"))
	float WeaponAttachScale = 1.0f;

	/** Socket marking where the LEFT hand grips this weapon (left-hand IK). Like AimSocket it may live on a PART (the
	 *  handguard) rather than the receiver, so the character resolves whichever part carries it — swapping the handguard
	 *  then moves the grip with it. NAME_None = no left-hand IK for this weapon (melee / one-handed / unarmed). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|메시", meta = (DisplayName = "왼손 그립 소켓"))
	FName LeftHandSocket = NAME_None;

	/** Socket marking where the RIGHT hand grips this weapon (right-hand IK) — same convention as LeftHandSocket above:
	 *  it may live on a PART rather than the receiver, so the character resolves whichever part carries it. NAME_None =
	 *  no right-hand IK for this weapon (the animation pose stands as authored, unmodified). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|메시", meta = (DisplayName = "오른손 그립 소켓"))
	FName RightHandSocket = NAME_None;

	/** Socket on the WEAPON mesh used as the muzzle-flash origin (cosmetic only; trace origin stays the camera). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|발사 연출", meta = (DisplayName = "총구 소켓(화염 원점)"))
	FName MuzzleSocket = NAME_None;

	/** Rotation offset applied to the muzzle-flash emitter relative to the muzzle socket, so the flash fires down the
	 *  barrel. This pack's weapon-forward is +Y (the same reason AimSocket needs ADSAimRotationOffset Yaw 90), and the
	 *  emitter's authored forward axis may not match the socket's — tune this per weapon (commonly Yaw 90) until the
	 *  flash points out the muzzle. Cosmetic only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|발사 연출", meta = (DisplayName = "총구 화염 회전 오프셋"))
	FRotator MuzzleFlashRotationOffset = FRotator::ZeroRotator;

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

	/** While aiming, suppress the BODY fire recoil montage for the owner. Under the old camera-parented arms this was a
	 *  correctness fix (the montage moved the sight); since ADR 0002 places the weapon against the camera in ADS, upper-
	 *  body recoil can no longer disturb the reticle, so this is now purely "do I want to watch my own body buck while
	 *  aiming". Owner-local — remote observers always see the recoil, so false is the setting that matches them. The
	 *  shot still reads via muzzle flash + sound + camera recoil. Only affects ADS weapons (BaseStats.bHasADS) while
	 *  aiming — hip fire and non-ADS weapons keep the full recoil montage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|조준(ADS)", meta = (DisplayName = "조준 중 발사몽타주 억제(바디)"))
	bool bSuppressFireMontagesWhileADS = true;

	/** 정지 상태에서 남는 조준 흔들림 비율(0~1). 0 = 서 있으면 완전히 정지(예전 동작 — 조준 화면이 사진처럼
	 *  얼어붙는다), 1 = 이동 중과 같은 크기. 이동하면 이 값에서 1까지 부드럽게 올라간다.
	 *  흔들림 자체의 크기는 아래 「조준 흔들림 좌우/상하(도)」가 정한다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|조준(ADS)", meta = (DisplayName = "조준 흔들림 정지 시 비율", ClampMin = "0.0", ClampMax = "1.0"))
	float ADSSwayIdleScale = 0.35f;

	/** 조준경을 축으로 돌지 않고 **무기 전체를 화면에서 그대로 밀어내는** 흔들림(cm). 0 = 끔(기본).
	 *
	 *  ⚠️ 이건 **조준점(레티클)이 같이 움직인다.** 강체는 한 점을 고정한 채 옆으로 평행이동할 수 없어서,
	 *  "총은 흔들리되 조준경은 화면에 박혀 있게"는 **조준경 축 회전으로만** 가능하다(위 좌우/상하 도 값).
	 *  기본 원칙은 "조준은 플레이어 입력의 것이고 연출이 그걸 움직이면 안 된다"이므로 기본 0이며,
	 *  조준점이 조금 흔들리는 편이 낫다고 판단할 때만 올린다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|조준(ADS)", meta = (DisplayName = "조준 흔들림 좌우 이동(cm, 조준점도 흔들림)", ClampMin = "0.0"))
	float ADSSwayFreeHorizontalCm = 0.0f;

	/** 위 항목의 상하 짝. 0 = 끔(기본). 같은 주의사항이 그대로 적용된다(조준점이 같이 움직인다). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|조준(ADS)", meta = (DisplayName = "조준 흔들림 상하 이동(cm, 조준점도 흔들림)", ClampMin = "0.0"))
	float ADSSwayFreeVerticalCm = 0.0f;

	/** While aiming, suppress the WEAPON bolt/action montage too. Default false: the bolt keeps cycling in ADS as fire
	 *  feedback (it animates the bolt bone, not the sight, so the reticle holds). Set true only for a weapon whose bolt
	 *  montage visibly disturbs the aimed sight. Independent of bSuppressFireMontagesWhileADS (which gates the BODY montage). */
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

	/** 절차 무기 모션(힙) — 살아있는 총 코스메틱(룩스웨이·걷기밥·발사킥). ADS 진입 시 페이드아웃. owner-local·복제0.
	 *  ⚠ True First Person(ADR 0002) 이후: 이 레이어는 무기만 움직이고 무기를 쥔 손은 안 움직인다 → 값이 크면 총이 손에서
	 *  뜬다. 힙 움직임은 바디 애니가 맡는 것이 원칙. 기본값(전부 0) = 이 레이어 없음. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|절차 무기모션(힙)", meta = (DisplayName = "힙 절차 무기모션 프로파일"))
	FFPSRProceduralWeaponMotionProfile ProceduralWeaponMotion;

	/** Optional montage played on the BODY when this weapon is equipped. Plays on every machine (one mesh, one anim
	 *  graph — ADR 0002 invariant 4), so remote observers see the swap too. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|애니 몽타주", meta = (DisplayName = "장착 몽타주(바디)"))
	TSoftObjectPtr<UAnimMontage> EquipMontage;

	/** Optional montage played on the BODY each shot. Owner plays it locally (PlayWeaponFireCosmetics, subject to
	 *  bSuppressFireMontagesWhileADS); remote observers get the same montage via MulticastFireCosmetics. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|애니 몽타주", meta = (DisplayName = "발사 몽타주(바디)"))
	TSoftObjectPtr<UAnimMontage> FireMontage;

	/** Optional montage played on the WEAPON mesh (WeaponMesh) each shot — the bolt/action cycle (A_FP_WEP_<W>_Fire).
	 *  Played on the same fire hook as FireMontage so the bolt syncs with the body recoil. Needs WeaponAnimInstanceClass
	 *  set. Null = no bolt animation (null-safe). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|애니 몽타주", meta = (DisplayName = "발사 몽타주(무기/노리쇠)"))
	TSoftObjectPtr<UAnimMontage> WeaponFireMontage;

	/** Optional montage played on the BODY on reload start, for owner and remotes alike. Driven by UFPSRWeaponInstance's
	 *  OnRep_Reloading (server-confirmed edge), scaled so its play length matches the resolved ReloadTime. Null = none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|애니 몽타주", meta = (DisplayName = "재장전 몽타주(바디)"))
	TSoftObjectPtr<UAnimMontage> ReloadMontage;

	/** Optional montage played on the WEAPON mesh (WeaponMesh) on reload — the magazine/bolt action (A_FP_WEP_<W>_
	 *  Reload). Played on the same reload hook as ReloadMontage, rate-scaled to the ReloadTime. Needs
	 *  WeaponAnimInstanceClass set. Null = no weapon reload animation (null-safe). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|애니 몽타주", meta = (DisplayName = "재장전 몽타주(무기)"))
	TSoftObjectPtr<UAnimMontage> WeaponReloadMontage;

	/** 위 두 몽타주의 **1인칭 팔** 버전(ADR 0003). 바디 몽타주와 값이 겹치는 게 아니라 **스켈레톤이 달라 물리적으로 다른
	 *  에셋**이다(팔=`S_Mannequin`, 바디=Blu) — 같은 값을 두 번 저작하게 만드는 필드가 아니므로 "무기 데이터는 한 벌"
	 *  원칙(ADR 0003 불변식 13)에 걸리지 않는다. 오너 화면에만 재생된다(팔은 오너 머신에만 존재).
	 *  null = 팔 애니 없음(null-safe) — 바디 쪽은 그대로 재생된다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|애니 몽타주", meta = (DisplayName = "장착 몽타주(1인칭 팔)"))
	TSoftObjectPtr<UAnimMontage> ArmsEquipMontage;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|애니 몽타주", meta = (DisplayName = "재장전 몽타주(1인칭 팔)"))
	TSoftObjectPtr<UAnimMontage> ArmsReloadMontage;

	/** 1인칭 팔 Idle 애니메이션 — 총모션 스튜디오(GunMotionTool_Spec.md §0)가 저작 베이스로 표시·검증하는 용도.
	 *  ABP의 실제 Idle 포즈 배선은 현행(링크드 애니 레이어)을 그대로 쓴다 — 이 필드는 그 배선을 대체하지 않고,
	 *  스튜디오가 "이 무기의 Idle이 뭔지" 읽어오는 참조일 뿐이다. null = 스튜디오에서 이 무기의 베이스를 못 찾음. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|애니 몽타주", meta = (DisplayName = "Idle 애니메이션(1인칭 팔)"))
	TSoftObjectPtr<UAnimSequence> ArmsIdleAnim;

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

	/** Optional modular cosmetic part slots child-attached to the skeletal weapon mesh on equip (U15, W-U1b 재설계).
	 *  Static/melee weapons and empty lists attach nothing (null-safe). Parts are visible to everyone, same as the
	 *  weapon they hang off (ADR 0002 — they used to be OnlyOwnerSee). Each slot is either purely structural (no
	 *  EvolutionFragment) or evolves in place (스택 진화 — FFPSRWeaponPartAttachment.Stages) while its Socket stays
	 *  fixed. A part may also carry AimSocket / LeftHandSocket, which is why swapping a sight or a handguard moves the
	 *  ADS reference and the left-hand grip with it. Read-only cosmetic: never touches
	 *  gameplay/cards/save/replication (§2-A). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|모듈 파츠", meta = (DisplayName = "파츠 슬롯(구조/진화)"))
	TArray<FFPSRWeaponPartAttachment> WeaponParts;

	/** Cascade muzzle-flash particle spawned at MuzzleSocket each shot (owner-client local). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "무기|발사 연출", meta = (DisplayName = "총구 화염 파티클"))
	TSoftObjectPtr<UParticleSystem> MuzzleFlash;

	// --- The old 3P visual block (U19: WeaponMesh3P / WeaponAttachSocket3P / FireMontage3P / ReloadMontage3P) is gone.
	//     ADR 0002 unified it into the block above: everyone sees the same mesh and the same montages, so the owner and
	//     remote observers can no longer drift apart. Nothing was lost — a survey of all 9 weapon DataAssets found the
	//     3P fields had never been authored (which is why teammates' weapons were invisible until now). ---

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
