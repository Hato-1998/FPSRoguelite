// Copyright Epic Games, Inc. All Rights Reserved.

#include "Messages/FPSRGameplayMessageSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Messages/FPSRCosmeticMessages.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPSRMessage, Log, All);

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

	// Walk from the broadcast channel up its parent chain. On the initial (exact) tag, invoke all listeners;
	// on ancestor tags, invoke only PartialMatch listeners.
	bool bOnInitialTag = true;
	for (FGameplayTag Tag = Channel; Tag.IsValid(); Tag = Tag.RequestDirectParent())
	{
		if (const FChannelListenerList* List = ListenerMap.Find(Tag))
		{
			// Copy the listener array so a listener that unregisters (itself or others) mid-broadcast is safe.
			TArray<FFPSRMessageListenerData> ListenerArray(List->Listeners);
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
		ensureMsgf(Handle.Subsystem == this, TEXT("Unregistering a GMS listener handle on the wrong subsystem."));
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
static FAutoConsoleCommandWithWorld GFPSRGMSDemoCmd(
	TEXT("FPSR.GMS.Demo"),
	TEXT("U8 GMS demo: register a temp logging listener on GameplayEvent.EnemyKilled and broadcast one cosmetic event."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		UFPSRGameplayMessageSubsystem* GMS = UFPSRGameplayMessageSubsystem::Get(World);
		if (!GMS)
		{
			UE_LOG(LogFPSRMessage, Warning, TEXT("FPSR.GMS.Demo: no GMS for world."));
			return;
		}

		const FGameplayTag Channel = FGameplayTag::RequestGameplayTag(FName("GameplayEvent.EnemyKilled"));

		FFPSRMessageListenerHandle Handle = GMS->RegisterListener<FFPSRCosmeticEventMessage>(Channel,
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

		Handle.Unregister();
		UE_LOG(LogFPSRMessage, Log, TEXT("[FPSR.GMS.Demo] broadcast complete on '%s' (real enemy-death publish = U13 seam)."), *Channel.ToString());
	}));
#endif // !UE_BUILD_SHIPPING
