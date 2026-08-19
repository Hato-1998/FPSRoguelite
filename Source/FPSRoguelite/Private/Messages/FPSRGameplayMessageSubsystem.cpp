// Copyright Epic Games, Inc. All Rights Reserved.

#include "Messages/FPSRGameplayMessageSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Messages/FPSRCosmeticMessages.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPSRMessage, Log, All);

#if CSV_PROFILER_STATS
CSV_DEFINE_CATEGORY(FPSRMsg, true);
#endif

UFPSRGameplayMessageSubsystem* UFPSRGameplayMessageSubsystem::Get(const UObject* WorldContextObject)
{
	if (const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr)
	{
		return UWorld::GetSubsystem<UFPSRGameplayMessageSubsystem>(World);
	}
	return nullptr;
}

void UFPSRGameplayMessageSubsystem::BroadcastMessageInternal(FGameplayTag Channel, const UScriptStruct* StructType, const void* MessageBytes)
{
	// Zero-cost fast path: no listeners registered anywhere -> nothing to do (hot path: enemy death x hundreds/frame).
	if (ListenerMap.IsEmpty())
	{
		return;
	}

#if CSV_PROFILER_STATS
	CSV_SCOPED_TIMING_STAT(FPSRMsg, GMSBroadcast);
	CSV_CUSTOM_STAT(FPSRMsg, Broadcasts, 1, ECsvCustomStatOp::Accumulate);
#endif

	// Walk from the broadcast channel up its parent chain. On the initial (exact) tag, invoke all listeners;
	// on ancestor tags, invoke only PartialMatch listeners.
	bool bOnInitialTag = true;
	for (FGameplayTag Tag = Channel; Tag.IsValid(); Tag = Tag.RequestDirectParent())
	{
		if (const FChannelListenerList* List = ListenerMap.Find(Tag))
		{
			// Copy the listener array so a listener that unregisters (itself or others) mid-broadcast is safe.
			TArray<FFPSRMessageListenerData> ListenerArray(List->Listeners);
#if CSV_PROFILER_STATS
			CSV_CUSTOM_STAT(FPSRMsg, ListenersCopied, ListenerArray.Num(), ECsvCustomStatOp::Accumulate);
#endif
			for (const FFPSRMessageListenerData& Listener : ListenerArray)
			{
				if (bOnInitialTag || (Listener.MatchType == EFPSRMessageMatch::PartialMatch))
				{
					if (!ensureMsgf(StructType && Listener.StructType && StructType->IsChildOf(Listener.StructType),
						TEXT("GMS broadcast on '%s' with payload '%s' but a listener expects '%s' — mismatched types, skipping."),
						*Channel.ToString(), *GetNameSafe(StructType), *GetNameSafe(Listener.StructType)))
					{
						continue;
					}

					Listener.ReceivedCallback(Channel, StructType, MessageBytes);
#if !UE_BUILD_SHIPPING
					++TotalDispatchCount;
#endif
#if CSV_PROFILER_STATS
					CSV_CUSTOM_STAT(FPSRMsg, Dispatches, 1, ECsvCustomStatOp::Accumulate);
#endif
				}
			}
		}
		bOnInitialTag = false;
	}
}

FFPSRMessageListenerHandle UFPSRGameplayMessageSubsystem::RegisterListenerInternal(FGameplayTag Channel, TFunction<void(FGameplayTag, const UScriptStruct*, const void*)>&& Callback, const UScriptStruct* StructType, EFPSRMessageMatch MatchType)
{
	FChannelListenerList& List = ListenerMap.FindOrAdd(Channel);

	FFPSRMessageListenerData& Entry = List.Listeners.AddDefaulted_GetRef();
	Entry.ReceivedCallback = MoveTemp(Callback);
	Entry.StructType = StructType;
	Entry.HandleID = ++NextListenerHandleID;
	Entry.MatchType = MatchType;

	return FFPSRMessageListenerHandle(this, Channel, Entry.HandleID);
}

void UFPSRGameplayMessageSubsystem::UnregisterListener(FFPSRMessageListenerHandle Handle)
{
	if (Handle.IsValid())
	{
		// Guard against a handle from a DIFFERENT world's GMS (multi-world PIE / listen-server share a process;
		// per-subsystem IDs both start at 1, so a foreign handle could collide with a valid local listener).
		if (!ensureMsgf(Handle.Subsystem == this, TEXT("Unregistering a GMS listener handle on the wrong subsystem.")))
		{
			return;
		}
		UnregisterListenerInternal(Handle.Channel, Handle.ID);
	}
}

void UFPSRGameplayMessageSubsystem::UnregisterListenerInternal(FGameplayTag Channel, int32 HandleID)
{
	if (FChannelListenerList* List = ListenerMap.Find(Channel))
	{
		List->Listeners.RemoveAllSwap([HandleID](const FFPSRMessageListenerData& Data) { return Data.HandleID == HandleID; });

		// Drop the channel entry entirely when empty so the IsEmpty() fast path stays valid.
		if (List->Listeners.Num() == 0)
		{
			ListenerMap.Remove(Channel);
		}
	}
}

