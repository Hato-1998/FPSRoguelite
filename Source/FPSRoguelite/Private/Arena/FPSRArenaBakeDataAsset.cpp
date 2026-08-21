// Copyright Epic Games, Inc. All Rights Reserved.

#include "Arena/FPSRArenaBakeDataAsset.h"
#include "Core/FPSRLogChannels.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "FPSRArenaBake"

namespace
{
	/** 회전·스케일 허용 오차. 레벨 스트리밍의 LevelTransform 과 액터 트랜스폼이 왕복하면서 붙는 부동소수
	 *  찌꺼기(1e-6 급)를 회전으로 오판하지 않을 만큼은 느슨하고, 사람이 의도적으로 준 회전(가장 작은
	 *  실수라도 0.01도 = 쿼터니언 성분 ~1e-4)은 반드시 잡을 만큼은 빡빡하다. */
	constexpr double ArenaTransformRotationTolerance = 1.0e-4;
	constexpr double ArenaTransformScaleTolerance = 1.0e-4;
}

bool UFPSRArenaBakeDataAsset::IsBaked() const
{
	// 치수만 보고 판단하지 않는다. FFPSRFlowFieldSurfaceData 의 벌크 배열에서 UPROPERTY 가 빠지면 스칼라는
	// 저장되는데 배열만 통째로 비어 로드된다(ADR 0012 「감수하기로 한 것」) — 그 상태가 정확히 "치수는
	// 멀쩡한데 마스크가 없다"로 나타나므로, 배열 길이를 격자 크기와 대조하는 것이 그 사고의 유일한 탐지선이다.
	const int32 NumCells = Surface.GridDimX * Surface.GridDimY;
	if (NumCells <= 0 || Surface.CellSize <= 0.0f)
	{
		return false;
	}

	const int32 NumSurf = NumCells * UFPSRFlowFieldComputer::NumLayers;
	return Surface.CellFloorZ.Num() == NumSurf
		&& Surface.BlockedField.Num() == NumSurf
		&& Surface.EdgeMask.Num() == NumCells * 2;
}

bool UFPSRArenaBakeDataAsset::ExtractPlanarOffset(const FTransform& ArenaTransform, FVector& OutOffset, FString& OutError)
{
	// 회전 거부: 셀 격자는 축정렬이고 AFPSRArenaActor::ContainsWorldLocation 이 XY min/max 로만 판정한다.
	// 회전을 "적용"하면 격자와 판정이 서로 다른 공간을 말하게 되는데, 화면에는 아무 흔적이 남지 않는다 —
	// 지오메트리는 제자리에 있고 적만 엉뚱하게 움직인다. 보정하는 대신 거부해서 저작 시점에 드러낸다.
	const FQuat Rotation = ArenaTransform.GetRotation();
	if (!Rotation.Equals(FQuat::Identity, ArenaTransformRotationTolerance))
	{
		OutError = FString::Printf(
			TEXT("아레나 트랜스폼에 회전이 있다(%s). 셀 격자는 축정렬이라 회전을 표현할 수 없다 — 아레나 액터의 회전을 0으로 두고, 방향이 필요하면 지오메트리 쪽을 돌려라."),
			*Rotation.Rotator().ToString());
		return false;
	}

	// 스케일 거부: 격자 간격은 Surface.CellSize 하나가 소유한다. 트랜스폼 스케일을 허용하면 같은 값이 두
	// 군데서 정의되고, 둘이 어긋나는 순간 어느 쪽이 진실인지 말할 수 없다.
	const FVector Scale = ArenaTransform.GetScale3D();
	if (!Scale.Equals(FVector::OneVector, ArenaTransformScaleTolerance))
	{
		OutError = FString::Printf(
			TEXT("아레나 트랜스폼에 스케일이 있다(%s). 셀 간격은 베이크 데이터의 CellSize 가 소유한다 — 액터 스케일은 1로 두고 아레나 파라미터에서 크기를 바꿔라."),
			*Scale.ToString());
		return false;
	}

	OutOffset = ArenaTransform.GetLocation();
	return true;
}

