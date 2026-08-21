// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Enemy/FPSRHoverWindowSubsystem.h"
#include "Arena/FPSRArenaBakeDataAsset.h"

#if WITH_AUTOMATION_TESTS

// ADR 0009 P1 S3 ("부양 3D 창 런타임"): worldless proof for the runtime-facing pure math UFPSRHoverWindowSubsystem
// leans on — FFPSRArenaVoxelData::CopyWindow's row-wise fail-closed occupancy fill, and the
// FPSRHoverWindowRuntime namespace's window-recentre + gradient/SeekZ derivation. No UWorld/UObject anywhere in
// this file, matching FPSRoguelite.FlowField.HoverWindowCore's own worldless-core style (FPSRHoverWindowBenchTest.cpp).

namespace
{
	/** A DimX x DimY x DimZ global voxel field at Origin/VoxelSize, all bits clear (open) — this file sets
	 *  individual occupied bits case-by-case via FPSRHoverWindow::SetOccupied. */
	FFPSRArenaVoxelData MakeVoxelData(int32 DimX, int32 DimY, int32 DimZ, float VoxelSize, const FVector& Origin)
	{
		FFPSRArenaVoxelData V;
		V.VoxelOrigin = Origin;
		V.VoxelSize = VoxelSize;
		V.DimX = DimX;
		V.DimY = DimY;
		V.DimZ = DimZ;
		V.Occupancy.Init(0, FPSRHoverWindow::OccupancyWords(V.NumVoxels()));
		V.FreeVoxels = V.NumVoxels();
		return V;
	}

