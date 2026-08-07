// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "Weapon/FPSRWeaponDataAsset.h"
#include "FPSRCharacter.generated.h"

class UAbilitySystemComponent;
class UFPSRAbilitySystemComponent;
class UFPSRCharacterMovementComponent;
class UCameraComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UInputAction;
class UFPSRWeaponInventoryComponent;
class UFPSRWeaponInstance;
class UFPSRWeaponFireComponent;
class UFPSRRecoilComponent;
class UFPSRWeaponDataAsset;
class UMaterialInterface;
class UFPSRPlayerFeedbackComponent;
class UFPSRReviveComponent;
class UFPSRBlindspotAudioComponent;
struct FInputActionValue;
struct FMinimalViewInfo;
class UStaticMeshComponent;
class UMeshComponent;
class UAnimInstance;
class UAnimMontage;
class USoundBase;
class UParticleSystem;
class UUserWidget;
#if WITH_EDITOR
struct FPropertyChangedEvent;
#endif

/** Base player character: first-person camera + ONE shared body mesh (True First Person, ADR 0002) + Enhanced Input +
 *  weapon inventory/firing. ASC lives on PlayerState. */
UCLASS()
class FPSROGUELITE_API AFPSRCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	/** Takes an FObjectInitializer so the character's movement component can be swapped to
	 *  UFPSRCharacterMovementComponent (ADR 0001: that component is the single owner of locomotion state). */
	AFPSRCharacter(const FObjectInitializer& ObjectInitializer);

	/** The locomotion component, already typed. Never null in practice (the ctor installs it), but callers should
	 *  still null-check — a Blueprint subclass can technically override the component class. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Movement")
	UFPSRCharacterMovementComponent* GetFPSRMovement() const;

	/** True when this pawn may START or CONTINUE special locomotion (slide today, wall-hang later). False during the
	 *  card-selection freeze and while downed. Reads replicated state (GameState run-paused + PlayerState life state)
	 *  so the server and the owning client agree — the movement component gates on this, and prediction requires both
	 *  machines to reach the same answer. Public because the movement component is a separate class. */
	bool CanPerformSpecialMovement() const;

	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;

	/** Weapon visibility follows the movement mode (hidden while holding a wall — that clip is bare-handed). Hooked on
	 *  the character rather than in the movement component so the movement component keeps being something others only
	 *  query, per ADR 0001. */
	virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode = 0) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void NotifyControllerChanged() override;

	/** First-person spectate fix (U9 §2-13): a DBNO teammate views this pawn via SetViewTarget. When this pawn isn't
	 *  locally controlled, UCameraComponent skips bUsePawnControlRotation (it only follows the LOCAL controller) so the
	 *  spectator's view would track yaw (replicated actor rotation) but not pitch. Drive the view from
	 *  GetBaseAimRotation() — its pitch comes from the replicated RemoteViewPitch16 — so up/down aim is reflected. The
	 *  locally-controlled owner keeps the default camera-component path. */
	virtual void CalcCamera(float DeltaTime, struct FMinimalViewInfo& OutResult) override;

	/** Drives the stance camera blend (and, in debug builds, the on-screen readouts). Ticks after the movement
	 *  component so the cached eye position is the one movement actually left behind this frame. */
	virtual void Tick(float DeltaSeconds) override;

#if ENABLE_DRAW_DEBUG
	/** Top-right movement readout (speed + locomotion state), drawn through the engine's debug-draw service because
	 *  AddOnScreenDebugMessage can only stack at the top LEFT, where the existing HP/run debug already lives. Registered
	 *  for the local player only; toggle with FPSR.Movement.Debug. */
	void DrawMovementDebug(class UCanvas* Canvas, class APlayerController* PC);
	FDelegateHandle MovementDebugDrawHandle;
