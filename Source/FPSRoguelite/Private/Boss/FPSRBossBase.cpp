// Copyright Epic Games, Inc. All Rights Reserved.

#include "Boss/FPSRBossBase.h"
#include "Boss/FPSRBossDefinitionDataAsset.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Core/FPSRGameMode.h"
#include "Core/FPSRLogChannels.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Settings/FPSRPlaceholderVisualSettings.h"
#include "Boss/FPSRPatternActorInterface.h"
#include "AbilitySystem/FPSRAbilitySystemComponent.h"
#include "AbilitySystem/Abilities/FPSRBossGameplayAbility.h"
#include "AbilitySystem/Abilities/FPSRFreezeCooldownAbility.h"
#include "AbilitySystem/Abilities/FPSRBossGA_Barrage.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
#include "AbilitySystemComponent.h"
#include "GameplayAbilitySpec.h"
#include "Combat/FPSRCombatStatics.h"
#include "Core/FPSRGameState.h"
#include "Hero/FPSRCharacter.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Boss/FPSRBossLaserMath.h"
#if !UE_BUILD_SHIPPING
#include "DrawDebugHelpers.h"
#include "Arena/FPSRArenaActor.h"
#endif

AFPSRBossBase::AFPSRBossBase()
{
	// 🔴 BOSS1: this was false while the boss was a health-only scaffold ("nothing to gate on the freeze"). The
	// pattern driver lives in Tick, so leaving it false would leave the selector, blast fuses, the sweeping laser
	// and the freeze-edge push ALL silently inert — with a green build and green automation. BeginPlay narrows it
	// to the server (SetActorTickEnabled), so clients pay nothing; their cosmetics run off Blueprint ticks.
	PrimaryActorTick.bCanEverTick = true;

	// Always relevant so the boss + its replicated HealthComponent reach every client regardless of distance — the
	// HUD boss bar (B11) must reflect boss health for all players, not just those near the boss. Cheap (one actor).
	bAlwaysRelevant = true;

	// Capsule (ACharacter root): object type ECC_Pawn so the weapon pawn-gather traces find the boss AND the
	// hitscan wall trace ignores it (NOT WorldStatic — that would self-block the boss's own bullets, P7 §6).
	// Mirror the swarm capsule: block everything, ignore other Pawns (residual swarm don't stack on the boss).
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->InitCapsuleSize(120.0f, 200.0f);
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Capsule->SetCollisionObjectType(ECC_Pawn);
		Capsule->SetCollisionResponseToAllChannels(ECR_Block);
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}

	// Visible placeholder body (no collision — the capsule is the hit volume). Designers replace it in the boss BP;
	// the mesh is resolved in BeginPlay from config (Game.md §6-2 — no hard-coded path in C++), fallback only.
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(GetCapsuleComponent());
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetRelativeScale3D(FVector(2.4f, 2.4f, 4.0f)); // ~fill the capsule (cube is 100^3)

	// Stationary scaffold: kill gravity so the boss never falls off its spawn point (real boss re-enables movement).
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->GravityScale = 0.0f;
	}

	// Non-GAS health — the single reason every weapon path damages the boss with no new damage code.
	HealthComponent = CreateDefaultSubobject<UFPSREnemyHealthComponent>(TEXT("HealthComponent"));

	// ASC owned by the actor itself, Minimal replication — the boss has no owning client, exactly like an elite
	// (AFPSREnemyEliteBase's constructor does the same two lines for the same reason). No AttributeSet: health
	// stays in HealthComponent so the D1 damage bridge keeps working untouched.
	AbilitySystem = CreateDefaultSubobject<UFPSRAbilitySystemComponent>(TEXT("AbilitySystem"));
	AbilitySystem->SetIsReplicated(true);
	AbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
}

UAbilitySystemComponent* AFPSRBossBase::GetAbilitySystemComponent() const
{
	return AbilitySystem;
}

void AFPSRBossBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRBossBase, CurrentPhase, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRBossBase, BlastMarks, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRBossBase, BeamCount, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRBossBase, BeamStartAngleDeg, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRBossBase, BeamSpeedDegPerSec, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRBossBase, BeamStartClock, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRBossBase, BeamWarmupEndClock, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRBossBase, BeamVisualHeightCm, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRBossBase, PatternStage, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRBossBase, MarkedPlayerState, Params);
}

void AFPSRBossBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Owner == Avatar == self. Cannot move to the constructor (the CDO has no real actor for ActorInfo to point at);
	// this runs exactly once per actor lifetime, which is all the boss needs since it is never pooled.
	if (AbilitySystem)
	{
		AbilitySystem->InitAbilityActorInfo(this, this);

		// Two-layer time-axis contract: the ability base replaces the cooldown path, and this guard blocks a
		// duration/periodic GE from ever being applied to this ASC in the first place. Shared with elites.
		AbilitySystem->EnableTimeAxisGuard();
	}
}

