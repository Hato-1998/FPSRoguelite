// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "FPSRGameplayMessageSubsystem.generated.h"

class UFPSRGameplayMessageSubsystem;

/** How a registered listener's channel is matched against a broadcast channel. */
UENUM(BlueprintType)
enum class EFPSRMessageMatch : uint8
{
	/** Only broadcasts on the exact registered channel reach this listener. */
	ExactMatch,
	/** Broadcasts on the registered channel OR any of its descendant tags reach this listener. */
	PartialMatch
};

/**
 * Opaque handle to a registered GMS listener. Value type, cheap to copy.
 * Call Unregister() (or pass to UFPSRGameplayMessageSubsystem::UnregisterListener) to stop listening.
 */
USTRUCT(BlueprintType)
struct FPSROGUELITE_API FFPSRMessageListenerHandle
{
	GENERATED_BODY()

	FFPSRMessageListenerHandle() {}

	/** Remove this listener from its subsystem (safe to call once; no-op if already unregistered). */
	void Unregister();

	bool IsValid() const { return ID != 0; }

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UFPSRGameplayMessageSubsystem> Subsystem;

	UPROPERTY(Transient)
	FGameplayTag Channel;

	UPROPERTY(Transient)
	int32 ID = 0;

	friend UFPSRGameplayMessageSubsystem;

	FFPSRMessageListenerHandle(UFPSRGameplayMessageSubsystem* InSubsystem, FGameplayTag InChannel, int32 InID)
		: Subsystem(InSubsystem), Channel(InChannel), ID(InID) {}
};

/** Internal per-listener record (untyped callback + expected struct type). Not reflected. */
struct FFPSRMessageListenerData
{
	/** Untyped receiver: (BroadcastChannel, PayloadStructType, PayloadBytes). */
	TFunction<void(FGameplayTag, const UScriptStruct*, const void*)> ReceivedCallback;

	const UScriptStruct* StructType = nullptr;

	int32 HandleID = 0;

	EFPSRMessageMatch MatchType = EFPSRMessageMatch::ExactMatch;
};

/**
 * UFPSRGameplayMessageSubsystem — lightweight, PURELY LOCAL, synchronous GameplayTag-channel pub/sub bus.
 * Lyra GameplayMessageSubsystem 경량 재구현 (해당 플러그인/클래스 엔진 부재 확인, U8).
 *
 * 제1원리 / 계약:
 *  - 순수 로컬 프로세스 내 버스: 복제 프로퍼티/RPC 없음. 각 클라이언트 월드마다 독립 인스턴스.
 *    (Performance.md §5: 히트/사망 코스메틱 = GameplayMessage/Cue, 복제 액터 상태 아님. 적 복제=Transform만.)
 *  - Zero-cost 발행: 구독자 0이면 BroadcastMessageInternal이 즉시 early-out (힙할당/순회 0) — 적 사망 ×수백/프레임 대비.
 *  - 소비처: U13(VFX/Gibs/핑), 미래 U20(적 애니). 게임로직 이벤트(레벨업/미션/전멸)는 이 버스로 넘기지 말 것
 *    (기존 GameState 복제 프로퍼티 / ASC->HandleGameplayEvent 타겟형 경로 유지).
 */
UCLASS()
class FPSROGUELITE_API UFPSRGameplayMessageSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Returns the GMS for the world that WorldContextObject lives in (null if unavailable). */
	static UFPSRGameplayMessageSubsystem* Get(const UObject* WorldContextObject);

	/**
	 * Broadcast Message on Channel to all matching local listeners, synchronously.
	 * Payload is passed by value/stack; no heap allocation when there are no listeners.
	 */
	template <typename FMessageStructType>
	void BroadcastMessage(FGameplayTag Channel, const FMessageStructType& Message)
	{
		const UScriptStruct* StructType = FMessageStructType::StaticStruct();
		BroadcastMessageInternal(Channel, StructType, &Message);
	}

	/** Register a TFunction listener for messages of FMessageStructType on Channel. */
	template <typename FMessageStructType>
	FFPSRMessageListenerHandle RegisterListener(FGameplayTag Channel, TFunction<void(FGameplayTag, const FMessageStructType&)>&& Callback, EFPSRMessageMatch MatchType = EFPSRMessageMatch::ExactMatch)
	{
		auto ThunkCallback = [InnerCallback = MoveTemp(Callback)](FGameplayTag ActualChannel, const UScriptStruct* SenderStructType, const void* SenderPayload)
		{
			InnerCallback(ActualChannel, *reinterpret_cast<const FMessageStructType*>(SenderPayload));
		};

		return RegisterListenerInternal(Channel, ThunkCallback, FMessageStructType::StaticStruct(), MatchType);
	}

	/** Register a UObject member-function listener (auto weak-guarded; skipped if the object is gone). */
	template <typename TOwner = UObject, typename FMessageStructType>
	FFPSRMessageListenerHandle RegisterListener(FGameplayTag Channel, TOwner* Object, void(TOwner::* Function)(FGameplayTag, const FMessageStructType&), EFPSRMessageMatch MatchType = EFPSRMessageMatch::ExactMatch)
	{
		TWeakObjectPtr<TOwner> WeakObject(Object);
		return RegisterListener<FMessageStructType>(Channel,
			[WeakObject, Function](FGameplayTag ActualChannel, const FMessageStructType& Payload)
			{
				if (TOwner* StrongObject = WeakObject.Get())
				{
					(StrongObject->*Function)(ActualChannel, Payload);
				}
			},
			MatchType);
	}

	/** Remove a previously registered listener. Safe with default/invalid handles. */
	void UnregisterListener(FFPSRMessageListenerHandle Handle);

#if !UE_BUILD_SHIPPING
	/** Debug/test instrument: total listener callbacks invoked over this subsystem's lifetime. */
	int64 GetTotalDispatchCount() const { return TotalDispatchCount; }
#endif

protected:
	void BroadcastMessageInternal(FGameplayTag Channel, const UScriptStruct* StructType, const void* MessageBytes);
	FFPSRMessageListenerHandle RegisterListenerInternal(FGameplayTag Channel, TFunction<void(FGameplayTag, const UScriptStruct*, const void*)>&& Callback, const UScriptStruct* StructType, EFPSRMessageMatch MatchType);
	void UnregisterListenerInternal(FGameplayTag Channel, int32 HandleID);

private:
	/** Per-channel listener list. */
	struct FChannelListenerList
	{
		TArray<FFPSRMessageListenerData> Listeners;
	};

	/** Channel tag -> listeners. Plain member (holds TFunctions; not reflected/replicated). */
	TMap<FGameplayTag, FChannelListenerList> ListenerMap;

	/**
	 * Global monotonic listener-ID counter (never reset). IDs must stay unique across a channel being
	 * emptied and re-populated, otherwise a stale copied handle could detach an unrelated future listener
	 * (Codex U8 merge-gate P2). Empty channel entries are still removed, so the IsEmpty() fast path holds.
	 */
	int32 NextListenerHandleID = 0;

#if !UE_BUILD_SHIPPING
	int64 TotalDispatchCount = 0;
#endif

	friend FFPSRMessageListenerHandle;
};
