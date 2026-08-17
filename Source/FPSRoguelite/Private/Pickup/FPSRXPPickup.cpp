// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pickup/FPSRXPPickup.h"
#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"

#include "AbilitySystem/Attributes/FPSRCombatSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Settings/FPSRPlaceholderVisualSettings.h"

AFPSRXPPickup::AFPSRXPPickup()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.3f));
	// Placeholder mesh is resolved in BeginPlay from config (Game.md §6-2: no hard-coded asset path in C++), on every
	// machine (cosmetic — clients need it too). A content BP that assigns its own mesh wins (the fallback is skipped).
	SetRootComponent(Mesh);
}

void AFPSRXPPickup::BeginPlay()
{
	Super::BeginPlay();

	// Resolve the placeholder gem mesh from config if the content BP left it unset (runs on all machines — cosmetic).
	// LoadSynchronous is the cached fast-path once the (engine BasicShapes) mesh is in memory, so per-gem cost is nil.
	if (Mesh && Mesh->GetStaticMesh() == nullptr)
	{
		if (const UFPSRPlaceholderVisualSettings* Settings = GetDefault<UFPSRPlaceholderVisualSettings>())
		{
			if (UStaticMesh* GemMesh = Settings->XPGemMesh.LoadSynchronous())
			{
				Mesh->SetStaticMesh(GemMesh);
			}
		}
	}

	// The magnet/collect/XP-grant pass runs only on the server (see Tick, which early-returns off-authority); clients
	// merely receive the replicated transform. Disable the tick off-authority so the many concurrent client gems don't
	// each wake a per-frame Tick just to early-return. The listen-server HOST keeps ticking (it is the authority);
	// standalone is authority too. Gems are spawn-and-destroy (not pooled), so this one-time BeginPlay gate suffices.
	if (!HasAuthority())
	{
		SetActorTickEnabled(false);
	}
}

void AFPSRXPPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!HasAuthority())
	{
		return; // collection/magnetism is server-authoritative; clients receive the replicated transform.
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// Global freeze (card selection, §2-2): the world is stopped — don't magnet, collect, or grant XP. Every other
	// progressing server loop (enemy movement/spawn, director, missions, projectiles) gates the same way (W1 P2-1).
	if (const AFPSRGameState* GameState = World->GetGameState<AFPSRGameState>())
	{
		if (GameState->IsRunPaused())
		{
			return;
		}

		// Stage-transition grace window (ADR 0010 D6): the whole point of the window is "grind the frozen swarm and
		// collect what it drops" (안 G), but player MOVEMENT is locked for that same window (IsMovementFrozen) — a
		// gem sitting outside the normal magnet radius would be visible but permanently unreachable, and the reward
		// would not actually pay out. A magnet pull doesn't fix it either: the window is a FIXED duration
		// (invariant 8), so a magnet's travel time would make arrival depend on how far the gem happened to drop.
		// ADR 0010 doesn't cover XP specifically (only the swarm/damage/movement axes) — this is the judgment call
		// that fills the gap: collect instantly, radius-independent, by whichever living player is nearest.
		if (GameState->IsStageTransitionActive())
		{
			APawn* NearestPlayer = nullptr;
			float NearestDistSq = TNumericLimits<float>::Max();
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				const APlayerController* PC = It->Get();
				APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
				if (!PlayerPawn)
				{
					continue;
				}
				if (const AFPSRPlayerState* PS = PC->GetPlayerState<AFPSRPlayerState>())
				{
					if (!PS->IsAlive())
					{
						continue; // DBNO/Dead players don't collect (mirrors the normal collect gate below)
					}
				}
				const float DistSq = FVector::DistSquaredXY(PlayerPawn->GetActorLocation(), GetActorLocation());
				if (DistSq < NearestDistSq)
				{
					NearestDistSq = DistSq;
					NearestPlayer = PlayerPawn;
				}
			}

			if (NearestPlayer != nullptr)
			{
				CollectBy(NearestPlayer);
			}
			return; // transition branch replaces the normal magnet/collect pass below for this tick
		}
	}

	const FVector PickupLocation = GetActorLocation();
	const float CollectRadiusSq = CollectRadius * CollectRadius;

	// Single pass over players. Collection target = nearest player within the (unscaled) CollectRadius.
	// Magnet target = the player with the strongest claim, ranked by distance relative to their OWN
	// effective radius (DistSq / EffRadiusSq). This lets a far player with an upgraded PickupRadius out-pull
	// a closer default-radius player, so the attribute stays meaningful in co-op (Codex P2).
	APawn* CollectPlayer = nullptr;
	float CollectBestDistSq = TNumericLimits<float>::Max();
	APawn* MagnetPlayer = nullptr;
	FVector MagnetPlayerLocation = FVector::ZeroVector;
	float MagnetBestRatio = TNumericLimits<float>::Max();

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
		if (PlayerPawn == nullptr)
		{
			continue;
		}

		// Non-alive players (DBNO downed or Dead) don't collect or magnet XP (U9).
		if (const AFPSRPlayerState* PS = PC->GetPlayerState<AFPSRPlayerState>())
		{
			if (!PS->IsAlive())
			{
				continue;
			}
		}

		const FVector PlayerLocation = PlayerPawn->GetActorLocation();
		const float DistSq = FVector::DistSquaredXY(PlayerLocation, PickupLocation);

		if (DistSq <= CollectRadiusSq && DistSq < CollectBestDistSq)
		{
			CollectBestDistSq = DistSq;
			CollectPlayer = PlayerPawn;
		}

		const float EffMagnetRadius = MagnetRadius * GetCollectorPickupRadiusMult(PlayerPawn);
		const float EffMagnetRadiusSq = EffMagnetRadius * EffMagnetRadius;
		if (EffMagnetRadiusSq > KINDA_SMALL_NUMBER && DistSq <= EffMagnetRadiusSq)
		{
			const float Ratio = DistSq / EffMagnetRadiusSq; // 0 = on the player, 1 = at the radius edge
			if (Ratio < MagnetBestRatio)
			{
				MagnetBestRatio = Ratio;
				MagnetPlayer = PlayerPawn;
				MagnetPlayerLocation = PlayerLocation;
			}
		}
	}

	if (CollectPlayer != nullptr)
	{
		CollectBy(CollectPlayer);
		return;
	}

	if (MagnetPlayer != nullptr)
	{
		const FVector ToPlayer = (MagnetPlayerLocation - PickupLocation).GetSafeNormal();
		AddActorWorldOffset(ToPlayer * MagnetSpeed * DeltaSeconds, true);
	}
}

