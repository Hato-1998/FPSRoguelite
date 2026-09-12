// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h" // FTestWorldWrapper — same idiom as FPSRStatusClockTest.cpp (A단계 precedent)
#include "HAL/IConsoleManager.h" // GASM1 측정 CVar 5종 리셋 (RunTest 시작부)
#include "Enemy/FPSREnemyBase.h"
#include "Enemy/FPSREnemyHealthComponent.h"
#include "Enemy/FPSREnemySpawnSubsystem.h"
#include "Status/FPSRStatusTypes.h"
#include "Settings/FPSRStatusEffectSettings.h"
#include "Core/FPSRGameState.h"
#include "Engine/World.h"

#if WITH_AUTOMATION_TESTS

// STAT1 §10 C2단계 — world items 11 / 13 / 14 (12 / 12-b are the A단계 status CLOCK's own scope, already covered by
// FPSRStatusClockTest.cpp — they test the clock's freeze axis, not this phase's driver wiring). Follows that file's
// FTestWorldWrapper idiom (its own header comment is the in-repo precedent for why a world is unavoidable here, and
// for deliberately SKIPPING WorldWrapper.BeginPlayInTestWorld() — see it for the full rationale, which applies
// unchanged: AFPSREnemyBase's heavier BeginPlay chain — health-bar widget bind, cosmetic LOD / metrics registry —
// never needs to run for what these three items actually check, and skipping it is what keeps this file from
// depending on content settings that don't exist in a bare test world).
//
// This file additionally spawns AFPSREnemyBase actors (a first — the Clock test only ever spawns AFPSRGameState)
// and, for item 14, drives UFPSREnemySpawnSubsystem::Tick directly rather than relying on the engine's automatic
// FTickableGameObject dispatch — the subsystem exposes Tick as a public FTickableGameObject override specifically
// callable this way, which keeps the test deterministic regardless of whether a bare EWorldType::Game test world
// actually pumps FTickableGameObject::TickObjects on its own.
//
// 🔴 World->SetGameState(GS) is NOT optional here (unlike FPSRStatusClockTest.cpp, which never needs it): that file
// calls GetStatusClockSeconds() DIRECTLY on the actor it spawned, but UFPSREnemyHealthComponent/
// UFPSREnemySpawnSubsystem resolve the clock via World->GetGameState<AFPSRGameState>() — merely SpawnActor'ing an
// AFPSRGameState does NOT register it as the world's GameState (that normally happens in AGameModeBase::InitGame,
// which never runs here). Without this, GetStatusClockNow always reads 0.0 and SlotExpiry can never be exceeded —
// a silent false-pass/false-fail trap this file avoids by registering the spawned GameState explicitly.

namespace
{
	UFPSRStatusEffectDataAsset* MakeStatus(uint8 Slot, EFPSRStatusKind Kind, float Duration)
	{
		UFPSRStatusEffectDataAsset* Status = NewObject<UFPSRStatusEffectDataAsset>();
		Status->SlotIndex = Slot;
		Status->Kind = Kind;
		Status->DurationSeconds = Duration;
		return Status;
	}