void AFPSRBossBase::BeginPlay()
{
	Super::BeginPlay();

	// Fallback placeholder body mesh from config only when the boss BP assigned none (Game.md §6-2 — no hard-coded
	// path in C++). Runs on all machines (cosmetic). A designer-authored boss BP mesh wins (guard skips the load).
	if (BodyMesh && BodyMesh->GetStaticMesh() == nullptr)
	{
		if (const UFPSRPlaceholderVisualSettings* Settings = GetDefault<UFPSRPlaceholderVisualSettings>())
		{
			if (UStaticMesh* PlaceholderMesh = Settings->BossPlaceholderMesh.LoadSynchronous())
			{
				BodyMesh->SetStaticMesh(PlaceholderMesh);
			}
		}
	}

	// The boss's health is shown ONLY by the dedicated screen-space HUD bar (A3 — WBP_BossHUDBar, driven by the
	// GameState's replicated ActiveBoss + this HealthComponent). It must NOT also carry the swarm-style world-space
	// overhead bar. Suppress any UWidgetComponent authored on the boss BP (runs on clients too, where the bar would
	// render). Idempotent — a no-op once the component is removed from the BP, so removing it there later is safe.
	{
		TArray<UWidgetComponent*> BossWidgetComps;
		GetComponents<UWidgetComponent>(BossWidgetComps);
		for (UWidgetComponent* WidgetComp : BossWidgetComps)
		{
			if (WidgetComp)
			{
				WidgetComp->DestroyComponent();
			}
		}
	}

	// Pin movement off so the static box stays exactly where it spawned (the real boss enables AI movement).
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->DisableMovement();
	}

	// Ticks on every machine, but the pattern DRIVER below is server-only. Clients run nothing but the debug overlay
	// (one branch on one actor) — and that client half is the point: it is the only way to see whether the beam a
	// client is looking at agrees with the beam the server is testing against.
	SetActorTickEnabled(true);

	if (HasAuthority() && PatternTriggers.Num() == 0)
	{
		// 🔴 An empty trigger list means a boss that never attacks again — and nothing anywhere would report it.
		// Fall back to a plain repeating cadence so an unauthored boss still fights; IsDataValid warns separately so
		// the fallback is visible at author time rather than only in a play session.
		FFPSRBossPatternTrigger& Fallback = PatternTriggers.AddDefaulted_GetRef();
		Fallback.Kind = EFPSRBossTriggerKind::Elapsed;
		Fallback.Threshold = FMath::Max(0.5f, PatternGapSeconds);
		Fallback.bRepeating = true;
		UE_LOG(LogFPSR, Warning, TEXT("[Boss] %s has no PatternTriggers — falling back to a repeating %.1fs cadence. Author them on the boss BP."),
			*GetName(), Fallback.Threshold);
	}

	if (HasAuthority() && AbilitySystem)
	{
		// Granted ONCE — the boss is never pooled, so there is no Activate/Deactivate re-grant cycle like elites have.
		for (const TSubclassOf<UFPSRBossGameplayAbility>& AbilityClass : GrantedAbilities)
		{
			if (AbilityClass)
			{
				GrantedAbilityHandles.Add(AbilitySystem->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this)));
			}
		}
	}

	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &AFPSRBossBase::HandleDeath);
		// Server: phase transitions ride the same edge the damage bridge already fires (see HandleHealthChanged).
		HealthComponent->OnHealthChanged.AddDynamic(this, &AFPSRBossBase::HandleHealthChanged);
		// Client death cosmetic (U20): OnDeathCosmetic fires from OnRep_bDead on clients (the authority plays it from
		// HandleDeath). Plays the boss death montage on the skeletal mesh. Harmless before content assigns a skel mesh.
		HealthComponent->OnDeathCosmetic.AddDynamic(this, &AFPSRBossBase::HandleDeathCosmetic);

		// Server: size health from the class default; a BossDefinition overrides it via InitializeFromDefinition.
		if (HasAuthority())
		{
			HealthComponent->InitializeMaxHealth(DefaultMaxHealth);
		}
	}
}

void AFPSRBossBase::InitializeFromDefinition(const UFPSRBossDefinitionDataAsset* Definition)
{
	if (!HasAuthority() || !Definition || !HealthComponent)
	{
		return;
	}

	HealthComponent->InitializeMaxHealth(Definition->MaxHealth);

	// Phase count is the array's length — cached here so the per-hit path never touches the asset.
	PhaseThresholds = Definition->PhaseHealthThresholds;
}

void AFPSRBossBase::HandleHealthChanged(float NewHealth, float MaxHealth)
{
	if (!HasAuthority() || MaxHealth <= 0.0f)
	{
		return;
	}

	const int32 Computed = FPSRBoss::ComputePhase(NewHealth / MaxHealth, PhaseThresholds);
	const int32 Latched = FPSRBoss::LatchPhase(CurrentPhase, Computed);
	if (Latched == CurrentPhase)
	{
		return;
	}

	CurrentPhase = Latched;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRBossBase, CurrentPhase, this);
	// The listen-server host never gets an OnRep, so the cosmetic has to be fired from the setter as well — the
	// same two-halves pattern the death cosmetic uses. Skipping this half is invisible in a 2-player PIE test
	// (the client's screen looks right) and then never plays for the host, who exists in every co-op session.
	OnPhaseChangedCosmetic(CurrentPhase);

	UE_LOG(LogFPSR, Log, TEXT("[Boss] phase -> %d (%.0f/%.0f)"), CurrentPhase, NewHealth, MaxHealth);
}

void AFPSRBossBase::OnRep_CurrentPhase()
{
	OnPhaseChangedCosmetic(CurrentPhase);
}

void AFPSRBossBase::OnRep_BeamState()
{
	OnBeamStateChangedCosmetic(BeamCount, GetPatternClockSeconds() < BeamWarmupEndClock);
}

void AFPSRBossBase::OnRep_PatternStage()
{
	OnPatternStageChangedCosmetic(PatternStage);
}