#endif

	/** Drop / raise the first-person camera with the crouch capsule. Without this the camera keeps its standing offset
	 *  and pokes out the TOP of the crouched capsule (standing eye 152cm vs crouched capsule 80cm tall), which lets the
	 *  view clip through geometry the capsule is still blocked by. Super updates BaseEyeHeight for us — the engine
	 *  derives CrouchedEyeHeight from the movement component's crouched half-height — so both overrides just push the
	 *  refreshed value onto the camera. Reacting to a state the movement component owns; not owning it (invariant 1). */
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	//~IAbilitySystemInterface
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	//~End IAbilitySystemInterface

	/** Owner-client: request a server-authoritative reload (used by auto-reload when the mag empties). */
	void RequestReload();

	/** Server: apply contact damage from an enemy to this character's Health (clamped via HealthSet). */
	void ApplyContactDamage(float DamageAmount, AActor* DamageInstigator, FGameplayTag DamageType = FGameplayTag());

	/** Push the card/meta move-speed multiplier layer into the movement component. Called by UFPSRCombatSet when
	 *  MoveSpeedMultiplier changes (server + client). The resulting MaxWalkSpeed is composed there, not here. */
	void ApplyMoveSpeedMultiplier(float Mult);

	/** Push the downed (DBNO) locomotion layer + refresh enemy-pawn collision (U9 DBNO). Called server-side on
	 *  DBNO/revive and on the owning client from AFPSRPlayerState::OnRep_LifeState so movement prediction matches
	 *  (mirrors ApplyMoveSpeedMultiplier). */
	void ApplyDownedLocomotion(bool bDowned);

	/** Server: start a grace window of Seconds — i-frames in ApplyContactDamage + the capsule passes through enemy
	 *  pawns (ECC_Pawn) so the player can escape a surround. Used by the post-revive grace
	 *  (UFPSRReviveComponent::PerformRevive) and the post-card-selection resume grace (HandleRunStateChanged_Movement).
	 *  No-op off authority or for Seconds <= 0. (U9 §2-13) */
	void BeginGraceWindow(float Seconds);

	/** Local reviver HUD (U9 §2-13): if this player is alive and standing within a DBNO ally's revive radius, returns
	 *  that ally's revive progress (0..1) so the HUD can show a "reviving teammate" gauge; returns -1 when not reviving
	 *  anyone. Client-side query over already-replicated data (LifeState + ReviveProgress). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Revive")
	float GetReviveTargetProgress() const;

	/** BlueprintPure aim-down-sights state. Forwards the aim flag on WeaponFire (which is only VisibleAnywhere, not
	 *  BlueprintReadable, so an AnimBP can't reach it directly — hence this accessor). Resets to false the instant ADS is
	 *  released (Input_ADSReleased -> SetAiming(false)), so an aim state driven by this reverts cleanly to hip.
	 *
	 *  VALID ON EVERY MACHINE — this is what the shared body AnimBP reads for the aim pose (ADR 0002 step 4). The owner
	 *  and the server write it directly (input edge / ServerSetAiming); non-owning clients receive it replicated
	 *  (COND_SkipOwner, push model). One accepted asymmetry: if the server REJECTS an aim-on (freeze / downed) the owner
	 *  is not sent a correction, so it can read true there for up to a round trip — the same OnRep that made the client's
	 *  view stale is what clears it. See UFPSRWeaponFireComponent::SetAiming + GetLifetimeReplicatedProps. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool IsAiming() const;

	/** Whether the equipped weapon is mid-reload. The same facade role as IsAiming above: the inventory component is not
	 *  Blueprint-readable from an AnimBP, and the reload flag is per-weapon-instance replicated state, so graphs read it
	 *  through here. VALID ON EVERY MACHINE (the flag replicates on the weapon instance). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool IsReloading() const;

	/** Owner-local ADS blend alpha (0 = hip, 1 = fully aimed). Only updated on the locally-controlled client. (W-U2) */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	float GetADSAlpha() const { return CurrentADSAlpha; }

	/** True while the owner-local ADS visual is active (procedural-sight blend alpha past the visual threshold — needs
	 *  an AimSocket). Reload-aware. Gates the scope overlay + weapon-hide (a scope IS a sight, so requiring the socket
	 *  is correct here). For the crosshair use IsADSFOVActive instead. (W-U2) */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool IsADSVisualActive() const;

	/** True while the weapon is visually committing to ADS (FOV zoom / sight): aiming an ADS weapon and not reloading.
	 *  AimSocket-INDEPENDENT (unlike IsADSVisualActive) so a bHasADS weapon without a procedural sight still reads as
	 *  aiming — it tracks the same commit the FOV zoom uses, so the crosshair-hide can't desync from the zoom.
	 *  Reload-aware. Owner-local. (W-U2) */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool IsADSFOVActive() const;

	/** True while a full-screen SCOPE is visually active: ADS visual active AND the currently-active sight part carries
	 *  a scope-overlay descriptor. Owner-local. (W-U2) */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool IsScopeVisualActive() const;

	/** Owner-local: resolve the effective camera FOV target for the ADS/scope interp (called from UpdateCameraFieldOfView).
	 *  Non-scope weapons return the passed hip target unchanged; scope weapons apply the strong scope FOV and drop the
	 *  zoom during a reload (per design). bBaseWantsADS = the caller's existing (bIsAiming && Stats.bHasADS).
	 *  HipTargetFOV is the UNAIMED target the caller composed (base FOV plus any stance offset), so returning it is what
	 *  makes an aiming weapon REPLACE those offsets rather than stack with them — a scope sits near 30 degrees, where a
	 *  few degrees of stance offset would be a visible change of magnification. (W-U2) */
	float ResolveADSTargetFOV(float HipTargetFOV, float BaseADSFOV, bool bBaseWantsADS) const;

	/** The unaimed camera FOV this character composes everything else on top of — captured from the camera component's
	 *  authored value at BeginPlay, and the single value a future settings menu has to move. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Camera")
	float GetBaseFieldOfView() const { return BaseFieldOfView; }

	/** Point a settings menu at this. The camera is not snapped: UpdateCameraFieldOfView eases to the new base on its
	 *  own, and every offset (slide, and whatever follows) is expressed relative to it, so they all follow for free.
	 *  Clamped to GetBaseFieldOfViewRange. Owner-local presentation — never replicated. */
	UFUNCTION(BlueprintCallable, Category = "FPSR|Camera")
	void SetBaseFieldOfView(float NewBaseFieldOfView);

	/** Legal span for the base FOV, as (min, max) degrees. Bind a settings slider's range to THIS rather than to its
	 *  own numbers — otherwise the slider can offer a value SetBaseFieldOfView silently clamps away. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Camera")
	FVector2D GetBaseFieldOfViewRange() const { return BaseFieldOfViewRange; }

	/** Owner-local: hide/show the weapon meshes (and their child parts) based on whether a full-screen scope is active.
	 *  Called each frame from the weapon-fire tick (which already no-ops for non-local pawns). Component visibility is a
	 *  local render flag, not replicated, so this only clears the OWNER's view — teammates still see the gun. (W-U2) */
	void UpdateScopeWeaponVisibility();

	/** Single owner of "is the weapon drawn". Composes every reason to hide it (scope, wall) so the reasons cannot
	 *  cancel each other out. Idempotent — safe to call from spawn, possession, equip and movement-mode paths.
	 *  @param bForce  Re-apply to the components even when the composed answer has not changed. Required after the
	 *                 meshes are re-pointed or re-attached: that restores them to visible while this class's latch
	 *                 still says "hidden", so a plain call would early-out and leave a wall-hung player holding a gun. */
	void RefreshWeaponVisibility(bool bForce = false);

	/** Single owner of the first-person body/arms split, for the same reason RefreshWeaponVisibility exists: several
	 *  lifecycle paths decide it (spawn, possession, controller replication, view-target changes) and two independent
	 *  togglers would undo each other. Idempotent. Runs on every machine but only ever splits the pawn we are LOOKING
	 *  THROUGH — a proxy that hid its own body would erase a teammate from the screen. */
	void RefreshFirstPersonRendering();

	/** True while the owner is being shown first-person arms instead of their own body. The one answer that decides
	 *  which hand the weapon hangs off (ADR 0003 invariant 14), which component the ADS solve moves, and whether the
	 *  body's left-hand IK has anything to point at. Local render state — never replicated, never a gameplay input. */
	bool IsFirstPersonSplitActive() const { return bFirstPersonSplitActive; }

	/** Re-attach the weapon meshes to whichever hand is currently VISIBLE (arms when the split is on, body otherwise)
	 *  and re-apply the first-person render tag that goes with it. Split out of RefreshEquippedWeaponVisual because the
	 *  split can flip without the weapon changing (going down, spectating, coming back) — and re-running the full equip
	 *  refresh for that would replay the equip montage every time. Idempotent. */
	void AttachWeaponMeshes();

	/** 이 무기가 붙을 소켓 이름. DA 쪽에 WeaponAttachSocket 이 있으면 그것, 없으면 캐릭터 기본(WeaponAttachSocketName)
	 *  으로 폴백한다. WeaponAttachSocketName 은 protected 라 이 클래스 밖(에디터의 무기 어셈블러 툴)에서 직접 읽을
	 *  수 없어서, 런타임(AttachWeaponMeshes)과 에디터 툴이 소켓 이름에 대해 **같은 규칙**을 쓰도록 그 규칙 자체를
	 *  여기 한 곳에 둔다 — 규칙이 갈리면 툴에서 구운 소켓과 게임이 실제로 붙는 소켓이 서로 달라진다. */
	FName ResolveWeaponAttachSocket(const UFPSRWeaponDataAsset* Weapon) const;

	/** 1인칭 구도 = **카메라가 팔에 대해 어디에 있는가** + 그 카메라의 뷰 정보. 무기 어셈블러(에디터 툴)가 프리뷰
	 *  카메라를 이 값에 놓아 "플레이어 눈에 총이 어떻게 걸리는가"를 재현하는 데 쓴다 — 자유 시점으로는 그립을
	 *  판정할 수 없다는 지적에서 나온 기능이다.
	 *
	 *  🚨 **부착 위상을 가정하지 않는다.** 팔이 카메라에 붙어 있든, 둘 다 캡슐에 붙어 있든, 사이에 무엇이 끼든
	 *  실제 컴포넌트 월드 트랜스폼끼리 상대를 구하므로 배선이 바뀌어도 값이 따라간다. 그래서 상수로 박지 말고
	 *  이걸 부를 것.
	 *  🚨 **CDO 가 아니라 스폰된 인스턴스에서 불러야 한다** — 등록되지 않은 CDO 컴포넌트의 월드 트랜스폼은
	 *  의미가 없다. 에디터 툴은 프리뷰 씬에 잠깐 스폰해서 읽고 파괴한다(엔진도 FBlueprintEditor 가 프리뷰 씬에
	 *  BP 를 스폰한다 — BlueprintEditor.cpp). 프리뷰 월드는 BeginPlay 를 걸지 않으므로 게임플레이 초기화는 안 돈다.
	 *
	 *  🚨 FOV 하나가 아니라 **FMinimalViewInfo 통째**로 준다. 종횡비(AspectRatio)·bConstrainAspectRatio·
	 *  AspectRatioAxisConstraint 가 있어야 "이 종횡비의 플레이어가 실제로 보는 구도"를 계산할 수 있고
	 *  (CameraStackTypes.cpp:263,287 — 축 제약에 따라 세로/가로 중 무엇이 고정되는지가 갈린다), UE 5.7 의 1인칭
	 *  렌더 파라미터(FirstPersonFOV/FirstPersonScale)도 여기 실려 온다. 게임이 쓰는 것과 **같은 호출**
	 *  (UCameraComponent::GetCameraView)로 채우므로 카메라 설정이 바뀌면 툴이 저절로 따라간다 — 필드를 손으로
	 *  베끼면 그때부터 갈린다.
	 *
	 *  두 컴포넌트 중 하나라도 없으면 false 를 반환하고 출력값은 건드리지 않는다. */
	bool GetFirstPersonViewSetup(FTransform& OutCameraRelativeToArms, FMinimalViewInfo& OutViewInfo) const;

	/** 활성 사이트의 스코프 오버레이 위젯 클래스(스코프 시각 활성 시). 없으면 null(HUD가 폴백 사용). 호출은 스코프
	 *  진입 엣지에서(프레임마다 아님) — 소프트 참조를 동기 로드한다. (스코프 위젯화) */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	TSubclassOf<UUserWidget> GetActiveScopeOverlayWidgetClass() const;

	/** True while a scope is active AND its descriptor requests the edge vignette. For the HUD scope overlay. (W-U2) */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool IsScopeVignetteEnabled() const;

	/** Refresh the equipped weapon's mesh + attachment + cached cosmetics when the equipped weapon changes (called from
	 *  the inventory's server EquipSlot + client OnRep). Runs on EVERY machine: one weapon mesh now serves the owner and
	 *  remote observers alike (ADR 0002), so a client that skipped this would render a teammate holding nothing. */
	void RefreshEquippedWeaponVisual();

	/** Owner-client: play the equipped weapon's per-shot cosmetics (fire montage + sound + muzzle flash). */
	void PlayWeaponFireCosmetics();

	/** Owner-client per-frame procedural aim-down-sights: place the ARMS (or, with no first-person arms authored, the
	 *  WEAPON itself) relative to the camera so the weapon's AimSocket sits on the camera's forward centre-line when
	 *  aiming, interpolated by the weapon's ADSInterpSpeed. Moving the arms is what keeps the gun in the hand that holds
	 *  it (ADR 0003); the solve is identical either way — only the target and the write space differ. Called from
	 *  UFPSRWeaponFireComponent::TickComponent (which already ticks + owns the aim state). No-op on remote pawns /
	 *  weapons without ADS or an AimSocket. Interpolates location AND rotation: when the weapon's bADSAlignRotation is
	 *  set it aligns the AimSocket frame to the camera, removing the authored hip cant so the sight reads level (else
	 *  translation-only, keeping the authored weapon tilt).
	 *
	 *  ADR 0002 measured the retargeted aim pose from the real eye position and found the sight +29.7 deg across / -27.1
	 *  deg down — outside a 55-deg ADS screen — so the pack's pose is a starting point, not a residual to nudge. The gun
	 *  is RE-PLACED against the camera, which is the single authored exception to invariant 4: remote observers keep the
	 *  weapon on the grip socket and read the aim from the body animation. Adding a second exception means re-opening
	 *  invariant 4 itself. */
	void UpdateAimDownSights(float DeltaTime);

	/** Left-hand grip anchor for a skeletal mesh's two-bone IK (ADR 0002 step 4): the WORLD transform of the equipped
	 *  weapon's LeftHandSocket, resolved to whichever modular part carries it (handguard) or the receiver. Returns false
	 *  when this weapon authors no left-hand grip (melee / one-handed / unarmed) or the socket is missing — the pack's
	 *  bullpup handguards have no SOCKET_LeftHand, so the AnimBP must branch on this rather than assume a target.
	 *  Set the Two Bone IK node's effector space to World Space.
	 *
	 *  @param ForMesh  The mesh that wants to reach for it. Answers false unless the grip is actually attached to that
	 *                  mesh, which is what keeps the two graphs independent (ADR 0003 invariant 11): with the arms up,
	 *                  the weapon hangs in CAMERA space, and a body that still reached for it would drag its left arm
	 *                  toward the camera — visible to the owner as a mangled arm in their own shadow. Neither caller
	 *                  has to know the other exists; each only asks "is it on me?".
	 *  GAME THREAD ONLY: read it from NativeUpdateAnimation / BlueprintUpdateAnimation into a member, not from a
	 *  thread-safe update (it walks component attachment state). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool GetLeftHandGripTransform(const USceneComponent* ForMesh, FTransform& OutGripWorld) const;

	/** Gun-anchor IK (fparms-gunanchor-ik): the equipped weapon's grip frame expressed as a CONSTANT in ik_hand_gun BONE
	 *  SPACE — i.e. the same space as hand_r once the arms' animgraph does CopyBone(ik_hand_gun := hand_r, component
	 *  space). Feed this straight into a Transform (Modify) Bone node on ik_hand_r / ik_hand_l with Bone Space =
	 *  "Parent Bone Space" (ik_hand_gun IS that parent in the retarget rig: ik_hand_root > ik_hand_gun > ik_hand_l/r).
	 *
	 *  🚨 POSE MUST NOT ENTER THIS VALUE — no world transform, no live bone read of an animated chain. It is composed
	 *  ENTIRELY from fixed, authored offsets (part/socket local transforms + the weapon's own RelativeTransform once
	 *  attached — see AttachWeaponMeshes) precisely so it stays correct without re-solving every frame: it only needs
	 *  to change when the equipped weapon or its parts change, which is exactly when it's recached (see
	 *  CachedRightGripInGun / RefreshHandGripInGunFrameCache). A pose-dependent read here would reintroduce the
	 *  one-frame lag this refactor exists to remove (see UFPSRFirstPersonArmsAnimInstance).
	 *
	 *  Same ForMesh gate as GetLeftHandGripTransform above (only answers true when the grip is actually attached to
	 *  THIS mesh — in practice always the first-person arms, since the ik_hand_gun rig only exists there).
	 *  GAME THREAD ONLY (same reason as GetLeftHandGripTransform). */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool GetRightHandGripInGunFrame(const USceneComponent* ForMesh, FTransform& OutGripInGun) const;

	/** Left-hand counterpart of GetRightHandGripInGunFrame above — same gun-frame contract, same gate, same caching.
	 *  Both hands share one internal composition helper; only the resolved grip component/socket/offset differ. */
	UFUNCTION(BlueprintPure, Category = "FPSR|Weapon")
	bool GetLeftHandGripInGunFrame(const USceneComponent* ForMesh, FTransform& OutGripInGun) const;

	/** Play reload cosmetics on a server-confirmed reload-start edge (called from UFPSRWeaponInstance::OnRep_Reloading,
	 *  which fires on every client holding the replicated instance). One body mesh, one montage, owner and remotes alike
	 *  (ADR 0002 invariant 4), plus the weapon's own magazine/bolt montage. No-op when bIsReloading is false, during the
	 *  level-up freeze, or when the equipped weapon has no reload montage. The play rate is scaled so the montage length
	 *  matches the ReloadTime. */
	void HandleReloadStateChanged(bool bIsReloading);

	/** W-U1 signature-diff rebuild: the equipped weapon's stat modifiers / behavior fragments changed (parts may need
	 *  to evolve). Coalesced to next tick, equipped-only, no-op on a dedicated server. Called from
	 *  UFPSRWeaponInstance::NotifyOwnerModifiersChanged and the PlayerState's AllWeapons-mod sites (mirrors the
	 *  cross-class notify pattern of HandleReloadStateChanged above). */
	void NotifyEquippedWeaponModifiersChanged(const UFPSRWeaponInstance* ChangedInstance);

	/** Server->all: play the spatialized fire SFX for REMOTE observers so teammates hear each other's weapon fire
	 *  (B4). The owning client already played it locally (PlayWeaponFireCosmetics), so the implementation early-outs
	 *  on IsLocallyControlled to avoid double-play. Unreliable (cosmetic — drops gracefully on packet loss). Fired
	 *  once per server-confirmed shot from FPSRWeaponHooks::NotifyFire (the central, all-weapons fire-confirm site). */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFireCosmetics();

