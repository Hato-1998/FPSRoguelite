// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Enemy/FPSRFlowFieldComputer.h" // FFPSRFlowFieldSurfaceData
#include "Enemy/FPSRHoverWindowCore.h" // FPSRHoverWindow::OccupancyWords/SetOccupied/IsOccupied — same bit layout, ADR 0009 S2
#include "FPSRArenaBakeDataAsset.generated.h"

/**
 * One arena's baked 3D voxel occupancy (ADR 0009 결정 3, S2 — "전역 3D 복셀 오프라인 베이크"). Additive to
 * FFPSRFlowFieldSurfaceData, never a replacement: the 2D field still owns flow/connectivity, this struct only
 * answers "is this world point solid" for the S3 hover-swarm window's occupancy input.
 *
 * Same click, same SourceHash, same 5-layer staleness check as the 2D bake — this is deliberately NOT a second
 * bake asset/pipeline. It rides BakeArenasInLevel and is invalidated by the exact same hash ContributesToBake
 * already defines (the probe below queries the identical object-type set, ECC_WorldStatic, as the 2D obstacle
 * probe — see FPSRArenaBakeHash.h's "WHAT COUNTS" contract).
 *
 * COORDINATE SYSTEM: like Surface, this is baked and stored ARENA-LOCAL (invariant 8) — VoxelOrigin is never the
 * world position on disk. BuildWorldVoxels/LocalizeVoxels are the only conversion, mirroring BuildWorldSurface/
 * LocalizeSurface exactly (translation-only; rotated/scaled arena transforms are rejected, same reasons).
 *
 * BIT LAYOUT: intentionally identical to FFPSRHoverWindowDims — VoxelIndex uses the SAME (Z*DimY+Y)*DimX+X axis
 * order and the SAME FPSRHoverWindow::OccupancyWords/SetOccupied/IsOccupied bit-packing (FPSRHoverWindowCore.h).
 * This is the precondition that lets S3's window fill be a row-wise bit copy out of this field rather than a
 * per-cell re-derivation — a different axis order here would silently transpose every window it seeds.
 *
 * Every field is UPROPERTY() — Occupancy included. ADR 0012's "감수하기로 한 것" already burned this project once
 * (see UFPSRArenaBakeDataAsset::IsBaked's comment): a bulk TArray without UPROPERTY loads with scalars intact
 * and the array silently empty. IsBakedVoxels() below re-applies that same length-not-just-dimensions check.
 */
USTRUCT()
struct FPSROGUELITE_API FFPSRArenaVoxelData
{
	GENERATED_BODY()

	/** Voxel edge length (cm). Fixed by the P1 plan, not designer-tunable — the whole point of a fixed value is
	 *  that S3's window math never has to ask a per-arena field what its cell size is. */
	static constexpr float VoxelCellSizeCm = 150.0f;
	/** Z band floor, relative to the arena's 2D GridOrigin.Z (below it). */
	static constexpr float VoxelBelowOriginCm = 600.0f;
	/** Z band ceiling, relative to the arena's 2D GridOrigin.Z (above it). */
	static constexpr float VoxelAboveOriginCm = 3000.0f;

	/** World-min corner of voxel (0,0,0), in whatever space this instance is currently expressed in (arena-local
	 *  when this is UFPSRArenaBakeDataAsset::Voxels; world when returned by BuildWorldVoxels). XY matches the 2D
	 *  field's GridOrigin.XY exactly; Z is GridOrigin.Z - VoxelBelowOriginCm (the fixed band's floor). */
	UPROPERTY()
	FVector VoxelOrigin = FVector::ZeroVector;

	/** Always VoxelCellSizeCm once baked; stored (not just the constant) so a saved asset from before a future
	 *  cell-size change can still be told apart from a same-generation one — same rationale as Surface.CellSize. */
	UPROPERTY()
	float VoxelSize = VoxelCellSizeCm;