void AFPSRBossBase::OnRep_MarkedPlayer()
{
	OnMarkedPlayerChangedCosmetic(MarkedPlayerState);
}

APawn* AFPSRBossBase::GetMarkedPawn() const
{
	// Null on a machine where the marked player's pawn is not relevant — the MARK is still known there (that is the
	// point of replicating the PlayerState), so a caller that needs a world position should fall back to something
	// else rather than treating this as "nobody is marked".
	return MarkedPlayerState ? MarkedPlayerState->GetPawn() : nullptr;
}

void AFPSRBossBase::ServerSetPatternStage(EFPSRBossPatternStage NewStage)
{
	if (!HasAuthority() || PatternStage == NewStage)
	{
		return;
	}
	PatternStage = NewStage;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRBossBase, PatternStage, this);
	// Authority half — the host never receives its own OnRep, and the host exists in every co-op session.
	OnPatternStageChangedCosmetic(PatternStage);
}

void AFPSRBossBase::ServerSetMarkedPlayer(APawn* Pawn)
{
	APlayerState* NewState = Pawn ? Pawn->GetPlayerState() : nullptr;
	if (!HasAuthority() || MarkedPlayerState == NewState)
	{
		return;
	}
	MarkedPlayerState = NewState;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRBossBase, MarkedPlayerState, this);
	// Authority half — the host never receives its own OnRep.
	OnMarkedPlayerChangedCosmetic(MarkedPlayerState);
}

float AFPSRBossBase::GetPatternClockSeconds() const
{
	const AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	if (!GS)
	{
		return 0.0f;
	}
	if (HasAuthority())
	{
		return GS->GetCombatClockSeconds();
	}
	// Client: derive from the replicated freeze anchors, then lead by (roughly) our own view lag so the beam we draw
	// sits where the server's hit test currently has it. Cosmetic only — every damage decision is server-side.
	float Lead = 0.0f;
	if (MaxClientVisualLeadSeconds > 0.0f)
	{
		if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
		{
			if (const APlayerState* PS = PC->PlayerState)
			{
				Lead = FMath::Clamp(PS->GetPingInMilliseconds() * 0.0005f, 0.0f, MaxClientVisualLeadSeconds);
			}
		}
	}
	return GS->GetCombatClockSecondsForClients() + Lead;
}

bool AFPSRBossBase::GetBeamState(int32& OutBeamCount, float& OutBaseAngleDeg, bool& bOutWarmup) const
{
	OutBeamCount = BeamCount;
	if (BeamCount <= 0)
	{
		OutBaseAngleDeg = 0.0f;
		bOutWarmup = false;
		return false;
	}
	const float Now = GetPatternClockSeconds();
	// 🔴 The SAME function the server's hit test uses (FPSRBossGA_SweepLaser) and the debug overlay draws with.
	// This used to inline a one-segment formula, and when §14 gave the beam a stationary grace the judgment and the
	// overlay were updated while this accessor was not — so the beam Blueprints draw ran ahead of the beam that
	// actually bites by Speed x BeamGraceSeconds (45 deg at phase 1, 90 at phase 3). A player would have jumped
	// through empty air and then been hit standing still, and nothing on screen would have explained it. The debug
	// overlay hid it precisely because the overlay was on the correct side of the split.
	// The rule this leaves behind: every consumer of the beam angle calls BeamBaseAngleAt — never its own arithmetic.
	OutBaseAngleDeg = FPSRBossLaser::BeamBaseAngleAt(BeamStartAngleDeg, BeamSpeedDegPerSec, BeamWarmupEndClock, Now);
	bOutWarmup = Now < BeamWarmupEndClock;
	return true;
}

void AFPSRBossBase::ServerSetBeamState(int32 InBeamCount, float StartAngleDeg, float SpeedDegPerSec, float StartClock,
	float WarmupEndClock, float VisualHeightCm)
{
	if (!HasAuthority())
	{
		return;
	}
	// No-op on an unchanged clear. Teardown deliberately clears from two places (the pattern's own ServerEndExecute
	// and the boss's release-everything path), which is right — but without this guard the host would fire the
	// "beams off" cosmetic twice while clients, seeing no property change, fire it once.
	if (BeamCount == FMath::Max(0, InBeamCount) && BeamCount == 0)
	{
		return;
	}
	BeamCount = FMath::Max(0, InBeamCount);
	BeamStartAngleDeg = StartAngleDeg;
	BeamSpeedDegPerSec = SpeedDegPerSec;
	BeamStartClock = StartClock;
	BeamWarmupEndClock = WarmupEndClock;
	BeamVisualHeightCm = VisualHeightCm;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRBossBase, BeamCount, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRBossBase, BeamStartAngleDeg, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRBossBase, BeamSpeedDegPerSec, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRBossBase, BeamStartClock, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRBossBase, BeamWarmupEndClock, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRBossBase, BeamVisualHeightCm, this);

	// Authority half of the cosmetic (the host gets no OnRep).
	OnBeamStateChangedCosmetic(BeamCount, GetPatternClockSeconds() < BeamWarmupEndClock);
}