float AFPSRXPPickup::GetCollectorPickupRadiusMult(class APawn* Pawn) const
{
	if (Pawn == nullptr)
	{
		return 1.0f;
	}

	IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn);
	UAbilitySystemComponent* ASC = ASI ? ASI->GetAbilitySystemComponent() : nullptr;
	if (ASC == nullptr)
	{
		return 1.0f;
	}

	const UFPSRCombatSet* CombatSet = ASC->GetSet<UFPSRCombatSet>();
	if (CombatSet == nullptr)
	{
		return 1.0f;
	}

	return FMath::Max(0.01f, CombatSet->GetPickupRadius());
}

void AFPSRXPPickup::CollectBy(APawn* Collector)
{
	// The one place a gem is banked. Two callers reach it — the normal radius collect and the stage-transition
	// instant collect (ADR 0010 D6) — and both must apply the collector's own XPGain multiplier before adding to the
	// shared pool; splitting that across two inline copies is how one of them eventually loses the multiplier.
	if (UWorld* World = GetWorld())
	{
		if (AFPSRGameState* GameState = World->GetGameState<AFPSRGameState>())
		{
			const float XPMult = GetCollectorXPGainMult(Collector);
			const int32 EffectiveXP = FMath::Max(1, FMath::RoundToInt(XPValue * XPMult));
			GameState->AddSharedXP(EffectiveXP);
		}
	}
	Destroy();
}

float AFPSRXPPickup::GetCollectorXPGainMult(class APawn* Pawn) const
{
	if (Pawn == nullptr)
	{
		return 1.0f;
	}

	IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn);
	UAbilitySystemComponent* ASC = ASI ? ASI->GetAbilitySystemComponent() : nullptr;
	if (ASC == nullptr)
	{
		return 1.0f;
	}

	const UFPSRCombatSet* CombatSet = ASC->GetSet<UFPSRCombatSet>();
	if (CombatSet == nullptr)
	{
		return 1.0f;
	}

	return FMath::Max(0.0f, CombatSet->GetXPGain());
}