protected:
	/** Place the first-person camera for this frame: the current BaseEyeHeight plus whatever is left of the stance
	 *  transition's held-back offset. Runs every frame from Tick — the eye height has to keep moving after the stance
	 *  itself has already flipped. */
	void UpdateStanceCamera();

	/** Set the first-person camera's FOV for this frame. The SINGLE writer of FieldOfView, for the same reason
	 *  UpdateStanceCamera is the single writer of the camera's position (ADR 0001 invariant 10): the value is composed
	 *  from layers that each know nothing about the others — the settings base, the slide offset, and the weapon's ADS
	 *  or scope zoom — and any second writer would simply erase whichever layers it doesn't know about.
	 *
	 *  Lives on the character rather than the weapon because the camera is the character's, and because ADR 0001
	 *  invariant 4 keeps the fire/ADS path from knowing the movement state at all: the weapon supplies a zoom target,
	 *  this asks the movement component whether we are sliding. Running from Tick (not the weapon-fire tick) is also
	 *  what keeps the FOV alive with no weapon equipped. Owner-local; a remote pawn keeps its authored FOV, which is
	 *  what a DBNO teammate spectating it renders through (CalcCamera). */
	void UpdateCameraFieldOfView(float DeltaSeconds);

	/** Called from the crouch overrides once the capsule and BaseEyeHeight have already changed. Measures how far the
	 *  view was about to jump and holds it back by exactly that much, so UpdateStanceCamera can ease it away. */
	void BeginStanceCameraBlend();

	/** Hide this pawn's head while we are looking THROUGH its eyes, so True First Person doesn't render the inside of a
	 *  skull. ADR 0002 invariant 5: the condition is the VIEW TARGET, not "is this the local player" — a DBNO teammate
	 *  spectating this pawn looks through this camera too (head must go), and the later spectator/debug 3P rig becomes
	 *  the view target instead (head must come back). Local render state only; never replicated. */
	void UpdateFirstPersonBodyVisibility();

	/** "Is the local viewer looking through THIS pawn's eyes." One question, three consumers (head hiding, the arms
	 *  split, which hand holds the weapon) — they have to agree, so they share the answer rather than each deriving it.
	 *  False on a dedicated server and on every remote pawn. Cheap: a pointer compare behind two null checks. */
	bool IsViewedThroughOwnEyes() const;

	/** Render tag the weapon meshes and their modular parts carry right now: FirstPerson while they ride the camera-space
	 *  arms (the engine then forces every shadow flag off), None while they ride the body. One accessor so the meshes,
	 *  the parts rebuilt on an equip, and the parts already attached can never disagree. */
	EFirstPersonPrimitiveType GetWeaponFirstPersonPrimitiveType() const;

	/** Resolve which mesh carries the equipped weapon's LeftHandSocket — the modular part that owns it (handguard) or
	 *  the receiver. Null when this weapon authors no left-hand grip. Shared by the AnimBP getter. */
	UMeshComponent* ResolveLeftHandGripComponent() const;

	/** Right-hand mirror of ResolveLeftHandGripComponent — resolves CachedRightHandSocket the same way. */
	UMeshComponent* ResolveRightHandGripComponent() const;

	/** Shared composition behind GetRightHandGripInGunFrame / GetLeftHandGripInGunFrame (both hands only differ in
	 *  which grip component/socket/offset they pass in). Same 3-stage gate as GetLeftHandGripTransform (null resolve,
	 *  ForMesh-attached check), then a POSE-FREE static composition:
	 *   1. GripSocket's transform in GripComp's own RTS_Component space (+ GripOffset) — the grip point relative to
	 *      whichever mesh actually carries the socket (a part, or the receiver).
	 *   2. Climb GripComp's attachment chain up to (not including) ActiveWeaponMesh, staying in RTS_Component space
	 *      the whole way, so nothing here ever reads a posed transform.
	 *   3. ActiveWeaponMesh's own RelativeTransform — relative to WHATEVER it is currently attached to. When
	 *      AttachWeaponMeshes used the ik_hand_gun-bone anchor (see there), that RelativeTransform IS ALREADY the
	 *      fixed gun-frame offset (no further hop needed). When it fell back to Snap-on-AttachSocket (body, or an
	 *      arms mesh with no ik_hand_gun bone), RelativeTransform is only relative to that SOCKET, so one more fixed
	 *      hop through the socket's own RTS_ParentBoneSpace offset finishes the trip into gun-frame. Which of the two
	 *      applies is read from ActiveWeaponMesh's OWN live attach-socket name — not assumed — so this stays correct
	 *      under either AttachWeaponMeshes path without the caller having to know which one is live.
	 *  Scale is carried through the composition (it affects position) and normalized to (1,1,1) only on the final
	 *  result — effectors read location/rotation only, but an intermediate normalization would drop the weapon's
	 *  WeaponAttachScale from the position math. */
	bool ComputeGripInGunFrame(UMeshComponent* GripComp, FName GripSocket, const FVector& GripOffset,
		const USceneComponent* ForMesh, FTransform& OutGripInGun) const;

	/** Recompute CachedRightGripInGun / CachedLeftGripInGun from ComputeGripInGunFrame and store them. Called once on
	 *  attach/part changes (AttachWeaponMeshes, RebuildPartsFromSelection, RefreshEquippedWeaponVisual) rather than
	 *  every animation frame — the gun-frame grip is a constant between those events (see GetRightHandGripInGunFrame),
	 *  so re-solving it per frame would just repeat the same answer at a per-frame cost. */
	void RefreshHandGripInGunFrameCache();

	/** Swap the body's per-weapon anim layer to the equipped weapon's (ADR 0002 step 4). Called from
	 *  RefreshEquippedWeaponVisual, so it runs on every machine — the layer decides the POSE, and a client that skipped
	 *  it would show a teammate holding a rifle in an unarmed stance. Passing null (or a weapon with no authored layer)
	 *  unlinks back to the body's own base pose, which is the correct state for a weapon whose animations don't exist
	 *  yet. Idempotent: re-linking the same class is skipped. */
	void RefreshBodyAnimLayer(const class UFPSRWeaponDataAsset* Weapon);

	/** The same swap on the FIRST-PERSON ARMS (ADR 0003), from the weapon's ArmsAnimLayerClass. A separate function over
	 *  a separate component with a separate field, because the two graphs are independent by design (invariant 11) —
	 *  merging them is the first step back toward one pose serving both cameras. Null unlinks to the arms' base pose,
	 *  the correct state for a weapon whose first-person animations don't exist yet. Idempotent. */
	void RefreshArmsAnimLayer(const class UFPSRWeaponDataAsset* Weapon);

	/** Full distance the eye travels between standing and crouching, in cm. Only used to bound the held-back offset. */
	float GetStanceEyeTravel() const;

	/** Hold a capsule-space point within the capsule's RADIUS (minus CameraCapsuleClampMargin), laterally only — the
	 *  height belongs to the stance system, which deliberately parks the view outside the capsule mid-crouch. This is
	 *  where invariant 2 is enforced: the shooting origin IS the camera, and the capsule can never bring its axis closer
	 *  than Radius to a wall, so bounding the camera to that radius makes "shot from past a wall you're pressed against"
	 *  structurally impossible instead of a value someone has to remember to keep small. Returns the held point;
	 *  OutClampedAmount gets how far it had to move (0 = untouched) for the debug readout. */
	FVector ClampPointInsideCapsule(const FVector& CapsuleSpacePoint, float& OutClampedAmount) const;

	void InitAbilitySystem();

	/** Server-authoritative run-start seam (U10): meta-progression stat effects are applied here, right after the ASC
	 *  actor info is initialized. Empty at U10 — the real GameplayEffects land in P0-③. Server-authoritative because
	 *  GAS attributes must be applied on the authority and replicate down; this does NOT commit to where the per-player
	 *  meta payload comes from (client-reported vs server-side) — that provenance is decided in P0-③. RunFlow §2-11. */
	void ApplyMetaProgressionEffects();

	/** True while the run is globally frozen for card selection (Game.MD §2-2) — gates player input. */
	bool IsRunFrozen() const;

	/** True when this player can't ACT (fire / swap / reload / ADS) — i.e. NOT alive (DBNO downed, or Dead).
	 *  Gates actions + contact damage like IsRunFrozen. DBNO still crawls/looks — those gate on IsTrulyDeadLocal. */
	bool IsIncapacitatedLocal() const;

	/** True only when truly Dead (out of the run) — blocks even crawl/look. DBNO returns false (can crawl + look). */
	bool IsTrulyDeadLocal() const;

	/** Bound to the health set's OnOutOfHealth (server). Transitions the player to DBNO (downed) + crawl and runs the
	 *  wipe check (team-wipe -> Defeat). Revive back to Alive is UFPSRReviveComponent (U9 Phase 1B, Game.MD §2-13). */
	void HandleOutOfHealth();

	//~ACharacter: no jumping while incapacitated (DBNO or Dead).
	virtual bool CanJumpInternal_Implementation() const override;

	/** Local client: react to GameState OnRunStateChanged — apply/clear the mission vision restriction PP. */
	UFUNCTION()
	void HandleRunStateChanged_Vision();

	/** Server (authority): on the run-freeze (§2-2) halt residual locomotion (e.g. an in-progress fall) so the player
	 *  can't drift across the frozen card screen. CMC replicates the stop. */
	UFUNCTION()
	void HandleRunStateChanged_Movement();

	/** Local client: try to bind to GameState OnRunStateChanged (GameState may arrive after BeginPlay). */
	void TryBindVisionDelegate();

	/** Local client: apply (true) or clear (false) the camera vision-restriction post-process. Idempotent. */
	void ApplyVisionRestriction(bool bRestricted);