void AFPSRBossBase::ServerAddBlastMark(const FVector& Center, float Radius, float FuseSeconds, APawn* TargetPawn,
	float Damage, float KnockbackStrength, const FFPSRDamageSpec& Spec)
{
	if (!HasAuthority())
	{
		return;
	}

	if (BlastMarks.Num() >= MaxConcurrentMarks)
	{
		// FIFO fizzle: drop the oldest WITHOUT detonating it. Authored values that pass IsDataValid never get here
		// (4 players x ceil(Fuse/Interval) stays far under the cap), so this is a safety valve, not a game rule.
		UE_LOG(LogFPSR, Warning, TEXT("[Boss] blast marks at cap (%d) — fizzling the oldest. Check the barrage's Fuse/Interval against MaxConcurrentMarks."), MaxConcurrentMarks);
		BlastMarks.RemoveAt(0, EAllowShrinking::No);
	}

	FFPSRBossBlastMark& Mark = BlastMarks.AddDefaulted_GetRef();
	Mark.Center = Center;
	Mark.Radius = Radius;
	Mark.DetonateAtClock = GetPatternClockSeconds() + FMath::Max(0.0f, FuseSeconds);
	Mark.TargetPawn = TargetPawn;
	Mark.Damage = Damage;
	Mark.KnockbackStrength = KnockbackStrength;
	Mark.Spec = Spec;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRBossBase, BlastMarks, this);
}

void AFPSRBossBase::ServerRegisterPatternActor(AActor* Actor)
{
	if (!HasAuthority() || !Actor)
	{
		return;
	}
	// Prune here as well as on the freeze edge: a boss fight with no level-up in it would otherwise accumulate an
	// entry per orb for the whole fight, and the freeze edge is not something the code gets to count on.
	SpawnedPatternActors.RemoveAllSwap([](const TWeakObjectPtr<AActor>& Ref) { return !Ref.IsValid(); }, EAllowShrinking::No);
	SpawnedPatternActors.Add(Actor);
}

void AFPSRBossBase::ServerScheduleLaserHit(APawn* Target, float Damage, const FFPSRDamageSpec& Spec)
{
	if (!HasAuthority() || !Target)
	{
		return;
	}
	FPendingLaserHit& Pending = PendingLaserHits.AddDefaulted_GetRef();
	Pending.Target = Target;
	Pending.DueClock = GetPatternClockSeconds() + FMath::Max(0.0f, LateJumpGraceSeconds);
	Pending.Damage = Damage;
	Pending.Spec = Spec;
}

bool AFPSRBossBase::WasRecentlyAirborne(const APawn* Pawn) const
{
	if (!Pawn)
	{
		return false;
	}
	if (const float* Last = LastAirborneClock.Find(const_cast<APawn*>(Pawn)))
	{
		return (GetPatternClockSeconds() - *Last) <= AirborneGraceSeconds;
	}
	// Never recorded (e.g. the boss has only just started ticking): fall back to the instantaneous state rather than
	// asserting "on the ground", so a player already mid-jump is not punished for the missing history.
	const UPawnMovementComponent* Move = Pawn->GetMovementComponent();
	return Move && Move->IsFalling();
}

void AFPSRBossBase::PlayBossMontage(UAnimMontage* Montage, float PlayRate)
{
	// Play on the inherited ACharacter skeletal mesh (the boss BP assigns Prime_Helix + its AnimBP there). No-op until
	// content assigns a skeletal mesh/AnimBP (no AnimInstance) or when Montage is null. Cosmetic, all net modes.
	if (!Montage)
	{
		return;
	}
	if (USkeletalMeshComponent* BossMesh = GetMesh())
	{
		if (UAnimInstance* AnimInst = BossMesh->GetAnimInstance())
		{
			AnimInst->Montage_Play(Montage, PlayRate);
		}
	}
}

void AFPSRBossBase::HandleDeathCosmetic()
{
	if (!DeathMontage.IsNull())
	{
		PlayBossMontage(DeathMontage.LoadSynchronous());
	}
}

void AFPSRBossBase::HandleDeath(AActor* DeadActor, AActor* Killer)
{
	// Death animation for the listen-server host / standalone (clients play it via OnDeathCosmetic / OnRep_bDead).
	HandleDeathCosmetic();

	// OnDeath broadcasts on the server (UFPSREnemyHealthComponent::ApplyDamage is authority-gated). End the run in
	// Victory through the GameMode — loose coupling: the boss never calls EndRun directly (U2 NotifyPlayerDefeated
	// mirror). bRunEnded inside EndRun guards against a same-frame defeat race.
	if (UWorld* World = GetWorld())
	{
		if (AFPSRGameMode* GameMode = World->GetAuthGameMode<AFPSRGameMode>())
		{
			GameMode->NotifyBossDefeated();
		}
	}

	UE_LOG(LogFPSR, Log, TEXT("[Boss] %s defeated by %s — run won"), *GetName(),
		Killer ? *Killer->GetName() : TEXT("unknown"));

	// 🔴 The boss is NOT destroyed here (see below), so its patterns have to be closed explicitly. Without this the
	// corpse keeps shelling: the run-end freeze happens to stop the tick today, but that is EndRun's behaviour, not
	// this class's contract, and a boss that dies for any other reason would keep firing.
	ServerReleaseAllPatternState();
	SetActorTickEnabled(false);

	// No XP drop / pooling / Destroy: EndRunFreeze stops the world behind the result screen and the lobby travel
	// tears the level down. Leaving the boss in place keeps it visible during the result beat.
}

void AFPSRBossBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Runs on clients too; the released state is all server-side, so gate it. StartRun destroys a leftover boss on a
	// re-run (FPSRRunDirectorSubsystem), and the orbs it spawned are independent actors that would otherwise survive
	// into the next run's world.
	if (HasAuthority())
	{
		ServerReleaseAllPatternState();
	}
	Super::EndPlay(EndPlayReason);
}