	UPROPERTY()
	int32 DimX = 0;

	UPROPERTY()
	int32 DimY = 0;

	UPROPERTY()
	int32 DimZ = 0;

	/** Bit-packed occupancy, FPSRHoverWindow::OccupancyWords(NumVoxels()) uint64 words, SAME layout as
	 *  FFPSRHoverWindowDims/FPSRHoverWindow::IsOccupied. A set bit = solid (ECC_WorldStatic overlap at bake time). */
	UPROPERTY()
	TArray<uint64> Occupancy;

	/** Free (unset-bit) voxel count at bake time — the human "does this look plausible" window (bulk Occupancy
	 *  can't be eyeballed in the details panel), same role as Surface's OpenCells. NOT maintained incrementally
	 *  by runtime stamps (ClearOccupiedAABB) — it is a bake-time snapshot, like OpenCells. */
	UPROPERTY()
	int32 FreeVoxels = 0;

	int32 NumVoxels() const { return DimX * DimY * DimZ; }

	/** Dimensions strictly positive AND Occupancy's length matches what those dimensions demand — the same
	 *  "don't trust the scalars alone" check IsBaked() applies to Surface, for the same reason (see class comment). */
	bool IsBakedVoxels() const
	{
		if (DimX <= 0 || DimY <= 0 || DimZ <= 0 || VoxelSize <= 0.0f)
		{
			return false;
		}
		return Occupancy.Num() == FPSRHoverWindow::OccupancyWords(NumVoxels());
	}

	/** Flat index for voxel (X,Y,Z). SAME axis order as FFPSRHoverWindowDims::CellIndex — see class comment.
	 *  Caller-verified in-range (no bounds check — this is the hot-path predicate IsWorldPosOccupied leans on). */
	int32 VoxelIndex(int32 X, int32 Y, int32 Z) const { return (Z * DimY + Y) * DimX + X; }

	/** World/local position -> voxel cell coordinate (floor-divide by VoxelSize, matching VoxelIndex's own cell
	 *  membership: cell N spans [VoxelOrigin + N*VoxelSize, VoxelOrigin + (N+1)*VoxelSize) on every axis). Returns
	 *  false (OutCell left at whatever FMath produced) when Pos falls outside [0,DimX)x[0,DimY)x[0,DimZ) — the
	 *  caller decides what "outside" means (IsWorldPosOccupied below treats it as occupied, fail-closed). */
	bool WorldToVoxel(const FVector& Pos, FIntVector& OutCell) const
	{
		if (VoxelSize <= 0.0f)
		{
			return false;
		}
		const FVector Rel = Pos - VoxelOrigin;
		OutCell = FIntVector(
			FMath::FloorToInt(Rel.X / VoxelSize),
			FMath::FloorToInt(Rel.Y / VoxelSize),
			FMath::FloorToInt(Rel.Z / VoxelSize));
		return OutCell.X >= 0 && OutCell.X < DimX && OutCell.Y >= 0 && OutCell.Y < DimY && OutCell.Z >= 0 && OutCell.Z < DimZ;
	}

	/**
	 * The field's fail-closed occupancy predicate (P1 plan: "필드 밖 = 점유"). Unbaked data and out-of-range
	 * positions are BOTH treated as occupied — a caller (the S3 window fill) that can't tell "no data" from
	 * "solid wall" apart must default to the safer of the two, exactly like the 2D field's edge-of-grid behavior.
	 */
	bool IsWorldPosOccupied(const FVector& Pos) const
	{
		if (!IsBakedVoxels())
		{
			return true;
		}
		FIntVector Cell;
		if (!WorldToVoxel(Pos, Cell))
		{
			return true;
		}
		return FPSRHoverWindow::IsOccupied(Occupancy, VoxelIndex(Cell.X, Cell.Y, Cell.Z));
	}

