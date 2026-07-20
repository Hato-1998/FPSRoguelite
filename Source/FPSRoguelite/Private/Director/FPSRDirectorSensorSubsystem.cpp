// Copyright Epic Games, Inc. All Rights Reserved.

#include "Director/FPSRDirectorSensorSubsystem.h"

#include "Core/FPSRGameState.h"
#include "Core/FPSRPlayerState.h"
#include "AbilitySystem/Attributes/FPSRHealthSet.h"

// Concrete actor classes for damage-source classification (this is the ONLY place they are needed).
#include "Enemy/FPSREnemyBase.h"
#include "Boss/FPSRBossBase.h"
#include "Hero/FPSRCharacter.h"
#include "Door/FPSRDoor.h"
#include "Run/Mission/FPSRMissionActor.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogFPSRTelemetry, Log, All);

// ==================================================================================================
//  Pure damage-source classifier (declared in FPSRDirectorSensorTypes.h; needs the concrete classes).
// ==================================================================================================
EFPSRDamageSourceClass FPSRTelemetry::ClassifyDamageSource(const AActor* Instigator, const AActor* Victim)
{
	if (Instigator == nullptr)
	{
		return EFPSRDamageSourceClass::Env;
	}
	if (Instigator == Victim)
	{
		return EFPSRDamageSourceClass::Self; // rocket-jump / explosion self-damage
	}
	// Boss FIRST: AFPSRBossBase derives from ACharacter, NOT AFPSREnemyBase, so an enemy cast would miss it.
	if (Cast<AFPSRBossBase>(Instigator))
	{
		return EFPSRDamageSourceClass::Boss;
	}
	if (Cast<AFPSREnemyBase>(Instigator)) // catches AFPSRRangedEnemyBase (subclass)
	{
		return EFPSRDamageSourceClass::Enemy;
	}
	// Self is already handled above, so any remaining player instigator is friendly fire.
	if (Cast<AFPSRCharacter>(Instigator))
	{
		return EFPSRDamageSourceClass::FriendlyFire;
	}
	if (Cast<AFPSRDoor>(Instigator))
	{
		return EFPSRDamageSourceClass::Door;
	}
	if (Cast<AFPSRMissionActor>(Instigator))
	{
		return EFPSRDamageSourceClass::Mission;
	}
	return EFPSRDamageSourceClass::Env;
}

// ==================================================================================================
//  UWorldSubsystem lifecycle
// ==================================================================================================
bool UFPSRDirectorSensorSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	// Game worlds only (skip editor/preview). Exists on server+client per project convention; every method
	// is gated by HasServerAuthority() so it is inert on a client.
	const UWorld* World = Cast<UWorld>(Outer);
	return World != nullptr && World->IsGameWorld();
}

void UFPSRDirectorSensorSubsystem::Deinitialize()
{
	if (bRunEndedBound)
	{
		if (AFPSRGameState* GS = GetGS())
		{
			GS->OnRunEnded.RemoveDynamic(this, &UFPSRDirectorSensorSubsystem::HandleRunEnded);
		}
		bRunEndedBound = false;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SensorTimerHandle);
	}
	Telemetry.Reset();
	Super::Deinitialize();
}

const FFPSRConfinementParams& UFPSRDirectorSensorSubsystem::DefaultConfinementParams()
{
	static const FFPSRConfinementParams Params;
	return Params;
}

bool UFPSRDirectorSensorSubsystem::HasServerAuthority() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->GetNetMode() != NM_Client;
}

AFPSRGameState* UFPSRDirectorSensorSubsystem::GetGS() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetGameState<AFPSRGameState>() : nullptr;
}

// ==================================================================================================
//  Run lifecycle (driven by AFPSRGameMode)
// ==================================================================================================
void UFPSRDirectorSensorSubsystem::StartRun()
{
	if (!HasServerAuthority())
	{
		return;
	}

	// Fresh baseline: no history carries across a run (design §8-2). Reset the clock and the map.
	Telemetry.Reset();
	SensorClock = 0.0f;
	bRunActive = true;

	// Observe run end via GameState (mirrors the director/GameMode pattern — run end is observed, not pushed).
	if (AFPSRGameState* GS = GetGS())
	{
		if (!bRunEndedBound)
		{
			GS->OnRunEnded.AddDynamic(this, &UFPSRDirectorSensorSubsystem::HandleRunEnded);
			bRunEndedBound = true;
		}
	}

	// Arm the aggregation clock (freeze-gated in SensorTick; a fixed step per unpaused tick = deterministic).
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(SensorTimerHandle, this, &UFPSRDirectorSensorSubsystem::SensorTick,
			SampleInterval, /*bLoop=*/true);
	}

	UE_LOG(LogFPSRTelemetry, Log, TEXT("[DirectorSensor] StartRun (interval=%.2fs)."), SampleInterval);
}