void AFPSRBossBase::ServerReleaseAllPatternState()
{
	if (!HasAuthority() || bPatternStateReleased)
	{
		return;
	}
	bPatternStateReleased = true;

	if (AbilitySystem)
	{
		AbilitySystem->CancelAbilities();
	}
	ActivePatternHandle = FGameplayAbilitySpecHandle();

	// Beam off FIRST: this also fires the cosmetic that tells clients to stop drawing it.
	ServerSetBeamState(0, 0.0f, 0.0f, 0.0f, 0.0f, BeamVisualHeightCm);

	// Markers are removed WITHOUT detonating (a fizzle) — clients judge detonation by the clock, so a marker that
	// disappears before its time simply never plays an explosion.
	if (BlastMarks.Num() > 0)
	{
		BlastMarks.Reset();
		MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRBossBase, BlastMarks, this);
	}

	for (const TWeakObjectPtr<AActor>& Spawned : SpawnedPatternActors)
	{
		if (AActor* Actor = Spawned.Get())
		{
			Actor->Destroy();
		}
	}
	SpawnedPatternActors.Reset();

	// Everything keyed by pawn goes too — "nothing of the boss outlives the boss" has to include the bookkeeping,
	// or it is a claim no one can check.
	PendingLaserHits.Reset();
	LastAirborneClock.Reset();
}

void AFPSRBossBase::ServerPushSimulationPaused(bool bPaused)
{
	// One detector, N pushes — pattern actors never poll the freeze state themselves (the projectile subsystem
	// solves the identical problem the same way).
	for (int32 Index = SpawnedPatternActors.Num() - 1; Index >= 0; --Index)
	{
		AActor* Actor = SpawnedPatternActors[Index].Get();
		if (!Actor)
		{
			SpawnedPatternActors.RemoveAtSwap(Index, EAllowShrinking::No);
			continue;
		}
		if (IFPSRPatternActor* Pattern = Cast<IFPSRPatternActor>(Actor))
		{
			Pattern->SetSimulationPaused(bPaused);
		}
	}
}

void AFPSRBossBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Order matters and is fixed (BOSS1 §5-3):
	//   1 authority  2 run-end recovery  3 freeze edge push  4 freeze/transition gate  5 airborne + pending hits
	//   6 blast fuses  7 selector / active pattern
	// (2) sits AHEAD of (4) on purpose: EndRunFreeze pins bRunPaused ON, so a recovery placed after the gate would
	// never run at all.
#if !UE_BUILD_SHIPPING
	DebugDrawPatterns();
#endif

	if (!HasAuthority())
	{
		return;
	}

	AFPSRGameState* GS = GetWorld() ? GetWorld()->GetGameState<AFPSRGameState>() : nullptr;
	if (!GS)
	{
		return;
	}

	if (GS->HasRunEnded())
	{
		ServerReleaseAllPatternState();
		SetActorTickEnabled(false);
		return;
	}

	const bool bFrozen = GS->IsRunPaused() || GS->IsStageTransitionActive();
	if (bFrozen != bWasFrozenLastTick)
	{
		bWasFrozenLastTick = bFrozen;
		ServerPushSimulationPaused(bFrozen);
	}
	if (bFrozen)
	{
		return;
	}

	const float Clock = GetPatternClockSeconds();
	if (FightStartClock < 0.0f)
	{
		// First non-frozen server tick IS the start of the fight — the boss spawns and immediately begins ticking,
		// and anchoring here (rather than at BeginPlay) means a freeze during the spawn beat cannot skew every
		// Elapsed trigger for the rest of the fight.
		FightStartClock = Clock;
	}

	// Airborne history — the "already jumped" half of the laser's dodge window. Recorded for every tracked player
	// every tick so the laser never has to trace or guess.
	//
	// Stale keys are dropped in the same pass: the map is keyed by PAWN, and a player who dies and respawns leaves
	// the old pawn behind, so without this the map would grow for the whole run. Same reasoning (and same fix) as
	// the swarm's LastGroundedZByPlayer, which prunes inside its own collection loop.
	for (auto It = LastAirborneClock.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (const APlayerController* PC = It->Get())
		{
			if (APawn* Pawn = PC->GetPawn())
			{
				const UPawnMovementComponent* Move = Pawn->GetMovementComponent();
				if (Move && Move->IsFalling())
				{
					LastAirborneClock.Add(Pawn, Clock);
				}
			}
		}
	}

	ServerSettlePendingLaserHits();
	ServerDetonateDueBlastMarks();

	// Drive the active pattern, or look for the next one.
	if (ActivePatternHandle.IsValid() && AbilitySystem)
	{
		const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(ActivePatternHandle);
		UFPSRBossGameplayAbility* Active = Spec ? Cast<UFPSRBossGameplayAbility>(Spec->GetPrimaryInstance()) : nullptr;
		if (Spec && Spec->IsActive() && Active)
		{
			Active->ServerTickPattern(DeltaSeconds);
			return;
		}
		// The ability ended (on its own or via cancel).
		ActivePatternHandle = FGameplayAbilitySpecHandle();
		++PatternsPerformed;
	}

	// Patterns do NOT chain. The boss idles here until a trigger fires (§14-3) — that idle is the whole reason the
	// fight has a rhythm instead of being a continuous stream.
	if (ServerConsumeAnyTrigger())
	{
		ServerTryActivateNextPattern();
	}
}