	FFPSRHoverWindowDims MakeDims(int32 DimX, int32 DimY, int32 DimZ)
	{
		FFPSRHoverWindowDims D;
		D.DimX = DimX; D.DimY = DimY; D.DimZ = DimZ;
		return D;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRHoverWindowRuntimeTest, "FPSRoguelite.FlowField.HoverWindowRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSRHoverWindowRuntimeTest::RunTest(const FString& Parameters)
{
	// ---- (1) CopyWindow: fully-inside window bit-matches the global field exactly. ----
	{
		FFPSRArenaVoxelData Global = MakeVoxelData(10, 10, 10, 150.0f, FVector::ZeroVector);
		// A handful of occupied cells inside the window we're about to copy, and one JUST outside it (must not leak in).
		FPSRHoverWindow::SetOccupied(Global.Occupancy, Global.VoxelIndex(3, 3, 3));
		FPSRHoverWindow::SetOccupied(Global.Occupancy, Global.VoxelIndex(4, 5, 4));
		FPSRHoverWindow::SetOccupied(Global.Occupancy, Global.VoxelIndex(1, 1, 1)); // outside the window below

		const FFPSRHoverWindowDims WinDims = MakeDims(4, 4, 4);
		const FIntVector MinCell(2, 2, 2); // window covers global [2,6) on every axis — fully inside [0,10)
		TArray<uint64> WinOccupancy;
		Global.CopyWindow(MinCell, WinDims, WinOccupancy);

		bool bAllMatch = true;
		for (int32 Z = 0; Z < WinDims.DimZ && bAllMatch; ++Z)
		{
			for (int32 Y = 0; Y < WinDims.DimY && bAllMatch; ++Y)
			{
				for (int32 X = 0; X < WinDims.DimX && bAllMatch; ++X)
				{
					const bool bWinOccupied = FPSRHoverWindow::IsOccupied(WinOccupancy, WinDims.CellIndex(X, Y, Z));
					const bool bGlobalOccupied = FPSRHoverWindow::IsOccupied(Global.Occupancy,
						Global.VoxelIndex(MinCell.X + X, MinCell.Y + Y, MinCell.Z + Z));
					bAllMatch = (bWinOccupied == bGlobalOccupied);
				}
			}
		}
		TestTrue(TEXT("fully-inside window bit-matches the global field for every cell"), bAllMatch);
		TestTrue(TEXT("the occupied (3,3,3) cell (window-local (1,1,1)) reads occupied"),
			FPSRHoverWindow::IsOccupied(WinOccupancy, WinDims.CellIndex(1, 1, 1)));
		TestFalse(TEXT("a cell with no occupied neighbour reads open"),
			FPSRHoverWindow::IsOccupied(WinOccupancy, WinDims.CellIndex(0, 0, 0)));
	}

	// ---- (2) CopyWindow: a window straddling the global field's boundary — in-range cells match, out-of-range
	//          cells (and rows) are fail-closed occupied. ----
	{
		FFPSRArenaVoxelData Global = MakeVoxelData(6, 6, 6, 150.0f, FVector::ZeroVector); // all open
		const FFPSRHoverWindowDims WinDims = MakeDims(4, 4, 4);
		const FIntVector MinCell(-2, 2, 4); // X: [-2,2) half outside (X<0) ; Y: [2,6) fully inside; Z: [4,8) half outside (Z>=6)
		TArray<uint64> WinOccupancy;
		Global.CopyWindow(MinCell, WinDims, WinOccupancy);

		// X in-range half (window-local X=2,3 -> global X=0,1) at a mid Y/Z that's also in range -> open (global is all-open).
		TestFalse(TEXT("straddled window: an X/Y/Z in-range cell reads open (global field is all-open)"),
			FPSRHoverWindow::IsOccupied(WinOccupancy, WinDims.CellIndex(2, 0, 1)));
		// X out-of-range half (window-local X=0,1 -> global X=-2,-1) -> fail-closed occupied regardless of Y/Z.
		TestTrue(TEXT("straddled window: an X-out-of-range cell is fail-closed occupied"),
			FPSRHoverWindow::IsOccupied(WinOccupancy, WinDims.CellIndex(0, 0, 1)));
		// Z out-of-range (window-local Z=2,3 -> global Z=6,7, DimZ=6) -> the WHOLE row is fail-closed occupied,
		// even at an X/Y that would otherwise be in range.
		TestTrue(TEXT("straddled window: a Z-out-of-range row is fail-closed occupied even at an in-range X/Y"),
			FPSRHoverWindow::IsOccupied(WinOccupancy, WinDims.CellIndex(2, 0, 2)));
	}

	// ---- (3) CopyWindow: a window entirely outside the global field -> every cell fail-closed occupied. ----
	{
		FFPSRArenaVoxelData Global = MakeVoxelData(4, 4, 4, 150.0f, FVector::ZeroVector); // all open
		const FFPSRHoverWindowDims WinDims = MakeDims(3, 3, 3);
		const FIntVector MinCell(100, 100, 100);
		TArray<uint64> WinOccupancy;
		Global.CopyWindow(MinCell, WinDims, WinOccupancy);

		bool bAllOccupied = true;
		for (int32 Cell = 0; Cell < WinDims.NumCells() && bAllOccupied; ++Cell)
		{
			bAllOccupied = FPSRHoverWindow::IsOccupied(WinOccupancy, Cell);
		}
		TestTrue(TEXT("a window entirely outside the global field is fail-closed occupied everywhere"), bAllOccupied);
	}

	// ---- (4) CopyWindow: an unbaked global field (no data at all) -> fail-closed occupied everywhere too. ----
	{
		FFPSRArenaVoxelData Unbaked; // default-constructed: DimX/Y/Z=0, Occupancy empty -> IsBakedVoxels() false
		const FFPSRHoverWindowDims WinDims = MakeDims(2, 2, 2);
		TArray<uint64> WinOccupancy;
		Unbaked.CopyWindow(FIntVector(0, 0, 0), WinDims, WinOccupancy);
		bool bAllOccupied = true;
		for (int32 Cell = 0; Cell < WinDims.NumCells() && bAllOccupied; ++Cell)
		{
			bAllOccupied = FPSRHoverWindow::IsOccupied(WinOccupancy, Cell);
		}
		TestTrue(TEXT("an unbaked global field yields a fail-closed occupied window"), bAllOccupied);
	}

	// ---- (5) ComputeWindowMinCell: a player exactly at a cell's centre snaps to that cell, then the window is
	//          re-centred so the player's cell sits at (DimX/2, DimY/2, DimZ/2) inside it. ----
	{
		const FFPSRHoverWindowDims Dims = MakeDims(64, 64, 24);
		constexpr float VoxelSize = 150.0f;
		const FVector Origin = FVector::ZeroVector;
		// Cell (32,32,12)'s centre.
		const FVector PlayerPos(32.5f * VoxelSize, 32.5f * VoxelSize, 12.5f * VoxelSize);
		const FIntVector MinCell = FPSRHoverWindowRuntime::ComputeWindowMinCell(PlayerPos, Origin, VoxelSize, Dims);
		TestEqual(TEXT("re-centred MinCell.X puts the player's cell at DimX/2"), MinCell.X, 32 - Dims.DimX / 2);
		TestEqual(TEXT("re-centred MinCell.Y puts the player's cell at DimY/2"), MinCell.Y, 32 - Dims.DimY / 2);
		TestEqual(TEXT("re-centred MinCell.Z puts the player's cell at DimZ/2"), MinCell.Z, 12 - Dims.DimZ / 2);

		// A non-zero VoxelOrigin offsets the same way (translation-only, matching FFPSRArenaVoxelData::WorldToVoxel).
		const FVector OffsetOrigin(1000.0f, -500.0f, 200.0f);
		const FVector PlayerPosOffset = PlayerPos + OffsetOrigin;
		const FIntVector MinCellOffset = FPSRHoverWindowRuntime::ComputeWindowMinCell(PlayerPosOffset, OffsetOrigin, VoxelSize, Dims);
		TestTrue(TEXT("a translated VoxelOrigin doesn't change the re-centred cell (relative math)"), MinCellOffset == MinCell);
	}

	// ---- (6) ResolveQuery: source directly ABOVE the query cell -> Direction.Z > 0 and SeekZ = the source layer's
	//          world-centre Z (the plan's "위층 소스 -> Direction Z>0, SeekZ=위층 Z" case). ----
	{
		const FFPSRHoverWindowDims Dims = MakeDims(3, 3, 3);
		constexpr float VoxelSize = 150.0f;
		const FVector WindowWorldOrigin = FVector::ZeroVector;

		FFPSRHoverWindowField Field;
		Field.Dist.Init(FPSRHoverWindow::UnreachedDist, Dims.NumCells());
		Field.StepDir.Init(0, Dims.NumCells());

		const int32 SourceCell = Dims.CellIndex(1, 1, 2); // upper layer
		const int32 AgentCell = Dims.CellIndex(1, 1, 1);  // directly below the source
		Field.Dist[SourceCell] = 0;
		Field.Dist[AgentCell] = 1;
		// StepDir[Agent] must name the offset FROM Agent back TOWARD Source: (0,0,+1). GetNeighborOffsets6() index 4
		// (0-based) is (0,0,1) — see FPSRHoverWindowCore.cpp's table — so the 1-based StepDir value is 5.
		Field.StepDir[AgentCell] = 5;

		const FVector AgentCentre((1.0f + 0.5f) * VoxelSize, (1.0f + 0.5f) * VoxelSize, (1.0f + 0.5f) * VoxelSize);
		FVector OutDirection;
		float OutSeekZ = -1.0f;
		bool bOutSeekValid = true; // deliberately pre-seeded true, so a bug that fails to reset it can't pass by accident
		const bool bResolved = FPSRHoverWindowRuntime::ResolveQuery(Dims, WindowWorldOrigin, VoxelSize, Field,
			/*EdgeMarginCells=*/0, AgentCentre, OutDirection, OutSeekZ, bOutSeekValid);

		TestTrue(TEXT("a reached, non-source cell with a valid StepDir resolves"), bResolved);
		TestEqual(TEXT("Direction points straight up toward the source layer"), OutDirection, FVector(0.0f, 0.0f, 1.0f));
		TestTrue(TEXT("a Z-changing step sets bSeekValid"), bOutSeekValid);
		TestEqual(TEXT("SeekZ is the source layer's world-centre Z (cell Z=2 -> 375cm)"), OutSeekZ, 375.0f);
	}

	// ---- (7) ResolveQuery: source on the SAME layer -> Direction.Z == 0 and bSeekValid stays false (2D terrain-
	//          follow keeps driving Z, the plan's "평지 -> bSeekValid=false" case). ----
	{
		const FFPSRHoverWindowDims Dims = MakeDims(3, 3, 3);
		constexpr float VoxelSize = 150.0f;
		const FVector WindowWorldOrigin = FVector::ZeroVector;

		FFPSRHoverWindowField Field;
		Field.Dist.Init(FPSRHoverWindow::UnreachedDist, Dims.NumCells());
		Field.StepDir.Init(0, Dims.NumCells());

		const int32 SourceCell = Dims.CellIndex(1, 0, 1);
		const int32 AgentCell = Dims.CellIndex(1, 1, 1); // same Z layer as the source, one step +Y away
		Field.Dist[SourceCell] = 0;
		Field.Dist[AgentCell] = 1;
		// StepDir[Agent] names the offset back toward Source: (0,-1,0) is GetNeighborOffsets6() index 3 (0-based) -> StepDir 4.
		Field.StepDir[AgentCell] = 4;

		const FVector AgentCentre((1.0f + 0.5f) * VoxelSize, (1.0f + 0.5f) * VoxelSize, (1.0f + 0.5f) * VoxelSize);
		FVector OutDirection;
		float OutSeekZ = 0.0f;
		bool bOutSeekValid = true;
		const bool bResolved = FPSRHoverWindowRuntime::ResolveQuery(Dims, WindowWorldOrigin, VoxelSize, Field,
			/*EdgeMarginCells=*/0, AgentCentre, OutDirection, OutSeekZ, bOutSeekValid);

		TestTrue(TEXT("a same-layer reached cell still resolves a Direction"), bResolved);
		TestEqual(TEXT("Direction.Z is exactly zero for a same-layer step"), OutDirection.Z, 0.0);
		TestFalse(TEXT("a same-layer step leaves bSeekValid false (2D terrain-follow keeps driving Z)"), bOutSeekValid);
	}

	// ---- (8) ResolveQuery: rejection cases — unreached cell, the source cell itself, and a query too close to the
	//          window's own edge (inside EdgeMarginCells) all fail closed to false. ----
	{
		const FFPSRHoverWindowDims Dims = MakeDims(4, 4, 4);
		constexpr float VoxelSize = 150.0f;
		const FVector WindowWorldOrigin = FVector::ZeroVector;

		FFPSRHoverWindowField Field;
		Field.Dist.Init(FPSRHoverWindow::UnreachedDist, Dims.NumCells());
		Field.StepDir.Init(0, Dims.NumCells());
		const int32 SourceCell = Dims.CellIndex(2, 2, 2);
		Field.Dist[SourceCell] = 0; // StepDir[Source] stays 0 (sentinel — "nowhere to step")

		FVector OutDirection;
		float OutSeekZ = 0.0f;
		bool bOutSeekValid = false;

		// Unreached cell (never touched by the synthetic Field above).
		const FVector UnreachedCentre((3.5f) * VoxelSize, (3.5f) * VoxelSize, (3.5f) * VoxelSize);
		TestFalse(TEXT("an unreached cell fails closed"),
			FPSRHoverWindowRuntime::ResolveQuery(Dims, WindowWorldOrigin, VoxelSize, Field, 0, UnreachedCentre, OutDirection, OutSeekZ, bOutSeekValid));

		// The source cell itself (StepDir==0 sentinel, nowhere to step).
		const FVector SourceCentre(2.5f * VoxelSize, 2.5f * VoxelSize, 2.5f * VoxelSize);
		TestFalse(TEXT("the source cell itself fails closed (nowhere to step)"),
			FPSRHoverWindowRuntime::ResolveQuery(Dims, WindowWorldOrigin, VoxelSize, Field, 0, SourceCentre, OutDirection, OutSeekZ, bOutSeekValid));

		// Cell (0,0,0) is reachable-in-principle (were it seeded) but falls inside a 1-cell edge margin on 4x4x4.
		Field.Dist[Dims.CellIndex(0, 0, 0)] = 5;
		Field.StepDir[Dims.CellIndex(0, 0, 0)] = 1; // any nonzero value — only the margin gate is under test here
		const FVector EdgeCentre(0.5f * VoxelSize, 0.5f * VoxelSize, 0.5f * VoxelSize);
		TestFalse(TEXT("a cell inside the edge margin fails closed even though it's reached"),
			FPSRHoverWindowRuntime::ResolveQuery(Dims, WindowWorldOrigin, VoxelSize, Field, /*EdgeMarginCells=*/1, EdgeCentre, OutDirection, OutSeekZ, bOutSeekValid));
	}

	// ---- (9) ResolveSourceCell: an occupied player cell snaps to the nearest free cell in the same XY column,
	//          preferring the closer of up/down and ties broken upward; an all-occupied column fails closed. ----
	{
		const FFPSRHoverWindowDims Dims = MakeDims(3, 3, 5);
		TArray<uint64> Occupancy;
		Occupancy.Init(0, FPSRHoverWindow::OccupancyWords(Dims.NumCells()));

		// Column (1,1): occupy Z=2 (the player's own cell) and Z=1 (one below) — nearest free is Z=3 (one above).
		FPSRHoverWindow::SetOccupied(Occupancy, Dims.CellIndex(1, 1, 2));
		FPSRHoverWindow::SetOccupied(Occupancy, Dims.CellIndex(1, 1, 1));

		int32 OutCell = INDEX_NONE;
		const bool bResolved = FPSRHoverWindowRuntime::ResolveSourceCell(Dims, Occupancy, FIntVector(1, 1, 2), OutCell);
		TestTrue(TEXT("an occupied player cell with a nearby free cell resolves"), bResolved);
		TestEqual(TEXT("resolves to the nearest free cell in the column (Z=3, one above)"), OutCell, Dims.CellIndex(1, 1, 3));

		// Fully-occupied column (1,2) across the whole DimZ=5 -> no free cell anywhere -> fails closed.
		for (int32 Z = 0; Z < Dims.DimZ; ++Z)
		{
			FPSRHoverWindow::SetOccupied(Occupancy, Dims.CellIndex(1, 2, Z));
		}
		int32 OutCell2 = INDEX_NONE;
		TestFalse(TEXT("a fully-occupied column fails closed (no source to seed)"),
			FPSRHoverWindowRuntime::ResolveSourceCell(Dims, Occupancy, FIntVector(1, 2, 2), OutCell2));
	}

	return true;
}

#endif // WITH_AUTOMATION_TESTS