#if WITH_EDITOR
	/** Editor-only authoring-loop shim (fparms-gunanchor-ik): the gun-frame grip cache (GetRightHandGripInGunFrame /
	 *  GetLeftHandGripInGunFrame) is a CONSTANT re-solved only on equip/part changes — by design (see those functions'
	 *  own comments) — so tuning a grip/attach socket's transform in the editor had no visible effect until a PIE
	 *  restart re-ran AttachWeaponMeshes. Bound to FCoreUObjectDelegates::OnObjectPropertyChanged in BeginPlay, this
	 *  re-attaches (which re-caches, see AttachWeaponMeshes) whenever ANY socket's property changes in the editor,
	 *  closing that loop. AttachWeaponMeshes is idempotent and this only fires on human editor edits (low frequency),
	 *  so reacting to every socket rather than filtering to "is this socket mine" costs nothing — and that filter
	 *  would itself be a place to get it wrong: a socket's Outer can be either the mesh or the skeleton (this project
	 *  has hit both), so a narrower check risks silently missing an update instead of just doing one harmless extra
	 *  re-attach. */
	void OnEditorObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& Event);

	/** Handle for the OnEditorObjectPropertyChanged binding above — bound in BeginPlay, removed in EndPlay. */
	FDelegateHandle EditorSocketChangedHandle;