void AFPSRBossBase::ServerSettlePendingLaserHits()
{
	const float Clock = GetPatternClockSeconds();
	for (int32 Index = PendingLaserHits.Num() - 1; Index >= 0; --Index)
	{
		FPendingLaserHit& Pending = PendingLaserHits[Index];
		APawn* Target = Pending.Target.Get();
		if (!Target)
		{
			PendingLaserHits.RemoveAtSwap(Index, EAllowShrinking::No);
			continue;
		}
		if (Clock < Pending.DueClock)
		{
			continue;
		}

		// Cancel if the target went airborne AFTER this hit was booked. Deliberately not WasRecentlyAirborne(): its
		// trailing window also matches jumps from BEFORE the booking, which would forgive a hit the player never
		// actually dodged.
		const float* LastAirborne = LastAirborneClock.Find(Target);
		const bool bJumpedInTime = LastAirborne && (*LastAirborne > (Pending.DueClock - LateJumpGraceSeconds));
		if (!bJumpedInTime)
		{
			if (AFPSRCharacter* Character = Cast<AFPSRCharacter>(Target))
			{
				Character->ApplyContactDamage(Pending.Damage, this, Pending.Spec);
			}
		}
		PendingLaserHits.RemoveAtSwap(Index, EAllowShrinking::No);
	}
}

void AFPSRBossBase::ServerDetonateDueBlastMarks()
{
	const float Clock = GetPatternClockSeconds();
	bool bAnyRemoved = false;
	for (int32 Index = BlastMarks.Num() - 1; Index >= 0; --Index)
	{
		const FFPSRBossBlastMark& Mark = BlastMarks[Index];
		if (Clock < Mark.DetonateAtClock)
		{
			continue;
		}
		// Instigator is THIS BOSS, not the marker — the marker is a struct, and the director's telemetry classifies
		// incoming pressure by the instigator's class.
		FPSRCombat::ApplyHostileExplosion(GetWorld(), Mark.Center, Mark.Radius, Mark.Damage, this, Mark.KnockbackStrength, Mark.Spec);
		BlastMarks.RemoveAt(Index, EAllowShrinking::No);
		bAnyRemoved = true;
	}
	if (bAnyRemoved)
	{
		MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRBossBase, BlastMarks, this);
	}
}

#if !UE_BUILD_SHIPPING
void AFPSRBossBase::DebugDrawPatterns() const
{
	UWorld* World = GetWorld();
	if (!World || !FPSRBoss::IsDebugDrawEnabled())
	{
		return;
	}

	// Everything below reads REPLICATED state and the derived clock, nothing server-only — that is what makes the
	// client's overlay a real comparison against the server rather than a second opinion drawn from the same data.
	const float Clock = GetPatternClockSeconds();
	const FVector Origin = GetActorLocation();
	// One frame's lifetime: this is called every tick, so persistent shapes would just pile up.
	const float Life = -1.0f;

	// ---- Blast markers ----------------------------------------------------------------------------------------
	for (const FFPSRBossBlastMark& Mark : BlastMarks)
	{
		const float Remaining = Mark.DetonateAtClock - Clock;
		// Green far out, red at the moment of detonation — the fuse has to be readable at a glance, because "did I
		// have time to leave" is the only question these markers exist to answer.
		const float T = FMath::Clamp(Remaining / 1.5f, 0.0f, 1.0f);
		const FColor Colour = FColor(static_cast<uint8>(255 * (1.0f - T)), static_cast<uint8>(255 * T), 0);
		DrawDebugCircle(World, Mark.Center + FVector(0, 0, 5.0f), Mark.Radius, 32, Colour, false, Life, 0, 4.0f,
			FVector(1, 0, 0), FVector(0, 1, 0), /*bDrawAxis=*/false);
		DrawDebugString(World, Mark.Center + FVector(0, 0, 120.0f),
			FString::Printf(TEXT("%.1fs"), FMath::Max(0.0f, Remaining)), nullptr, Colour, 0.0f, true);
	}

	// ---- Beams ------------------------------------------------------------------------------------------------
	if (BeamCount > 0)
	{
		// Long enough to leave the arena; falls back to a fixed length when the arena has no grid bounds yet.
		float Length = 12000.0f;
		if (const AFPSRGameState* GS = World->GetGameState<AFPSRGameState>())
		{
			if (const AFPSRArenaActor* Arena = GS->GetActiveArena())
			{
				FVector2D Min, Max;
				if (Arena->GetGridBoundsXY(Min, Max))
				{
					Length = FVector2D::Distance(Min, Max);
				}
			}
		}

		const bool bWarmup = Clock < BeamWarmupEndClock;
		// Yellow while it is standing still and harmless, red once it sweeps and bites. If these two ever look the
		// same, the warning has stopped being a warning.
		const FColor Colour = bWarmup ? FColor::Yellow : FColor::Red;
		const float Base = FPSRBossLaser::BeamBaseAngleAt(BeamStartAngleDeg, BeamSpeedDegPerSec, BeamWarmupEndClock, Clock);
		const float Step = 360.0f / BeamCount;
		for (int32 Index = 0; Index < BeamCount; ++Index)
		{
			const float Rad = FMath::DegreesToRadians(Base + Index * Step);
			const FVector Dir(FMath::Cos(Rad), FMath::Sin(Rad), 0.0f);
			const FVector Start = Origin + Dir * 1250.0f + FVector(0, 0, BeamVisualHeightCm);
			DrawDebugLine(World, Start, Origin + Dir * Length + FVector(0, 0, BeamVisualHeightCm), Colour, false, Life, 0, 12.0f);
		}
	}

	// ---- Marked player ----------------------------------------------------------------------------------------
	if (const APawn* Marked = GetMarkedPawn())
	{
		const FVector Head = Marked->GetActorLocation() + FVector(0, 0, 140.0f);
		DrawDebugLine(World, Head, Head + FVector(0, 0, 260.0f), FColor::Magenta, false, Life, 0, 6.0f);
		DrawDebugString(World, Head + FVector(0, 0, 280.0f), TEXT("MARKED"), nullptr, FColor::Magenta, 0.0f, true);
	}

	// ---- Stage readout ----------------------------------------------------------------------------------------
	// The stage is the one thing with no other tell: "the boss is winding up" and "the boss is recovering" look
	// identical on an unanimated placeholder, and half the PIE checks are about exactly that distinction.
	const TCHAR* StageName = TEXT("Finished");
	switch (PatternStage)
	{
	case EFPSRBossPatternStage::Prep:     StageName = TEXT("PREP (nothing spawned yet)"); break;
	case EFPSRBossPatternStage::Execute:  StageName = TEXT("EXECUTE"); break;
	case EFPSRBossPatternStage::Recovery: StageName = TEXT("RECOVERY (harmless)"); break;
	default: break;
	}
	DrawDebugString(World, Origin + FVector(0, 0, 3000.0f),
		FString::Printf(TEXT("%s | phase %d | %s | clock %.1f"),
			HasAuthority() ? TEXT("SERVER") : TEXT("CLIENT"), CurrentPhase, StageName, Clock),
		nullptr, FColor::White, 0.0f, true);
}
#endif // !UE_BUILD_SHIPPING

