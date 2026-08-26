// Copyright Epic Games, Inc. All Rights Reserved.

#include "Destructible/FPSRDestructible.h"
#include "Destructible/FPSRDestructibleReward.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Core/FPSRLogChannels.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"

AFPSRDestructible::AFPSRDestructible()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true; // bBroken/DamageStage replicate so late joiners / clients see the correct broken state

	// Neutral scene root — subclasses attach their own mesh(es) under this (Game.MD §2: no hardcoded asset paths,
	// so this base never assigns a mesh itself). AFPSRDoor attaches DoorMesh/FrameMesh as siblings of each other
	// under this same root.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	HealthComponent = CreateDefaultSubobject<UFPSREnemyHealthComponent>(TEXT("HealthComponent"));
	HealthComponent->SetCountsAsKill(false); // destructible, but NOT an enemy (no kill credit / on-kill / lifesteal)
}

void AFPSRDestructible::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		if (HealthComponent)
		{
			// Size HP to the designer's durability, or a server-set override if one arrived BEFORE BeginPlay
			// (GetEffectiveDurability — a suppressor's stage/party-size-scaled durability, ADR 0010 D6; see
			// ServerSetDurabilityOverride's comment for why either arrival order is safe), then listen for health
			// changes (damage stages) and death (break).
			HealthComponent->InitializeMaxHealth(GetEffectiveDurability());
			HealthComponent->OnHealthChanged.AddDynamic(this, &AFPSRDestructible::HandleHealthChanged);
			HealthComponent->OnDeath.AddDynamic(this, &AFPSRDestructible::HandleBroken);
		}
	}
	else if (bBroken)
	{
		// Late-joining client: already broken when it became net-relevant — apply the open state now (OnRep won't
		// fire for the initial replicated value). Virtual, so a late-joining client sees the correct subclass state.
		ApplyBrokenState();
	}
}

void AFPSRDestructible::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRDestructible, bBroken, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(AFPSRDestructible, DamageStage, Params);
}

int32 AFPSRDestructible::ComputeDamageStage(TConstArrayView<float> Thresholds, float HealthPct)
{
	// Count how many thresholds the current percent is at-or-below (thresholds are descending, so this is the
	// stage count). Order-independent for the count; index semantics assume descending (see header).
	int32 NewStage = 0;
	for (const float Threshold : Thresholds)
	{
		if (HealthPct <= Threshold)
		{
			++NewStage;
		}
	}
	return NewStage;
}

void AFPSRDestructible::HandleHealthChanged(float NewHealth, float MaxHealth)
{
	// Server-only (OnHealthChanged broadcasts on authority): advance the damage stage and fire the BP presentation
	// for any thresholds the hit just crossed. Clients fire the same stages via OnRep_DamageStage.
	if (!HasAuthority() || bBroken || MaxHealth <= 0.0f)
	{
		return;
	}

	const float Pct = NewHealth / MaxHealth;
	const int32 NewStage = ComputeDamageStage(DamageStageThresholds, Pct);

	// 파괴물이 맞았다는 **유일한 관측 신호**. 이게 없으면 "안 맞은 것"과 "맞았는데 연출이 없는 것"을
	// 구분할 방법이 없다 — 실제로 억제기 배치 검증에서 그 둘을 못 가려 한참 헤맸다. 파괴물은 개수가
	// 적고(스웜과 달리) 맞을 때만 찍히므로 Verbose 가 아니라 Log 로 둔다.
	UE_LOG(LogFPSR, Log, TEXT("[Destructible] %s 피격 — 체력 %.0f/%.0f (%.0f%%), 손상 단계 %d"),
		*GetName(), NewHealth, MaxHealth, Pct * 100.0f, NewStage);

	if (NewStage > DamageStage)
	{
		FireDamageStages(DamageStage, NewStage, Pct); // server-local presentation
		DamageStage = static_cast<uint8>(NewStage);
		MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRDestructible, DamageStage, this);
	}
}

void AFPSRDestructible::OnRep_DamageStage(uint8 OldStage)
{
	if (DamageStage > OldStage)
	{
		FireDamageStages(OldStage, DamageStage, -1.0f); // client: no exact health, report per-stage threshold
	}
}

void AFPSRDestructible::FireDamageStages(int32 FromStage, int32 ToStage, float CurrentPct)
{
	for (int32 Stage = FromStage; Stage < ToStage; ++Stage)
	{
		const float Threshold = DamageStageThresholds.IsValidIndex(Stage) ? DamageStageThresholds[Stage] : 0.0f;
		const float ReportPct = (CurrentPct >= 0.0f) ? CurrentPct : Threshold;
		FireDamageStagePresentation(Stage, ReportPct, Threshold);
	}
}

