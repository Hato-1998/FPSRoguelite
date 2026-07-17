// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blockout/FPSRBlockoutValidator.h"

#include "Enemy/FPSRFlowFieldBoundsVolume.h"
#include "Enemy/FPSREnemySpawnPoint.h"
#include "Enemy/FPSRFlowFieldComputer.h"
#include "Enemy/FPSREnemyBase.h" // AFPSREnemyBase::DefaultCapsuleHalfHeight (shared spawn-clearance constant)

#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "Logging/MessageLog.h"

#define LOCTEXT_NAMESPACE "FPSRBlockoutValidator"

const FName FFPSRBlockoutValidator::MessageLogName(TEXT("FPSRBlockout"));

namespace
{
	/** Down-trace at (X,Y) from TopZ to BottomZ against ECC_WorldStatic (the exact channel the flow-field obstacle
	 *  mask uses). Returns true + the first hit's Z / actor. */
	bool TraceWorldStaticDown(UWorld* World, float X, float Y, float TopZ, float BottomZ, float& OutHitZ, AActor*& OutHitActor)
	{
		FHitResult Hit;
		const FVector Start(X, Y, TopZ);
		const FVector End(X, Y, BottomZ);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FPSRBlockoutFloorTrace), /*bTraceComplex=*/false);
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
		{
			OutHitZ = Hit.ImpactPoint.Z;
			OutHitActor = Hit.GetActor();
			return true;
		}
		return false;
	}

	FString ActorLabelSafe(const AActor* Actor)
	{
		return Actor ? Actor->GetActorLabel() : FString(TEXT("?"));
	}
}