	UFPSRStatusCatalogDataAsset* MakeCatalog(const TArray<UFPSRStatusEffectDataAsset*>& Statuses)
	{
		UFPSRStatusCatalogDataAsset* Catalog = NewObject<UFPSRStatusCatalogDataAsset>();
		for (UFPSRStatusEffectDataAsset* Status : Statuses)
		{
			Catalog->Statuses.Add(Status);
		}
		return Catalog;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRStatusWorldTest, "FPSRoguelite.Status.World",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRStatusWorldTest::RunTest(const FString& Parameters)
{
	// GASM1(Docs/Specs/GASM1_SwarmASCCostMeasurement.md §4·§12 항목 6 "기본 경로 무변경") — 이 테스트는
	// AFPSREnemyBase 를 직접 스폰하고 AcquireEnemy 도 부르므로(아래 항목 14), 어떤 이전 세션/콘솔 조작이
	// 측정 CVar 5종을 켜 둔 채로 남아 있으면 조용히 오염된다(강제 스폰 클래스가 로스터 가중추첨을
	// 우회하거나, 측정 ASC 가 붙어 이 테스트가 가정하는 "기본 경로" 자체가 바뀌는 식). CVar 는 자동화
	// 테스트 경계에서 자동으로 리셋되지 않고(정적 파일-로컬 TAutoConsoleVariable — 이름으로만 접근 가능),
	// 이 파일은 그 CVar 들을 선언한 FPSREnemyBase.cpp/FPSREnemySpawnSubsystem.cpp 를 include 하지 않으므로
	// IConsoleManager 로 이름을 찾아 기본값으로 되돌린다.
	//
	// 🔁 정정(G2 P2-2) — Set() 의 두 번째 인자는 반드시 ECVF_SetByConsole 이어야 한다. 이전 코드는
	// ECVF_SetByCode(=0x0E000000, IConsoleManager.h:183)를 썼는데, IConsoleVariable::CanChange() 는
	// `NewPri >= OldPri`(ConsoleManager.cpp:275-281)일 때만 값을 바꾼다 — ECVF_SetByCode 는
	// ECVF_SetByConsole(=0x10000000, :187)보다 우선순위가 낮아, 정확히 이 리셋이 노리는 경우(누군가
	// 콘솔에서 직접 켜 둔 값)를 못 되돌리고 "낮은 우선순위" 경고만 찍고 무시된다. ECVF_SetByConsole 로
	// 세팅하면 동일 우선순위(NewPri == OldPri)라 항상 통과한다.
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("FPSR.Debug.ForceSpawnClass")))
	{
		CVar->Set(TEXT(""), ECVF_SetByConsole);
	}
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("FPSR.Debug.AttachASC")))
	{
		CVar->Set(TEXT("0"), ECVF_SetByConsole);
	}
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("FPSR.Debug.MeasureLoadout")))
	{
		CVar->Set(TEXT("0"), ECVF_SetByConsole);
	}
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("FPSR.Debug.MeasureCadence")))
	{
		CVar->Set(TEXT("1.0"), ECVF_SetByConsole);
	}
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("FPSR.Debug.MeasureTickable")))
	{
		CVar->Set(TEXT("0"), ECVF_SetByConsole);
	}

	FTestWorldWrapper WorldWrapper;
	if (!WorldWrapper.CreateTestWorld(EWorldType::Game))
	{
		AddError(TEXT("CreateTestWorld(EWorldType::Game) failed — cannot exercise enemy/subsystem status wiring"));
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!World)
	{
		AddError(TEXT("GetTestWorld() returned null after a successful CreateTestWorld"));
		return false;
	}

	AFPSRGameState* GS = World->SpawnActor<AFPSRGameState>();
	if (!GS)
	{
		AddError(TEXT("SpawnActor<AFPSRGameState> failed"));
		return false;
	}
	// See this file's header comment — without this, every status-clock read below is a frozen 0.0.
	World->SetGameState(GS);

	// --- 11. ResetForReuse 후 StatusBits == 0 (풀 재사용 누수) ------------------------------------------------------
	{
		AFPSREnemyBase* Enemy = World->SpawnActor<AFPSREnemyBase>();
		if (!Enemy || !Enemy->GetHealthComponent())
		{
			AddError(TEXT("11. SpawnActor<AFPSREnemyBase> (or its HealthComponent) failed"));
			return false;
		}
		UFPSREnemyHealthComponent* Health = Enemy->GetHealthComponent();
		// §5-6 gate — AcquireEnemy's own wiring (ActiveEnemies.Add) is exercised separately by item 14 below; this
		// item only cares about the ResetForReuse/ClearStatusForReuse closure, so the flag is set by hand.
		Health->SetStatusDriverPresent(true);

		UFPSRStatusEffectDataAsset* Weak0 = MakeStatus(0, EFPSRStatusKind::Weak, 5.0f);
		UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Weak0 });

		TArray<uint8, TInlineAllocator<8>> Fired;
		const bool bApplied = Health->ApplyStatus(Catalog, 0, 1.0f, 1.0f, nullptr, nullptr, Fired);
		TestTrue(TEXT("11. ApplyStatus succeeds with the driver present"), bApplied);
		TestTrue(TEXT("11. slot 0 set before reuse"), Health->HasStatus(0));

		// The REAL pool-reuse trigger (AFPSREnemyBase::Activate is ResetForReuse's one call site, per that
		// function's own B단계 header comment) — not a direct HealthComponent call — so this exercises the actual
		// wiring, not just the component in isolation.
		Enemy->Activate(FVector::ZeroVector);

		TestFalse(TEXT("11. slot 0 clear after Activate()/ResetForReuse()"), Health->HasStatus(0));
	}

	// --- 13. 드라이버 없는 액터(문)에 부여가 거부되는가 (§5-6) -------------------------------------------------------
	{
		AFPSREnemyBase* Enemy = World->SpawnActor<AFPSREnemyBase>();
		if (!Enemy || !Enemy->GetHealthComponent())
		{
			AddError(TEXT("13. SpawnActor<AFPSREnemyBase> (or its HealthComponent) failed"));
			return false;
		}
		UFPSREnemyHealthComponent* Health = Enemy->GetHealthComponent();
		// Deliberately do NOT call SetStatusDriverPresent — a freshly constructed actor's default (false, §5-6) is
		// exactly the "door / mission-flee-target" case this item tests.

		UFPSRStatusEffectDataAsset* Weak0 = MakeStatus(0, EFPSRStatusKind::Weak, 5.0f);
		UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Weak0 });

		TArray<uint8, TInlineAllocator<8>> Fired;
		const bool bApplied = Health->ApplyStatus(Catalog, 0, 1.0f, 1.0f, nullptr, nullptr, Fired);
		TestFalse(TEXT("13. ApplyStatus rejected with no driver present"), bApplied);
		TestFalse(TEXT("13. slot 0 never set"), Health->HasStatus(0));
	}

	// --- 14. 전원 DBNO 구간(PlayerPawns 비었음)을 끼워도 상태 진행이 멈추지 않는가 (§6 삽입 위치) ---------------------
	{
		// A bare test world spawns NO PlayerControllers/pawns at all — TickEnemyMovement's PlayerPawns collection is
		// naturally empty here, which IS the "전원 DBNO" condition this item exercises, with no need to fake a
		// downed-player state.
		UFPSREnemySpawnSubsystem* Subsystem = World->GetSubsystem<UFPSREnemySpawnSubsystem>();
		if (!Subsystem)
		{
			AddError(TEXT("14. World->GetSubsystem<UFPSREnemySpawnSubsystem>() returned null"));
			return false;
		}

		// bSnapToGround=false: an authoritative Location straight through, no floor trace needed (no static
		// geometry exists in this bare test world) — AcquireEnemy's own doc comment for this parameter.
		AFPSREnemyBase* Enemy = Subsystem->AcquireEnemy(FVector::ZeroVector, /*bSnapToGround=*/false);
		if (!Enemy || !Enemy->GetHealthComponent())
		{
			AddError(TEXT("14. AcquireEnemy (or its HealthComponent) failed"));
			return false;
		}
		UFPSREnemyHealthComponent* Health = Enemy->GetHealthComponent();

		// A short duration so a handful of world ticks definitely carries it past expiry. AcquireEnemy already
		// wired SetStatusDriverPresent(true) at its ActiveEnemies.Add (this phase's own change, exercised here for
		// real rather than by hand).
		UFPSRStatusEffectDataAsset* Weak0 = MakeStatus(0, EFPSRStatusKind::Weak, 0.3f);
		UFPSRStatusCatalogDataAsset* Catalog = MakeCatalog({ Weak0 });

		TArray<uint8, TInlineAllocator<8>> Fired;
		const bool bApplied = Health->ApplyStatus(Catalog, 0, 1.0f, 1.0f, nullptr, nullptr, Fired);
		TestTrue(TEXT("14. ApplyStatus succeeds (AcquireEnemy wired the driver)"), bApplied);
		Subsystem->RegisterStatusActive(Enemy); // the real call site is UFPSRStatusApplyFragment::OnDamageApplied

		// Swap the settings' catalog for OURS so AdvanceStatusEffects' UFPSRStatusEffectSettings::ResolveCatalog()
		// resolves the SAME in-memory asset ApplyStatus above used, without touching DefaultGame.ini. Restored
		// unconditionally below — CDO mutation is scoped to this automation process (FPSREnemyDormantPoolTest.cpp
		// is this codebase's own precedent for touching a mutable CDO from a test).
		UFPSRStatusEffectSettings* Settings = GetMutableDefault<UFPSRStatusEffectSettings>();
		const TSoftObjectPtr<UFPSRStatusCatalogDataAsset> PreviousCatalog = Settings->StatusCatalog;
		Settings->StatusCatalog = Catalog;

		// Tick past the 0.3s duration in small slices (World->Tick clamps a single call's DeltaSeconds to
		// AWorldSettings::MaxUndilatedFrameTime, the same reason FPSRStatusClockTest.cpp's TickStatusTestWorld
		// steps rather than ticking 8s in one call). Tick() is called DIRECTLY on the subsystem (a public
		// FTickableGameObject override) rather than relied on via the engine's automatic tickable-object dispatch,
		// so this is deterministic regardless of whether a bare EWorldType::Game test world actually pumps
		// FTickableGameObject::TickObjects.
		for (int32 i = 0; i < 5; ++i)
		{
			WorldWrapper.TickTestWorld(0.1f);
			Subsystem->Tick(0.1f);
		}

		Settings->StatusCatalog = PreviousCatalog; // restore — this CDO persists across automation tests in-process

		TestFalse(TEXT("14. status expired even with zero players in the world (PlayerPawns empty)"), Health->HasStatus(0));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