bool AFPSRBossBase::ServerConsumeAnyTrigger()
{
	const float Clock = GetPatternClockSeconds();
	const float Elapsed = FightStartClock >= 0.0f ? (Clock - FightStartClock) : 0.0f;
	const float HealthFraction = (HealthComponent && HealthComponent->GetMaxHealth() > 0.0f)
		? HealthComponent->GetHealth() / HealthComponent->GetMaxHealth()
		: 1.0f;

	// The decision itself is a pure function (FPSRBoss::ShouldTriggerFire) so the whole fight's cadence can be
	// verified with no world; this loop owns only writing the latch back.
	bool bFired = false;
	for (FFPSRBossPatternTrigger& Trigger : PatternTriggers)
	{
		int32 NewFireCount = Trigger.FireCount;
		if (FPSRBoss::ShouldTriggerFire(Trigger, Elapsed, PatternsPerformed, HealthFraction, NewFireCount))
		{
			Trigger.FireCount = NewFireCount;
			bFired = true;
		}
	}
	return bFired;
}

void AFPSRBossBase::ServerTryActivateNextPattern()
{
	if (!AbilitySystem || GrantedAbilityHandles.Num() == 0)
	{
		return;
	}

	// Gather what is legal right now, then let the policy choose among those. Filtering first is what keeps Random
	// from "rolling" a pattern that is on cooldown and then doing nothing that tick.
	TArray<int32, TInlineAllocator<8>> Eligible;
	for (int32 Index = 0; Index < GrantedAbilityHandles.Num(); ++Index)
	{
		const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(GrantedAbilityHandles[Index]);
		const UFPSRBossGameplayAbility* Ability = Spec ? Cast<UFPSRBossGameplayAbility>(Spec->Ability) : nullptr;
		if (Ability && CurrentPhase >= Ability->GetMinPhase())
		{
			Eligible.Add(Index);
		}
	}
	if (Eligible.Num() == 0)
	{
		return;
	}

	// The visit order is a pure function so the policy is testable with no ASC and no world
	// (FPSRBoss::BuildSelectionOrder). Both policies walk EVERY candidate — only the starting point differs — which
	// is what stops a cooldown-blocked pick from wasting the trigger that fired.
	//
	// The roll is taken only for Random: BuildSelectionOrder ignores it otherwise, but FMath::RandHelper draws from
	// the global stream, and a Sequential boss must not perturb that stream for everything else in the run.
	const int32 RandomStart = (SelectionPolicy == EFPSRBossPatternSelection::Random)
		? FMath::RandHelper(Eligible.Num())
		: 0;

	TArray<int32> Order;
	FPSRBoss::BuildSelectionOrder(SelectionPolicy, Eligible.Num(), NextPatternIndex, RandomStart, Order);

	for (const int32 Slot : Order)
	{
		const int32 Index = Eligible[Slot];

		// TryActivateAbility runs the ability's own CheckCooldown, so the freeze-paused cooldown contract is honoured
		// without this selector re-implementing it.
		if (AbilitySystem->TryActivateAbility(GrantedAbilityHandles[Index]))
		{
			ActivePatternHandle = GrantedAbilityHandles[Index];
			NextPatternIndex = (Slot + 1) % Eligible.Num();
			return;
		}
	}
}