int32 FFPSRBlockoutValidator::ValidateLevel(UWorld* World)
{
	FMessageLog Log(MessageLogName);
	Log.NewPage(LOCTEXT("PageTitle", "FPSR 블록아웃 검증"));

	if (!World)
	{
		Log.Error(LOCTEXT("NoWorld", "편집 가능한 에디터 월드가 없습니다."));
		Log.Open(EMessageSeverity::Error, /*bOpenEvenIfEmpty=*/true);
		return 1;
	}

	int32 FindingCount = 0;

	// --- Check 4: bounds volume presence (done first; checks 5/6 reference it) -----------------------------------
	AFPSRFlowFieldBoundsVolume* BoundsVolume = nullptr;
	int32 BoundsVolumeCount = 0;
	for (TActorIterator<AFPSRFlowFieldBoundsVolume> It(World); It; ++It)
	{
		++BoundsVolumeCount;
		if (!BoundsVolume)
		{
			BoundsVolume = *It;
		}
	}
	if (BoundsVolumeCount == 0)
	{
		Log.Warning(LOCTEXT("NoBoundsVolume", "FlowFieldBoundsVolume이 없습니다 — 플로우필드가 원점 폴백 범위를 씁니다. 플레이 영역을 감싸는 볼륨 1개를 배치하세요."));
		++FindingCount;
	}
	else if (BoundsVolumeCount > 1)
	{
		Log.Warning(FText::Format(LOCTEXT("MultiBoundsVolume", "FlowFieldBoundsVolume이 {0}개입니다 — 단일맵은 1개여야 합니다(다중맵은 MapId별)."), FText::AsNumber(BoundsVolumeCount)));
		++FindingCount;
	}

	FBox VolumeBounds(ForceInit);
	if (BoundsVolume)
	{
		VolumeBounds = BoundsVolume->GetWorldBounds();
	}

	// --- Checks 2 (floor) + 6 (center clear): one down-trace at the bounds center (or origin) --------------------
	{
		const FVector Center = VolumeBounds.IsValid ? VolumeBounds.GetCenter() : FVector::ZeroVector;
		const float TopZ = VolumeBounds.IsValid ? VolumeBounds.Max.Z + 200.0f : 10000.0f;
		const float BottomZ = VolumeBounds.IsValid ? VolumeBounds.Min.Z - 200.0f : -10000.0f;

		float HitZ = 0.0f;
		AActor* HitActor = nullptr;
		if (TraceWorldStaticDown(World, Center.X, Center.Y, TopZ, BottomZ, HitZ, HitActor))
		{
			Log.Info(FText::Format(LOCTEXT("FloorOk", "지면 감지: {0} @ Z={1}."), FText::FromString(ActorLabelSafe(HitActor)), FText::AsNumber(FMath::RoundToInt(HitZ))));

			// Check 6: an elevated FIRST hit at the exact box center likely mis-anchors the grid-origin floor trace.
			if (VolumeBounds.IsValid && HitZ > VolumeBounds.Min.Z + 150.0f)
			{
				Log.Warning(FText::Format(LOCTEXT("CenterBlocked", "볼륨 박스 중심의 첫 WorldStatic 히트({0})가 바닥보다 {1}cm 높습니다 — grid origin Z 오앵커 위험. 중심에서 콜리전 액터를 비켜 배치하세요."),
					FText::FromString(ActorLabelSafe(HitActor)), FText::AsNumber(FMath::RoundToInt(HitZ - VolumeBounds.Min.Z))));
				++FindingCount;
			}
		}
		else
		{
			Log.Warning(LOCTEXT("NoFloor", "지면을 찾지 못했습니다 (중심에서 아래로 WorldStatic 없음) — WorldStatic 지면을 Z=0 부근에 배치하세요."));
			++FindingCount;
		}
	}

	// --- Check 5: cell budget ------------------------------------------------------------------------------------
	if (BoundsVolume && VolumeBounds.IsValid)
	{
		const float CellSize = BoundsVolume->GetCellSizeOverride() > 0.0f ? BoundsVolume->GetCellSizeOverride() : 200.0f;
		const FVector Size = VolumeBounds.GetSize();
		const int32 DimX = FMath::Max(1, FMath::CeilToInt(Size.X / CellSize));
		const int32 DimY = FMath::Max(1, FMath::CeilToInt(Size.Y / CellSize));
		const int64 TotalCells = static_cast<int64>(DimX) * static_cast<int64>(DimY);
		const int32 MaxDim = UFPSRFlowFieldComputer::GetMaxGridDimPerAxis();
		const int32 MaxTotal = UFPSRFlowFieldComputer::GetMaxTotalCells();

		if (DimX > MaxDim || DimY > MaxDim)
		{
			Log.Error(FText::Format(LOCTEXT("CellDimOver", "플로우필드 격자 {0}x{1} — 축당 최대 {2} 초과. 볼륨을 줄이거나 CellSizeOverride를 키우세요."),
				FText::AsNumber(DimX), FText::AsNumber(DimY), FText::AsNumber(MaxDim)));
			++FindingCount;
		}
		else if (TotalCells > MaxTotal)
		{
			Log.Error(FText::Format(LOCTEXT("CellTotalOver", "플로우필드 셀 {0}개 — 최대 {1} 초과. 볼륨을 줄이거나 CellSizeOverride를 키우세요."),
				FText::AsNumber(TotalCells), FText::AsNumber(MaxTotal)));
			++FindingCount;
		}
		else
		{
			Log.Info(FText::Format(LOCTEXT("CellOk", "플로우필드 셀 {0}개 ({1}x{2}, cell {3}cm) — 예산 내 (최대 {4})."),
				FText::AsNumber(TotalCells), FText::AsNumber(DimX), FText::AsNumber(DimY),
				FText::AsNumber(FMath::RoundToInt(CellSize)), FText::AsNumber(MaxTotal)));
		}
	}

	// --- Check 1: collision (blocking static meshes must be WorldStatic for the flow-field obstacle mask) --------
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		AStaticMeshActor* MeshActor = *It;
		UStaticMeshComponent* Comp = MeshActor ? MeshActor->GetStaticMeshComponent() : nullptr;
		if (!Comp)
		{
			continue;
		}
		if (Comp->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
		{
			continue; // intentionally non-blocking (decoration) — not a flow-field concern
		}
		if (Comp->GetCollisionObjectType() != ECC_WorldStatic)
		{
			Log.Warning(FText::Format(LOCTEXT("NotWorldStatic", "{0}: 콜리전이 WorldStatic이 아닙니다 — 플로우필드가 장애물로 인식하지 못합니다(적이 통과)."),
				FText::FromString(ActorLabelSafe(MeshActor))));
			++FindingCount;
		}
	}

	// --- Check 3: enemy spawn points at ~floor+100 ---------------------------------------------------------------
	for (TActorIterator<AFPSREnemySpawnPoint> It(World); It; ++It)
	{
		AFPSREnemySpawnPoint* SpawnPoint = *It;
		if (!SpawnPoint)
		{
			continue;
		}
		const FVector SpawnLoc = SpawnPoint->GetSpawnLocation();
		float FloorZ = 0.0f;
		AActor* FloorActor = nullptr;
		// Trace from just above the spawn point down a long way (the spawn point itself has no collision).
		if (TraceWorldStaticDown(World, SpawnLoc.X, SpawnLoc.Y, SpawnLoc.Z + 50.0f, SpawnLoc.Z - 5000.0f, FloorZ, FloorActor))
		{
			const float Height = SpawnLoc.Z - FloorZ;
			if (Height < AFPSREnemyBase::DefaultCapsuleHalfHeight)
			{
				Log.Warning(FText::Format(LOCTEXT("SpawnTooLow", "{0}: 스폰 지점이 지면보다 {1}cm 높습니다 (권장 ~100cm; 캡슐 반높이 {2} 미만이면 적이 바닥을 뚫고 낙하)."),
					FText::FromString(ActorLabelSafe(SpawnPoint)), FText::AsNumber(FMath::RoundToInt(Height)),
					FText::AsNumber(FMath::RoundToInt(AFPSREnemyBase::DefaultCapsuleHalfHeight))));
				++FindingCount;
			}
		}
		else
		{
			Log.Warning(FText::Format(LOCTEXT("SpawnNoFloor", "{0}: 스폰 지점 아래에 WorldStatic 지면이 없습니다 — 적이 낙하합니다."),
				FText::FromString(ActorLabelSafe(SpawnPoint))));
			++FindingCount;
		}
	}

	// --- Summary --------------------------------------------------------------------------------------------------
	if (FindingCount == 0)
	{
		Log.Info(LOCTEXT("AllClear", "블록아웃 가드레일 통과 — 경고/오류 없음."));
	}

	Log.Open(EMessageSeverity::Info, /*bOpenEvenIfEmpty=*/true);
	return FindingCount;
}

#undef LOCTEXT_NAMESPACE
