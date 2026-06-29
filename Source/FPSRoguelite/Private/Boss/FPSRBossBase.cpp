// Copyright Epic Games, Inc. All Rights Reserved.

#include "Boss/FPSRBossBase.h"
#include "Boss/FPSRBossDefinitionDataAsset.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Core/FPSRGameMode.h"
#include "Core/FPSRLogChannels.h"

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AFPSRBossBase::AFPSRBossBase()
{
	PrimaryActorTick.bCanEverTick = false;

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

	// Visible placeholder cube (no collision — the capsule is the hit volume). Designers replace it in the boss BP.
	BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(GetCapsuleComponent());
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetRelativeScale3D(FVector(2.4f, 2.4f, 4.0f)); // ~fill the capsule (cube is 100^3)
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		BodyMesh->SetStaticMesh(CubeMesh.Object);
	}

	// Stationary scaffold: kill gravity so the boss never falls off its spawn point (real boss re-enables movement).
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->GravityScale = 0.0f;
	}

	// Non-GAS health — the single reason every weapon path damages the boss with no new damage code.
	HealthComponent = CreateDefaultSubobject<UFPSREnemyHealthComponent>(TEXT("HealthComponent"));
}

void AFPSRBossBase::BeginPlay()
{
	Super::BeginPlay();

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

	if (HealthComponent)
	{
		HealthComponent->OnDeath.AddDynamic(this, &AFPSRBossBase::HandleDeath);

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
}

void AFPSRBossBase::HandleDeath(AActor* DeadActor, AActor* Killer)
{
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

	// No XP drop / pooling / Destroy: EndRunFreeze stops the world behind the result screen and the lobby travel
	// tears the level down. Leaving the boss in place keeps it visible during the result beat.
}