void UFPSRDirectorSensorSubsystem::EndRun()
{
	bRunActive = false;
	Telemetry.Reset(); // explicit cleanup -> tombstone leak 0
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SensorTimerHandle);
	}
	UE_LOG(LogFPSRTelemetry, Log, TEXT("[DirectorSensor] EndRun (telemetry cleared)."));
}

void UFPSRDirectorSensorSubsystem::HandleRunEnded()
{
	EndRun();
}

void UFPSRDirectorSensorSubsystem::OnPlayerLogout(AController* Exiting)
{
	if (!HasServerAuthority() || Exiting == nullptr)
	{
		return;
	}
	// The PlayerState is still valid synchronously in AFPSRGameMode::Logout (before CleanupPlayerState).
	if (AFPSRPlayerState* PS = Exiting->GetPlayerState<AFPSRPlayerState>())
	{
		Telemetry.Remove(TWeakObjectPtr<AFPSRPlayerState>(PS));
	}
	// Belt-and-suspenders: also sweep any already-invalid keys (a freeze halts the lazy prune in Advance).
	FPSRTelemetry::PruneInvalidTelemetry(Telemetry);
}

// ==================================================================================================
//  Live hooks (server-authoritative)
// ==================================================================================================
void UFPSRDirectorSensorSubsystem::NotifyPlayerDamageTaken(AActor* Victim, float Amount, AActor* Instigator)
{
	if (!HasServerAuthority() || Amount <= 0.0f || Victim == nullptr)
	{
		return;
	}
	const EFPSRDamageSourceClass Src = FPSRTelemetry::ClassifyDamageSource(Instigator, Victim);
	if (!FPSRTelemetry::CountsAsIncoming(Src)) // enemy/boss only — FF/self/door/mission/env excluded
	{
		return;
	}
	APawn* Pawn = Cast<APawn>(Victim);
	AFPSRPlayerState* PS = Pawn ? Pawn->GetPlayerState<AFPSRPlayerState>() : nullptr;
	if (PS == nullptr)
	{
		return;
	}
	FFPSRPlayerTelemetry& T = FindOrAddEntry(PS);
	if (Src == EFPSRDamageSourceClass::Boss)
	{
		T.BossDamageAccum += Amount;
	}
	else
	{
		T.EnemyDamageAccum += Amount;
	}
}

void UFPSRDirectorSensorSubsystem::NotifyPlayerDowned(AFPSRPlayerState* PS)
{
	if (!HasServerAuthority() || PS == nullptr)
	{
		return;
	}
	FFPSRPlayerTelemetry& T = FindOrAddEntry(PS);
	++T.DownedCount;
	T.LastDownedTime = SensorClock;
	T.DownedRecent01 = 1.0f;
}

FFPSRPlayerTelemetry& UFPSRDirectorSensorSubsystem::FindOrAddEntry(AFPSRPlayerState* PS)
{
	return Telemetry.FindOrAdd(TWeakObjectPtr<AFPSRPlayerState>(PS));
}

bool UFPSRDirectorSensorSubsystem::GetPlayerSnapshot(const AFPSRPlayerState* PS, FFPSRPlayerTelemetry& OutSnapshot) const
{
	if (const FFPSRPlayerTelemetry* T = Telemetry.Find(TWeakObjectPtr<AFPSRPlayerState>(const_cast<AFPSRPlayerState*>(PS))))
	{
		OutSnapshot = *T;
		return true;
	}
	return false;
}

// ==================================================================================================
//  Aggregation clock
// ==================================================================================================
void UFPSRDirectorSensorSubsystem::SensorTick()
{
	const AFPSRGameState* GS = GetGS();
	const bool bPaused = GS ? GS->IsRunPaused() : true; // no GameState -> treat as paused (do nothing)
	if (!FPSRTelemetry::ShouldAdvance(HasServerAuthority(), bRunActive, bPaused))
	{
		return; // frozen / not a run / client -> the sensor clock and windows do NOT progress
	}
	Advance(SampleInterval);
}

void UFPSRDirectorSensorSubsystem::Advance(float Dt)
{
	SensorClock += Dt;
	ReconcilePlayers(); // add new players (fresh baseline), prune invalid weak keys (tombstone leak 0)

	for (TPair<TWeakObjectPtr<AFPSRPlayerState>, FFPSRPlayerTelemetry>& Pair : Telemetry)
	{
		if (AFPSRPlayerState* PS = Pair.Key.Get())
		{
			SamplePlayer(PS, Pair.Value, Dt);
		}
	}
}