void UFPSRArenaBakeDataAsset::OffsetSurface(const FFPSRFlowFieldSurfaceData& In, const FVector& Offset,
	FFPSRFlowFieldSurfaceData& Out)
{
	Out = In; // 치수·CellSize·ClimbableStepHeight·BlockedField·EdgeMask 는 좌표와 무관하므로 그대로 옮겨간다
	Out.GridOrigin = In.GridOrigin + Offset;

	// CellFloorZ 만 좌표를 담는다. MAX_flt 는 "이 (셀,랭크)에는 표면이 없다"는 센티넬이지 높이가 아니다 —
	// 여기에 오프셋을 더하면 유한값이 되어 없던 바닥이 생기고, 적이 허공을 걷는다. 반드시 건너뛴다.
	const int32 Num = In.CellFloorZ.Num();
	for (int32 i = 0; i < Num; ++i)
	{
		if (In.CellFloorZ[i] != MAX_flt)
		{
			Out.CellFloorZ[i] = In.CellFloorZ[i] + static_cast<float>(Offset.Z);
		}
	}
}

bool UFPSRArenaBakeDataAsset::BuildWorldSurface(const FTransform& ArenaTransform, FFPSRFlowFieldSurfaceData& OutWorld) const
{
	if (!IsBaked())
	{
		UE_LOG(LogFPSR, Error,
			TEXT("[Arena] 베이크 에셋 '%s' 에 쓸 수 있는 마스크가 없다 — 에디터에서 아레나를 굽고 저장했는지 확인할 것. (ADR 0012)"),
			*GetName());
		return false;
	}

	FVector Offset;
	FString Error;
	if (!ExtractPlanarOffset(ArenaTransform, Offset, Error))
	{
		UE_LOG(LogFPSR, Error, TEXT("[Arena] 베이크 에셋 '%s' 를 월드로 펼 수 없다: %s"), *GetName(), *Error);
		return false;
	}

	OffsetSurface(Surface, Offset, OutWorld);
	return true;
}

bool UFPSRArenaBakeDataAsset::LocalizeSurface(const FFPSRFlowFieldSurfaceData& World, const FTransform& ArenaTransform,
	FFPSRFlowFieldSurfaceData& OutLocal)
{
	FVector Offset;
	FString Error;
	if (!ExtractPlanarOffset(ArenaTransform, Offset, Error))
	{
		UE_LOG(LogFPSR, Error, TEXT("[Arena] 베이크 결과를 로컬로 접을 수 없다: %s"), *Error);
		return false;
	}

	OffsetSurface(World, -Offset, OutLocal); // BuildWorldSurface 의 정확한 역연산
	return true;
}

bool UFPSRArenaBakeDataAsset::BuildWorldVoxels(const FTransform& ArenaTransform, FFPSRArenaVoxelData& OutWorld) const
{
	if (!Voxels.IsBakedVoxels())
	{
		// Warning, not Error: a pre-P1 asset legitimately has no voxels yet, and the runtime adopter treats that
		// as a documented SOFT downgrade (2D flow keeps working, the S3 hover window just has no field until the
		// arena is re-baked). The hard "this must be fixed" surface is IsDataValid()'s Error — at author/validate
		// time, where the fix (re-bake) actually happens — not every adoption at runtime.
		UE_LOG(LogFPSR, Warning,
			TEXT("[Arena] 베이크 에셋 '%s' 에 쓸 수 있는 복셀 점유 데이터가 없다 — 에디터에서 아레나를 다시 굽고 저장했는지 확인할 것. (ADR 0009 S2)"),
			*GetName());
		return false;
	}

	FVector Offset;
	FString Error;
	if (!ExtractPlanarOffset(ArenaTransform, Offset, Error))
	{
		UE_LOG(LogFPSR, Error, TEXT("[Arena] 베이크 에셋 '%s' 의 복셀을 월드로 펼 수 없다: %s"), *GetName(), *Error);
		return false;
	}

	// Dims/size/Occupancy/FreeVoxels are coordinate-independent — only the origin moves (unlike OffsetSurface,
	// there is no MAX_flt sentinel to skip; every Occupancy entry is a bit, not a height).
	OutWorld = Voxels;
	OutWorld.VoxelOrigin = Voxels.VoxelOrigin + Offset;
	return true;
}