#if WITH_EDITOR
EDataValidationResult AFPSRBossBase::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (GrantedAbilities.Num() == 0)
	{
		Context.AddWarning(NSLOCTEXT("FPSRBoss", "NoPatterns",
			"Boss has no GrantedAbilities — the selector has nothing to choose from, so this boss will never attack."));
	}

	if (PatternTriggers.Num() == 0)
	{
		Context.AddWarning(NSLOCTEXT("FPSRBoss", "NoTriggers",
			"Boss has no PatternTriggers. At runtime this falls back to a repeating PatternGapSeconds cadence, but that "
			"fallback is a safety net rather than a design — author the triggers you actually want."));
	}

	// The RULE lives in FPSRBoss::ValidateTrigger (pure, unit-tested); this loop only chooses the wording.
	for (const FFPSRBossPatternTrigger& Trigger : PatternTriggers)
	{
		switch (FPSRBoss::ValidateTrigger(Trigger))
		{
		case FPSRBoss::ETriggerAuthoringIssue::ThresholdNotPositive:
			Context.AddError(NSLOCTEXT("FPSRBoss", "TriggerThresholdZero",
				"A PatternTrigger has Threshold <= 0. Elapsed/PatternCount would be due on the first tick and never stop "
				"being due; HealthBelow could only ever fire on the death frame."));
			Result = EDataValidationResult::Invalid;
			break;

		case FPSRBoss::ETriggerAuthoringIssue::HealthThresholdFull:
			Context.AddError(NSLOCTEXT("FPSRBoss", "TriggerHealthFull",
				"A HealthBelow trigger has Threshold >= 1, so it fires before the boss has taken any damage at all."));
			Result = EDataValidationResult::Invalid;
			break;

		default:
			break;
		}
	}

	// The marker cap lives on the boss but the numbers that fill it live on the barrage, so this is the only place
	// both are visible.
	for (const TSubclassOf<UFPSRBossGameplayAbility>& AbilityClass : GrantedAbilities)
	{
		const UFPSRBossGA_Barrage* Barrage = AbilityClass
			? Cast<UFPSRBossGA_Barrage>(AbilityClass->GetDefaultObject())
			: nullptr;
		if (!Barrage)
		{
			continue;
		}
		const int32 Peak = FPSRBoss::EstimatePeakBlastMarks(FPSRBoss::MaxSupportedPlayers,
			Barrage->GetFuseSeconds(), Barrage->GetIntervalSeconds());
		if (Peak > MaxConcurrentMarks)
		{
			Context.AddWarning(FText::Format(
				NSLOCTEXT("FPSRBoss", "MarksOverCap",
					"{0} can have up to {1} markers alive at once (players x fuse/interval) but MaxConcurrentMarks is {2}. "
					"The oldest would be fizzled without detonating — raise the cap or shorten the fuse."),
				FText::FromString(AbilityClass->GetName()), FText::AsNumber(Peak), FText::AsNumber(MaxConcurrentMarks)));
		}
	}

	return Result;
}
#endif // WITH_EDITOR

#if !UE_BUILD_SHIPPING
void AFPSRBossBase::DebugForcePattern(int32 Index)
{
	if (!HasAuthority() || !AbilitySystem || !GrantedAbilityHandles.IsValidIndex(Index))
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Boss] FPSR.BossPattern %d — out of range (0..%d)."), Index, GrantedAbilityHandles.Num() - 1);
		return;
	}
	// Cancel whatever is running so the forced pattern starts from a clean slate. Triggers are deliberately NOT
	// consumed here — this command exists to test a pattern in isolation, and making it burn a trigger would change
	// the cadence you were trying to observe.
	AbilitySystem->CancelAbilities();
	ActivePatternHandle = FGameplayAbilitySpecHandle();

	// Cancelling does not clear a cooldown, so without this the command refuses exactly when you want it most —
	// right after the pattern you are iterating on has just run.
	if (const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(GrantedAbilityHandles[Index]))
	{
		if (UFPSRFreezeCooldownAbility* Instance = Cast<UFPSRFreezeCooldownAbility>(Spec->GetPrimaryInstance()))
		{
			Instance->DebugClearCooldown();
		}
	}

	if (AbilitySystem->TryActivateAbility(GrantedAbilityHandles[Index]))
	{
		ActivePatternHandle = GrantedAbilityHandles[Index];
		UE_LOG(LogFPSR, Log, TEXT("[Boss] FPSR.BossPattern %d — activated."), Index);
	}
	else
	{
		// The cooldown was cleared just above, so a refusal here is a real precondition — most often MinPhase.
		const FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(GrantedAbilityHandles[Index]);
		const UFPSRBossGameplayAbility* Ability = Spec ? Cast<UFPSRBossGameplayAbility>(Spec->Ability) : nullptr;
		UE_LOG(LogFPSR, Warning, TEXT("[Boss] FPSR.BossPattern %d — refused (MinPhase %d, boss is phase %d)."),
			Index, Ability ? Ability->GetMinPhase() : -1, CurrentPhase);
	}
}

void AFPSRBossBase::DebugSetPhase(int32 Phase)
{
	if (!HasAuthority())
	{
		return;
	}
	const int32 Latched = FPSRBoss::LatchPhase(CurrentPhase, FMath::Max(1, Phase));
	if (Latched == CurrentPhase)
	{
		UE_LOG(LogFPSR, Warning, TEXT("[Boss] FPSR.BossPhase %d — phases are monotonic; already at %d."), Phase, CurrentPhase);
		return;
	}
	CurrentPhase = Latched;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRBossBase, CurrentPhase, this);
	OnPhaseChangedCosmetic(CurrentPhase);
	UE_LOG(LogFPSR, Log, TEXT("[Boss] FPSR.BossPhase -> %d"), CurrentPhase);
}
#endif // !UE_BUILD_SHIPPING