	/**
	 * Clears every occupancy bit whose voxel center falls inside WorldAABB (a door/destructible break — "unblock
	 * these voxels"). WorldAABB must be in the SAME space as VoxelOrigin (both runtime call sites — NotifyDoorBroken
	 * and NotifyArenaVolumeOpened — stamp UFPSRFlowFieldSubsystem::AdoptedVoxels, which is WORLD-space). Clamps to
	 * the field's own bounds first: a box that only partly overlaps, or misses entirely, touches only the voxels
	 * actually inside it and never indexes Occupancy out of range. No-op on an unbaked field. Static + pure array
	 * math (no UWorld) so it is exercised worldless by FPSRoguelite.Arena.VoxelData.
	 */
	static void ClearOccupiedAABB(FFPSRArenaVoxelData& Voxels, const FBox& WorldAABB)
	{
		if (!Voxels.IsBakedVoxels() || !WorldAABB.IsValid)
		{
			return;
		}

		const FVector RelMin = WorldAABB.Min - Voxels.VoxelOrigin;
		const FVector RelMax = WorldAABB.Max - Voxels.VoxelOrigin;
		const int32 RawMinX = FMath::FloorToInt(RelMin.X / Voxels.VoxelSize);
		const int32 RawMinY = FMath::FloorToInt(RelMin.Y / Voxels.VoxelSize);
		const int32 RawMinZ = FMath::FloorToInt(RelMin.Z / Voxels.VoxelSize);
		const int32 RawMaxX = FMath::FloorToInt(RelMax.X / Voxels.VoxelSize);
		const int32 RawMaxY = FMath::FloorToInt(RelMax.Y / Voxels.VoxelSize);
		const int32 RawMaxZ = FMath::FloorToInt(RelMax.Z / Voxels.VoxelSize);

		// Entirely outside on some axis -> nothing to clear. Checked BEFORE clamping: clamping first would fold an
		// out-of-range box onto voxel 0 (both raw bounds negative -> both clamp to 0), clearing a bit that was
		// never actually inside WorldAABB.
		if (RawMaxX < 0 || RawMinX >= Voxels.DimX || RawMaxY < 0 || RawMinY >= Voxels.DimY
			|| RawMaxZ < 0 || RawMinZ >= Voxels.DimZ)
		{
			return;
		}

		const int32 MinX = FMath::Clamp(RawMinX, 0, Voxels.DimX - 1);
		const int32 MinY = FMath::Clamp(RawMinY, 0, Voxels.DimY - 1);
		const int32 MinZ = FMath::Clamp(RawMinZ, 0, Voxels.DimZ - 1);
		const int32 MaxX = FMath::Clamp(RawMaxX, 0, Voxels.DimX - 1);
		const int32 MaxY = FMath::Clamp(RawMaxY, 0, Voxels.DimY - 1);
		const int32 MaxZ = FMath::Clamp(RawMaxZ, 0, Voxels.DimZ - 1);

		for (int32 Z = MinZ; Z <= MaxZ; ++Z)
		{
			for (int32 Y = MinY; Y <= MaxY; ++Y)
			{
				for (int32 X = MinX; X <= MaxX; ++X)
				{
					const int32 Idx = Voxels.VoxelIndex(X, Y, Z);
					Voxels.Occupancy[Idx >> 6] &= ~(1ull << (Idx & 63));
				}
			}
		}
	}
};