bool UFPSRArenaBakeDataAsset::LocalizeVoxels(const FFPSRArenaVoxelData& World, const FTransform& ArenaTransform,
	FFPSRArenaVoxelData& OutLocal)
{
	FVector Offset;
	FString Error;
	if (!ExtractPlanarOffset(ArenaTransform, Offset, Error))
	{
		UE_LOG(LogFPSR, Error, TEXT("[Arena] 베이크 복셀 결과를 로컬로 접을 수 없다: %s"), *Error);
		return false;
	}

	OutLocal = World; // BuildWorldVoxels 의 정확한 역연산
	OutLocal.VoxelOrigin = World.VoxelOrigin - Offset;
	return true;
}

#if WITH_EDITOR
EDataValidationResult UFPSRArenaBakeDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// 여기서 보는 것은 이 에셋 혼자서 알 수 있는 것뿐이다. 레벨과의 스테일 대조(해시 재계산)는 레벨을
	// 로드해야 하므로 에디터 검증기 쪽이 맡는다 — ADR 0012 5겹 검사 중 ①②③④. 규칙을 두 군데 두지 않기
	// 위해 그 판정은 여기로 끌어오지 않는다.
	if (!IsBaked())
	{
		Context.AddError(LOCTEXT("NotBaked",
			"베이크 데이터가 비었거나 배열 길이가 격자 크기와 맞지 않는다. 에디터에서 아레나를 다시 구울 것."));
		Result = EDataValidationResult::Invalid;
	}

	if (SourceHash.IsEmpty())
	{
		Context.AddError(LOCTEXT("NoHash",
			"소스 해시가 비었다 — 스테일 검사를 할 수 없다. 해시 없는 베이크는 '일치'가 아니라 '모름'이다."));
		Result = EDataValidationResult::Invalid;
	}

	if (SourceLevel.IsNull())
	{
		Context.AddError(LOCTEXT("NoSourceLevel",
			"소스 레벨이 비었다 — 검증기가 무엇을 다시 해시해야 할지 알 수 없다."));
		Result = EDataValidationResult::Invalid;
	}

	// ADR 0009 S2 마이그레이션 신호: 2D 는 구웠는데 복셀이 없거나(비어 있음) 치수/배열 길이가 안 맞으면
	// 그 자체가 "이 에셋을 P1 이전에 구웠다"는 뜻이다. IsBaked()(런타임 채택 게이트)는 건드리지 않는다 —
	// 복셀 없이도 2D 서페이스만으로 런타임은 계속 살아야 하고(부양 스웜 3D 창이 그냥 안 뜨는 소프트 강등,
	// UFPSRFlowFieldSubsystem::AdoptArenaField 참고), 이 검증은 에디터에서만 그 갭을 드러낸다.
	if (IsBaked() && !Voxels.IsBakedVoxels())
	{
		Context.AddError(LOCTEXT("NoVoxels",
			"2D 표면은 구웠지만 복셀 점유 데이터가 없거나 치수가 맞지 않는다 — 에디터에서 아레나를 다시 구울 것 (ADR 0009 S2)."));
		Result = EDataValidationResult::Invalid;
	}

	// 오류가 아니라 경고: 액터가 정말 몇 개뿐인 화이트박스 초기 단계일 수 있다. 다만 콜리전 설정을
	// 잘못 건드려 벽이 통째로 베이크에서 빠지는 사고가 정확히 이 숫자로 나타나므로 눈에 띄게 둔다.
	if (IsBaked() && SourceActorCount <= 1)
	{
		Context.AddWarning(FText::Format(LOCTEXT("FewSourceActors",
			"베이크에 들어간 액터가 {0}개뿐이다. 벽 메시의 콜리전이 꺼져 있지 않은지 확인할 것 (ADR 0012 불변식 2: 콜리전이 곧 마스크다)."),
			FText::AsNumber(SourceActorCount)));
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
