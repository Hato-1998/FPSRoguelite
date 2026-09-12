// Copyright Epic Games, Inc. All Rights Reserved.

// GASM1(Docs/Specs/GASM1_SwarmASCCostMeasurement.md §5-A·§12-B) — FPSR.Debug.ExecAfter <Seconds> <Command...>.
// 러너(Scripts/measure_swarm_render.ps1)의 -ExecCmds 시퀀스가 "N초 뒤에 이 콘솔 명령을 실행"을 표현할
// 방법이 없어서 만든 시임: §12-A 의 러너 종료 순서(ExecAfter <t> CsvProfile Stop 을 먼저 예약해 캡처를
// 시간 기준으로 닫은 뒤 ASCDump → obj list → memreport -full)가 이 명령 하나로 표현된다.
//
// 클래스/헤더 없음 — 이 모듈에 이미 있는 "콘솔 명령 전용 static" 관용구(FPSREnemySpawnSubsystem.cpp 의
// GFPSREliteDumpCmd, FPSRPlayerController.cpp:921/980 의 GCmd_SkipCards/GCmd_Invuln)를 그대로 따르되, 이
// 명령은 어느 기존 클래스에도 속하지 않는 범용 유틸이라 독립 .cpp 로 둔다(신규 클래스 불필요 — UBT 는
// 헤더 유무와 무관하게 모듈의 모든 .cpp 를 컴파일한다).

#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "Core/FPSRLogChannels.h"

// 이 파일 전체를 #if !UE_BUILD_SHIPPING 로 가드한다 — 이 명령 자체는 어떤 GASM1 #if !UE_BUILD_SHIPPING
// 심볼도 참조하지 않아 기술적으로는 필요 없지만, 이 모듈의 모든 디버그 콘솔 명령이 예외 없이 이 관용구를
// 쓴다(FPSREnemySpawnSubsystem.cpp:2151-2355 의 FPSR.EliteDump/SpawnEnemies/EnemyTarget,
// FPSRPlayerController.cpp:647-1014 의 FPSR.SkipCards/FPSR.Invuln 전부 포함) — 일관성을 따른다.
#if !UE_BUILD_SHIPPING

namespace
{
	// 🔴 §12-B 필수 제약: 호출마다 "새" FTimerHandle. 하나를 재사용하면(정적 핸들 1개에 거듭 SetTimer) 앞서
	// 예약해 둔 타이머가 덮여 사라진다 — §12-A 의 러너 종료 순서는 서로 다른 지연시간으로 ExecAfter 를
	// 여러 번(CsvProfile Stop 용 1회 + 그 뒤 ASCDump/obj list/memreport 체인 용으로 더) 동시에 걸어 두는
	// 시나리오라 실제로 겹친다. TArray 에 매 호출 AddDefaulted_GetRef 로 항상 새 원소를 만들어 그 참조에
	// SetTimer 를 건다.
	//
	// FPSRPlayerController.cpp:911-1011(SkipCards/Invuln) 가 같은 모양의 선례지만, 그 주석이 근거로 든 "월드가
	// 타이머를 소유하므로 월드 테어다운이 자동으로 정리한다"는 **틀렸다** — 월드는 게임인스턴스의 타이머
	// 매니저를 돌려준다(engine World.cpp:8056). 그래서 여기서는 델리게이트를 월드에 약하게 묶는다(아래
	// CreateWeakLambda 주석). 그 선례 두 곳도 이제 같은 방식으로 고쳤고 틀린 전제 주석도 함께 정정했다.
	// 또 그 둘은 반복 재사용 가능한 "반복" 타이머 하나만 필요해 정적 핸들 1개를 재사용하는 반면, 이 명령은
	// 서로 겹칠 수 있는 "1회성" 예약을 여러 개 동시에 지원해야 하므로 컨테이너로 늘린다.
	TArray<FTimerHandle> GFPSRDebugExecAfterTimers;
}

static FAutoConsoleCommandWithWorldAndArgs GFPSRDebugExecAfterCmd(
	TEXT("FPSR.Debug.ExecAfter"),
	TEXT("GASM1 §12-B: <Seconds> 뒤에 <Command...> 를 GEngine->Exec 로 실행한다 — FPSR.*, obj, memreport 등 ")
	TEXT("콘솔 매니저가 처리하는 어떤 명령이든 가능하다(UnrealEngine.cpp:5722 경유). 예약되는 명령 문자열에 ")
	TEXT("쉼표(,)를 넣지 말 것 — -ExecCmds 자체가 쉼표로 명령을 나누므로(ParseExecCommands.cpp) 그 안에 ")
	TEXT("쉼표가 있으면 -ExecCmds 파싱 단계에서 먼저 잘린다. ")
	TEXT("Usage: FPSR.Debug.ExecAfter <Seconds> <Command...>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2)
		{
			UE_LOG(LogFPSR, Warning,
				TEXT("[GASM1] FPSR.Debug.ExecAfter requires 2+ args: <Seconds> <Command...>. Got %d — no-op."),
				Args.Num());
			return;
		}

		const float Seconds = FCString::Atof(*Args[0]);

		// Args[1..] 를 공백으로 다시 이어붙인다 — 콘솔 명령 프레임워크가 이미 공백으로 토큰화해 넘겨준 것을
		// 되돌리는 것뿐이라, 원래 명령이 인자를 여러 개 가졌어도(예: "FPSR.SpawnEnemies 300 6000") 그대로
		// 복원된다.
		FString Cmd = Args[1];
		for (int32 i = 2; i < Args.Num(); ++i)
		{
			Cmd += TEXT(" ");
			Cmd += Args[i];
		}

		// CreateWeakLambda(World, ...), not CreateLambda: UWorld::GetTimerManager() hands back the OWNING GAME
		// INSTANCE's timer manager, not one the world owns (engine World.cpp:8056 —
		// `return (OwningGameInstance ? OwningGameInstance->GetTimerManager() : *TimerManager)`), and world teardown
		// does not clear it. A pending schedule therefore SURVIVES a map travel, and firing it would Exec against a
		// dead UWorld — several FPSR.* handlers dereference that pointer immediately (FPSR.Debug.ASCDump's
		// World->GetSubsystem, FPSR.SkipCards' World->GetGameState). Binding weakly to the world makes the timer
		// invalidate itself instead; it is the same guard the engine uses for its own world-captured timers
		// (World.cpp:5831).
		FTimerHandle& Handle = GFPSRDebugExecAfterTimers.AddDefaulted_GetRef();
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(World, [World, Cmd]()
		{
			if (GEngine)
			{
				GEngine->Exec(World, *Cmd);
			}
		}), FMath::Max(0.0f, Seconds), /*bLoop=*/false);

		UE_LOG(LogFPSR, Log, TEXT("[GASM1] FPSR.Debug.ExecAfter: scheduled '%s' in %.2fs."), *Cmd, Seconds);
	}));

#endif // !UE_BUILD_SHIPPING
