// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaBakeHash.h"
#include "Arena/FPSRArenaActor.h"
#include "Core/FPSRLogChannels.h"

#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/SecureHash.h"

namespace
{
	/** 트랜스폼을 문자열로 굳힐 때의 자릿수는 %.3f — 0.001cm = 10nm 로, 사람이 의도적으로 준 어떤 이동보다도
	 *  작고 float 을 레벨에 저장했다 읽는 왕복에서 마지막 자리가 흔들릴 여지보다는 크다. 너무 성글면 진짜
	 *  변경을 놓치고, 너무 빡빡하면 아무것도 안 건드렸는데 스테일이라고 우긴다.
	 *  (UE 5.7 의 FString::Printf 는 컴파일타임 검사 포맷을 받으므로 리터럴이어야 한다 — 상수로 뺄 수 없다.) */
	FString Quantize(double V)
	{
		// -0.0 과 0.0 이 다른 문자열이 되면 아무도 건드리지 않은 액터가 스테일로 뜬다.
		const double Snapped = FMath::IsNearlyZero(V, UE_DOUBLE_SMALL_NUMBER) ? 0.0 : V;
		return FString::Printf(TEXT("%.3f"), Snapped);
	}

	FString DescribeTransform(const FTransform& T)
	{
		const FVector L = T.GetLocation();
		const FRotator R = T.Rotator();
		const FVector S = T.GetScale3D();
		return FString::Printf(TEXT("%s,%s,%s|%s,%s,%s|%s,%s,%s"),
			*Quantize(L.X), *Quantize(L.Y), *Quantize(L.Z),
			*Quantize(R.Pitch), *Quantize(R.Yaw), *Quantize(R.Roll),
			*Quantize(S.X), *Quantize(S.Y), *Quantize(S.Z));
	}

	/** 이 컴포넌트가 베이크의 장애물 프로브에 잡히는가.
	 *  UFPSRFlowFieldComputer 의 프로브는 ECC_WorldStatic **오브젝트 타입**을 질의한다 — 트레이스 채널 응답이
	 *  아니라 오브젝트 타입이므로, 여기서도 같은 것을 본다. */
	bool ContributesToBake(const UPrimitiveComponent* Comp)
	{
		if (!IsValid(Comp))
		{
			return false;
		}

		// QueryOnly / QueryAndPhysics 여야 라인트레이스에 걸린다. PhysicsOnly 와 NoCollision 은 프로브가 통과한다.
		const ECollisionEnabled::Type Enabled = Comp->GetCollisionEnabled();
		if (Enabled != ECollisionEnabled::QueryOnly && Enabled != ECollisionEnabled::QueryAndPhysics)
		{
			return false;
		}

		return Comp->GetCollisionObjectType() == ECC_WorldStatic;
	}
}

bool FFPSRArenaBakeHash::Compute(const AFPSRArenaActor& Arena, FFPSRArenaBakeSourceDigest& Out)
{
	Out = FFPSRArenaBakeSourceDigest();

	const UWorld* World = Arena.GetWorld();
	const ULevel* ArenaLevel = Arena.GetLevel();
	if (!World || !ArenaLevel)
	{
		UE_LOG(LogFPSR, Error, TEXT("[Arena] 베이크 해시: '%s' 의 월드/레벨이 없다."), *Arena.GetName());
		return false;
	}

	// 아레나 로컬로 접기 위한 역변환. 트랜스폼 자체를 해시하면 파킹 위치를 옮기는 것만으로 전 레벨이
	// 스테일로 뜬다(ADR 0012 불변식 8).
	const FTransform ArenaToWorld = Arena.GetActorTransform();

	// 정렬 가능한 항목으로 모은 뒤 이름순으로 정렬한다. TActorIterator 의 순회 순서는 보장되지 않으므로,
	// 그대로 해시하면 같은 레벨이 머신마다 다른 해시를 낸다 — 그러면 검사가 매번 스테일이라고 외친다.
	TArray<FString> Entries;
	TSet<FName> ActorsSeen;

	for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		const AActor* Actor = *It;
		if (!IsValid(Actor) || Actor->GetLevel() != ArenaLevel)
		{
			continue; // 이 아레나의 서브레벨만 (불변식 4: 아레나 1 = 서브레벨 1)
		}

		bool bActorContributed = false;

		TInlineComponentArray<UPrimitiveComponent*> Primitives(Actor);
		for (const UPrimitiveComponent* Comp : Primitives)
		{
			if (!ContributesToBake(Comp))
			{
				continue;
			}

			// 아레나 격자 밖의 지오메트리는 프로브가 닿지 않으므로 해시에도 넣지 않는다. 넣으면 아레나와
			// 무관한 장식을 옮긴 것만으로 재베이크를 요구하게 된다. 컴포넌트 바운드의 중심으로 판정한다 —
			// ContainsWorldLocation 이 XY 만 보므로 경계에 걸친 벽도 중심이 안에 있으면 포함된다.
			if (!Arena.ContainsWorldLocation(Comp->Bounds.Origin))
			{
				continue;
			}

			// 지오메트리 정체성: 메시가 바뀌면 같은 자리라도 다른 벽이다.
			FString GeometryId = TEXT("none");
			if (const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Comp))
			{
				GeometryId = SMC->GetStaticMesh() ? SMC->GetStaticMesh()->GetPathName() : TEXT("null");
			}
			else
			{
				// 박스/캡슐/스피어 콜리전 등: 클래스 + 로컬 바운드 크기가 형상을 대신한다.
				GeometryId = FString::Printf(TEXT("%s:%s"), *Comp->GetClass()->GetName(),
					*Quantize(Comp->Bounds.SphereRadius));
			}

			Entries.Add(FString::Printf(TEXT("%s|%s|%s|%d"),
				*Comp->GetPathName(),
				*DescribeTransform(Comp->GetComponentTransform().GetRelativeTransform(ArenaToWorld)),
				*GeometryId,
				static_cast<int32>(Comp->GetCollisionEnabled())));

			bActorContributed = true;
		}

		if (bActorContributed)
		{
			ActorsSeen.Add(Actor->GetFName());
		}
	}

	Entries.Sort(); // 결정론: 순회 순서가 아니라 이름 순서가 해시를 정한다

	FSHA1 Sha;
	for (const FString& Entry : Entries)
	{
		const FTCHARToUTF8 Utf8(*Entry);
		Sha.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
		Sha.Update(reinterpret_cast<const uint8*>("\n"), 1); // 구분자: 두 항목이 이어붙어 같은 바이트열이 되는 것 방지
	}
	Sha.Final();

	FSHAHash Digest;
	Sha.GetHash(Digest.Hash);

	Out.Hash = Digest.ToString();
	Out.ComponentCount = Entries.Num();
	Out.ActorCount = ActorsSeen.Num();

	// 빈 결과도 "성공"이다 — 아직 아무것도 안 놓은 아레나는 정상적인 저작 상태다. 다만 해시가 빈 목록의
	// SHA1(고정값)이 되므로, "굽지 않음"(빈 문자열)과 "비어 있는 것을 구움"이 구분된다.
	return true;
}