/**
 * One arena's baked obstacle mask (ADR 0012).
 *
 * ADR 0012 invariant 1: the mask is baked IN THE EDITOR from the level's collision and stored here; the
 * runtime only loads it. Nothing in the shipping path traces the world or generates geometry. What made the
 * editor bake acceptable where ADR 0010 rejected a world trace is that all three of 0010's objections
 * (thousands of downtraces per stage transition, the silent CellSize coarsen, the spare arena poisoning the
 * PlayerStart anchor) are objections to doing it AT RUNTIME.
 *
 * Why a plain UDataAsset and not UPrimaryDataAsset like UFPSRArenaParamsDataAsset: this asset is a hard
 * dependency of the arena actor that references it and is never looked up by id or enumerated. A primary
 * asset type can be pulled in wholesale by an Asset Manager scan rule, and each of these carries ~300 KB of
 * bulk arrays — several arenas' worth of mask loaded at startup for no reason. The params asset IS enumerated
 * (a designer picks one), so the difference is deliberate rather than an inconsistency.
 *
 * COORDINATE SYSTEM: Surface is ARENA-LOCAL (invariant 8) — the arena actor's own transform is NOT folded in.
 * Never hand Surface straight to BuildFromSurfaceData; go through BuildWorldSurface, which is the only place
 * the conversion lives. Baking world coordinates would pin the arena to one position forever, which kills
 * both reusing one arena .umap at several LevelTransforms and simply nudging a parked arena later.
 *
 * Also owns Voxels (ADR 0009 S2, FFPSRArenaVoxelData above) — the 3D occupancy bake rides this SAME asset,
 * SAME click, SAME SourceHash as Surface, on purpose (see that struct's own comment for why).
 */
