// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Arena/FPSRArenaBakeDataAsset.h"
#include "Enemy/FPSRHoverWindowCore.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_AUTOMATION_TESTS

// ADR 0009 P1 (S2 "전역 3D 복셀 오프라인 베이크"): worldless proof for FFPSRArenaVoxelData — the axis-order contract
// it shares with FFPSRHoverWindowDims, the Build/Localize world<->local round trip (mirroring
// FPSRArenaBakeDataAsset.h's Surface pair), the fail-closed occupancy query, and the AABB stamping helper the
// runtime door/destructible break paths use in place. No UWorld anywhere in this file — every case here is pure
// array math, matching FPSRoguelite.FlowField.HoverFloorSample's style.

namespace
{
	/** A DimX x DimY x DimZ voxel field at Origin/VoxelSize, all bits either set or clear (never a mix) — this file
	 *  builds the mixed states it needs case-by-case via explicit SetOccupied calls instead. */
	FFPSRArenaVoxelData MakeVoxelData(int32 DimX, int32 DimY, int32 DimZ, float VoxelSize, const FVector& Origin, bool bAllOccupied)
	{
		FFPSRArenaVoxelData V;
		V.VoxelOrigin = Origin;
		V.VoxelSize = VoxelSize;
		V.DimX = DimX;
		V.DimY = DimY;
		V.DimZ = DimZ;
		V.Occupancy.Init(0, FPSRHoverWindow::OccupancyWords(V.NumVoxels()));
		V.FreeVoxels = V.NumVoxels();
		if (bAllOccupied)
		{
			for (int32 Z = 0; Z < DimZ; ++Z)
			{
				for (int32 Y = 0; Y < DimY; ++Y)
				{
					for (int32 X = 0; X < DimX; ++X)
					{
						FPSRHoverWindow::SetOccupied(V.Occupancy, V.VoxelIndex(X, Y, Z));
					}
				}
			}
			V.FreeVoxels = 0;
		}
		return V;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRArenaVoxelDataTest, "FPSRoguelite.Arena.VoxelData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRArenaVoxelDataTest::RunTest(const FString& Parameters)
{
	// ---- (1) VoxelIndex axis order MUST match FFPSRHoverWindowDims::CellIndex exactly — this is the precondition
	//          the class comment calls out: a mismatch here would silently transpose every S3 window fill. ----
	{
		FFPSRHoverWindowDims Dims;
		Dims.DimX = 4;
		Dims.DimY = 3;
		Dims.DimZ = 2;

		FFPSRArenaVoxelData V = MakeVoxelData(Dims.DimX, Dims.DimY, Dims.DimZ, 150.0f, FVector::ZeroVector, false);

		bool bAllMatch = true;
		for (int32 Z = 0; Z < Dims.DimZ && bAllMatch; ++Z)
		{
			for (int32 Y = 0; Y < Dims.DimY && bAllMatch; ++Y)
			{
				for (int32 X = 0; X < Dims.DimX && bAllMatch; ++X)
				{
					if (V.VoxelIndex(X, Y, Z) != Dims.CellIndex(X, Y, Z))
					{
						bAllMatch = false;
					}
				}
			}
		}
		TestTrue(TEXT("VoxelIndex matches FFPSRHoverWindowDims::CellIndex for every (X,Y,Z) in a 4x3x2 grid"), bAllMatch);
	}

	// ---- (2) Build/Localize round trip: a translation-only arena transform must round-trip VoxelOrigin exactly;
	//          rotation/scale must be rejected by BOTH directions (same ExtractPlanarOffset the Surface pair uses). ----
	{
		TStrongObjectPtr<UFPSRArenaBakeDataAsset> Asset(NewObject<UFPSRArenaBakeDataAsset>());
		Asset->Voxels = MakeVoxelData(2, 2, 2, 150.0f, FVector(10.0f, 20.0f, -600.0f), /*bAllOccupied=*/true);
		Asset->Voxels.Occupancy[0] &= ~1ull; // clear voxel (0,0,0) so we have a mixed field to round-trip too
		Asset->Voxels.FreeVoxels = 1;

		const FTransform Translation(FRotator::ZeroRotator, FVector(5000.0f, -2500.0f, 100.0f), FVector::OneVector);

		FFPSRArenaVoxelData World;
		TestTrue(TEXT("BuildWorldVoxels succeeds for a translation-only transform"), Asset->BuildWorldVoxels(Translation, World));
		TestTrue(TEXT("BuildWorldVoxels offsets VoxelOrigin by the transform's location"),
			World.VoxelOrigin.Equals(Asset->Voxels.VoxelOrigin + Translation.GetLocation(), 1e-3f));
		TestEqual(TEXT("BuildWorldVoxels preserves DimX/DimY/DimZ"), World.DimX * World.DimY * World.DimZ, 8);
		TestTrue(TEXT("BuildWorldVoxels preserves Occupancy verbatim (coordinate-independent)"),
			World.Occupancy[0] == Asset->Voxels.Occupancy[0]);

		FFPSRArenaVoxelData RoundTrip;
		TestTrue(TEXT("LocalizeVoxels succeeds for the same translation-only transform"),
			UFPSRArenaBakeDataAsset::LocalizeVoxels(World, Translation, RoundTrip));
		TestTrue(TEXT("Localize(BuildWorld(X)) restores the original VoxelOrigin"),
			RoundTrip.VoxelOrigin.Equals(Asset->Voxels.VoxelOrigin, 1e-3f));
		TestTrue(TEXT("round trip preserves Occupancy"), RoundTrip.Occupancy[0] == Asset->Voxels.Occupancy[0]);
		TestEqual(TEXT("round trip preserves FreeVoxels"), RoundTrip.FreeVoxels, Asset->Voxels.FreeVoxels);

		// Rotation and scale are rejected outright (ExtractPlanarOffset) — same as BuildWorldSurface/LocalizeSurface.
		// The rejection logs at Error level by design (an authored arena with rotation/scale is a content bug that
		// must be loud) — declare those errors EXPECTED here, or the automation framework fails the test on the
		// very log lines this case exists to provoke. Two per transform kind: BuildWorldVoxels + LocalizeVoxels.
		AddExpectedError(TEXT("회전이 있다"), EAutomationExpectedErrorFlags::Contains, 2);
		AddExpectedError(TEXT("스케일이 있다"), EAutomationExpectedErrorFlags::Contains, 2);
		const FTransform Rotated(FRotator(0.0f, 45.0f, 0.0f), FVector::ZeroVector, FVector::OneVector);
		FFPSRArenaVoxelData Unused;
		TestFalse(TEXT("BuildWorldVoxels rejects a rotated arena transform"), Asset->BuildWorldVoxels(Rotated, Unused));
		TestFalse(TEXT("LocalizeVoxels rejects a rotated arena transform"),
			UFPSRArenaBakeDataAsset::LocalizeVoxels(World, Rotated, Unused));

		const FTransform Scaled(FRotator::ZeroRotator, FVector::ZeroVector, FVector(2.0f, 1.0f, 1.0f));
		TestFalse(TEXT("BuildWorldVoxels rejects a scaled arena transform"), Asset->BuildWorldVoxels(Scaled, Unused));
		TestFalse(TEXT("LocalizeVoxels rejects a scaled arena transform"),
			UFPSRArenaBakeDataAsset::LocalizeVoxels(World, Scaled, Unused));

		// An unbaked asset (no Voxels ever assigned) refuses to build a world copy at all.
		TStrongObjectPtr<UFPSRArenaBakeDataAsset> Empty(NewObject<UFPSRArenaBakeDataAsset>());
		TestFalse(TEXT("BuildWorldVoxels fails on an asset with no baked voxels"), Empty->BuildWorldVoxels(Translation, Unused));
	}

	// ---- (3) IsWorldPosOccupied: a set bit reads true, an unset bit reads false, and — the P1 plan's explicit
	//          fail-closed rule — anything outside the field's own bounds reads true (occupied), never false. ----
	{
		const float Size = 150.0f;
		const FVector Origin(0.0f, 0.0f, -600.0f);
		FFPSRArenaVoxelData V = MakeVoxelData(3, 3, 3, Size, Origin, /*bAllOccupied=*/false);
		FPSRHoverWindow::SetOccupied(V.Occupancy, V.VoxelIndex(1, 1, 1)); // one solid voxel in the middle

		const FVector CenterOfSolidVoxel = Origin + FVector((1 + 0.5f) * Size, (1 + 0.5f) * Size, (1 + 0.5f) * Size);
		TestTrue(TEXT("a set voxel reads occupied"), V.IsWorldPosOccupied(CenterOfSolidVoxel));

		const FVector CenterOfEmptyVoxel = Origin + FVector((0 + 0.5f) * Size, (0 + 0.5f) * Size, (0 + 0.5f) * Size);
		TestFalse(TEXT("an unset voxel reads unoccupied"), V.IsWorldPosOccupied(CenterOfEmptyVoxel));

		const FVector FarOutside = Origin - FVector(10000.0f, 10000.0f, 10000.0f);
		TestTrue(TEXT("a position outside the field is fail-closed occupied"), V.IsWorldPosOccupied(FarOutside));

		// An unbaked field (dims 0) is likewise fail-closed occupied everywhere, including at its own default origin.
		FFPSRArenaVoxelData Unbaked;
		TestTrue(TEXT("an unbaked field is fail-closed occupied"), Unbaked.IsWorldPosOccupied(FVector::ZeroVector));
	}

	// ---- (4) ClearOccupiedAABB: a box straddling the field's boundary clears only the voxels actually inside it —
	//          neither reads/writes past Occupancy's bounds nor folds an entirely-outside box onto voxel 0. ----
	{
		// 4 voxels along X (100cm cells), single row/column on Y/Z. All 4 start solid.
		FFPSRArenaVoxelData V = MakeVoxelData(4, 1, 1, 100.0f, FVector::ZeroVector, /*bAllOccupied=*/true);
		const auto IsSet = [&V](int32 X) { return FPSRHoverWindow::IsOccupied(V.Occupancy, V.VoxelIndex(X, 0, 0)); };

		// (4a) A box entirely BELOW the grid on X (max corner still negative) must be a strict no-op — this is the
		// exact case the ClearOccupiedAABB header comment warns about: clamp-before-check would fold both raw
		// bounds to 0 and spuriously clear voxel 0.
		FFPSRArenaVoxelData::ClearOccupiedAABB(V, FBox(FVector(-500.0f, -50.0f, -50.0f), FVector(-100.0f, 50.0f, 50.0f)));
		TestTrue(TEXT("an AABB entirely outside the grid does not clear voxel 0"), IsSet(0));
		TestTrue(TEXT("...nor any other voxel"), IsSet(1) && IsSet(2) && IsSet(3));

		// (4b) A box straddling the MIN boundary (X in [-50,150)) overlaps voxel 0 fully and voxel 1 partially —
		// both should clear; voxels 2 and 3 (untouched) must remain set.
		FFPSRArenaVoxelData::ClearOccupiedAABB(V, FBox(FVector(-50.0f, -50.0f, -50.0f), FVector(150.0f, 50.0f, 50.0f)));
		TestFalse(TEXT("straddling box clears the fully-inside voxel"), IsSet(0));
		TestFalse(TEXT("straddling box clears the partially-overlapped voxel"), IsSet(1));
		TestTrue(TEXT("straddling box leaves untouched voxels set"), IsSet(2) && IsSet(3));

		// (4c) An invalid box is a no-op (defensive — callers pass GetActorBounds()-derived boxes, which are
		// always valid, but the helper must not crash on a degenerate one).
		FFPSRArenaVoxelData::ClearOccupiedAABB(V, FBox(ForceInit));
		TestTrue(TEXT("an invalid AABB is a no-op"), IsSet(2) && IsSet(3));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