#endif

	// Enhanced Input handlers
	void Input_MoveForward(const FInputActionValue& Value);
	void Input_MoveRight(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_Fire(const FInputActionValue& Value);
	void Input_FireReleased(const FInputActionValue& Value);
	void Input_EquipSlot1(const FInputActionValue& Value);
	void Input_EquipSlot2(const FInputActionValue& Value);
	void Input_EquipSlot3(const FInputActionValue& Value);
	void Input_Reload(const FInputActionValue& Value);
	void Input_ADSPressed(const FInputActionValue& Value);
	void Input_ADSReleased(const FInputActionValue& Value);
	/** Crouch input, bound to Triggered (every frame the key is held) rather than Started. Held = crouch; pressing it
	 *  while running fast enough starts a SLIDE instead (the movement component decides — see
	 *  UFPSRCharacterMovementComponent::CanEnterSlide). This only forwards intent: the engine already ships
	 *  bWantsToCrouch in every move packet, so no extra RPC and no custom flag is needed.
	 *  Per-frame because Input_Jump CLEARS the crouch intent — a once-only Started binding could never restore it while
	 *  the key stayed down, so the player would land from a slide-jump and never crouch again until they let go. */
	void Input_CrouchHeld(const FInputActionValue& Value);
	void Input_CrouchReleased(const FInputActionValue& Value);

	/** Jump input. Drops the crouch/slide intent before jumping: crouch is a HELD state, so jumping without clearing it
	 *  leaves the intent set — the engine stands the player up mid-air, then re-crouches them the instant they land,
	 *  and every following jump is swallowed. Treating a jump press as "let go of crouch" is what makes slide-jump and
	 *  crouch-jump work while the key is still down. */
	void Input_Jump(const FInputActionValue& Value);
	/** Esc: open the settings overlay (delegates to the owning PC; non-pause overlay). */
	void Input_Menu(const FInputActionValue& Value);

	/** Server: equip a weapon slot (input is client-side; equip is server-authoritative). */
	UFUNCTION(Server, Reliable)
	void ServerEquipSlot(int32 SlotIndex);

	/** Server: begin reload (input is client-side; reload is server-authoritative). */
	UFUNCTION(Server, Reliable)
	void ServerReload();

	/** Server: sync aim-down-sights state so the fire GA applies ADS spread server-side. */
	UFUNCTION(Server, Reliable)
	void ServerSetAiming(bool bNewAiming);

	UPROPERTY()
	TObjectPtr<UFPSRAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	/** Fixed camera correction in CAPSULE space (X = forward, Y = right, Z = up), applied on top of BaseEyeHeight.
	 *
	 *  Not "where the eyes are" — it is the correction this CHARACTER + this CAPSULE need, which is why the name doesn't
	 *  promise anatomy. Without it the camera sits on the capsule's centre axis at neck height: looking forward is fine
	 *  (the body is behind and below the frustum), but looking DOWN aims the frustum straight into your own chest from
	 *  zero distance and you see its inside instead of its surface. Pushing the camera forward puts the chest behind and
	 *  below it, which is the whole fix — height is not what's wrong (ADR 0002 축 2).
	 *
	 *  BUDGET: UpdateStanceCamera holds the lateral result within the capsule radius (invariant 2 — the shooting origin
	 *  is this camera, so a camera reaching past a wall is a shot from past it). Capsule radius is 34, so X+Y together
	 *  have ~32cm to spend after the margin. Z is unbounded here because the stance system owns it.
	 *
	 *  Tune this on the BP CLASS DEFAULTS, not on a PIE instance — server and clients each compute the camera from their
	 *  own copy of this value, so a per-instance tweak makes them disagree about where shots start. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Camera")
	FVector FirstPersonCameraOffset = FVector(10.0f, 0.0f, 0.0f);

	/** Safety inset (cm) kept between the clamped camera and the capsule surface. Small on purpose: the project runs
	 *  NearClipPlane=1.0 (Config/DefaultEngine.ini, engine default is 10) so a couple of cm is enough to keep the near
	 *  plane on the inside. Raising the near plane instead would clip the first-person weapon too, which is why it was
	 *  lowered in the first place. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Camera", meta = (ClampMin = "0.0"))
	float CameraCapsuleClampMargin = 2.0f;

	/** Degrees added to the base FOV while sliding. POSITIVE WIDENS the view (FieldOfView is an angle), which is the
	 *  point — a slide should show more of what is beside you and read as fast. An offset rather than an absolute FOV
	 *  so that whatever the player later picks as their base FOV, the slide still widens it by this much.
	 *  Does not apply while aiming: an ADS or scope zoom replaces the hip target instead of stacking with it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Camera")
	float SlideFieldOfViewOffset = 5.0f;

	/** How fast the slide FOV offset eases in and out (FInterpTo speed; higher = snappier). Kept independent of the
	 *  movement component's SlideBlendDuration, which is the ANIMATION pose blend — tuning how the body drops into the
	 *  slide should not silently retune how the view breathes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Camera", meta = (ClampMin = "0.01"))
	float SlideFieldOfViewInterpSpeed = 8.0f;

	/** (min, max) degrees a player may set the base FOV to. Lives here rather than in the settings widget so the clamp
	 *  and the slider cannot disagree — the widget reads it through GetBaseFieldOfViewRange. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Camera")
	FVector2D BaseFieldOfViewRange = FVector2D(60.0f, 130.0f);

	/** Optional post-process material for the LimitedVision mission (tunnel/radial mask). When unset, a built-in
	 *  vignette fallback is used so the effect works without content. Assigned in the BP subclass (no hardcoded path). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Vision")
	TObjectPtr<UMaterialInterface> VisionRestrictionMaterial = nullptr;

	/** Built-in fallback vignette intensity used when VisionRestrictionMaterial is unset. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Vision", meta = (ClampMin = "0.0"))
	float VisionVignetteIntensity = 1.4f;

	/** Weapon skeletal mesh (firearms), hanging off the BODY mesh's grip socket — one mesh for the owner and every
	 *  remote observer (ADR 0002). Mesh set from the equipped weapon's DataAsset. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Mesh")
	TObjectPtr<USkeletalMeshComponent> WeaponMesh;

	/** Weapon static mesh (e.g. melee), same grip socket + same visibility as WeaponMesh above. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Mesh")
	TObjectPtr<UStaticMeshComponent> WeaponMeshStatic;

	/** The owner's own arms, hanging off the CAMERA and running their OWN anim graph on their own skeleton (ADR 0003).
	 *  Third-person animation is authored to look right from a third-person camera; replayed at eye height it puts the
	 *  arms low and wrapped around the view, which is why these no longer follow the body's pose. Owner-only.
	 *
	 *  AUTHOR EVERYTHING ON THIS COMPONENT IN THE BLUEPRINT — mesh, anim class, and relative transform. There is no
	 *  parallel "arms mesh" data field: a field that overwrote the slot would make the slot a lie (you set it, it looks
	 *  right in the viewport, and the runtime silently swaps in something else). The rest of this project already keeps
	 *  the component authoritative and treats config only as a fallback for the never-authored case
	 *  (AFPSREnemyBase / AFPSRXPPickup both do `if (GetStaticMesh() == nullptr)`); the player is always a content BP,
	 *  so it needs no fallback at all.
	 *
	 *  **No mesh = the whole first-person split stays off**, which is the safe default rather than a bug — see
	 *  RefreshFirstPersonRendering. The relative transform is the arms' REST pose and BeginPlay snapshots it as the
	 *  hip end of the ADS blend, so it is load-bearing, not a viewport nicety. Visibility is runtime-decided (the
	 *  split owns it); tick it here only to preview in the Blueprint viewport. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Mesh")
	TObjectPtr<USkeletalMeshComponent> FirstPersonArms;

	/** Socket on the BODY skeleton the weapon meshes attach to (authored on the grip hand — Blu: "SOCKET_Weapon" on
	 *  hand_R). C++-created component sockets can't be edited in the BP, so this exposes the default here; the
	 *  design-time preview attaches to it, and a weapon DA's WeaponAttachSocket overrides it per-weapon at equip.
	 *  Data, not a literal in code (ADR 0002 invariant 9) — swapping the player mesh must not need a recompile. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Mesh")
	FName WeaponAttachSocketName = FName(TEXT("SOCKET_Weapon"));

	/** Nudge applied to the left-hand IK target after resolving the weapon's LeftHandSocket. The socket marks the grip
	 *  LINE on the gun; the hand BONE sits a hand's thickness off that contact point, so aiming the IK straight at the
	 *  socket drives the fingers through the handguard. That difference comes from the arms, not the weapon — it is the
	 *  same for every gun and every handguard part — so it is authored once here instead of being baked into each
	 *  weapon's socket, which is also what keeps a swapped handguard from needing its own correction.
	 *  Applied in the grip component's space (what a socket's RelativeLocation is authored in), so a value measured by
	 *  nudging the socket in the editor transfers verbatim. Zero by default: this is a tuning value, and a literal here
	 *  would make the next adjustment a code change (ADR 0002 invariant 9). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Mesh")
	FVector LeftHandGripOffset = FVector::ZeroVector;

	/** Right-hand mirror of LeftHandGripOffset above — same authoring convention (grip component space), same reason
	 *  (the hand bone sits a hand's thickness off the socket's grip line, and that's a property of the arms, not the
	 *  weapon). Zero by default. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Mesh")
	FVector RightHandGripOffset = FVector::ZeroVector;

	/** Name of the IK bone the weapon anchors to on arms that have the two-bone hand-IK rig (ik_hand_root > ik_hand_gun
	 *  > ik_hand_l/r — gun-anchor refactor). Data, not a literal (ADR 0002 invariant 9): a future rig could rename or
	 *  restructure the IK chain without a recompile. AttachWeaponMeshes attaches here instead of the grip socket
	 *  whenever this bone exists on the current arms mesh; GetRightHandGripInGunFrame / GetLeftHandGripInGunFrame
	 *  express their result in this bone's space. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Mesh")
	FName IkHandGunBoneName = FName(TEXT("ik_hand_gun"));

	/** The source bone the arms' animgraph CopyBones INTO IkHandGunBoneName every frame (component space) — i.e. the
	 *  bone the gun-anchor math in AttachWeaponMeshes treats ik_hand_gun's frame as equal to. AttachWeaponMeshes can
	 *  only verify HALF of that equality from C++ — that AttachSocket's own parent bone matches this name — since
	 *  whether the ABP's CopyBone node actually reads FROM this bone is Blueprint-side and unverifiable here. Data,
	 *  not a literal, for the same reason as IkHandGunBoneName: a re-rig could rename the source bone without a
	 *  recompile. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Mesh")
	FName ExpectedGripSourceBoneName = FName(TEXT("hand_r"));

	/** Bone hidden while looking through this pawn's eyes (Blu: "head"; its children — hair, eyes, glasses — go with it,
	 *  engine: BVS_HiddenByParent). Data for the same reason as WeaponAttachSocketName: HideBoneByName does NOTHING and
	 *  logs NOTHING for a name the skeleton lacks, and this project has already swapped the player mesh twice. None
	 *  disables head hiding. UpdateFirstPersonBodyVisibility warns when the name doesn't resolve. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Mesh")
	FName HeadBoneName = FName(TEXT("head"));

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Weapon")
	TObjectPtr<UFPSRWeaponInventoryComponent> WeaponInventory;

	UPROPERTY(VisibleAnywhere, Category = "FPSR|Weapon")
	TObjectPtr<UFPSRWeaponFireComponent> WeaponFire;

	/** Owner-local recoil + heat-spread driver (CrystalRecoil adapter, P1). Driven by WeaponFire (SetRecoilPattern on
	 *  equip, StartShooting/ApplyShot on fire). Applies to the controller so the server fire trace matches on-screen recoil. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Weapon")
	TObjectPtr<UFPSRRecoilComponent> RecoilComponent;

	/** Co-op DBNO revive (U9 §2-13): server-authoritative proximity revive; replicates ReviveProgress for the HUD gauge. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Revive")
	TObjectPtr<UFPSRReviveComponent> ReviveComponent;

	/** Local-only hit-marker + threat-indicator feedback (Game.MD §2-14). Not replicated; WBP HUD binds to it. */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Feedback")
	TObjectPtr<UFPSRPlayerFeedbackComponent> PlayerFeedback;

	/** Local-only blind-spot threat audio (Game.MD §2-14). Not replicated; warns by sound when an enemy is
	 *  close and outside the forward view (audio only — no visual indicator). */
	UPROPERTY(VisibleAnywhere, Category = "FPSR|Feedback")
	TObjectPtr<UFPSRBlindspotAudioComponent> BlindspotAudio;

	/** Starting weapons granted on possession (slot order). Assigned per-character in the BP class defaults
	 *  (EditDefaultsOnly); later folded into a HeroDataAsset. No hard-coded weapon paths in C++ (Game.md §6-2). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Weapon")
	TObjectPtr<UFPSRWeaponDataAsset> DefaultPrimaryWeapon;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Weapon")
	TObjectPtr<UFPSRWeaponDataAsset> DefaultSecondaryWeapon;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> MoveForwardAction;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> MoveRightAction;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> LookAction;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> JumpAction;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> FireAction;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> EquipSlot1Action;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> EquipSlot2Action;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> EquipSlot3Action;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> ReloadAction;

	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> ADSAction;

	/** Crouch / slide (one key — see Input_CrouchPressed). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> CrouchAction;

	/** Esc — opens the settings overlay (non-pause). */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Input")
	TObjectPtr<UInputAction> MenuAction;

	/** Server: end the grace collision-ignore window (recomputes the enemy-pawn response). */
	void EndGraceWindow();

	/** Server: recompute the capsule's response to enemy pawns (ECC_Pawn) — ignore while within a grace window or
	 *  while downed (DBNO/Dead), block otherwise. Shared by the grace window + the downed state so neither restores
	 *  blocking while the other is still active. */
	void RefreshPawnCollisionResponse();

	/** Baseline walk speed before MoveSpeedMultiplier. Designers may tune per-hero. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "FPSR|Movement")
	float BaseWalkSpeed = 600.0f;


	/** Invulnerability window (seconds) after taking contact damage; further hits within it are ignored.
	 *  Prevents a swarm from melting the player in one frame. Balance-tunable. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Combat")
	float DamageInvulnerabilityDuration = 0.25f;

	/** Seconds of grace (invulnerable + enemy pass-through) granted when the card-selection freeze ENDS, so a player
	 *  who unfreezes surrounded by the swarm isn't hit the instant the world resumes (U9 §2-13). Balance value; 0 disables. */
	UPROPERTY(EditDefaultsOnly, Category = "FPSR|Combat", meta = (ClampMin = "0.0"))
	float PostFreezeInvulnSeconds = 3.0f;

	/** Server-only: world time of the last accepted contact hit (i-frame gate). */
	float LastDamagedTime = -1000.0f;

	/** Server-only: world time until which the player is invulnerable + passes through enemy pawns (grace window:
	 *  post-revive and post-card-selection resume, U9 §2-13). Set in BeginGraceWindow; read by ApplyContactDamage. */
	float GraceUntil = -1000.0f;

	/** Server-only: timer to end the grace collision-ignore window. */
	FTimerHandle GraceTimerHandle;

	/** Server-only: previous run-paused state (authority) — detects the card-selection freeze ENDING so the resume
	 *  grace fires once on the paused->unpaused transition. */
	bool bWasRunPausedAuth = false;

	/** Local-client: true while the vision-restriction PP is currently applied (idempotency guard). */
	bool bVisionRestrictionApplied = false;

	/** Local-client: saved camera vignette override flag/intensity (fallback path) so the camera's authored
	 *  settings are restored when the mission ends instead of being clobbered. */
	bool bSavedVignetteOverride = false;
	float SavedVignetteIntensity = 0.0f;

	/** Local-client: true once bound to GameState OnRunStateChanged. */
	bool bVisionDelegateBound = false;

	/** Local-client: retry timer for binding the vision delegate before GameState is replicated. */
	FTimerHandle VisionBindRetryTimerHandle;

	/** Cached hard refs for the currently-equipped weapon's fire cosmetics (resolved once on equip to avoid
	 *  per-shot soft-pointer loads). Refreshed in RefreshEquippedWeaponVisual. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CachedFireMontage;

	/** Cached hard ref for the equipped weapon's body reload montage. Refreshed on equip. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CachedReloadMontage;

	/** Cached hard refs for the equipped weapon MESH's bolt montages (fire/reload), played on WeaponMesh's own
	 *  AnimInstance so the bolt/magazine syncs with the body montages. Refreshed on equip. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CachedWeaponFireMontage;

	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CachedWeaponReloadMontage;

	/** Cached hard ref for the FIRST-PERSON ARMS reload montage (ADR 0003). Separate from the body's because the two
	 *  meshes have different skeletons — a montage is bound to one of them and cannot be shared. */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CachedArmsReloadMontage;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> CachedFireSound;

	UPROPERTY(Transient)
	TObjectPtr<UParticleSystem> CachedMuzzleFlash;

	FName CachedMuzzleSocket = NAME_None;

	/** The per-weapon body anim layer currently linked to GetMesh() (ADR 0002 step 4). Kept so the next equip knows what
	 *  to unlink — LinkAnimClassLayers has no "replace" and layering a second set on top would leave both running. */
	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> LinkedBodyAnimLayerClass;

	/** Same, for the first-person arms (ADR 0003). Tracked separately because the two components link independently and
	 *  a weapon may author one layer without the other. */
	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> LinkedArmsAnimLayerClass;

	/** Rotation offset applied to the muzzle-flash emitter relative to the muzzle socket (owner-local cosmetic).
	 *  This pack's weapon-forward is +Y, so the flash needs a yaw offset to fire down the barrel (same reason as
	 *  ADSAimRotationOffset). Cached on equip from the weapon DA; designer-tuned per weapon. */
	FRotator CachedMuzzleFlashRotationOffset = FRotator::ZeroRotator;

	/** The weapon mesh currently shown (skeletal OR static — whichever the equipped weapon's DA provides). Fire
	 *  cosmetics (fire sound) attach here so they track the active mesh. Null when no weapon is equipped. */
	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> ActiveWeaponMesh;

	/** Component the muzzle flash attaches to at CachedMuzzleSocket. On modular weapons the muzzle socket lives on a
	 *  cosmetic PART (barrel/forestock) — swapping the part moves the muzzle — so RefreshWeaponPartComponents resolves
	 *  this to whichever part component carries CachedMuzzleSocket, falling back to ActiveWeaponMesh (receiver) when no
	 *  part provides it. Convention-based: no extra DA field — the part that owns a socket named MuzzleSocket wins. */
	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> CachedMuzzleComponent;

	/** Component the procedural-ADS AimSocket is read from. Like the muzzle, the sight (iron sight / optic) is a modular
	 *  PART, so RefreshWeaponPartComponents resolves this to whichever part component carries CachedAimSocket — swapping
	 *  the sight then moves the ADS reference — falling back to the receiver (ActiveWeaponMesh) when no part provides it.
	 *  Convention-based: the part that owns a socket named AimSocket wins (first in WeaponParts order). */
	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> CachedAimComponent;

	/** Component the left-hand grip socket is read from — same resolution as the aim socket above (the handguard is a
	 *  modular part, so swapping it moves the grip). Null when no part carries CachedLeftHandSocket; the getter then
	 *  falls back to the receiver. (ADR 0002 step 4 seam) */
	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> CachedLeftHandComponent;

	/** Right-hand mirror of CachedLeftHandComponent above — same resolution (RebuildPartsFromSelection), same
	 *  fallback (ResolveRightHandGripComponent). */
	UPROPERTY(Transient)
	TObjectPtr<UMeshComponent> CachedRightHandComponent;

	/** Scope descriptor of the currently-active sight part (the one carrying CachedAimSocket), captured alongside
	 *  CachedAimComponent in RebuildPartsFromSelection. Default (bScopeOverlay=false) when no scope sight is active.
	 *  Owner-local cosmetic; not replicated/saved. (W-U2) */
	FFPSRWeaponScopeDescriptor CachedScopeDescriptor;

	/** Whether RefreshWeaponVisibility currently has the weapon meshes hidden, so it only toggles on change.
	 *  Not "hidden for a scope" any more: the wall pose hides it too, and a per-reason latch is exactly how the two
	 *  reasons would start undoing each other. */
	bool bWeaponHidden = false;

	/** Tracks the last head-visibility decision so UpdateFirstPersonBodyVisibility only touches bone visibility when the
	 *  view target actually changes (the steady state is a pointer compare). Local render state; never replicated. */
	bool bHeadHiddenForOwnView = false;

	/** Latched result of RefreshFirstPersonRendering — see IsFirstPersonSplitActive(). Starts false so a pawn that is
	 *  mid-spawn, mid-possession, or authored without an arms mesh behaves exactly as it did before ADR 0003. */
	bool bFirstPersonSplitActive = false;

	/** The arms' AUTHORED transform relative to the camera, snapshotted once at BeginPlay. This is the hip end of the
	 *  ADS blend, and it has to be a snapshot rather than a live read: UpdateAimDownSights overwrites the component's
	 *  relative transform every frame, so reading the component back would feed last frame's solve into this one and
	 *  let the pose walk away. (The weapon path reads its parent socket instead, for the same "independent reference"
	 *  reason — there the parent is an animated hand, here it is the camera and the rest pose is authored data.) */
	FTransform ArmsHipRelativeTransform = FTransform::Identity;

	// --- Procedural aim-down-sights (owner-local) ---
	/** Height (cm) the camera is currently held back from its nominal eye position, measured at the instant the stance
	 *  changed. Eased to 0 across the stance transition; 0 means no blend in flight. Owner-local presentation. */
	float CameraEyeOffsetStart = 0.0f;

	/** World Z the camera ended last frame at. The reference the next stance change holds the view to — measuring the
	 *  jump beats predicting it, because the capsule is re-anchored at the feet on the ground, at its centre in the
	 *  air, and not moved at all on a remote proxy. */
	float CachedCameraWorldZ = 0.0f;

	/** Unaimed FOV everything else is composed on top of. Captured at BeginPlay from the camera component's authored
	 *  value, so the BP class default IS the base until a settings menu calls SetBaseFieldOfView. Captured there and
	 *  not lazily mid-tick because UpdateCameraFieldOfView starts writing FieldOfView on the first frame — a later
	 *  read would sample a value this class had already moved.
	 *  The initializer only covers the (impossible) no-camera case and mirrors UCameraComponent's own default. */
	float BaseFieldOfView = 90.0f;

	/** Slide FOV offset actually applied this frame, eased toward SlideFieldOfViewOffset / 0 as the slide starts and
	 *  ends. Held as its own value rather than driven off the movement component's SlideBlend so the camera keeps its
	 *  own timing, and so entering and leaving a slide are symmetrical without any edge handling. Owner-local. */
	float SlideFOVOffsetCurrent = 0.0f;

#if ENABLE_DRAW_DEBUG
	/** How far the capsule clamp had to pull the camera back this frame (cm, 0 = untouched). Surfaced in the movement
	 *  readout so an authored FirstPersonCameraOffset that is being silently cut reads as "cut", not as "ignored". */
	float LastCameraClampAmount = 0.0f;
#endif

	/** Runtime ADS blend state: interpolated alpha (0 = hip, 1 = fully aimed) + the EXACT aim-pose WEAPON transform
	 *  (relative to the camera), recomputed each aiming frame. Blending hip<->aim by alpha (instead of chasing the live
	 *  target with a lagging transform interp) glues the sight onto the centre-line at full ADS, so body/weapon animation
	 *  sway/bob is cancelled AT THE SIGHT and the reticle holds steady rather than wobbling as an interp lags the
	 *  animated socket. The hip end of the blend is no longer a captured rest pose — it's the grip socket read fresh each
	 *  frame, because the weapon now rides an animated hand instead of a camera-parented arms component. */
	float CurrentADSAlpha = 0.0f;
	FVector ADSAimLoc = FVector::ZeroVector;
	FRotator ADSAimRot = FRotator::ZeroRotator;
	/** Equipped weapon's ADS params, cached on equip (RefreshEquippedWeaponVisual). */
	FName CachedAimSocket = NAME_None;
	/** Equipped weapon's left-hand grip socket (ADR 0002 step 4). None = no left-hand IK for this weapon. */
	FName CachedLeftHandSocket = NAME_None;
	/** Right-hand mirror of CachedLeftHandSocket above (fparms-gunanchor-ik). None = no right-hand IK for this weapon. */
	FName CachedRightHandSocket = NAME_None;
	/** Gun-anchor IK (fparms-gunanchor-ik): grip frame in ik_hand_gun bone space, recomputed on attach/part changes
	 *  (RefreshHandGripInGunFrameCache) rather than every frame — see GetRightHandGripInGunFrame / GetLeftHandGripInGunFrame.
	 *  Unset = this hand has no grip on the current arms (weapon has no socket for it, or it isn't on the arms at all). */
	TOptional<FTransform> CachedRightGripInGun;
	TOptional<FTransform> CachedLeftGripInGun;
	float CachedADSSightDistance = 25.0f;
	bool bCachedHasADS = false;
	bool bCachedADSAlignRotation = true;
	FRotator CachedADSAimRotationOffset = FRotator::ZeroRotator;
	bool bCachedSuppressFireMontagesWhileADS = true;
	float CachedADSInterpSpeed = 12.0f;
	/** Fraction of the aim pose's animated positional bob allowed through while aiming (0 = sight fully glued to the
	 *  centre-line; >0 lets that fraction of the bob survive). Rotation stays fully glued regardless. */
	float CachedADSPositionBobScale = 0.0f;
	bool bCachedSuppressWeaponBoltWhileADS = false;
	float CachedADSMuzzleFlashScale = 0.35f;
	float CachedADSFireKickDegrees = 1.5f;
	float CachedADSFireKickRecoveryRate = 12.0f;
	/** Equipped weapon's ADS idle-sway params, cached on equip (owner-local cosmetic). Amplitudes in degrees about the
	 *  sight pivot (yaw = L-R, pitch = subtle up-down); speed = oscillation frequency. 0 amplitude = no sway. */
	float CachedADSSwayYawDegrees = 0.0f;
	float CachedADSSwayPitchDegrees = 0.0f;
	float CachedADSSwaySpeed = 1.2f;
	/** Current decaying ADS fire-kick angle (deg), owner-local. Bumped on each aimed shot, settled toward 0 each frame;
	 *  applied as a rotation about the AimSocket pivot in UpdateAimDownSights so the gun kicks while the sight stays put. */
	float ADSFireKickPitch = 0.0f;

	/** Smoothed 0..1 movement factor gating the ADS idle sway: 0 when the pawn is standing still (steady aim), ramps
	 *  to 1 with planar speed (relative to BaseWalkSpeed) so the handheld sway only lives while moving. Owner-local. */
	float ADSSwayMoveAlpha = 0.0f;

	// --- Hip-space procedural weapon motion (owner-local cosmetic; P1: look-sway + walk-bob + fire-kick) ---
	/** Control rotation captured last frame, to derive the per-frame aim delta that drives look-sway. Reset on equip
	 *  and initialised in BeginPlay so the first frame's delta isn't a huge jump. */
	FRotator PreviousControlRotation = FRotator::ZeroRotator;
	/** Interpolated look-sway (yaw/pitch degrees) the weapon lags behind the aim by; eases back to zero when the aim stops. */
	FRotator CurrentHipLookSway = FRotator::ZeroRotator;
	/** Accumulated walk-bob phase (cycles), advanced by dt * frequency * movement factor (frame-rate independent). */
	float HipBobPhase = 0.0f;
	/** Decaying per-shot hip fire-kick alpha (0..1). Bumped to 1 on each local shot (PlayWeaponFireCosmetics), settled
	 *  toward 0 each frame; scales the kick offset/pitch. */
	float HipFireKickAlpha = 0.0f;
	/** Equipped weapon's hip procedural-motion profile, cached on equip. */
	FFPSRProceduralWeaponMotionProfile CachedHipMotion;
	/** True when the cached profile has any non-zero amplitude (skip the whole hip block otherwise). */
	bool bCachedHasHipMotion = false;

	/** Runtime-created modular weapon-part components (U15), child-attached to WeaponMesh and rebuilt on each weapon
	 *  change. Visible to everyone, like the weapon they hang off (ADR 0002 — they used to be OnlyOwnerSee). Empty for
	 *  static/melee/partless weapons. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> WeaponPartComponents;

	/** W-U1: pending next-tick parts rebuild flag (coalesces a burst of modifier/fragment OnReps into one rebuild). */
	bool bWeaponPartsRebuildPending = false;
	/** W-U1: hash of the last-applied selected part set; ProcessPendingWeaponPartsRebuild skips rebuild when unchanged.
	 *  Transient, NOT replicated, NOT saved (§2-A gate②) — each machine recomputes from replicated stats/fragments. */
	uint32 LastWeaponPartSignature = 0;

	/** Destroy any existing modular part components and rebuild them from the equipped weapon's WeaponParts list
	 *  (only when a SKELETAL weapon mesh is shown; static/melee/empty attach nothing). Called from the weapon refresh. */
	void RefreshWeaponPartComponents(const UFPSRWeaponDataAsset* Weapon);

	/** W-U1: rebuild the modular parts from an already-computed selection (shared by equip + modifier-change paths).
	 *  Tears down existing part components, RESETS CachedMuzzle/Aim/LeftHandComponent, attaches the selection, then
	 *  re-resolves the muzzle / aim / left-hand source components. */
	void RebuildPartsFromSelection(const TArray<FFPSRWeaponPartAttachment>& Selected);

	/** W-U1 signature-diff rebuild (next-tick coalesced half of NotifyEquippedWeaponModifiersChanged, see the public
	 *  declaration above for the full contract). */
	void ProcessPendingWeaponPartsRebuild();

};