UCLASS(BlueprintType)
class FPSROGUELITE_API UFPSRArenaBakeDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 구워진 표면 그래프. **아레나 로컬 좌표**다 — 런타임은 `BuildWorldSurface` 로만 읽는다. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|베이크", meta = (DisplayName = "표면 데이터(로컬)"))
	FFPSRFlowFieldSurfaceData Surface;

	/** 구워진 3D 복셀 점유(ADR 0009 S2). **아레나 로컬 좌표**다 — 런타임은 `BuildWorldVoxels` 로만 읽는다.
	 *  Surface 와 같은 클릭·같은 SourceHash 로 함께 구워진다(별도 파이프라인 아님). 비어 있을 수 있다 —
	 *  이 P1 이전에 구운 에셋의 마이그레이션 상태이며, `IsBaked()`(2D 기준)는 이것과 무관하게 참을 유지한다. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|베이크", meta = (DisplayName = "복셀 점유(로컬)"))
	FFPSRArenaVoxelData Voxels;

	/** 베이크 대상 액터 집합의 해시. 현재 레벨에서 다시 계산한 값과 다르면 **스테일**이다 — ADR 0012
	 *  5겹 검사가 전부 이 한 값을 본다. 비어 있으면 "아직 안 구움"이지 "일치"가 아니다. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|베이크", meta = (DisplayName = "소스 해시"))
	FString SourceHash;

	/** 어느 레벨에서 구웠는지. 검증기가 재해시할 대상을 찾는 데 쓴다. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|베이크", meta = (DisplayName = "소스 레벨"))
	FSoftObjectPath SourceLevel;

	/** 그 레벨 안의 어느 아레나 액터인지. 한 레벨에 아레나는 하나여야 하지만(불변식 4), 그 불변식이
	 *  깨졌을 때 조용히 엉뚱한 마스크를 쓰는 대신 검증기가 짚을 수 있어야 한다. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|베이크", meta = (DisplayName = "소스 아레나"))
	FName SourceArenaName;

	/** 베이크에 들어간 액터 수. 사람이 "이게 그럴듯한 값인가"를 1초 만에 판단하는 용도 — 0 이나 1 이면
	 *  콜리전 설정을 잘못 건드린 것이다. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|베이크", meta = (DisplayName = "소스 액터 수"))
	int32 SourceActorCount = 0;

	/** 마지막 베이크 시각(UTC). 스테일 판정은 해시가 하고, 이건 사람이 읽는 값이다. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|베이크", meta = (DisplayName = "베이크 시각(UTC)"))
	FDateTime BakedAtUtc = FDateTime(0);

	/** 열린 셀 수 / 전체 셀 수 — 검증기가 이미 세는 값을 요약으로 남긴 것. 벌크 배열은 디테일 패널에
	 *  띄울 수 없으므로(51,200 엔트리) 여기가 유일한 눈으로 보는 창이다. */
	UPROPERTY(VisibleAnywhere, Category = "아레나|베이크", meta = (DisplayName = "열린 셀"))
	int32 OpenCells = 0;

	/** 굽힌 데이터가 실제로 들어 있는가. 배열 길이까지 본다 — 필드에 UPROPERTY 를 빠뜨려 저장이
	 *  통째로 날아간 경우(ADR 0012 「감수하기로 한 것」) 치수만 보면 멀쩡해 보이기 때문이다. */
	bool IsBaked() const;

	/**
	 * 로컬 좌표 Surface 에 아레나 트랜스폼을 적용해 **월드 좌표** 사본을 만든다. 런타임이 마스크를 읽는
	 * 유일한 경로다.
	 *
	 * 평행이동만 허용한다. 셀 격자는 축정렬 AABB 이고(`AFPSRArenaActor::ContainsWorldLocation` 이 XY
	 * min/max 로만 판정한다) 회전을 먹이면 격자 인덱싱 전체가 조용히 어긋난다 — 회전이 섞인 트랜스폼은
	 * 보정하지 않고 **거부한다**. 스케일도 같은 이유로 거부한다(CellSize 와 이중 정의가 된다).
	 *
	 * @return 성공 여부. 실패 시 OutWorld 는 건드리지 않는다.
	 */
	bool BuildWorldSurface(const FTransform& ArenaTransform, FFPSRFlowFieldSurfaceData& OutWorld) const;

	/** 월드 좌표 베이크 결과를 아레나 로컬로 접어 이 에셋에 넣는다(에디터 베이커 전용).
	 *  `BuildWorldSurface` 의 정확한 역연산 — 둘이 같은 파일에 있어야 한쪽만 고쳐지지 않는다. */
	static bool LocalizeSurface(const FFPSRFlowFieldSurfaceData& World, const FTransform& ArenaTransform,
		FFPSRFlowFieldSurfaceData& OutLocal);

	/**
	 * Same conversion as `BuildWorldSurface`, for `Voxels` (ADR 0009 S2). Deliberately does NOT reuse
	 * `OffsetSurface` — voxels have no MAX_flt sentinel to skip (every entry is a bit, not a height), so only
	 * `VoxelOrigin` moves; DimX/DimY/DimZ/VoxelSize/Occupancy/FreeVoxels are coordinate-independent and copy as-is.
	 * Same rotation/scale rejection as the surface pair (`ExtractPlanarOffset`) for the same reason: the voxel
	 * grid is axis-aligned too.
	 *
	 * @return 성공 여부. 실패 시 OutWorld 는 건드리지 않는다.
	 */
	bool BuildWorldVoxels(const FTransform& ArenaTransform, FFPSRArenaVoxelData& OutWorld) const;

	/** `BuildWorldVoxels` 의 정확한 역연산(에디터 베이커 전용) — `LocalizeSurface` 와 같은 파일에 있어야
	 *  하는 이유도 같다: 한쪽만 고쳐지는 사고를 막는다. */
	static bool LocalizeVoxels(const FFPSRArenaVoxelData& World, const FTransform& ArenaTransform,
		FFPSRArenaVoxelData& OutLocal);

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

private:
	/** 두 변환의 공통 부분: 트랜스폼을 검사해 평행이동 성분만 뽑는다. */
	static bool ExtractPlanarOffset(const FTransform& ArenaTransform, FVector& OutOffset, FString& OutError);

	/** GridOrigin + CellFloorZ 에 Z 오프셋을 적용한 사본을 만든다. MAX_flt(=표면 없음) 센티넬은
	 *  절대 이동시키지 않는다 — 더하는 순간 유한값이 되어 "없던 바닥"이 생긴다. */
	static void OffsetSurface(const FFPSRFlowFieldSurfaceData& In, const FVector& Offset,
		FFPSRFlowFieldSurfaceData& Out);
};