void AFPSRDestructible::HandleBroken(AActor* DeadActor, AActor* Killer)
{
	if (!HasAuthority() || bBroken)
	{
		return;
	}

	bBroken = true;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRDestructible, bBroken, this);

	// 보상 개수까지 찍는다: 억제기를 부쉈는데 전환이 안 오는 흔한 원인이 「파괴 보상」 배열이 비어
	// 있는 것이고, 그때 리워드 쪽 로그는 아무것도 안 남는다(Grant 가 호출조차 안 되므로).
	UE_LOG(LogFPSR, Log, TEXT("[Destructible] %s 파괴됨 (파괴자 %s) — 보상 %d개 적용"),
		*GetName(), Killer ? *Killer->GetName() : TEXT("?"), Rewards.Num());

	// Fixed order: HandleBrokenAuthority BEFORE ApplyBrokenState BEFORE FireBrokenPresentation. AFPSRDoor's
	// HandleBrokenAuthority override notifies the flow field of the broken seam using the door leaf's bounds —
	// that collision is still live at this point; ApplyBrokenState (next) is what disables it, so any
	// authority-only reaction that needs the pre-broken collision/component state MUST run first.
	HandleBrokenAuthority(Killer);
	ApplyBrokenState();
	FireBrokenPresentation();
}

void AFPSRDestructible::OnRep_Broken()
{
	if (bBroken)
	{
		ApplyBrokenState();
		FireBrokenPresentation();
	}
	else
	{
		// F2: the false edge — a broken destructible replicated back to intact (ServerReset, e.g. an arena revisit
		// — see AFPSRArenaActor::SetArenaActive). No presentation fires here, unlike the true edge: there is no
		// "un-breaking" cosmetic to show, and re-firing OnDestructibleBroken() on the way back up would be backwards.
		ClearBrokenState();
	}
}

void AFPSRDestructible::ServerReset()
{
	if (!HasAuthority() || !bBroken)
	{
		return; // already intact — no-op. Called unconditionally by SetArenaActive(true) for every destructible in
		        // the grid, broken or not, so this guard is what keeps an already-intact prop a true no-op.
	}

	bBroken = false;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRDestructible, bBroken, this);

	DamageStage = 0;
	MARK_PROPERTY_DIRTY_FROM_NAME(AFPSRDestructible, DamageStage, this);

	// InitializeMaxHealth, NOT ResetForReuse: both restore Health to full and clear the health component's dead
	// flag, but InitializeMaxHealth also re-asserts MaxHealth = GetEffectiveDurability() — the designer-authored
	// Durability, or the server-set stage/party-size override if one is currently active (ADR 0010 D6) — rather
	// than trusting whatever MaxHealth already holds. That is the more defensive restore for something coming back
	// from being fully destroyed (this reset), as opposed to ResetForReuse's pooled-actor reuse (where MaxHealth
	// was never touched to begin with).
	if (HealthComponent)
	{
		HealthComponent->InitializeMaxHealth(GetEffectiveDurability());
	}

	ClearBrokenState();
}

bool AFPSRDestructible::IsSuppressor() const
{
	// ADR 0010 D6's own definition, applied literally: "any AFPSRArenaDestructible authored with exactly this one
	// Reward IS the suppressor" — so this asks the Rewards array the same question HandleBrokenAuthority's payout
	// loop already answers, rather than tracking a second, independently-authored flag that could disagree with it.
	for (const TObjectPtr<UFPSRDestructibleReward>& Reward : Rewards)
	{
		if (Reward && Reward->IsA<UFPSRDestructibleReward_StageTransition>())
		{
			return true;
		}
	}
	return false;
}

void AFPSRDestructible::ServerSetDurabilityOverride(float NewDurability)
{
	if (!HasAuthority())
	{
		return;
	}

	// 🔴 InitializeMaxHealth fully heals AND clears the health component's dead flag (see its own header comment) —
	// calling it on an already-broken destructible would resurrect it at full health, a "broken but full-HP
	// zombie". Current call sites (UFPSRRunDirectorSubsystem::ApplyStageDifficultyToArena, right after a stage
	// commit) only ever touch freshly-(re)activated arenas whose destructibles were just ServerReset(), so bBroken
	// is never actually true there today — but the guard nails the contract down rather than relying on that
	// staying true at every future call site.
	if (bBroken)
	{
		return;
	}

	DurabilityOverride = NewDurability;
	if (HealthComponent)
	{
		HealthComponent->InitializeMaxHealth(GetEffectiveDurability());
	}
}

float AFPSRDestructible::GetEffectiveDurability() const
{
	return (DurabilityOverride > 0.0f) ? DurabilityOverride : Durability;
}

void AFPSRDestructible::HandleBrokenAuthority(AActor* Breaker)
{
	FFPSRDestructibleRewardContext Context;
	Context.Source = this;
	Context.Breaker = Breaker;
	Context.World = GetWorld();

	for (const TObjectPtr<UFPSRDestructibleReward>& Reward : Rewards)
	{
		if (!Reward)
		{
			continue; // an author left an empty Instanced slot — skip, don't crash the rest of the array
		}
		Reward->Grant(Context);
	}
}

void AFPSRDestructible::ApplyBrokenState()
{
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
}

void AFPSRDestructible::ClearBrokenState()
{
	SetActorEnableCollision(true);
	SetActorHiddenInGame(false);
}

void AFPSRDestructible::FireBrokenPresentation()
{
	OnDestructibleBroken();
}

void AFPSRDestructible::FireDamageStagePresentation(int32 StageIndex, float HealthPct, float Threshold)
{
	OnDestructibleDamageStage(StageIndex, HealthPct, Threshold);
}