void UFPSRDirectorSensorSubsystem::ReconcilePlayers()
{
	FPSRTelemetry::PruneInvalidTelemetry(Telemetry);

	AFPSRGameState* GS = GetGS();
	if (GS == nullptr)
	{
		return;
	}
	for (APlayerState* PSBase : GS->PlayerArray)
	{
		AFPSRPlayerState* PS = Cast<AFPSRPlayerState>(PSBase);
		if (PS == nullptr || PS->IsOnlyASpectator())
		{
			continue;
		}
		if (!Telemetry.Contains(TWeakObjectPtr<AFPSRPlayerState>(PS)))
		{
			Telemetry.Add(TWeakObjectPtr<AFPSRPlayerState>(PS), FFPSRPlayerTelemetry());
		}
	}
}

void UFPSRDirectorSensorSubsystem::SamplePlayer(AFPSRPlayerState* PS, FFPSRPlayerTelemetry& T, float Dt)
{
	// FrontId = read-only committed occupancy snapshot (PS->GetCurrentMapId, committed by the director's
	// ComputeOccupancy). We NEVER call ComputeOccupancy from the sensor (it mutates PS + spawn state); reading
	// the already-committed field keeps the sensor's Front CONSISTENT with the actuator's Front.
	T.FrontId = PS->GetCurrentMapId();

	// HealthPct: forced injection override (harness) else the player's ASC HealthSet.
	bool bHasHealth = false;
	float HealthPctIn = 0.0f;
#if !UE_BUILD_SHIPPING
	if (T.ForcedHealthPct >= 0.0f && T.ForcedHealthUntil > SensorClock)
	{
		bHasHealth = true;
		HealthPctIn = T.ForcedHealthPct;
	}
	else
#endif
	if (const UFPSRHealthSet* HS = PS->GetHealthSet())
	{
		const float MaxH = HS->GetMaxHealth();
		if (MaxH > 0.0f)
		{
			bHasHealth = true;
			HealthPctIn = HS->GetHealth() / MaxH;
		}
	}

	// Position sample for confinement (XY only).
	bool bHasSample = false;
	FVector2D SampleXY = FVector2D::ZeroVector;
	if (const APawn* Pawn = PS->GetPawn())
	{
		const FVector Loc = Pawn->GetActorLocation();
		SampleXY = FVector2D(Loc.X, Loc.Y);
		bHasSample = true;
	}

	T.Integrate(Dt, SensorClock, IncomingRateEwmaAlpha, DownedRecentWindow, DefaultConfinementParams(),
		bHasHealth, HealthPctIn, bHasSample, SampleXY);
}

AFPSRPlayerState* UFPSRDirectorSensorSubsystem::ResolvePlayerByIndex(int32 Idx) const
{
	AFPSRGameState* GS = GetGS();
	if (GS == nullptr)
	{
		return nullptr;
	}
	int32 Cursor = 0;
	for (APlayerState* PSBase : GS->PlayerArray)
	{
		AFPSRPlayerState* PS = Cast<AFPSRPlayerState>(PSBase);
		if (PS == nullptr || PS->IsOnlyASpectator())
		{
			continue;
		}
		if (Cursor == Idx)
		{
			return PS;
		}
		++Cursor;
	}
	return nullptr;
}

// ==================================================================================================
//  Injection harness (development builds only)
// ==================================================================================================
#if !UE_BUILD_SHIPPING
void UFPSRDirectorSensorSubsystem::InjectDamageTaken(int32 Idx, float Amount, EFPSRDamageSourceClass Src)
{
	if (!HasServerAuthority() || Amount <= 0.0f)
	{
		return;
	}
	if (!FPSRTelemetry::CountsAsIncoming(Src))
	{
		return; // harness respects the same enemy/boss-only gate as the live hook
	}
	AFPSRPlayerState* PS = ResolvePlayerByIndex(Idx);
	if (PS == nullptr)
	{
		return;
	}
	FFPSRPlayerTelemetry& T = FindOrAddEntry(PS);
	if (Src == EFPSRDamageSourceClass::Boss)
	{
		T.BossDamageAccum += Amount;
	}
	else
	{
		T.EnemyDamageAccum += Amount;
	}
}

void UFPSRDirectorSensorSubsystem::InjectDown(int32 Idx)
{
	if (AFPSRPlayerState* PS = ResolvePlayerByIndex(Idx))
	{
		NotifyPlayerDowned(PS);
	}
}

