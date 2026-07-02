// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Messages/FPSRGameplayMessageSubsystem.h"
#include "Messages/FPSRCosmeticMessages.h"
#include "GameplayTagContainer.h"
#include "UObject/Package.h"

#if WITH_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRGameplayMessageTest, "FPSRoguelite.Smoke.GameplayMessage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRGameplayMessageTest::RunTest(const FString& Parameters)
{
	UFPSRGameplayMessageSubsystem* GMS = NewObject<UFPSRGameplayMessageSubsystem>(GetTransientPackage());
	TestNotNull(TEXT("GMS instance created"), GMS);
	if (!GMS)
	{
		return false;
	}

	const FGameplayTag Channel = FGameplayTag::RequestGameplayTag(FName("GameplayEvent.EnemyKilled"));
	TestTrue(TEXT("Test channel tag resolves"), Channel.IsValid());

	// (a) Register -> broadcast -> callback fires with a value-copied payload.
	int32 ReceivedCount = 0;
	FFPSRCosmeticEventMessage Received;
	FFPSRMessageListenerHandle Handle = GMS->RegisterListener<FFPSRCosmeticEventMessage>(Channel,
		[&ReceivedCount, &Received](FGameplayTag InChannel, const FFPSRCosmeticEventMessage& Msg)
		{
			++ReceivedCount;
			Received = Msg;
		});
	TestTrue(TEXT("Handle valid after register"), Handle.IsValid());

	FFPSRCosmeticEventMessage Sent;
	Sent.WorldLocation = FVector(10.f, 20.f, 30.f);
	Sent.InstigatorTeam = 2;
	Sent.bWasKill = true;
	GMS->BroadcastMessage(Channel, Sent);

	TestEqual(TEXT("Listener invoked exactly once"), ReceivedCount, 1);
	TestEqual(TEXT("Payload WorldLocation copied"), Received.WorldLocation, Sent.WorldLocation);
	TestEqual(TEXT("Payload team copied"), (int32)Received.InstigatorTeam, 2);
	TestTrue(TEXT("Payload bWasKill copied"), Received.bWasKill);
#if !UE_BUILD_SHIPPING
	TestEqual(TEXT("Dispatch count is 1 after one delivery"), (int32)GMS->GetTotalDispatchCount(), 1);
#endif

	// (b) Unregister -> broadcast -> not invoked again.
	Handle.Unregister();
	TestFalse(TEXT("Handle invalid after unregister"), Handle.IsValid());
	GMS->BroadcastMessage(Channel, Sent);
	TestEqual(TEXT("Listener not invoked after unregister"), ReceivedCount, 1);

	// (c) Zero-subscriber broadcast (map non-empty via unrelated channel) -> early-out, no dispatch.
	int32 UnrelatedCount = 0;
	const FGameplayTag UnrelatedChannel = FGameplayTag::RequestGameplayTag(FName("GameplayEvent.LevelUp"));
	FFPSRMessageListenerHandle Keepalive = GMS->RegisterListener<FFPSRCosmeticEventMessage>(UnrelatedChannel,
		[&UnrelatedCount](FGameplayTag, const FFPSRCosmeticEventMessage&)
		{
			++UnrelatedCount;
		});
#if !UE_BUILD_SHIPPING
	const int32 BeforeCount = (int32)GMS->GetTotalDispatchCount();
#endif
	const FGameplayTag EmptyChannel = FGameplayTag::RequestGameplayTag(FName("GameplayEvent.PickupCollected"));
	GMS->BroadcastMessage(EmptyChannel, Sent);
	TestEqual(TEXT("Unrelated-channel listener not invoked"), UnrelatedCount, 0);
#if !UE_BUILD_SHIPPING
	TestEqual(TEXT("No dispatch on a channel with zero listeners"), (int32)GMS->GetTotalDispatchCount(), BeforeCount);
#endif
	Keepalive.Unregister();

	return true;
}

#endif // WITH_AUTOMATION_TESTS