void FFPSRMessageListenerHandle::Unregister()
{
	if (UFPSRGameplayMessageSubsystem* StrongSubsystem = Subsystem.Get())
	{
		StrongSubsystem->UnregisterListenerInternal(Channel, ID);
	}
	Subsystem.Reset();
	Channel = FGameplayTag();
	ID = 0;
}

#if !UE_BUILD_SHIPPING
// U8 demo (verification-only, §6-2): register a temp logging listener and broadcast one cosmetic event.
// NOTE: the REAL enemy-death publish (NotifyKill / HealthComponent death) is U13's wiring seam — NOT wired here.
// Debug fixture, not production lifecycle (§5-C(1)(d)): the world owns the timer, so a world teardown kills it with
// the world, and the next FPSR.GMS.Demo repeat call's SetTimer simply reuses this handle (mirrors Invuln §5-C(2)).
static FTimerHandle GFPSRGMSDemoTimer;

// Active listener from the most recent FPSR.GMS.Demo call (§5-C(1)(d) red-team P2-3): kept file-scope static —
// not a local captured by the timer lambda — so a re-run mid-repeat can Unregister it before SetTimer silently
// destroys the previous timer's lambda unrun (which would otherwise leak the listener: unregistered-but-live
// entries keep inflating Dispatches/ListenersCopied on every subsequent broadcast).
static FFPSRMessageListenerHandle GFPSRGMSDemoHandle;

static FAutoConsoleCommandWithWorldAndArgs GFPSRGMSDemoCmd(
	TEXT("FPSR.GMS.Demo"),
	TEXT("U8 GMS demo: register a temp logging listener on GameplayEvent.EnemyKilled and broadcast one cosmetic event. "
	     "RepeatSeconds>0 keeps the listener alive and re-broadcasts every 1s until it elapses (CSV capture "
	     "non-zero verification, §5-C(1)(d)). Usage: FPSR.GMS.Demo [RepeatSeconds=0]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		UFPSRGameplayMessageSubsystem* GMS = UFPSRGameplayMessageSubsystem::Get(World);
		if (!GMS)
		{
			UE_LOG(LogFPSRMessage, Warning, TEXT("FPSR.GMS.Demo: no GMS for world."));
			return;
		}

		// Stale-listener cleanup (§5-C(1)(d) red-team P2-3): common to both modes — clear any timer + listener left
		// behind by an in-flight repeat from a previous call before registering the new one.
		World->GetTimerManager().ClearTimer(GFPSRGMSDemoTimer);
		GFPSRGMSDemoHandle.Unregister();

		const FGameplayTag Channel = FGameplayTag::RequestGameplayTag(FName("GameplayEvent.EnemyKilled"));

		GFPSRGMSDemoHandle = GMS->RegisterListener<FFPSRCosmeticEventMessage>(Channel,
			[](FGameplayTag InChannel, const FFPSRCosmeticEventMessage& Msg)
			{
				UE_LOG(LogFPSRMessage, Log, TEXT("[FPSR.GMS.Demo] received on '%s': Loc=%s Kill=%d Team=%u"),
					*InChannel.ToString(), *Msg.WorldLocation.ToString(), Msg.bWasKill ? 1 : 0, (uint32)Msg.InstigatorTeam);
			});

		FFPSRCosmeticEventMessage Msg;
		Msg.WorldLocation = FVector(1.f, 2.f, 3.f);
		Msg.bWasKill = true;
		Msg.InstigatorTeam = 1;
		GMS->BroadcastMessage(Channel, Msg);

		const float RepeatSeconds = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.0f;
		if (RepeatSeconds <= 0.0f)
		{
			GFPSRGMSDemoHandle.Unregister();
			UE_LOG(LogFPSRMessage, Log, TEXT("[FPSR.GMS.Demo] broadcast complete on '%s' (real enemy-death publish = U13 seam)."), *Channel.ToString());
			return;
		}

		// Repeat mode (§5-C(1)(d)): a single ExecCmds-time broadcast finishes before the capture's first frame
		// boundary, so CsvProfiler can never show it as non-zero. Keep the listener alive and re-broadcast every 1s
		// via a world timer until RepeatSeconds elapses, so several in-capture frames carry the FPSRMsg/* stats.
		World->GetTimerManager().SetTimer(GFPSRGMSDemoTimer, FTimerDelegate::CreateLambda(
			[World, GMS, Channel, Msg, ExpiryTime = World->GetTimeSeconds() + RepeatSeconds]()
			{
				if (World->GetTimeSeconds() >= ExpiryTime)
				{
					World->GetTimerManager().ClearTimer(GFPSRGMSDemoTimer);
					GFPSRGMSDemoHandle.Unregister();
					UE_LOG(LogFPSRMessage, Log, TEXT("[FPSR.GMS.Demo] repeat complete on '%s' (real enemy-death publish = U13 seam)."), *Channel.ToString());
					return;
				}
				GMS->BroadcastMessage(Channel, Msg);
			}), 1.0f, true);
	}));
#endif // !UE_BUILD_SHIPPING