void UFPSRDirectorSensorSubsystem::SetHealthBand(int32 Idx, float Pct, float Duration)
{
	if (AFPSRPlayerState* PS = ResolvePlayerByIndex(Idx))
	{
		FFPSRPlayerTelemetry& T = FindOrAddEntry(PS);
		T.ForcedHealthPct = FMath::Clamp(Pct, 0.0f, 1.0f);
		T.ForcedHealthUntil = SensorClock + FMath::Max(0.0f, Duration);
	}
}

void UFPSRDirectorSensorSubsystem::SampleMove(int32 Idx, float X, float Y, float Z, float T)
{
	(void)Z; // XY only
	if (AFPSRPlayerState* PS = ResolvePlayerByIndex(Idx))
	{
		FFPSRPlayerTelemetry& Tel = FindOrAddEntry(PS);
		FPSRTelemetry::AdvanceConfinement(Tel.Confinement, FVector2D(X, Y), T, DefaultConfinementParams());
		Tel.MovementConfinement01 = FPSRTelemetry::ConfinementLevel01(Tel.Confinement, T, DefaultConfinementParams());
		Tel.bMovementConfined = Tel.Confinement.bConfined;
	}
}

void UFPSRDirectorSensorSubsystem::SetFront(int32 Idx, FName FrontTagName)
{
	if (AFPSRPlayerState* PS = ResolvePlayerByIndex(Idx))
	{
		// Transient: FrontId is re-derived from the occupancy snapshot every Advance. Harness affordance only.
		FFPSRPlayerTelemetry& T = FindOrAddEntry(PS);
		T.FrontId = FrontTagName.IsNone() ? FGameplayTag() : FGameplayTag::RequestGameplayTag(FrontTagName, /*ErrorIfNotFound=*/false);
	}
}

void UFPSRDirectorSensorSubsystem::DebugLogout(int32 Idx)
{
	if (AFPSRPlayerState* PS = ResolvePlayerByIndex(Idx))
	{
		Telemetry.Remove(TWeakObjectPtr<AFPSRPlayerState>(PS));
	}
}

void UFPSRDirectorSensorSubsystem::DumpSnapshot() const
{
	UE_LOG(LogFPSRTelemetry, Log, TEXT("=== DirectorSensor snapshot (SensorClock=%.2f, players=%d) ==="),
		SensorClock, Telemetry.Num());

	FString Csv = TEXT("Idx,Front,HealthPct,HealthValid,IncEnemyRate,IncBossRate,DownedCount,DownedRecent01,Confine01,Confined\n");
	if (const AFPSRGameState* GS = GetGS())
	{
		int32 Idx = 0;
		for (APlayerState* PSBase : GS->PlayerArray)
		{
			const AFPSRPlayerState* PS = Cast<AFPSRPlayerState>(PSBase);
			if (PS == nullptr || PS->IsOnlyASpectator())
			{
				continue;
			}
			const FFPSRPlayerTelemetry* T = Telemetry.Find(TWeakObjectPtr<AFPSRPlayerState>(const_cast<AFPSRPlayerState*>(PS)));
			if (T != nullptr)
			{
				const FString Row = FString::Printf(
					TEXT("%d,%s,%.3f,%d,%.2f,%.2f,%d,%.3f,%.3f,%d"),
					Idx, *T->FrontId.ToString(), T->HealthPct, T->bHealthValid ? 1 : 0,
					T->IncomingDamageEnemyRateEwma, T->IncomingDamageBossRateEwma,
					T->DownedCount, T->DownedRecent01, T->MovementConfinement01, T->bMovementConfined ? 1 : 0);
				UE_LOG(LogFPSRTelemetry, Log, TEXT("  %s"), *Row);
				Csv += Row + TEXT("\n");
			}
			++Idx;
		}
	}
	const FString Path = FPaths::Combine(FPaths::ProjectDir(), TEXT("logs"), TEXT("TelemetrySnapshot.csv"));
	FFileHelper::SaveStringToFile(Csv, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogFPSRTelemetry, Log, TEXT("  (wrote %s)"), *Path);
}

