// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaBakeHash.h"
#include "Arena/FPSRArenaActor.h"
#include "Arena/FPSRArenaBakeDataAsset.h" // CheckFreshness reads IsBaked()/SourceHash off the asset
#include "Core/FPSRLogChannels.h"

#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/SecureHash.h"
#include "PhysicsEngine/BodyInstance.h" // AuthoredCollisionEnabled — 액터 게이트를 우회한 원본 콜리전 설정

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

	/**
	 * 이 컴포넌트에 **저작된** 콜리전 설정. `UPrimitiveComponent::GetCollisionEnabled()` 를 쓰면 안 된다 —
	 * 그것은 소유 액터가 `bActorEnableCollision=false` 면 무조건 `NoCollision` 을 돌려준다
	 * (`PrimitiveComponentPhysics.cpp:1564`).
	 *
	 * 그 차이가 실제 사고를 냈다. `AFPSRArenaActor::SetArenaActive(false)` 는 예비 아레나의 레벨 액터 전체에
	 * `SetActorEnableCollision(false)` 를 건다. 그 상태에서 해시를 구하면 **모든 컴포넌트가 탈락해 소스가
	 * 0개**가 되고, 빈 해시(SHA1 of "" = DA39A3EE…)가 나와 멀쩡한 베이크가 STALE 로 보고된다.
	 * 해시는 **저작된 레벨**을 기술해야지 런타임에 켜졌다 꺼졌다 하는 상태를 기술하면 안 된다.
	 */
	ECollisionEnabled::Type AuthoredCollisionEnabled(const UPrimitiveComponent* Comp)
	{
		return Comp->BodyInstance.GetCollisionEnabled(/*bCheckOwner*/ false);
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
		const ECollisionEnabled::Type Enabled = AuthoredCollisionEnabled(Comp);
		if (Enabled != ECollisionEnabled::QueryOnly && Enabled != ECollisionEnabled::QueryAndPhysics)
		{
			return false;
		}

		return Comp->GetCollisionObjectType() == ECC_WorldStatic;
	}
}

FFPSRArenaBakeCheck FFPSRArenaBakeHash::CheckFreshness(const AFPSRArenaActor& Arena)
{
	FFPSRArenaBakeCheck Check;

	const UFPSRArenaBakeDataAsset* Asset = Arena.GetBakeData();
	if (!Asset)
	{
		// 스테일과 구분한다. 아직 안 물린 것은 저작 중인 정상 상태일 수 있고, 스테일은 언제나 사고다.
		Check.Freshness = EFPSRArenaBakeFreshness::NoAsset;
		Check.Message = FString::Printf(
			TEXT("[%s] 베이크 데이터가 지정되지 않았습니다. UFPSRArenaBakeDataAsset 을 만들어 액터의 '베이크 데이터' 에 물리고 'Tools > FPSR > 아레나 베이크' 를 실행하세요."),
			*Arena.GetName());
		return Check;
	}

	if (!Asset->IsBaked() || Asset->SourceHash.IsEmpty())
	{
		Check.Freshness = EFPSRArenaBakeFreshness::NotBaked;
		Check.Message = FString::Printf(
			TEXT("[%s] 베이크 에셋 '%s' 가 비어 있습니다(굽지 않았거나 저장되지 않음). 'Tools > FPSR > 아레나 베이크' 후 Ctrl+S."),
			*Arena.GetName(), *Asset->GetName());
		return Check;
	}

	if (!Compute(Arena, Check.Current))
	{
		// 해시를 못 구한 상태에서 "일치"라고 답하면 안 된다 — 모르는 것은 스테일로 친다(fail-closed).
		Check.Freshness = EFPSRArenaBakeFreshness::Stale;
		Check.Message = FString::Printf(
			TEXT("[%s] 현재 레벨의 소스 해시를 계산할 수 없어 베이크 신선도를 확인하지 못했습니다 — 스테일로 간주합니다."),
			*Arena.GetName());
		return Check;
	}

	if (Check.Current.Hash != Asset->SourceHash)
	{
		Check.Freshness = EFPSRArenaBakeFreshness::Stale;
		Check.Message = FString::Printf(
			TEXT("[%s] 베이크가 레벨과 다릅니다(STALE). 지오메트리를 고치고 다시 굽지 않았습니다 — 화면은 새 지오메트리를, 적은 옛 마스크를 봅니다. 저장 해시 %s / 현재 %s · 현재 소스 %d액터(%d컴포넌트). 'Tools > FPSR > 아레나 베이크' 를 다시 실행하세요."),
			*Arena.GetName(), *Asset->SourceHash.Left(12), *Check.Current.Hash.Left(12),
			Check.Current.ActorCount, Check.Current.ComponentCount);
		return Check;
	}

	Check.Freshness = EFPSRArenaBakeFreshness::Fresh;
	Check.Message = FString::Printf(TEXT("[%s] 베이크 최신 (소스 %d액터 / %d컴포넌트)."),
		*Arena.GetName(), Check.Current.ActorCount, Check.Current.ComponentCount);
	return Check;
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

			// 경로는 **레벨 기준 상대 경로**여야 한다(`GetPathName(ArenaLevel)`). 절대 경로를 쓰면 PIE 가
			// 패키지에 `UEDPIE_0_` 접두사를 붙이므로(`/Game/Maps/UEDPIE_0_L_Map_1...`) 에디터에서 구운 해시와
			// 인게임에서 다시 구한 해시가 **아무것도 안 바꿔도 항상 다르다** — 실제로 그렇게 났다.
			// 레벨 상대 경로는 그 접두사 바깥이라 양쪽에서 같고, 이미 ArenaLevel 로 스코프하고 있으므로
			// 액터·컴포넌트 이름만으로 이 레벨 안에서 유일하다.
			Entries.Add(FString::Printf(TEXT("%s|%s|%s|%d"),
				*Comp->GetPathName(ArenaLevel),
				*DescribeTransform(Comp->GetComponentTransform().GetRelativeTransform(ArenaToWorld)),
				*GeometryId,
				static_cast<int32>(AuthoredCollisionEnabled(Comp))));

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