// ---- Console commands (FPSR.Telemetry.*; excluded from shipping) ----------------------------------
namespace
{
	UFPSRDirectorSensorSubsystem* FPSRTelemetryGetSensor(UWorld* World)
	{
		return World ? World->GetSubsystem<UFPSRDirectorSensorSubsystem>() : nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GFPSRTelemetryDumpCmd(
	TEXT("FPSR.Telemetry.Dump"),
	TEXT("Dump the director-sensor per-player snapshot to the log + logs/TelemetrySnapshot.csv."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UFPSRDirectorSensorSubsystem* S = FPSRTelemetryGetSensor(World)) { S->DumpSnapshot(); }
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRTelemetryInjectDamageCmd(
	TEXT("FPSR.Telemetry.InjectDamage"),
	TEXT("Inject incoming damage. Usage: FPSR.Telemetry.InjectDamage <playerIdx> <amount> <srcClass:1=Enemy,2=Boss>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UFPSRDirectorSensorSubsystem* S = FPSRTelemetryGetSensor(World))
		{
			const int32 Idx = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
			const float Amt = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.0f;
			const int32 SrcInt = Args.Num() > 2 ? FCString::Atoi(*Args[2]) : (int32)EFPSRDamageSourceClass::Enemy;
			if (SrcInt >= 0 && SrcInt < (int32)EFPSRDamageSourceClass::Count)
			{
				S->InjectDamageTaken(Idx, Amt, (EFPSRDamageSourceClass)SrcInt);
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRTelemetryInjectDownCmd(
	TEXT("FPSR.Telemetry.InjectDown"),
	TEXT("Stamp a down for a player. Usage: FPSR.Telemetry.InjectDown <playerIdx>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UFPSRDirectorSensorSubsystem* S = FPSRTelemetryGetSensor(World))
		{
			S->InjectDown(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRTelemetrySetHealthCmd(
	TEXT("FPSR.Telemetry.SetHealth"),
	TEXT("Force HealthPct for a duration. Usage: FPSR.Telemetry.SetHealth <playerIdx> <pct0..1> <durationSec>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UFPSRDirectorSensorSubsystem* S = FPSRTelemetryGetSensor(World))
		{
			const int32 Idx = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
			const float Pct = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 1.0f;
			const float Dur = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 5.0f;
			S->SetHealthBand(Idx, Pct, Dur);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRTelemetrySampleMoveCmd(
	TEXT("FPSR.Telemetry.SampleMove"),
	TEXT("Feed a confinement position sample. Usage: FPSR.Telemetry.SampleMove <playerIdx> <x> <y> <z> <t>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UFPSRDirectorSensorSubsystem* S = FPSRTelemetryGetSensor(World))
		{
			const int32 Idx = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
			const float X = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.0f;
			const float Y = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.0f;
			const float Z = Args.Num() > 3 ? FCString::Atof(*Args[3]) : 0.0f;
			const float T = Args.Num() > 4 ? FCString::Atof(*Args[4]) : 0.0f;
			S->SampleMove(Idx, X, Y, Z, T);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRTelemetrySetFrontCmd(
	TEXT("FPSR.Telemetry.SetFront"),
	TEXT("Set a player's FrontId tag (transient; re-derived each Advance). Usage: FPSR.Telemetry.SetFront <playerIdx> <TagName>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UFPSRDirectorSensorSubsystem* S = FPSRTelemetryGetSensor(World))
		{
			const int32 Idx = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
			const FName Tag = Args.Num() > 1 ? FName(*Args[1]) : NAME_None;
			S->SetFront(Idx, Tag);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRTelemetryAdvanceCmd(
	TEXT("FPSR.Telemetry.Advance"),
	TEXT("Advance the sensor by t seconds (deterministic; bypasses the timer). Usage: FPSR.Telemetry.Advance <t>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UFPSRDirectorSensorSubsystem* S = FPSRTelemetryGetSensor(World))
		{
			S->Advance(Args.Num() > 0 ? FCString::Atof(*Args[0]) : UFPSRDirectorSensorSubsystem::SampleInterval);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRTelemetryStartRunCmd(
	TEXT("FPSR.Telemetry.StartRun"),
	TEXT("(debug) Reset + arm the director sensor."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UFPSRDirectorSensorSubsystem* S = FPSRTelemetryGetSensor(World)) { S->StartRun(); }
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRTelemetryEndRunCmd(
	TEXT("FPSR.Telemetry.EndRun"),
	TEXT("(debug) Clear + disarm the director sensor."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (UFPSRDirectorSensorSubsystem* S = FPSRTelemetryGetSensor(World)) { S->EndRun(); }
	}));

static FAutoConsoleCommandWithWorldAndArgs GFPSRTelemetryLogoutCmd(
	TEXT("FPSR.Telemetry.Logout"),
	TEXT("(debug) Drop a player's telemetry. Usage: FPSR.Telemetry.Logout <playerIdx>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (UFPSRDirectorSensorSubsystem* S = FPSRTelemetryGetSensor(World))
		{
			S->DebugLogout(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0);
		}
	}));
#endif // !UE_BUILD_SHIPPING
