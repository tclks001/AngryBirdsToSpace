// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAG5BSemanticEnvelope.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73DAGTypes.h"
#include "Building/ABTSM73StructureData.h"
#include "Misc/Crc.h"

namespace
{
	using FTileMask = uint32;

	constexpr int32 TileCount =
		static_cast<int32>(EABTSM73DAG5BSemanticCell::Cantilever) + 1;

	FTileMask TileBit(const EABTSM73DAG5BSemanticCell Tile)
	{
		return 1u << static_cast<uint32>(Tile);
	}

	bool IsSingleton(const FTileMask Domain)
	{
		return Domain != 0u && (Domain & (Domain - 1u)) == 0u;
	}

	int32 DomainCount(const FTileMask Domain)
	{
		return FMath::CountBits(Domain);
	}

	EABTSM73DAG5BSemanticCell FirstTile(const FTileMask Domain)
	{
		for (int32 Index = 0; Index < TileCount; ++Index)
		{
			if ((Domain & (1u << Index)) != 0u)
			{
				return static_cast<EABTSM73DAG5BSemanticCell>(Index);
			}
		}
		return EABTSM73DAG5BSemanticCell::Void;
	}

	FString FamilyName(const EABTSM73DAG5BShapeFamily Family)
	{
		switch (Family)
		{
		case EABTSM73DAG5BShapeFamily::SetbackTower:
			return TEXT("SetbackTower");
		case EABTSM73DAG5BShapeFamily::OffsetBridge:
			return TEXT("OffsetBridge");
		case EABTSM73DAG5BShapeFamily::ThroughOpeningWall:
			return TEXT("ThroughOpeningWall");
		case EABTSM73DAG5BShapeFamily::OneSidedHighTower:
			return TEXT("OneSidedHighTower");
		default:
			return TEXT("Auto");
		}
	}

	EABTSM73DAG5BPort PortsForTile(
		const EABTSM73DAG5BSemanticCell Tile)
	{
		switch (Tile)
		{
		case EABTSM73DAG5BSemanticCell::Foundation:
			return EABTSM73DAG5BPort::TopLoad
				| EABTSM73DAG5BPort::BottomSupport;
		case EABTSM73DAG5BSemanticCell::FloorCarrier:
		case EABTSM73DAG5BSemanticCell::ColumnZone:
		case EABTSM73DAG5BSemanticCell::WallPier:
			return EABTSM73DAG5BPort::TopLoad
				| EABTSM73DAG5BPort::BottomSupport;
		case EABTSM73DAG5BSemanticCell::Frame:
			return EABTSM73DAG5BPort::Frame;
		case EABTSM73DAG5BSemanticCell::DoorVoid:
		case EABTSM73DAG5BSemanticCell::WindowVoid:
			return EABTSM73DAG5BPort::AttackClearance;
		case EABTSM73DAG5BSemanticCell::BeamZone:
			return EABTSM73DAG5BPort::LeftBeam
				| EABTSM73DAG5BPort::RightBeam
				| EABTSM73DAG5BPort::Bridge;
		case EABTSM73DAG5BSemanticCell::Cantilever:
			return EABTSM73DAG5BPort::TopLoad
				| EABTSM73DAG5BPort::BottomSupport
				| EABTSM73DAG5BPort::Bridge;
		default:
			return EABTSM73DAG5BPort::None;
		}
	}

	bool IsVoidTile(const EABTSM73DAG5BSemanticCell Tile)
	{
		return Tile == EABTSM73DAG5BSemanticCell::Void
			|| Tile == EABTSM73DAG5BSemanticCell::DoorVoid
			|| Tile == EABTSM73DAG5BSemanticCell::WindowVoid;
	}

	bool IsVerticallyCompatible(
		const EABTSM73DAG5BSemanticCell Lower,
		const EABTSM73DAG5BSemanticCell Upper)
	{
		if (Upper == EABTSM73DAG5BSemanticCell::Foundation)
		{
			return Lower == EABTSM73DAG5BSemanticCell::Foundation;
		}
		if (Lower == EABTSM73DAG5BSemanticCell::Roof)
		{
			return Upper == EABTSM73DAG5BSemanticCell::Roof
				|| IsVoidTile(Upper);
		}
		if (IsVoidTile(Lower))
		{
			return IsVoidTile(Upper)
				|| Upper == EABTSM73DAG5BSemanticCell::Frame
				|| Upper == EABTSM73DAG5BSemanticCell::BeamZone
				|| Upper == EABTSM73DAG5BSemanticCell::Roof
				|| Upper == EABTSM73DAG5BSemanticCell::Cantilever;
		}
		return true;
	}

	bool IsCompatible(
		const EABTSM73DAG5BSemanticCell A,
		const EABTSM73DAG5BSemanticCell B,
		const FIntVector& Direction)
	{
		if (Direction.Z > 0) return IsVerticallyCompatible(A, B);
		if (Direction.Z < 0) return IsVerticallyCompatible(B, A);
		// Horizontal adjacency is intentionally broad. Shape Grammar owns the
		// silhouette; WFC resolves local facade/frame vocabulary inside it.
		return !(A == EABTSM73DAG5BSemanticCell::Foundation
			&& B == EABTSM73DAG5BSemanticCell::Roof)
			&& !(B == EABTSM73DAG5BSemanticCell::Foundation
				&& A == EABTSM73DAG5BSemanticCell::Roof);
	}

	uint32 CoordinateHash(const int32 Seed, const FIntVector& Coordinate)
	{
		uint32 Hash = HashCombineFast(
			static_cast<uint32>(Seed),
			static_cast<uint32>(Coordinate.X + 0x100));
		Hash = HashCombineFast(Hash, static_cast<uint32>(Coordinate.Y + 0x200));
		return HashCombineFast(
			Hash,
			static_cast<uint32>(Coordinate.Z + 0x400));
	}

	int32 GridIndex(const FIntVector& P, const FIntVector& Size)
	{
		return P.X + P.Y * Size.X + P.Z * Size.X * Size.Y;
	}

	bool IsInside(const FIntVector& P, const FIntVector& Size)
	{
		return P.X >= 0 && P.X < Size.X
			&& P.Y >= 0 && P.Y < Size.Y
			&& P.Z >= 0 && P.Z < Size.Z;
	}

	struct FGrammarCell
	{
		FTileMask Domain = 0u;
		bool bHardAnchor = false;
		FString Path;
	};

	void MakeHard(
		FGrammarCell& Cell,
		const EABTSM73DAG5BSemanticCell Tile,
		const FString& Path)
	{
		Cell.Domain = TileBit(Tile);
		Cell.bHardAnchor = true;
		Cell.Path = Path;
	}

	void MakeChoice(
		FGrammarCell& Cell,
		const FTileMask Domain,
		const FString& Path)
	{
		Cell.Domain = Domain;
		Cell.bHardAnchor = false;
		Cell.Path = Path;
	}

	bool IntersectsShapeScope(
		const FIntVector& P,
		const FIntVector& Size,
		const FABTSM73SemanticEnvelope& ShapeEnvelope)
	{
		const FVector GridMin(
			-0.5f + static_cast<float>(P.X)
				/ static_cast<float>(Size.X),
			-0.5f + static_cast<float>(P.Y)
				/ static_cast<float>(Size.Y),
			static_cast<float>(P.Z)
				/ static_cast<float>(Size.Z));
		const FVector GridMax(
			-0.5f + static_cast<float>(P.X + 1)
				/ static_cast<float>(Size.X),
			-0.5f + static_cast<float>(P.Y + 1)
				/ static_cast<float>(Size.Y),
			static_cast<float>(P.Z + 1)
				/ static_cast<float>(Size.Z));
		for (const FABTSM73DAG5BShapeScope& Scope :
			ShapeEnvelope.ShapeScopes)
		{
			if (!Scope.NormalizedBounds.IsValid) continue;
			const FVector OverlapMin(
				FMath::Max(GridMin.X, Scope.NormalizedBounds.Min.X),
				FMath::Max(GridMin.Y, Scope.NormalizedBounds.Min.Y),
				FMath::Max(GridMin.Z, Scope.NormalizedBounds.Min.Z));
			const FVector OverlapMax(
				FMath::Min(GridMax.X, Scope.NormalizedBounds.Max.X),
				FMath::Min(GridMax.Y, Scope.NormalizedBounds.Max.Y),
				FMath::Min(GridMax.Z, Scope.NormalizedBounds.Max.Z));
			if (OverlapMax.X > OverlapMin.X + KINDA_SMALL_NUMBER
				&& OverlapMax.Y > OverlapMin.Y + KINDA_SMALL_NUMBER
				&& OverlapMax.Z > OverlapMin.Z + KINDA_SMALL_NUMBER)
			{
				return true;
			}
		}
		return false;
	}

	void AuthorGrammarCell(
		const EABTSM73DAG5BShapeFamily Family,
		const FIntVector& P,
		const FIntVector& Size,
		const FABTSM73SemanticEnvelope& ShapeEnvelope,
		FGrammarCell& OutCell)
	{
		// Shape Grammar owns the admissible coarse mass. WFC only resolves
		// vocabulary inside the rasterized scopes and can never recreate
		// geometry outside them.
		if (!IntersectsShapeScope(P, Size, ShapeEnvelope))
		{
			MakeHard(
				OutCell,
				EABTSM73DAG5BSemanticCell::Void,
				TEXT("Shape/ScopeRaster/ExteriorVoid"));
			return;
		}
		const int32 CenterX = Size.X / 2;
		const int32 Quarter = FMath::Max(1, Size.X / 4);
		const int32 BridgeZ = FMath::Clamp(Size.Z / 2, 1, Size.Z - 2);
		const int32 LowRoofZ = FMath::Clamp(Size.Z / 2, 1, Size.Z - 2);
		const FTileMask StructuralChoices =
			TileBit(EABTSM73DAG5BSemanticCell::ColumnZone)
			| TileBit(EABTSM73DAG5BSemanticCell::WallPier);
		const FTileMask FacadeChoices =
			StructuralChoices
			| TileBit(EABTSM73DAG5BSemanticCell::WindowVoid)
			| TileBit(EABTSM73DAG5BSemanticCell::Frame);

		switch (Family)
		{
		case EABTSM73DAG5BShapeFamily::SetbackTower:
		{
			const int32 Shrink = (P.Z * (Size.X / 2))
				/ FMath::Max(1, Size.Z * 2);
			const int32 HalfWidth = FMath::Max(1, CenterX - Shrink);
			const bool bInside = FMath::Abs(P.X - CenterX) <= HalfWidth;
			if (!bInside)
			{
				MakeHard(
					OutCell,
					EABTSM73DAG5BSemanticCell::Void,
					TEXT("Mass/Setback/ExteriorVoid"));
			}
			else if (P.Z == 0)
			{
				MakeHard(
					OutCell,
					EABTSM73DAG5BSemanticCell::Foundation,
					TEXT("Mass/Foundation"));
			}
			else if (P.Z == Size.Z - 1)
			{
				MakeHard(
					OutCell,
					EABTSM73DAG5BSemanticCell::Roof,
					TEXT("Mass/Setback/Crown"));
			}
			else
			{
				MakeChoice(
					OutCell,
					FMath::Abs(P.X - CenterX) <= 1
						? StructuralChoices
						: FacadeChoices,
					TEXT("Mass/Setback/FacadeBay"));
			}
			break;
		}
		case EABTSM73DAG5BShapeFamily::OffsetBridge:
		{
			const bool bLeft = FMath::Abs(P.X - Quarter) <= 1;
			const bool bRight =
				FMath::Abs(P.X - (Size.X - 1 - Quarter)) <= 1;
			const bool bBridge = P.Z == BridgeZ
				&& P.X >= Quarter
				&& P.X <= Size.X - 1 - Quarter;
			if (P.Z == 0 && (bLeft || bRight))
			{
				MakeHard(
					OutCell,
					EABTSM73DAG5BSemanticCell::Foundation,
					TEXT("Split/Foundation"));
			}
			else if (bBridge)
			{
				MakeHard(
					OutCell,
					EABTSM73DAG5BSemanticCell::BeamZone,
					TEXT("Split/Bridge"));
			}
			else if (P.Z == Size.Z - 1
				&& P.X >= CenterX
				&& P.X <= CenterX + 1)
			{
				MakeHard(
					OutCell,
					EABTSM73DAG5BSemanticCell::Roof,
					TEXT("Split/OffsetCrown/Roof"));
			}
			else if (bLeft || bRight)
			{
				MakeChoice(
					OutCell,
					(P.X >= Quarter
							&& P.X <= Quarter + 1)
							|| (P.X >= Size.X - 2 - Quarter
								&& P.X <= Size.X - 1 - Quarter)
						? StructuralChoices
						: FacadeChoices,
					bLeft
						? TEXT("Split/LeftTower")
						: TEXT("Split/RightTower"));
			}
			else if (P.Z > BridgeZ
				&& P.X >= CenterX
				&& P.X <= CenterX + 1)
			{
				MakeChoice(
					OutCell,
					StructuralChoices,
					TEXT("Split/OffsetCrown/Core"));
			}
			else
			{
				MakeHard(
					OutCell,
					EABTSM73DAG5BSemanticCell::Void,
					TEXT("Split/BridgeClearance"));
			}
			break;
		}
		case EABTSM73DAG5BShapeFamily::ThroughOpeningWall:
		{
			const bool bOpening = FMath::Abs(P.X - CenterX) <= 1
				&& P.Z > 0
				&& P.Z < BridgeZ;
			if (P.Z == 0)
			{
				MakeHard(
					OutCell,
					EABTSM73DAG5BSemanticCell::Foundation,
					TEXT("Wall/Foundation"));
			}
			else if (bOpening)
			{
				MakeHard(
					OutCell,
					EABTSM73DAG5BSemanticCell::DoorVoid,
					TEXT("Wall/CarveThroughOpening"));
			}
			else if (P.Z == BridgeZ)
			{
				MakeHard(
					OutCell,
					EABTSM73DAG5BSemanticCell::BeamZone,
					TEXT("Wall/OpeningLintel"));
			}
			else if (P.Z == Size.Z - 2
				&& P.Y == 0
				&& (P.X <= Quarter
					|| P.X >= Size.X - 1 - Quarter))
			{
				MakeHard(
					OutCell,
					EABTSM73DAG5BSemanticCell::Roof,
					TEXT("Wall/LowCrown"));
			}
			else if (P.Z == Size.Z - 1 && P.X < CenterX)
			{
				MakeHard(
					OutCell,
					EABTSM73DAG5BSemanticCell::Roof,
					TEXT("Wall/AsymmetricCrown"));
			}
			else if (P.Z == Size.Z - 1)
			{
				MakeHard(
					OutCell,
					EABTSM73DAG5BSemanticCell::Void,
					TEXT("Wall/RooflineVoid"));
			}
			else
			{
				MakeChoice(
					OutCell,
					P.Z > BridgeZ
						? (P.X != CenterX
							? StructuralChoices
							: FacadeChoices)
						: (P.X <= Quarter
								|| P.X >= Size.X - 1 - Quarter
						? StructuralChoices
						: FacadeChoices),
					TEXT("Wall/FrameBay"));
			}
			break;
		}
		case EABTSM73DAG5BShapeFamily::OneSidedHighTower:
		default:
		{
			const bool bHighSide = P.X <= CenterX;
			const bool bLowSide = P.X > CenterX && P.Z <= LowRoofZ;
			const bool bCantileverCrown =
				P.Z == Size.Z - 1
				&& P.X <= CenterX + 1;
			if (P.Z == 0)
			{
				MakeHard(
					OutCell,
					EABTSM73DAG5BSemanticCell::Foundation,
					TEXT("Asymmetric/Foundation"));
			}
			else if (bCantileverCrown)
			{
				MakeHard(
					OutCell,
					EABTSM73DAG5BSemanticCell::Cantilever,
					TEXT("Asymmetric/HighCrown/Cantilever"));
			}
			else if (bLowSide && P.Z == LowRoofZ)
			{
				MakeHard(
					OutCell,
					EABTSM73DAG5BSemanticCell::Roof,
					TEXT("Asymmetric/LowCrown"));
			}
			else if (bHighSide || bLowSide)
			{
				MakeChoice(
					OutCell,
					(P.X >= Quarter
							&& P.X <= Quarter + 1)
							|| (P.X >= Size.X - 2 - Quarter
								&& P.X <= Size.X - 1 - Quarter)
						? StructuralChoices
						: FacadeChoices,
					bHighSide
						? TEXT("Asymmetric/HighTower")
						: TEXT("Asymmetric/LowTower"));
			}
			else
			{
				MakeHard(
					OutCell,
					EABTSM73DAG5BSemanticCell::Void,
					TEXT("Asymmetric/HeightVoid"));
			}
			break;
		}
		}
	}

	bool Propagate(
		const FIntVector& Size,
		TArray<FTileMask>& InOutDomains,
		const int32 MaxOperations,
		int32& InOutOperationCount)
	{
		static const FIntVector Directions[] = {
			FIntVector(1, 0, 0),
			FIntVector(-1, 0, 0),
			FIntVector(0, 1, 0),
			FIntVector(0, -1, 0),
			FIntVector(0, 0, 1),
			FIntVector(0, 0, -1)
		};
		TArray<int32> Queue;
		Queue.Reserve(InOutDomains.Num());
		for (int32 Index = 0; Index < InOutDomains.Num(); ++Index)
		{
			Queue.Add(Index);
		}
		int32 Cursor = 0;
		while (Cursor < Queue.Num())
		{
			const int32 Index = Queue[Cursor++];
			const int32 LayerSize = Size.X * Size.Y;
			const FIntVector P(
				Index % Size.X,
				(Index / Size.X) % Size.Y,
				Index / LayerSize);
			const FTileMask Domain = InOutDomains[Index];
			for (const FIntVector& Direction : Directions)
			{
				if (++InOutOperationCount > MaxOperations) return false;
				const FIntVector Neighbor = P + Direction;
				if (!IsInside(Neighbor, Size)) continue;
				const int32 NeighborIndex = GridIndex(Neighbor, Size);
				const FTileMask NeighborDomain =
					InOutDomains[NeighborIndex];
				FTileMask Supported = 0u;
				for (int32 BIndex = 0; BIndex < TileCount; ++BIndex)
				{
					const FTileMask BBit = 1u << BIndex;
					if ((NeighborDomain & BBit) == 0u) continue;
					const EABTSM73DAG5BSemanticCell B =
						static_cast<EABTSM73DAG5BSemanticCell>(BIndex);
					bool bHasSupport = false;
					for (int32 AIndex = 0; AIndex < TileCount; ++AIndex)
					{
						if ((Domain & (1u << AIndex)) == 0u) continue;
						const EABTSM73DAG5BSemanticCell A =
							static_cast<EABTSM73DAG5BSemanticCell>(AIndex);
						if (IsCompatible(A, B, Direction))
						{
							bHasSupport = true;
							break;
						}
					}
					if (bHasSupport) Supported |= BBit;
				}
				if (Supported == 0u) return false;
				if (Supported != NeighborDomain)
				{
					InOutDomains[NeighborIndex] = Supported;
					Queue.Add(NeighborIndex);
				}
			}
		}
		return true;
	}

	enum class EWFCSolveStatus : uint8
	{
		Solved,
		Contradiction,
		BudgetExceeded
	};

	EWFCSolveStatus SolveWFCDecisionTree(
		const FABTSM73DAG5BSettings& Settings,
		const int32 Seed,
		const FIntVector& Size,
		TArray<FTileMask>& InOutDomains,
		TArray<FString>& InOutTrace,
		int32& InOutOperations,
		int32& InOutBacktracks,
		FString& OutError)
	{
		int32 SelectedIndex = INDEX_NONE;
		int32 BestEntropy = MAX_int32;
		uint32 BestTie = MAX_uint32;
		for (int32 Index = 0; Index < InOutDomains.Num(); ++Index)
		{
			const int32 Entropy = DomainCount(InOutDomains[Index]);
			if (Entropy == 0)
			{
				return EWFCSolveStatus::Contradiction;
			}
			if (Entropy == 1) continue;
			const int32 LayerSize = Size.X * Size.Y;
			const FIntVector P(
				Index % Size.X,
				(Index / Size.X) % Size.Y,
				Index / LayerSize);
			const uint32 Tie = CoordinateHash(Seed, P);
			if (Entropy < BestEntropy
				|| (Entropy == BestEntropy && Tie < BestTie))
			{
				SelectedIndex = Index;
				BestEntropy = Entropy;
				BestTie = Tie;
			}
		}
		if (SelectedIndex == INDEX_NONE)
		{
			// A resolved facade cannot degenerate into an all-column/all-wall
			// fill. This bounded completion rule makes Frame a real WFC
			// vocabulary requirement and forces DFS to reconsider an earlier
			// locally valid choice when the completed facade has no opening
			// frame.
			const bool bHasFacadeFrame =
				InOutDomains.ContainsByPredicate(
					[](const FTileMask Domain)
					{
						return IsSingleton(Domain)
							&& FirstTile(Domain)
								== EABTSM73DAG5BSemanticCell::Frame;
					});
			if (!bHasFacadeFrame)
			{
				return EWFCSolveStatus::Contradiction;
			}
			return EWFCSolveStatus::Solved;
		}

		const int32 LayerSize = Size.X * Size.Y;
		const FIntVector P(
			SelectedIndex % Size.X,
			(SelectedIndex / Size.X) % Size.Y,
			SelectedIndex / LayerSize);
		const FTileMask OriginalDomain = InOutDomains[SelectedIndex];
		TArray<EABTSM73DAG5BSemanticCell> Choices;
		for (int32 TileIndex = 0; TileIndex < TileCount; ++TileIndex)
		{
			if ((OriginalDomain & (1u << TileIndex)) != 0u)
			{
				Choices.Add(
					static_cast<EABTSM73DAG5BSemanticCell>(
						TileIndex));
			}
		}
		const int32 Rotation = Choices.IsEmpty()
			? 0
			: static_cast<int32>(
				CoordinateHash(Seed ^ 0x5b17, P)
				% static_cast<uint32>(Choices.Num()));
		for (int32 ChoiceOffset = 0;
			ChoiceOffset < Choices.Num();
			++ChoiceOffset)
		{
			const EABTSM73DAG5BSemanticCell Choice =
				Choices[(Rotation + ChoiceOffset) % Choices.Num()];
			TArray<FTileMask> TrialDomains = InOutDomains;
			TrialDomains[SelectedIndex] = TileBit(Choice);
			if (!Propagate(
				Size,
				TrialDomains,
				Settings.MaxWFCPropagationOperations,
				InOutOperations))
			{
				if (InOutOperations
					> Settings.MaxWFCPropagationOperations)
				{
					OutError =
						TEXT("DAG5BWFCPropagationBudgetExceeded");
					return EWFCSolveStatus::BudgetExceeded;
				}
				++InOutBacktracks;
				if (InOutBacktracks
					> Settings.MaxWFCBacktrackSteps)
				{
					OutError =
						TEXT("DAG5BWFCBacktrackBudgetExceeded");
					return EWFCSolveStatus::BudgetExceeded;
				}
				continue;
			}

			const int32 TraceCountBeforeChoice = InOutTrace.Num();
			InOutTrace.Add(FString::Printf(
				TEXT("%d,%d,%d=%d"),
				P.X,
				P.Y,
				P.Z,
				static_cast<int32>(Choice)));
			const EWFCSolveStatus ChildStatus =
				SolveWFCDecisionTree(
					Settings,
					Seed,
					Size,
					TrialDomains,
					InOutTrace,
					InOutOperations,
					InOutBacktracks,
					OutError);
			if (ChildStatus == EWFCSolveStatus::Solved)
			{
				InOutDomains = MoveTemp(TrialDomains);
				return EWFCSolveStatus::Solved;
			}
			if (ChildStatus == EWFCSolveStatus::BudgetExceeded)
			{
				return ChildStatus;
			}
			InOutTrace.SetNum(TraceCountBeforeChoice);
			++InOutBacktracks;
			if (InOutBacktracks > Settings.MaxWFCBacktrackSteps)
			{
				OutError =
					TEXT("DAG5BWFCBacktrackBudgetExceeded");
				return EWFCSolveStatus::BudgetExceeded;
			}
		}
		return EWFCSolveStatus::Contradiction;
	}

	bool CollapseWFC(
		const FABTSM73DAG5BSettings& Settings,
		const int32 Seed,
		const FIntVector& Size,
		const TArray<FGrammarCell>& GrammarCells,
		TArray<EABTSM73DAG5BSemanticCell>& OutTiles,
		TArray<FString>& OutTrace,
		int32& OutOperations,
		int32& OutBacktracks,
		int32& OutNonAnchorCollapses,
		FString& OutError)
	{
		TArray<FTileMask> Domains;
		Domains.Reserve(GrammarCells.Num());
		for (const FGrammarCell& Cell : GrammarCells)
		{
			Domains.Add(Cell.Domain);
		}
		OutOperations = 0;
		OutBacktracks = 0;
		OutNonAnchorCollapses = 0;
		if (!Propagate(
			Size,
			Domains,
			Settings.MaxWFCPropagationOperations,
			OutOperations))
		{
			OutError = OutOperations
				> Settings.MaxWFCPropagationOperations
				? TEXT("DAG5BWFCPropagationBudgetExceeded")
				: TEXT("DAG5BWFCInitialContradiction");
			return false;
		}

		const EWFCSolveStatus SolveStatus = SolveWFCDecisionTree(
			Settings,
			Seed,
			Size,
			Domains,
			OutTrace,
			OutOperations,
			OutBacktracks,
			OutError);
		if (SolveStatus != EWFCSolveStatus::Solved)
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("DAG5BWFCContradiction");
			}
			return false;
		}
		OutNonAnchorCollapses = OutTrace.Num();

		OutTiles.Reserve(Domains.Num());
		for (const FTileMask Domain : Domains)
		{
			if (!IsSingleton(Domain))
			{
				OutError = TEXT("DAG5BWFCUncollapsedDomain");
				return false;
			}
			OutTiles.Add(FirstTile(Domain));
		}
		return true;
	}

	FBox MakeCellBounds(
		const FIntVector& Coordinate,
		const FIntVector& GridSize,
		const FABTSM73DAGLayoutSettings& Settings)
	{
		const FVector CellSize(
			Settings.TargetWidthCM / static_cast<float>(GridSize.X),
			Settings.TargetDepthCM / static_cast<float>(GridSize.Y),
			Settings.TargetHeightCM / static_cast<float>(GridSize.Z));
		const FVector RootMin(
			-Settings.TargetWidthCM * 0.5f,
			-Settings.TargetDepthCM * 0.5f,
			0.0f);
		const FVector Min = RootMin + FVector(
			Coordinate.X * CellSize.X,
			Coordinate.Y * CellSize.Y,
			Coordinate.Z * CellSize.Z);
		return FBox(Min, Min + CellSize);
	}

	void AddMacro(
		const FString& Path,
		const FVector2D& Center,
		const FVector2D& Dimensions,
		const bool bGround,
		const FABTSM73DAGLayoutSettings& LayoutSettings,
		FABTSM73DAGGenerationResult& InOutGraph,
		FABTSM73DAGSpatialLayout& InOutLayout,
		FABTSM73SemanticEnvelope& InOutEnvelope)
	{
		const int32 NodeId = InOutGraph.MacroNodes.Num();
		FABTSM73DAGMacroNode& Macro =
			InOutGraph.MacroNodes.AddDefaulted_GetRef();
		Macro.NodeId = NodeId;
		Macro.DerivationPath = Path;
		Macro.ExpansionDepth = 0;

		FABTSM73DAGMacroLayout& MacroLayout =
			InOutLayout.MacroLayouts.AddDefaulted_GetRef();
		MacroLayout.MacroNodeId = NodeId;
		MacroLayout.PlateCenter = FVector(Center.X, Center.Y, 0.0f);
		MacroLayout.PlateDimensionsCM = FVector(
			Dimensions.X,
			Dimensions.Y,
			LayoutSettings.PlateThicknessCM);
		MacroLayout.AllowedScope = FBox(
			FVector(
				Center.X - Dimensions.X * 0.5f,
				Center.Y - Dimensions.Y * 0.5f,
				0.0f),
			FVector(
				Center.X + Dimensions.X * 0.5f,
				Center.Y + Dimensions.Y * 0.5f,
				LayoutSettings.TargetHeightCM));
		MacroLayout.bGroundTerminal = bGround;
		if (bGround) InOutGraph.GroundNodeIds.Add(NodeId);

		FABTSM73DAG5BShapeScope& Scope =
			InOutEnvelope.ShapeScopes.AddDefaulted_GetRef();
		Scope.MacroNodeId = NodeId;
		Scope.NormalizedBounds = FBox(
			FVector(
				(Center.X - Dimensions.X * 0.5f)
					/ LayoutSettings.TargetWidthCM,
				(Center.Y - Dimensions.Y * 0.5f)
					/ LayoutSettings.TargetDepthCM,
				0.0f),
			FVector(
				(Center.X + Dimensions.X * 0.5f)
					/ LayoutSettings.TargetWidthCM,
				(Center.Y + Dimensions.Y * 0.5f)
					/ LayoutSettings.TargetDepthCM,
				1.0f));
		Scope.DerivationPath = Path;
		Scope.Semantic = bGround
			? EABTSM73DAG5BSemanticCell::Foundation
			: EABTSM73DAG5BSemanticCell::FloorCarrier;

		FABTSM73DAG5BMacroConstraint& Constraint =
			InOutEnvelope.MacroConstraints.AddDefaulted_GetRef();
		Constraint.MacroNodeId = NodeId;
		Constraint.OffsetCM = Center;
		Constraint.FootprintScale = FVector2D(
			Dimensions.X / LayoutSettings.TargetWidthCM,
			Dimensions.Y / LayoutSettings.TargetDepthCM);
		Constraint.DerivationPath = Path;
	}

	void AddSupport(
		const int32 Support,
		const int32 Load,
		FABTSM73DAGGenerationResult& InOutGraph)
	{
		FABTSM73DAGSupportEdge& Edge =
			InOutGraph.SupportEdges.AddDefaulted_GetRef();
		Edge.SupportNodeId = Support;
		Edge.LoadNodeId = Load;
	}

	void BuildFamilyGraph(
		const EABTSM73DAG5BShapeFamily Family,
		const FABTSM73DAG5BSettings& BSettings,
		const FABTSM73DAGLayoutSettings& LayoutSettings,
		FABTSM73DAGGenerationResult& OutGraph,
		FABTSM73DAGSpatialLayout& OutLayout,
		FABTSM73SemanticEnvelope& OutEnvelope)
	{
		const float W = LayoutSettings.TargetWidthCM;
		const float D = LayoutSettings.TargetDepthCM;
		const float Offset =
			FMath::Clamp(BSettings.OffsetRatio, 0.02f, 0.25f) * W;
		const float Setback =
			FMath::Clamp(BSettings.SetbackRatio, 0.05f, 0.35f);
		const float SideX = W * 0.27f;
		const float SideWidth = W * 0.38f;
		const float SideDepth = D * 0.72f;

		switch (Family)
		{
		case EABTSM73DAG5BShapeFamily::SetbackTower:
		{
			AddMacro(
				TEXT("Mass/Foundation"),
				FVector2D(-Offset * 0.35f, 0.0f),
				FVector2D(W * 0.88f, D * 0.84f),
				true,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Mass/Setback[1]"),
				FVector2D(-Offset * 0.15f, 0.0f),
				FVector2D(W * (0.88f - Setback * 0.55f), D * 0.76f),
				false,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Mass/Setback[2]"),
				FVector2D(Offset * 0.25f, 0.0f),
				FVector2D(W * (0.78f - Setback), D * 0.66f),
				false,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Mass/Crown"),
				FVector2D(Offset * 0.55f, 0.0f),
				FVector2D(W * (0.62f - Setback), D * 0.56f),
				false,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddSupport(0, 1, OutGraph);
			AddSupport(1, 2, OutGraph);
			AddSupport(2, 3, OutGraph);
			OutGraph.TopLoadNodeIds = {3};
			OutEnvelope.FeatureMask =
				EABTSM73DAG5BFeature::Setback
				| EABTSM73DAG5BFeature::FootprintCentroidShift;
			OutEnvelope.ShapeDerivationTrace = {
				TEXT("Root -> Mass"),
				TEXT("Mass -> Setback(SetbackTower,3)"),
				TEXT("Setback[3] -> Offset + Crown")
			};
			break;
		}
		case EABTSM73DAG5BShapeFamily::OffsetBridge:
		{
			AddMacro(
				TEXT("Split/Left/Foundation"),
				FVector2D(-SideX, 0.0f),
				FVector2D(SideWidth, SideDepth),
				true,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Split/Right/Foundation"),
				FVector2D(SideX, 0.0f),
				FVector2D(SideWidth, SideDepth),
				true,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Split/Left/Tower"),
				FVector2D(-SideX + Offset * 0.15f, 0.0f),
				FVector2D(SideWidth * 0.94f, SideDepth * 0.90f),
				false,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Split/Right/Tower"),
				FVector2D(SideX + Offset * 0.20f, 0.0f),
				FVector2D(SideWidth * 0.94f, SideDepth * 0.90f),
				false,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Split/Bridge"),
				FVector2D(Offset * 0.35f, 0.0f),
				FVector2D(W * 0.92f, D * 0.66f),
				false,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Split/Bridge/OffsetCrown"),
				FVector2D(Offset * 0.75f, 0.0f),
				FVector2D(W * 0.48f, D * 0.54f),
				false,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddSupport(0, 2, OutGraph);
			AddSupport(1, 3, OutGraph);
			AddSupport(2, 4, OutGraph);
			AddSupport(3, 4, OutGraph);
			AddSupport(4, 5, OutGraph);
			OutGraph.TopLoadNodeIds = {5};
			OutEnvelope.FeatureMask =
				EABTSM73DAG5BFeature::BridgeSpan
				| EABTSM73DAG5BFeature::FootprintCentroidShift;
			OutEnvelope.ShapeDerivationTrace = {
				TEXT("Root -> SplitHorizontal(Left,Right)"),
				TEXT("Left + Right -> Bridge"),
				TEXT("Bridge -> Offset + Crown")
			};
			break;
		}
		case EABTSM73DAG5BShapeFamily::ThroughOpeningWall:
		{
			AddMacro(
				TEXT("Wall/Left/Foundation"),
				FVector2D(-SideX, 0.0f),
				FVector2D(SideWidth, SideDepth),
				true,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Wall/Right/Foundation"),
				FVector2D(SideX, 0.0f),
				FVector2D(SideWidth, SideDepth),
				true,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Wall/Left/Pier"),
				FVector2D(-SideX * 1.20f, 0.0f),
				FVector2D(SideWidth * 0.56f, SideDepth * 0.92f),
				false,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Wall/Right/Pier"),
				FVector2D(SideX * 1.20f, 0.0f),
				FVector2D(SideWidth * 0.56f, SideDepth * 0.92f),
				false,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Wall/OpeningLintel"),
				FVector2D::ZeroVector,
				FVector2D(W * 0.90f, D * 0.68f),
				false,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Wall/Left/Crown"),
				FVector2D(-SideX * 0.90f, 0.0f),
				FVector2D(SideWidth * 0.92f, SideDepth * 0.80f),
				false,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Wall/Right/Crown"),
				FVector2D(SideX * 0.90f, 0.0f),
				FVector2D(SideWidth * 0.82f, SideDepth * 0.75f),
				false,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Wall/Left/HighCrown"),
				FVector2D(-SideX * 0.90f, 0.0f),
				FVector2D(SideWidth * 0.72f, SideDepth * 0.66f),
				false,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddSupport(0, 2, OutGraph);
			AddSupport(1, 3, OutGraph);
			AddSupport(2, 4, OutGraph);
			AddSupport(3, 4, OutGraph);
			AddSupport(4, 5, OutGraph);
			AddSupport(4, 6, OutGraph);
			AddSupport(5, 7, OutGraph);
			OutGraph.TopLoadNodeIds = {6, 7};
			OutEnvelope.FeatureMask =
				EABTSM73DAG5BFeature::ThroughOpening
				| EABTSM73DAG5BFeature::BridgeSpan
				| EABTSM73DAG5BFeature::NonUniformRoofline;
			OutEnvelope.ShapeDerivationTrace = {
				TEXT("Root -> Wall"),
				TEXT("Wall -> CarveThroughOpening"),
				TEXT("Opening -> Frame + Lintel"),
				TEXT("Crown -> AsymmetricHeight")
			};
			break;
		}
		case EABTSM73DAG5BShapeFamily::OneSidedHighTower:
		default:
		{
			AddMacro(
				TEXT("Asymmetric/Foundation"),
				FVector2D(-Offset * 0.30f, 0.0f),
				FVector2D(W * 0.78f, D * 0.82f),
				true,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Asymmetric/LowTower"),
				FVector2D(SideX * 0.80f, 0.0f),
				FVector2D(W * 0.32f, D * 0.62f),
				false,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Asymmetric/HighTower[1]"),
				FVector2D(-SideX * 0.80f, 0.0f),
				FVector2D(W * 0.36f, D * 0.68f),
				false,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Asymmetric/HighTower[2]"),
				FVector2D(-SideX * 0.76f, 0.0f),
				FVector2D(W * 0.34f, D * 0.62f),
				false,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddMacro(
				TEXT("Asymmetric/HighCrown/Cantilever"),
				FVector2D(-Offset * 1.20f, 0.0f),
				FVector2D(
					W * (0.52f
						+ FMath::Clamp(
							BSettings.CantileverRatio,
							0.05f,
							0.30f)),
					D * 0.58f),
				false,
				LayoutSettings,
				OutGraph,
				OutLayout,
				OutEnvelope);
			AddSupport(0, 1, OutGraph);
			AddSupport(0, 2, OutGraph);
			AddSupport(2, 3, OutGraph);
			AddSupport(3, 4, OutGraph);
			OutGraph.TopLoadNodeIds = {1, 4};
			OutEnvelope.FeatureMask =
				EABTSM73DAG5BFeature::HeightAsymmetry
				| EABTSM73DAG5BFeature::Cantilever
				| EABTSM73DAG5BFeature::NonUniformRoofline
				| EABTSM73DAG5BFeature::FootprintCentroidShift;
			OutEnvelope.ShapeDerivationTrace = {
				TEXT("Root -> SplitHorizontal(High,Low)"),
				TEXT("High -> AsymmetricHeight(3)"),
				TEXT("Low -> AsymmetricHeight(1)"),
				TEXT("HighCrown -> Cantilever")
			};
			break;
		}
		}
	}

	FString MakeShapeCanonical(
		const FABTSM73SemanticEnvelope& Envelope,
		const FABTSM73DAGGenerationResult& Graph,
		const FABTSM73DAGSpatialLayout& Layout)
	{
		FString Canonical = FString::Printf(
			TEXT("Family=%d|Grid=%d,%d,%d|Feature=%u"),
			static_cast<int32>(Envelope.ShapeFamily),
			Envelope.GridSize.X,
			Envelope.GridSize.Y,
			Envelope.GridSize.Z,
			static_cast<uint32>(Envelope.FeatureMask));
		for (const FString& Step : Envelope.ShapeDerivationTrace)
		{
			Canonical += TEXT("|Rule=") + Step;
		}
		for (const FABTSM73DAGMacroNode& Macro : Graph.MacroNodes)
		{
			const FABTSM73DAGMacroLayout* MacroLayout =
				Layout.MacroLayouts.FindByPredicate(
					[&Macro](const FABTSM73DAGMacroLayout& Candidate)
					{
						return Candidate.MacroNodeId == Macro.NodeId;
					});
			if (MacroLayout == nullptr) continue;
			Canonical += FString::Printf(
				TEXT("|M=%d,%s,%.3f,%.3f,%.3f,%.3f"),
				Macro.NodeId,
				*Macro.DerivationPath,
				MacroLayout->PlateCenter.X,
				MacroLayout->PlateCenter.Y,
				MacroLayout->PlateDimensionsCM.X,
				MacroLayout->PlateDimensionsCM.Y);
		}
		for (const FABTSM73DAGSupportEdge& Edge : Graph.SupportEdges)
		{
			Canonical += FString::Printf(
				TEXT("|E=%d>%d"),
				Edge.SupportNodeId,
				Edge.LoadNodeId);
		}
		return Canonical;
	}

	FString MakeEnvelopeCanonical(
		const FABTSM73SemanticEnvelope& Envelope)
	{
		FString Canonical = FString::Printf(
			TEXT("V=%d|Family=%d|Grid=%d,%d,%d")
			TEXT("|Bounds=%.3f,%.3f,%.3f,%.3f,%.3f,%.3f")
			TEXT("|Shape=%u|WFC=%u|Features=%u"),
			Envelope.EnvelopeVersion,
			static_cast<int32>(Envelope.ShapeFamily),
			Envelope.GridSize.X,
			Envelope.GridSize.Y,
			Envelope.GridSize.Z,
			Envelope.LocalBounds.Min.X,
			Envelope.LocalBounds.Min.Y,
			Envelope.LocalBounds.Min.Z,
			Envelope.LocalBounds.Max.X,
			Envelope.LocalBounds.Max.Y,
			Envelope.LocalBounds.Max.Z,
			Envelope.ShapeHash,
			Envelope.WFCHash,
			static_cast<uint32>(Envelope.FeatureMask));
		for (const FString& Step : Envelope.ShapeDerivationTrace)
		{
			Canonical += TEXT("|ShapeTrace=") + Step;
		}
		for (const FString& Step : Envelope.WFCCollapseTrace)
		{
			Canonical += TEXT("|WFCTrace=") + Step;
		}
		for (const FABTSM73DAG5BShapeScope& Scope :
			Envelope.ShapeScopes)
		{
			Canonical += FString::Printf(
				TEXT("|Scope=%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%s"),
				Scope.MacroNodeId,
				static_cast<int32>(Scope.Semantic),
				Scope.NormalizedBounds.Min.X,
				Scope.NormalizedBounds.Min.Y,
				Scope.NormalizedBounds.Min.Z,
				Scope.NormalizedBounds.Max.X,
				Scope.NormalizedBounds.Max.Y,
				Scope.NormalizedBounds.Max.Z,
				*Scope.DerivationPath);
		}
		for (const FABTSM73DAG5BMacroConstraint& Constraint :
			Envelope.MacroConstraints)
		{
			Canonical += FString::Printf(
				TEXT("|Macro=%d,%.3f,%.3f,%.6f,%.6f,%s"),
				Constraint.MacroNodeId,
				Constraint.OffsetCM.X,
				Constraint.OffsetCM.Y,
				Constraint.FootprintScale.X,
				Constraint.FootprintScale.Y,
				*Constraint.DerivationPath);
		}
		for (const FABTSM73DAG5BSupportPortConstraint& Port :
			Envelope.SupportPorts)
		{
			Canonical += FString::Printf(
				TEXT("|Port=%d,%d,%.3f,%.3f,%.3f,%.3f")
				TEXT(",%d,%d,%d,%d,%d,%d,%d,%u,%s"),
				Port.SupportMacroNodeId,
				Port.LoadMacroNodeId,
				Port.AllowedColumnRegion.Min.X,
				Port.AllowedColumnRegion.Min.Y,
				Port.AllowedColumnRegion.Max.X,
				Port.AllowedColumnRegion.Max.Y,
				Port.SourceCellMin.X,
				Port.SourceCellMin.Y,
				Port.SourceCellMin.Z,
				Port.SourceCellMax.X,
				Port.SourceCellMax.Y,
				Port.SourceCellMax.Z,
				static_cast<int32>(Port.SourceSemantic),
				Port.SourceCellHash,
				*Port.DerivationPath);
		}
		for (const FABTSM73DAG5BWeaknessSocket& Socket :
			Envelope.WeaknessSockets)
		{
			Canonical += FString::Printf(
				TEXT("|Socket=%d,%d,%d,%d")
				TEXT(",%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%s"),
				Socket.SourceCell.X,
				Socket.SourceCell.Y,
				Socket.SourceCell.Z,
				static_cast<int32>(Socket.RequiredPort),
				Socket.LocalBounds.Min.X,
				Socket.LocalBounds.Min.Y,
				Socket.LocalBounds.Min.Z,
				Socket.LocalBounds.Max.X,
				Socket.LocalBounds.Max.Y,
				Socket.LocalBounds.Max.Z,
				Socket.SourceCellHash,
				*Socket.DerivationPath);
		}
		for (const FABTSM73DAG5BSemanticCellRecord& Cell :
			Envelope.Cells)
		{
			Canonical += FString::Printf(
				TEXT("|Cell=%d,%d,%d,%d,%d,%d,%d")
				TEXT(",%.3f,%.3f,%.3f,%.3f,%.3f,%.3f")
				TEXT(",%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%s"),
				Cell.Coordinate.X,
				Cell.Coordinate.Y,
				Cell.Coordinate.Z,
				static_cast<int32>(Cell.Semantic),
				static_cast<int32>(Cell.Occupancy),
				static_cast<int32>(Cell.Ports),
				Cell.RequiredMacroNodeId,
				Cell.LocalBounds.Min.X,
				Cell.LocalBounds.Min.Y,
				Cell.LocalBounds.Min.Z,
				Cell.LocalBounds.Max.X,
				Cell.LocalBounds.Max.Y,
				Cell.LocalBounds.Max.Z,
				Cell.RequiredSolidBounds.Min.X,
				Cell.RequiredSolidBounds.Min.Y,
				Cell.RequiredSolidBounds.Min.Z,
				Cell.RequiredSolidBounds.Max.X,
				Cell.RequiredSolidBounds.Max.Y,
				Cell.RequiredSolidBounds.Max.Z,
				*Cell.DerivationPath);
			Canonical += Cell.bHardAnchor
				? TEXT(",Hard=1")
				: TEXT(",Hard=0");
		}
		return Canonical;
	}

	FBox BrickBox(const FABTSM73BrickNode& Brick)
	{
		return FBox(
			Brick.LocalCenter - Brick.DimensionsCM * 0.5f,
			Brick.LocalCenter + Brick.DimensionsCM * 0.5f);
	}

	bool BoxesOverlapWithVolume(const FBox& A, const FBox& B)
	{
		if (!A.IsValid || !B.IsValid) return false;
		const FVector Min(
			FMath::Max(A.Min.X, B.Min.X),
			FMath::Max(A.Min.Y, B.Min.Y),
			FMath::Max(A.Min.Z, B.Min.Z));
		const FVector Max(
			FMath::Min(A.Max.X, B.Max.X),
			FMath::Min(A.Max.Y, B.Max.Y),
			FMath::Min(A.Max.Z, B.Max.Z));
		return Max.X > Min.X + KINDA_SMALL_NUMBER
			&& Max.Y > Min.Y + KINDA_SMALL_NUMBER
			&& Max.Z > Min.Z + KINDA_SMALL_NUMBER;
	}

	const FABTSM73DAGMacroLayout* FindMacroLayout(
		const FABTSM73DAGSpatialLayout& Layout,
		const int32 MacroNodeId)
	{
		return Layout.MacroLayouts.FindByPredicate(
			[MacroNodeId](
				const FABTSM73DAGMacroLayout& Candidate)
			{
				return Candidate.MacroNodeId == MacroNodeId;
			});
	}

	EABTSM73DAG5BSemanticCell SemanticForMacro(
		const FABTSM73DAGMacroLayout& Macro,
		const FABTSM73DAGGenerationResult& Graph)
	{
		if (Macro.bGroundTerminal)
		{
			return EABTSM73DAG5BSemanticCell::Foundation;
		}
		const FABTSM73DAGMacroNode* MacroNode =
			Graph.MacroNodes.FindByPredicate(
				[&Macro](const FABTSM73DAGMacroNode& Candidate)
				{
					return Candidate.NodeId == Macro.MacroNodeId;
				});
		const FString Path = MacroNode != nullptr
			? MacroNode->DerivationPath
			: FString();
		if (Path.Contains(TEXT("Cantilever")))
		{
			return EABTSM73DAG5BSemanticCell::Cantilever;
		}
		if (Path.Contains(TEXT("Crown")))
		{
			return EABTSM73DAG5BSemanticCell::Roof;
		}
		if (Path.Contains(TEXT("Bridge"))
			|| Path.Contains(TEXT("Lintel")))
		{
			return EABTSM73DAG5BSemanticCell::BeamZone;
		}
		return EABTSM73DAG5BSemanticCell::FloorCarrier;
	}

	FBox2D MacroPlateBounds(
		const FABTSM73DAGMacroLayout& Macro)
	{
		const FVector2D Center(
			Macro.PlateCenter.X,
			Macro.PlateCenter.Y);
		const FVector2D Half(
			Macro.PlateDimensionsCM.X * 0.5f,
			Macro.PlateDimensionsCM.Y * 0.5f);
		return FBox2D(Center - Half, Center + Half);
	}

	bool Intersect2D(
		const FBox2D& A,
		const FBox2D& B,
		FBox2D& OutIntersection)
	{
		if (!A.bIsValid || !B.bIsValid) return false;
		const FVector2D Min(
			FMath::Max(A.Min.X, B.Min.X),
			FMath::Max(A.Min.Y, B.Min.Y));
		const FVector2D Max(
			FMath::Min(A.Max.X, B.Max.X),
			FMath::Min(A.Max.Y, B.Max.Y));
		if (Max.X <= Min.X || Max.Y <= Min.Y) return false;
		OutIntersection = FBox2D(Min, Max);
		return true;
	}

	bool IsVerticalSupportSemantic(
		const EABTSM73DAG5BSemanticCell Semantic)
	{
		return Semantic == EABTSM73DAG5BSemanticCell::Foundation
			|| Semantic == EABTSM73DAG5BSemanticCell::ColumnZone
			|| Semantic == EABTSM73DAG5BSemanticCell::WallPier;
	}

	struct FWFCSpatialSupportRegion
	{
		bool bValid = false;
		FBox2D Bounds = FBox2D(EForceInit::ForceInit);
		FIntVector CellMin = FIntVector::ZeroValue;
		FIntVector CellMax = FIntVector::ZeroValue;
		EABTSM73DAG5BSemanticCell DominantSemantic =
			EABTSM73DAG5BSemanticCell::Void;
		uint32 SourceHash = 0;
		int32 LayerDistance = MAX_int32;
		bool bContainsTarget = false;
		float TargetDistanceSquared = TNumericLimits<float>::Max();
		float Area = 0.0f;
	};

	bool FindWFCSpatialSupportRegion(
		const FABTSM73SemanticEnvelope& Envelope,
		const FBox2D& MacroIntersection,
		const float PhysicalMidZ,
		const float ColumnBottomZ,
		const float ColumnTopZ,
		const FVector2D& TargetXY,
		const FABTSM73DAGLayoutSettings& Settings,
		FWFCSpatialSupportRegion& OutRegion)
	{
		OutRegion = FWFCSpatialSupportRegion();
		const int32 RawCellCount =
			Envelope.GridSize.X
			* Envelope.GridSize.Y
			* Envelope.GridSize.Z;
		if (Envelope.GridSize.X <= 0
			|| Envelope.GridSize.Y <= 0
			|| Envelope.GridSize.Z <= 0
			|| Envelope.Cells.Num() < RawCellCount)
		{
			return false;
		}
		const int32 TargetLayer = FMath::Clamp(
			FMath::FloorToInt(
				PhysicalMidZ
					/ FMath::Max(1.0f, Settings.TargetHeightCM)
					* static_cast<float>(Envelope.GridSize.Z)),
			0,
			Envelope.GridSize.Z - 1);
		const float MinExtent =
			Settings.MinAdaptiveColumnWidthCM
			+ Settings.ColumnClearanceCM * 2.0f;

		for (int32 Z = 0; Z < Envelope.GridSize.Z; ++Z)
		{
			const int32 LayerDistance = FMath::Abs(Z - TargetLayer);
			if (LayerDistance > 1) continue;
			for (int32 MinY = 0; MinY < Envelope.GridSize.Y; ++MinY)
			{
				for (int32 MaxY = MinY;
					MaxY < Envelope.GridSize.Y;
					++MaxY)
				{
					for (int32 MinX = 0;
						MinX < Envelope.GridSize.X;
						++MinX)
					{
						for (int32 MaxX = MinX;
							MaxX < Envelope.GridSize.X;
							++MaxX)
						{
							bool bAllSupport = true;
							uint32 SourceHash = 0;
							bool bHasWallPier = false;
							for (int32 Y = MinY;
								Y <= MaxY && bAllSupport;
								++Y)
							{
								for (int32 X = MinX;
									X <= MaxX;
									++X)
								{
									const FIntVector Coordinate(X, Y, Z);
									const FABTSM73DAG5BSemanticCellRecord&
										Cell = Envelope.Cells[
											GridIndex(
												Coordinate,
												Envelope.GridSize)];
									if (!IsVerticalSupportSemantic(
										Cell.Semantic))
									{
										bAllSupport = false;
										break;
									}
									bHasWallPier |= Cell.Semantic
										== EABTSM73DAG5BSemanticCell::WallPier;
									SourceHash = HashCombineFast(
										SourceHash,
										HashCombineFast(
											CoordinateHash(0x5b31, Coordinate),
											static_cast<uint32>(
												Cell.Semantic)));
								}
							}
							if (!bAllSupport) continue;
							const FABTSM73DAG5BSemanticCellRecord& First =
								Envelope.Cells[GridIndex(
									FIntVector(MinX, MinY, Z),
									Envelope.GridSize)];
							const FABTSM73DAG5BSemanticCellRecord& Last =
								Envelope.Cells[GridIndex(
									FIntVector(MaxX, MaxY, Z),
									Envelope.GridSize)];
							const FBox2D CellRectangle(
								FVector2D(
									First.LocalBounds.Min.X,
									First.LocalBounds.Min.Y),
								FVector2D(
									Last.LocalBounds.Max.X,
									Last.LocalBounds.Max.Y));
							FBox2D Clipped(EForceInit::ForceInit);
							if (!Intersect2D(
								CellRectangle,
								MacroIntersection,
								Clipped))
							{
								continue;
							}
							const FVector2D Size = Clipped.GetSize();
							if (Size.X < MinExtent || Size.Y < MinExtent)
							{
								continue;
							}
							const FBox CandidateColumnPrism(
								FVector(
									Clipped.Min.X,
									Clipped.Min.Y,
									ColumnBottomZ),
								FVector(
									Clipped.Max.X,
									Clipped.Max.Y,
									ColumnTopZ));
							bool bIntersectsRequiredVoid = false;
							for (const FABTSM73DAG5BSemanticCellRecord&
								ContractCell : Envelope.Cells)
							{
								if (ContractCell.Occupancy
										!= EABTSM73DAG5BOccupancy::MustVoid
									|| !BoxesOverlapWithVolume(
										CandidateColumnPrism,
										ContractCell.LocalBounds))
								{
									continue;
								}
								bIntersectsRequiredVoid = true;
								break;
							}
							if (bIntersectsRequiredVoid)
							{
								continue;
							}
							const float Area = Size.X * Size.Y;
							const bool bContainsTarget =
								Clipped.IsInsideOrOn(TargetXY);
							const float TargetDistanceSquared =
								FVector2D::DistSquared(
									Clipped.GetCenter(),
									TargetXY);
							const bool bBetter =
								!OutRegion.bValid
								|| (bContainsTarget
										!= OutRegion.bContainsTarget
									&& bContainsTarget)
								|| (bContainsTarget
										== OutRegion.bContainsTarget
									&& Area
										> OutRegion.Area
											+ KINDA_SMALL_NUMBER)
								|| (bContainsTarget
										== OutRegion.bContainsTarget
									&& FMath::IsNearlyEqual(
										Area,
										OutRegion.Area)
									&& LayerDistance
										< OutRegion.LayerDistance)
								|| (bContainsTarget
										== OutRegion.bContainsTarget
									&& FMath::IsNearlyEqual(
										Area,
										OutRegion.Area)
									&& LayerDistance
										== OutRegion.LayerDistance
									&& TargetDistanceSquared
										< OutRegion.TargetDistanceSquared
											- KINDA_SMALL_NUMBER)
								|| (bContainsTarget
										== OutRegion.bContainsTarget
									&& FMath::IsNearlyEqual(
										Area,
										OutRegion.Area)
									&& LayerDistance
										== OutRegion.LayerDistance
									&& FMath::IsNearlyEqual(
										TargetDistanceSquared,
										OutRegion.TargetDistanceSquared)
									&& SourceHash
										< OutRegion.SourceHash);
							if (!bBetter) continue;
							OutRegion.bValid = true;
							OutRegion.Bounds = Clipped;
							OutRegion.CellMin =
								FIntVector(MinX, MinY, Z);
							OutRegion.CellMax =
								FIntVector(MaxX, MaxY, Z);
							OutRegion.DominantSemantic =
								bHasWallPier
									? EABTSM73DAG5BSemanticCell::WallPier
									: Z == 0
										? EABTSM73DAG5BSemanticCell::Foundation
										: EABTSM73DAG5BSemanticCell::ColumnZone;
							OutRegion.SourceHash = SourceHash;
							OutRegion.LayerDistance = LayerDistance;
							OutRegion.bContainsTarget = bContainsTarget;
							OutRegion.TargetDistanceSquared =
								TargetDistanceSquared;
							OutRegion.Area = Area;
						}
					}
				}
			}
		}
		return OutRegion.bValid;
	}

	bool MakeWFCOpenAirContract(
		const FABTSM73DAG5BSemanticCellRecord& Cell,
		FBox& OutVoidBounds)
	{
		const FVector Size = Cell.LocalBounds.GetSize();
		const FVector Inset(
			FMath::Min(10.0f, Size.X * 0.16f),
			FMath::Min(10.0f, Size.Y * 0.16f),
			Cell.Semantic
					== EABTSM73DAG5BSemanticCell::DoorVoid
				? FMath::Min(24.0f, Size.Z * 0.18f)
				: Cell.Semantic
						== EABTSM73DAG5BSemanticCell::WindowVoid
					? Size.Z * 0.22f
					: Size.Z * 0.12f);
		OutVoidBounds = FBox(
			Cell.LocalBounds.Min + Inset,
			Cell.LocalBounds.Max - Inset);
		const FVector ContractSize = OutVoidBounds.GetSize();
		if (ContractSize.X < 8.0f
			|| ContractSize.Y < 8.0f
			|| ContractSize.Z < 12.0f)
		{
			return false;
		}
		return true;
	}

	bool PredictPhysicalLevels(
		const FABTSM73DAGGenerationResult& Graph,
		const FABTSM73DAGLayoutSettings& Settings,
		const FABTSM73DAGSpatialLayout& InitialLayout,
		FABTSM73DAGSpatialLayout& OutPredicted,
		FString& OutError)
	{
		OutPredicted = InitialLayout;
		TMap<int32, int32> LevelByMacro;
		for (const FABTSM73DAGMacroNode& Macro : Graph.MacroNodes)
		{
			LevelByMacro.Add(Macro.NodeId, 0);
		}
		for (int32 Pass = 0; Pass < Graph.MacroNodes.Num(); ++Pass)
		{
			bool bChanged = false;
			for (const FABTSM73DAGSupportEdge& Edge :
				Graph.SupportEdges)
			{
				const int32* Support =
					LevelByMacro.Find(Edge.SupportNodeId);
				int32* Load =
					LevelByMacro.Find(Edge.LoadNodeId);
				if (Support == nullptr || Load == nullptr)
				{
					OutError =
						TEXT("DAG5BContractGraphNodeMissing");
					return false;
				}
				if (*Load < *Support + 1)
				{
					*Load = *Support + 1;
					bChanged = true;
				}
			}
			if (!bChanged) break;
			if (Pass == Graph.MacroNodes.Num() - 1)
			{
				OutError = TEXT("DAG5BContractGraphCycle");
				return false;
			}
		}
		int32 MaxLevel = 0;
		for (const TPair<int32, int32>& Pair : LevelByMacro)
		{
			MaxLevel = FMath::Max(MaxLevel, Pair.Value);
		}
		const float RequiredPitch =
			Settings.PlateThicknessCM
			+ Settings.MinColumnHeightCM;
		const float TargetPitch = MaxLevel > 0
			? (Settings.TargetHeightCM
				- Settings.PlateThicknessCM)
				/ static_cast<float>(MaxLevel)
			: RequiredPitch;
		const float LevelPitch =
			FMath::Max(RequiredPitch, TargetPitch);
		for (FABTSM73DAGMacroLayout& Macro :
			OutPredicted.MacroLayouts)
		{
			const int32* Level =
				LevelByMacro.Find(Macro.MacroNodeId);
			if (Level == nullptr)
			{
				OutError =
					TEXT("DAG5BContractLayoutNodeMissing");
				return false;
			}
			Macro.StructuralLevel = *Level;
			Macro.PlateCenter.Z =
				Macro.PlateDimensionsCM.Z * 0.5f
				+ LevelPitch * static_cast<float>(*Level);
		}
		OutPredicted.bAccepted = true;
		return true;
	}

	bool FinalizeShapeScopes(
		const FABTSM73DAGGenerationResult& Graph,
		const FABTSM73DAGSpatialLayout& Predicted,
		FABTSM73SemanticEnvelope& InOutEnvelope,
		FString& OutError)
	{
		const float Height =
			InOutEnvelope.LocalBounds.GetSize().Z;
		if (Height <= KINDA_SMALL_NUMBER
			|| InOutEnvelope.ShapeScopes.Num()
				!= Predicted.MacroLayouts.Num())
		{
			OutError = TEXT("DAG5BShapeScopeCardinalityInvalid");
			return false;
		}
		for (FABTSM73DAG5BShapeScope& Scope :
			InOutEnvelope.ShapeScopes)
		{
			const FABTSM73DAGMacroLayout* Macro =
				FindMacroLayout(Predicted, Scope.MacroNodeId);
			if (Macro == nullptr)
			{
				OutError = FString::Printf(
					TEXT("DAG5BShapeScopeMacroMissing:%d"),
					Scope.MacroNodeId);
				return false;
			}
			float ScopeBottom =
				Macro->PlateCenter.Z
					- Macro->PlateDimensionsCM.Z * 0.5f;
			if (!Macro->bGroundTerminal)
			{
				bool bHasIncomingSupport = false;
				for (const FABTSM73DAGSupportEdge& Edge :
					Graph.SupportEdges)
				{
					if (Edge.LoadNodeId != Scope.MacroNodeId)
					{
						continue;
					}
					const FABTSM73DAGMacroLayout* Support =
						FindMacroLayout(
							Predicted,
							Edge.SupportNodeId);
					if (Support == nullptr)
					{
						OutError =
							TEXT("DAG5BShapeScopeSupportMissing");
						return false;
					}
					const float SupportTop =
						Support->PlateCenter.Z
							+ Support->PlateDimensionsCM.Z * 0.5f;
					ScopeBottom = bHasIncomingSupport
						? FMath::Min(ScopeBottom, SupportTop)
						: SupportTop;
					bHasIncomingSupport = true;
				}
				if (!bHasIncomingSupport)
				{
					OutError = FString::Printf(
						TEXT("DAG5BShapeScopeIncomingSupportMissing:%d"),
						Scope.MacroNodeId);
					return false;
				}
			}
			const float ScopeTop =
				Macro->PlateCenter.Z
					+ Macro->PlateDimensionsCM.Z * 0.5f;
			Scope.NormalizedBounds.Min.Z =
				FMath::Clamp(ScopeBottom / Height, 0.0f, 1.0f);
			Scope.NormalizedBounds.Max.Z =
				FMath::Clamp(ScopeTop / Height, 0.0f, 1.0f);
			Scope.Semantic = SemanticForMacro(*Macro, Graph);
			if (Scope.NormalizedBounds.Max.Z
				<= Scope.NormalizedBounds.Min.Z)
			{
				OutError = FString::Printf(
					TEXT("DAG5BShapeScopeVerticalRangeInvalid:%d"),
					Scope.MacroNodeId);
				return false;
			}
		}
		return true;
	}

	bool AppendFamilyClearanceContract(
		const FABTSM73DAGSpatialLayout& Predicted,
		FABTSM73SemanticEnvelope& InOutEnvelope,
		FString& OutError)
	{
		FVector VoidCenter = FVector::ZeroVector;
		FVector VoidHalf(10.0f, 10.0f, 10.0f);
		switch (InOutEnvelope.ShapeFamily)
		{
		case EABTSM73DAG5BShapeFamily::SetbackTower:
		{
			const FABTSM73DAGMacroLayout* Base =
				FindMacroLayout(Predicted, 0);
			const FABTSM73DAGMacroLayout* Crown =
				FindMacroLayout(Predicted, 3);
			if (Base == nullptr || Crown == nullptr)
			{
				OutError =
					TEXT("DAG5BSetbackContractMacroMissing");
				return false;
			}
			VoidCenter = FVector(
				Base->PlateCenter.X
					- Base->PlateDimensionsCM.X * 0.42f,
				Base->PlateCenter.Y,
				Crown->PlateCenter.Z);
			break;
		}
		case EABTSM73DAG5BShapeFamily::OffsetBridge:
		case EABTSM73DAG5BShapeFamily::ThroughOpeningWall:
		{
			const FABTSM73DAGMacroLayout* Left =
				FindMacroLayout(Predicted, 2);
			const FABTSM73DAGMacroLayout* Right =
				FindMacroLayout(Predicted, 3);
			const FABTSM73DAGMacroLayout* Bridge =
				FindMacroLayout(Predicted, 4);
			if (Left == nullptr || Right == nullptr
				|| Bridge == nullptr)
			{
				OutError =
					TEXT("DAG5BBridgeContractMacroMissing");
				return false;
			}
			const float LowerTop = FMath::Max(
				Left->PlateCenter.Z
					+ Left->PlateDimensionsCM.Z * 0.5f,
				Right->PlateCenter.Z
					+ Right->PlateDimensionsCM.Z * 0.5f);
			const float BridgeBottom =
				Bridge->PlateCenter.Z
					- Bridge->PlateDimensionsCM.Z * 0.5f;
			VoidCenter = FVector(
				0.0f,
				0.0f,
				(LowerTop + BridgeBottom) * 0.5f);
			VoidHalf = FVector(
				FMath::Max(
					12.0f,
					FMath::Abs(
						Left->PlateCenter.X) * 0.25f),
				12.0f,
				FMath::Max(
					8.0f,
					(BridgeBottom - LowerTop) * 0.20f));
			break;
		}
		case EABTSM73DAG5BShapeFamily::OneSidedHighTower:
		default:
		{
			const FABTSM73DAGMacroLayout* High =
				FindMacroLayout(Predicted, 3);
			if (High == nullptr)
			{
				OutError =
					TEXT("DAG5BAsymmetricContractMacroMissing");
				return false;
			}
			VoidCenter = FVector(
				High->PlateCenter.X
					+ High->PlateDimensionsCM.X * 1.20f,
				High->PlateCenter.Y,
				High->PlateCenter.Z);
			break;
		}
		}
		FABTSM73DAG5BSemanticCellRecord& Void =
			InOutEnvelope.Cells.AddDefaulted_GetRef();
		Void.Coordinate =
			FIntVector(INDEX_NONE, INDEX_NONE, INDEX_NONE);
		Void.LocalBounds = FBox(
			VoidCenter - VoidHalf,
			VoidCenter + VoidHalf);
		Void.Semantic =
			InOutEnvelope.ShapeFamily
				== EABTSM73DAG5BShapeFamily::ThroughOpeningWall
				? EABTSM73DAG5BSemanticCell::DoorVoid
				: EABTSM73DAG5BSemanticCell::Void;
		Void.Occupancy =
			EABTSM73DAG5BOccupancy::MustVoid;
		Void.Ports = EABTSM73DAG5BPort::AttackClearance;
		Void.bHardAnchor = true;
		Void.DerivationPath =
			TEXT("Shape/WFC/RequiredClearance");
		return true;
	}

	bool AppendPreSolvePhysicalContract(
		const FABTSM73DAGGenerationResult& Graph,
		const FABTSM73DAGLayoutSettings& Settings,
		const FABTSM73DAGSpatialLayout& InitialLayout,
		FABTSM73SemanticEnvelope& InOutEnvelope,
		FString& OutError)
	{
		FABTSM73DAGSpatialLayout Predicted;
		if (!PredictPhysicalLevels(
			Graph,
			Settings,
			InitialLayout,
			Predicted,
			OutError))
		{
			return false;
		}

		TArray<FABTSM73DAG5BSemanticCellRecord> WFCVoidContracts;
		const int32 RawCellCount =
			InOutEnvelope.GridSize.X
			* InOutEnvelope.GridSize.Y
			* InOutEnvelope.GridSize.Z;
		for (int32 CellIndex = 0;
			CellIndex < RawCellCount;
			++CellIndex)
		{
			const FABTSM73DAG5BSemanticCellRecord& Cell =
				InOutEnvelope.Cells[CellIndex];
			if (!IsVoidTile(Cell.Semantic)) continue;
			FABTSM73DAG5BSemanticCellRecord Contract = Cell;
			Contract.Occupancy =
				EABTSM73DAG5BOccupancy::MustVoid;
			Contract.bHardAnchor = true;
			FBox VoidBounds(EForceInit::ForceInit);
			if (!MakeWFCOpenAirContract(
				Cell,
				VoidBounds))
			{
				OutError = FString::Printf(
					TEXT("DAG5BWFCOpenAirSpanMissing:%d,%d,%d"),
					Cell.Coordinate.X,
					Cell.Coordinate.Y,
					Cell.Coordinate.Z);
				return false;
			}
			Contract.LocalBounds = VoidBounds;
			Contract.DerivationPath += TEXT("/PhysicalOpenAir");
			WFCVoidContracts.Add(MoveTemp(Contract));
		}
		InOutEnvelope.Cells.Append(WFCVoidContracts);
		if (!AppendFamilyClearanceContract(
			Predicted,
			InOutEnvelope,
			OutError))
		{
			return false;
		}

		for (const FABTSM73DAGSupportEdge& Edge :
			Graph.SupportEdges)
		{
			const FABTSM73DAGMacroLayout* Support =
				FindMacroLayout(
					Predicted,
					Edge.SupportNodeId);
			const FABTSM73DAGMacroLayout* Load =
				FindMacroLayout(Predicted, Edge.LoadNodeId);
			FBox2D Intersection(EForceInit::ForceInit);
			if (Support == nullptr || Load == nullptr
				|| !Intersect2D(
					MacroPlateBounds(*Support),
					MacroPlateBounds(*Load),
					Intersection))
			{
				OutError = FString::Printf(
					TEXT("DAG5BSupportPortIntersectionMissing:%d:%d"),
					Edge.SupportNodeId,
					Edge.LoadNodeId);
				return false;
			}
			const float PhysicalMidZ =
				(Support->PlateCenter.Z + Load->PlateCenter.Z)
				* 0.5f;
			const float ColumnBottomZ =
				Support->PlateCenter.Z
				+ Support->PlateDimensionsCM.Z * 0.5f;
			const float ColumnTopZ =
				Load->PlateCenter.Z
				- Load->PlateDimensionsCM.Z * 0.5f;
			FWFCSpatialSupportRegion SpatialRegion;
			if (!FindWFCSpatialSupportRegion(
				InOutEnvelope,
				Intersection,
				PhysicalMidZ,
				ColumnBottomZ,
				ColumnTopZ,
				FVector2D(
					Load->PlateCenter.X,
					Load->PlateCenter.Y),
				Settings,
				SpatialRegion))
			{
				OutError = FString::Printf(
					TEXT("DAG5BWFCSpatialSupportRegionMissing:%d:%d"),
					Edge.SupportNodeId,
					Edge.LoadNodeId);
				return false;
			}
			FABTSM73DAG5BSupportPortConstraint& Port =
				InOutEnvelope.SupportPorts.AddDefaulted_GetRef();
			Port.SupportMacroNodeId = Edge.SupportNodeId;
			Port.LoadMacroNodeId = Edge.LoadNodeId;
			Port.AllowedColumnRegion = SpatialRegion.Bounds;
			Port.SourceCellMin = SpatialRegion.CellMin;
			Port.SourceCellMax = SpatialRegion.CellMax;
			Port.SourceSemantic = SpatialRegion.DominantSemantic;
			Port.SourceCellHash = SpatialRegion.SourceHash;
			Port.DerivationPath = FString::Printf(
				TEXT("WFC/Cells[%d,%d,%d..%d,%d,%d]/SupportPort[%d>%d]/%u"),
				SpatialRegion.CellMin.X,
				SpatialRegion.CellMin.Y,
				SpatialRegion.CellMin.Z,
				SpatialRegion.CellMax.X,
				SpatialRegion.CellMax.Y,
				SpatialRegion.CellMax.Z,
				Edge.SupportNodeId,
				Edge.LoadNodeId,
				SpatialRegion.SourceHash);
			UE_LOG(
				LogABTSRuntime,
				Verbose,
				TEXT("[ABTS][M7.3-DAG5-B][SupportPort] Edge=%d>%d Cells=(%d,%d,%d)-(%d,%d,%d) Semantic=%d Region=(%.1f,%.1f)-(%.1f,%.1f) Target=(%.1f,%.1f)"),
				Edge.SupportNodeId,
				Edge.LoadNodeId,
				SpatialRegion.CellMin.X,
				SpatialRegion.CellMin.Y,
				SpatialRegion.CellMin.Z,
				SpatialRegion.CellMax.X,
				SpatialRegion.CellMax.Y,
				SpatialRegion.CellMax.Z,
				static_cast<int32>(SpatialRegion.DominantSemantic),
				Port.AllowedColumnRegion.Min.X,
				Port.AllowedColumnRegion.Min.Y,
				Port.AllowedColumnRegion.Max.X,
				Port.AllowedColumnRegion.Max.Y,
				Load->PlateCenter.X,
				Load->PlateCenter.Y);
			const FVector2D PortSize =
				Port.AllowedColumnRegion.GetSize();
			const float MinPortExtent =
				Settings.MinAdaptiveColumnWidthCM
				+ Settings.ColumnClearanceCM * 2.0f;
			if (!Port.AllowedColumnRegion.bIsValid
				|| PortSize.X < MinPortExtent
				|| PortSize.Y < MinPortExtent)
			{
				OutError = FString::Printf(
					TEXT("DAG5BSupportPortTooSmall:%d:%d"),
					Edge.SupportNodeId,
					Edge.LoadNodeId);
				return false;
			}
		}

		const FABTSM73DAG5BSemanticCellRecord* WeaknessSource = nullptr;
		uint32 WeaknessHash = MAX_uint32;
		for (const FABTSM73DAG5BSemanticCellRecord& Cell :
			InOutEnvelope.Cells)
		{
			if (Cell.Semantic != EABTSM73DAG5BSemanticCell::Frame
				&& Cell.Semantic
					!= EABTSM73DAG5BSemanticCell::WallPier
				&& Cell.Semantic
					!= EABTSM73DAG5BSemanticCell::Cantilever)
			{
				continue;
			}
			const uint32 CandidateHash = HashCombineFast(
				CoordinateHash(0x5b41, Cell.Coordinate),
				static_cast<uint32>(Cell.Semantic));
			if (CandidateHash < WeaknessHash)
			{
				WeaknessHash = CandidateHash;
				WeaknessSource = &Cell;
			}
		}
		if (WeaknessSource != nullptr)
		{
			FABTSM73DAG5BWeaknessSocket& Socket =
				InOutEnvelope.WeaknessSockets.AddDefaulted_GetRef();
			Socket.SourceCell = WeaknessSource->Coordinate;
			Socket.LocalBounds = WeaknessSource->LocalBounds;
			Socket.RequiredPort =
				WeaknessSource->Semantic
					== EABTSM73DAG5BSemanticCell::Frame
					? EABTSM73DAG5BPort::Frame
					: EABTSM73DAG5BPort::TopLoad;
			Socket.SourceCellHash = WeaknessHash;
			Socket.DerivationPath =
				WeaknessSource->DerivationPath
				+ TEXT("/WeaknessSocketCandidate");
		}

		for (const FABTSM73DAGMacroLayout& Macro :
			Predicted.MacroLayouts)
		{
			FABTSM73DAG5BSemanticCellRecord& Cell =
				InOutEnvelope.Cells.AddDefaulted_GetRef();
			Cell.Coordinate = FIntVector(
				Macro.MacroNodeId,
				INDEX_NONE,
				Macro.StructuralLevel);
			Cell.Semantic = SemanticForMacro(Macro, Graph);
			Cell.Occupancy =
				EABTSM73DAG5BOccupancy::MustOccupy;
			Cell.Ports = EABTSM73DAG5BPort::TopLoad
				| EABTSM73DAG5BPort::BottomSupport;
			Cell.RequiredMacroNodeId = Macro.MacroNodeId;
			Cell.bHardAnchor = true;
			Cell.DerivationPath = FString::Printf(
				TEXT("Shape/RequiredMacro[%d]"),
				Macro.MacroNodeId);
			const FVector CoreHalf(
				FMath::Min(
					12.0f,
					Macro.PlateDimensionsCM.X * 0.15f),
				FMath::Min(
					12.0f,
					Macro.PlateDimensionsCM.Y * 0.15f),
				FMath::Min(
					6.0f,
					Macro.PlateDimensionsCM.Z * 0.20f));
			Cell.RequiredSolidBounds = FBox(
				Macro.PlateCenter - CoreHalf,
				Macro.PlateCenter + CoreHalf);
			Cell.LocalBounds = Cell.RequiredSolidBounds;
		}
		return true;
	}

	bool HasMeasuredFeatures(
		const FABTSM73DAGSpatialLayout& Layout,
		const EABTSM73DAG5BShapeFamily Family,
		EABTSM73DAG5BFeature& OutFeatures)
	{
		OutFeatures = EABTSM73DAG5BFeature::None;
		const auto Area = [](const FABTSM73DAGMacroLayout* Macro)
		{
			return Macro != nullptr
				? Macro->PlateDimensionsCM.X
					* Macro->PlateDimensionsCM.Y
				: 0.0f;
		};
		const auto MinX = [](const FABTSM73DAGMacroLayout* Macro)
		{
			return Macro->PlateCenter.X
				- Macro->PlateDimensionsCM.X * 0.5f;
		};
		const auto MaxX = [](const FABTSM73DAGMacroLayout* Macro)
		{
			return Macro->PlateCenter.X
				+ Macro->PlateDimensionsCM.X * 0.5f;
		};
		switch (Family)
		{
		case EABTSM73DAG5BShapeFamily::SetbackTower:
		{
			const FABTSM73DAGMacroLayout* M0 =
				FindMacroLayout(Layout, 0);
			const FABTSM73DAGMacroLayout* M1 =
				FindMacroLayout(Layout, 1);
			const FABTSM73DAGMacroLayout* M2 =
				FindMacroLayout(Layout, 2);
			const FABTSM73DAGMacroLayout* M3 =
				FindMacroLayout(Layout, 3);
			if (M0 == nullptr || M1 == nullptr
				|| M2 == nullptr || M3 == nullptr)
			{
				return false;
			}
			if (Area(M1) < Area(M0) * 0.92f
				&& Area(M2) < Area(M1) * 0.92f
				&& Area(M3) < Area(M2) * 0.92f)
			{
				OutFeatures |= EABTSM73DAG5BFeature::Setback;
			}
			if (FMath::Abs(
				M3->PlateCenter.X - M0->PlateCenter.X)
				> M0->PlateDimensionsCM.X * 0.04f)
			{
				OutFeatures |=
					EABTSM73DAG5BFeature::FootprintCentroidShift;
			}
			break;
		}
		case EABTSM73DAG5BShapeFamily::OffsetBridge:
		{
			const FABTSM73DAGMacroLayout* Left =
				FindMacroLayout(Layout, 2);
			const FABTSM73DAGMacroLayout* Right =
				FindMacroLayout(Layout, 3);
			const FABTSM73DAGMacroLayout* Bridge =
				FindMacroLayout(Layout, 4);
			const FABTSM73DAGMacroLayout* Crown =
				FindMacroLayout(Layout, 5);
			if (Left == nullptr || Right == nullptr
				|| Bridge == nullptr || Crown == nullptr)
			{
				return false;
			}
			if (MaxX(Left) < MinX(Right)
				&& MinX(Bridge) <= Left->PlateCenter.X
				&& MaxX(Bridge) >= Right->PlateCenter.X)
			{
				OutFeatures |= EABTSM73DAG5BFeature::BridgeSpan;
			}
			const float LowerCentroid =
				(Left->PlateCenter.X + Right->PlateCenter.X)
				* 0.5f;
			if (FMath::Abs(
				Crown->PlateCenter.X - LowerCentroid)
				> Bridge->PlateDimensionsCM.X * 0.04f)
			{
				OutFeatures |=
					EABTSM73DAG5BFeature::FootprintCentroidShift;
			}
			break;
		}
		case EABTSM73DAG5BShapeFamily::ThroughOpeningWall:
		{
			const FABTSM73DAGMacroLayout* Left =
				FindMacroLayout(Layout, 2);
			const FABTSM73DAGMacroLayout* Right =
				FindMacroLayout(Layout, 3);
			const FABTSM73DAGMacroLayout* Header =
				FindMacroLayout(Layout, 4);
			const FABTSM73DAGMacroLayout* LowCrown =
				FindMacroLayout(Layout, 6);
			const FABTSM73DAGMacroLayout* HighCrown =
				FindMacroLayout(Layout, 7);
			if (Left == nullptr || Right == nullptr
				|| Header == nullptr || LowCrown == nullptr
				|| HighCrown == nullptr)
			{
				return false;
			}
			if (MaxX(Left) < MinX(Right)
				&& MinX(Header) <= Left->PlateCenter.X
				&& MaxX(Header) >= Right->PlateCenter.X)
			{
				OutFeatures |=
					EABTSM73DAG5BFeature::ThroughOpening;
				OutFeatures |= EABTSM73DAG5BFeature::BridgeSpan;
			}
			if (HighCrown->StructuralLevel
				> LowCrown->StructuralLevel)
			{
				OutFeatures |=
					EABTSM73DAG5BFeature::NonUniformRoofline;
			}
			break;
		}
		case EABTSM73DAG5BShapeFamily::OneSidedHighTower:
		default:
		{
			const FABTSM73DAGMacroLayout* Low =
				FindMacroLayout(Layout, 1);
			const FABTSM73DAGMacroLayout* Support =
				FindMacroLayout(Layout, 3);
			const FABTSM73DAGMacroLayout* Crown =
				FindMacroLayout(Layout, 4);
			if (Low == nullptr || Support == nullptr
				|| Crown == nullptr)
			{
				return false;
			}
			if (Crown->StructuralLevel
				- Low->StructuralLevel >= 2)
			{
				OutFeatures |=
					EABTSM73DAG5BFeature::HeightAsymmetry;
				OutFeatures |=
					EABTSM73DAG5BFeature::NonUniformRoofline;
			}
			const float LeftOverhang =
				MinX(Support) - MinX(Crown);
			const float RightOverhang =
				MaxX(Crown) - MaxX(Support);
			if (FMath::Max(LeftOverhang, RightOverhang)
				> Support->PlateDimensionsCM.X * 0.30f)
			{
				OutFeatures |=
					EABTSM73DAG5BFeature::Cantilever;
			}
			if (FMath::Abs(
				Crown->PlateCenter.X - Support->PlateCenter.X)
				> Support->PlateDimensionsCM.X * 0.10f)
			{
				OutFeatures |=
					EABTSM73DAG5BFeature::FootprintCentroidShift;
			}
			break;
		}
		}
		return FMath::CountBits(
			static_cast<uint32>(OutFeatures)) >= 2;
	}
}

bool FABTSM73DAG5BSemanticEnvelopeBuilder::Build(
	const FABTSM73DAG5BSettings& Settings,
	const FABTSM73DAGGenerationSettings& DAGSettings,
	const FABTSM73DAGLayoutSettings& LayoutSettings,
	FABTSM73DAGGenerationResult& OutGraph,
	FABTSM73DAGSpatialLayout& OutInitialLayout,
	FABTSM73SemanticEnvelope& OutEnvelope,
	FABTSM73DAG5BResult& OutResult,
	FString& OutError) const
{
	OutGraph = FABTSM73DAGGenerationResult();
	OutInitialLayout = FABTSM73DAGSpatialLayout();
	OutEnvelope = FABTSM73SemanticEnvelope();
	OutResult = FABTSM73DAG5BResult();
	OutResult.bEnabled = Settings.bEnableSemanticEnvelope;
	OutError.Reset();
	if (!Settings.bEnableSemanticEnvelope)
	{
		OutError = TEXT("DAG5BSemanticEnvelopeNotEnabled");
		OutResult.RejectReason = OutError;
		return false;
	}
	if (Settings.EnvelopeVersion < 1
		|| Settings.EnvelopeVersion > 64
		|| Settings.GridSizeX < 5
		|| Settings.GridSizeX > 9
		|| Settings.GridSizeY < 3
		|| Settings.GridSizeY > 5
		|| Settings.GridSizeZ < 4
		|| Settings.GridSizeZ > 8
		|| Settings.MaxWFCPropagationOperations < 64
		|| Settings.MaxWFCPropagationOperations > 65536
		|| Settings.MaxWFCBacktrackSteps < 0
		|| Settings.MaxWFCBacktrackSteps > 256
		|| !FMath::IsFinite(Settings.SetbackRatio)
		|| !FMath::IsFinite(Settings.OffsetRatio)
		|| !FMath::IsFinite(Settings.CantileverRatio)
		|| !FMath::IsFinite(LayoutSettings.TargetWidthCM)
		|| !FMath::IsFinite(LayoutSettings.TargetDepthCM)
		|| !FMath::IsFinite(LayoutSettings.TargetHeightCM)
		|| !FMath::IsFinite(LayoutSettings.PlateThicknessCM)
		|| !FMath::IsFinite(
			LayoutSettings.MinAdaptivePlateExtentCM)
		|| !FMath::IsFinite(LayoutSettings.ColumnClearanceCM)
		|| !FMath::IsFinite(
			LayoutSettings.MinAdaptiveColumnWidthCM)
		|| LayoutSettings.PlateThicknessCM <= 0.0f
		|| LayoutSettings.MinAdaptivePlateExtentCM <= 0.0f
		|| LayoutSettings.MinAdaptiveColumnWidthCM <= 0.0f
		|| LayoutSettings.ColumnClearanceCM < 0.0f
		|| Settings.SetbackRatio < 0.05f
		|| Settings.SetbackRatio > 0.35f
		|| Settings.OffsetRatio < 0.02f
		|| Settings.OffsetRatio > 0.25f
		|| Settings.CantileverRatio < 0.05f
		|| Settings.CantileverRatio > 0.30f
		|| LayoutSettings.TargetHeightCM
			<= LayoutSettings.PlateThicknessCM
		|| LayoutSettings.PreferredLogicalSupportsPerLoad < 1
		|| LayoutSettings.MaxLogicalSupportsPerLoad < 1
		|| LayoutSettings.PreferredLogicalSupportsPerLoad
			> LayoutSettings.MaxLogicalSupportsPerLoad
		|| DAGSettings.GeneratorVersion < 1
		|| DAGSettings.MaxEstimatedBrickCount < 1
		|| DAGSettings.MaxEstimatedBrickCount > 256
		|| LayoutSettings.TargetWidthCM
			< LayoutSettings.MinAdaptivePlateExtentCM * 2.0f
		|| LayoutSettings.TargetDepthCM
			< LayoutSettings.MinAdaptivePlateExtentCM)
	{
		OutError = TEXT("DAG5BSettingsInvalid");
		OutResult.RejectReason = OutError;
		return false;
	}

	const EABTSM73DAG5BShapeFamily Family =
		Settings.ShapeFamily == EABTSM73DAG5BShapeFamily::Auto
		? static_cast<EABTSM73DAG5BShapeFamily>(
			1 + static_cast<uint32>(DAGSettings.BuildingSeed)
				% 4u)
		: Settings.ShapeFamily;
	if (Family < EABTSM73DAG5BShapeFamily::SetbackTower
		|| Family > EABTSM73DAG5BShapeFamily::OneSidedHighTower)
	{
		OutError = TEXT("DAG5BShapeFamilyInvalid");
		OutResult.RejectReason = OutError;
		return false;
	}

	OutEnvelope.ShapeFamily = Family;
	OutEnvelope.GridSize = FIntVector(
		Settings.GridSizeX,
		Settings.GridSizeY,
		Settings.GridSizeZ);
	OutEnvelope.LocalBounds = FBox(
		FVector(
			-LayoutSettings.TargetWidthCM * 0.5f,
			-LayoutSettings.TargetDepthCM * 0.5f,
			0.0f),
		FVector(
			LayoutSettings.TargetWidthCM * 0.5f,
			LayoutSettings.TargetDepthCM * 0.5f,
			LayoutSettings.TargetHeightCM));
	OutEnvelope.EnvelopeVersion = Settings.EnvelopeVersion;

	// Shape Grammar is the upstream artifact. Its macro scopes are finalized
	// to predicted structural levels before the semantic grid is authored, so
	// WFC consumes the Shape result instead of duplicating it as a parallel
	// family template.
	BuildFamilyGraph(
		Family,
		Settings,
		LayoutSettings,
		OutGraph,
		OutInitialLayout,
		OutEnvelope);
	if (OutGraph.MacroNodes.IsEmpty()
		|| OutGraph.SupportEdges.IsEmpty()
		|| OutGraph.GroundNodeIds.IsEmpty()
		|| OutGraph.TopLoadNodeIds.IsEmpty()
		|| OutGraph.MacroNodes.Num()
			!= OutInitialLayout.MacroLayouts.Num())
	{
		OutError = TEXT("DAG5BShapeGraphInvalid");
		OutResult.RejectReason = OutError;
		return false;
	}
	for (const FABTSM73DAGMacroLayout& Macro :
		OutInitialLayout.MacroLayouts)
	{
		if (Macro.PlateDimensionsCM.X
				< LayoutSettings.MinAdaptivePlateExtentCM
			|| Macro.PlateDimensionsCM.Y
				< LayoutSettings.MinAdaptivePlateExtentCM
			|| Macro.PlateDimensionsCM.X
				> LayoutSettings.TargetWidthCM
			|| Macro.PlateDimensionsCM.Y
				> LayoutSettings.TargetDepthCM)
		{
			OutError = FString::Printf(
				TEXT("DAG5BMacroScopeInvalid:%d"),
				Macro.MacroNodeId);
			OutResult.RejectReason = OutError;
			return false;
		}
	}
	FABTSM73DAGSpatialLayout PredictedShapeLayout;
	if (!PredictPhysicalLevels(
			OutGraph,
			LayoutSettings,
			OutInitialLayout,
			PredictedShapeLayout,
			OutError)
		|| !FinalizeShapeScopes(
			OutGraph,
			PredictedShapeLayout,
			OutEnvelope,
			OutError))
	{
		OutResult.RejectReason = OutError;
		return false;
	}

	TArray<FGrammarCell> GrammarCells;
	const int32 CellCount =
		OutEnvelope.GridSize.X
		* OutEnvelope.GridSize.Y
		* OutEnvelope.GridSize.Z;
	GrammarCells.SetNum(CellCount);
	for (int32 Z = 0; Z < OutEnvelope.GridSize.Z; ++Z)
	{
		for (int32 Y = 0; Y < OutEnvelope.GridSize.Y; ++Y)
		{
			for (int32 X = 0; X < OutEnvelope.GridSize.X; ++X)
			{
				const FIntVector P(X, Y, Z);
				AuthorGrammarCell(
					Family,
					P,
					OutEnvelope.GridSize,
					OutEnvelope,
					GrammarCells[GridIndex(P, OutEnvelope.GridSize)]);
			}
		}
	}

	TArray<EABTSM73DAG5BSemanticCell> CollapsedTiles;
	if (!CollapseWFC(
		Settings,
		DAGSettings.BuildingSeed,
		OutEnvelope.GridSize,
		GrammarCells,
		CollapsedTiles,
		OutEnvelope.WFCCollapseTrace,
		OutResult.PropagationOperationCount,
		OutResult.BacktrackStepCount,
		OutResult.CollapsedNonAnchorCellCount,
		OutError))
	{
		OutResult.RejectReason = OutError;
		return false;
	}
	if (OutResult.CollapsedNonAnchorCellCount < 1)
	{
		OutError = TEXT("DAG5BWFCNoNonAnchorCollapse");
		OutResult.RejectReason = OutError;
		return false;
	}

	for (int32 Index = 0; Index < CellCount; ++Index)
	{
		const int32 LayerSize =
			OutEnvelope.GridSize.X * OutEnvelope.GridSize.Y;
		const FIntVector P(
			Index % OutEnvelope.GridSize.X,
			(Index / OutEnvelope.GridSize.X)
				% OutEnvelope.GridSize.Y,
			Index / LayerSize);
		const FGrammarCell& Grammar = GrammarCells[Index];
		const EABTSM73DAG5BSemanticCell Tile =
			CollapsedTiles[Index];
		FABTSM73DAG5BSemanticCellRecord& Record =
			OutEnvelope.Cells.AddDefaulted_GetRef();
		Record.Coordinate = P;
		Record.LocalBounds = MakeCellBounds(
			P,
			OutEnvelope.GridSize,
			LayoutSettings);
		Record.Semantic = Tile;
		// The tiled WFC cell is semantic evidence, not an assertion that a
		// whole coarse voxel must be filled. Exact MustOccupy/MustVoid
		// primitives are emitted below, before DAG2.3 runs.
		Record.Occupancy =
			EABTSM73DAG5BOccupancy::MayOccupy;
		Record.Ports = PortsForTile(Tile);
		Record.bHardAnchor = Grammar.bHardAnchor;
		Record.DerivationPath = Grammar.Path;
	}
	FString WFCCanonical;
	for (const FABTSM73DAG5BSemanticCellRecord& Cell :
		OutEnvelope.Cells)
	{
		WFCCanonical += FString::Printf(
			TEXT("|C=%d,%d,%d,%d,%d,%d"),
			Cell.Coordinate.X,
			Cell.Coordinate.Y,
			Cell.Coordinate.Z,
			static_cast<int32>(Cell.Semantic),
			Cell.bHardAnchor ? 1 : 0,
			static_cast<int32>(Cell.Ports));
	}
	OutEnvelope.WFCHash = FCrc::StrCrc32(*WFCCanonical);

	if (!AppendPreSolvePhysicalContract(
		OutGraph,
		LayoutSettings,
		OutInitialLayout,
		OutEnvelope,
		OutError))
	{
		OutResult.RejectReason = OutError;
		return false;
	}

	OutGraph.bAccepted = true;
	OutGraph.BuildingSeed = DAGSettings.BuildingSeed;
	OutGraph.GeneratorVersion = Settings.EnvelopeVersion;
	OutGraph.Preset = DAGSettings.Preset;
	OutGraph.InitialTerminalCount = OutGraph.MacroNodes.Num();
	OutGraph.EstimatedBrickCount =
		OutGraph.MacroNodes.Num()
		+ OutGraph.SupportEdges.Num() * 4;
	OutGraph.CanonicalExpression =
		FString::Printf(TEXT("DAG5B(%s)"), *FamilyName(Family));
	OutGraph.DebugExpression = OutGraph.CanonicalExpression;
	const FString ShapeCanonical = MakeShapeCanonical(
		OutEnvelope,
		OutGraph,
		OutInitialLayout);
	OutEnvelope.ShapeHash = FCrc::StrCrc32(*ShapeCanonical);
	OutGraph.CanonicalTopologyHash = OutEnvelope.ShapeHash;

	const FString EnvelopeCanonical =
		MakeEnvelopeCanonical(OutEnvelope);
	OutEnvelope.EnvelopeHash =
		FCrc::StrCrc32(*EnvelopeCanonical);
	OutEnvelope.bAccepted = true;
	OutInitialLayout.bAccepted = false;
	OutResult.bAccepted = false;
	OutResult.ShapeFamily = Family;
	OutResult.FeatureMask = OutEnvelope.FeatureMask;
	OutResult.ShapeHash = OutEnvelope.ShapeHash;
	OutResult.WFCHash = OutEnvelope.WFCHash;
	OutResult.EnvelopeHash = OutEnvelope.EnvelopeHash;
	for (const FABTSM73DAG5BSemanticCellRecord& Cell :
		OutEnvelope.Cells)
	{
		if (Cell.Occupancy
				== EABTSM73DAG5BOccupancy::MustVoid
			&& Cell.DerivationPath.Contains(
				TEXT("/PhysicalOpenAir")))
		{
			++OutResult.WFCDerivedMustVoidCount;
		}
	}
	return true;
}

bool FABTSM73DAG5BSemanticEnvelopeBuilder::ValidateEnvelopeIdentity(
	const FABTSM73SemanticEnvelope& Envelope,
	FString& OutError) const
{
	OutError.Reset();
	if (!Envelope.bAccepted
		|| Envelope.EnvelopeVersion < 1
		|| Envelope.EnvelopeHash == 0)
	{
		OutError = TEXT("DAG5BEnvelopeIdentityInputRejected");
		return false;
	}
	const uint32 RecomputedHash =
		FCrc::StrCrc32(*MakeEnvelopeCanonical(Envelope));
	if (RecomputedHash != Envelope.EnvelopeHash)
	{
		OutError = FString::Printf(
			TEXT("DAG5BEnvelopeIdentityMismatch:Stored=%u:Actual=%u"),
			Envelope.EnvelopeHash,
			RecomputedHash);
		return false;
	}
	return true;
}

bool FABTSM73DAG5BSemanticEnvelopeBuilder::BindPhysicalContract(
	const FABTSM73DAGSpatialLayout& Layout,
	FABTSM73SemanticEnvelope& InOutEnvelope,
	FString& OutError) const
{
	OutError.Reset();
	if (!Layout.bAccepted || !InOutEnvelope.bAccepted)
	{
		OutError = TEXT("DAG5BContractInputRejected");
		return false;
	}
	if (!ValidateEnvelopeIdentity(InOutEnvelope, OutError))
	{
		return false;
	}
	for (const FABTSM73DAG5BSupportPortConstraint& Port :
		InOutEnvelope.SupportPorts)
	{
		if (!ValidateSupportPortProvenance(
			InOutEnvelope,
			Port,
			OutError))
		{
			return false;
		}
	}
	const EABTSM73DAG5BFeature DeclaredFeatures =
		InOutEnvelope.FeatureMask;
	EABTSM73DAG5BFeature MeasuredFeatures =
		EABTSM73DAG5BFeature::None;
	if (!HasMeasuredFeatures(
		Layout,
		InOutEnvelope.ShapeFamily,
		MeasuredFeatures)
		|| (static_cast<uint32>(MeasuredFeatures)
			& static_cast<uint32>(DeclaredFeatures))
			!= static_cast<uint32>(DeclaredFeatures))
	{
		OutError = FString::Printf(
			TEXT("DAG5BMeasuredFeatureMismatch:Declared=%u:Measured=%u"),
			static_cast<uint32>(DeclaredFeatures),
			static_cast<uint32>(MeasuredFeatures));
		return false;
	}
	if (MeasuredFeatures != DeclaredFeatures)
	{
		OutError = FString::Printf(
			TEXT("DAG5BMeasuredFeatureSetDrift:Declared=%u:Measured=%u"),
			static_cast<uint32>(DeclaredFeatures),
			static_cast<uint32>(MeasuredFeatures));
		return false;
	}
	int32 MustOccupyCount = 0;
	int32 MustVoidCount = 0;
	int32 WFCDerivedMustVoidCount = 0;
	for (const FABTSM73DAG5BSemanticCellRecord& Cell :
		InOutEnvelope.Cells)
	{
		if (Cell.Occupancy
			== EABTSM73DAG5BOccupancy::MustOccupy)
		{
			++MustOccupyCount;
			const FABTSM73DAGMacroLayout* Macro =
				FindMacroLayout(
					Layout,
					Cell.RequiredMacroNodeId);
			if (Macro == nullptr)
			{
				OutError = FString::Printf(
					TEXT("DAG5BRequiredMacroMissing:%d"),
					Cell.RequiredMacroNodeId);
				return false;
			}
			const FBox Plate(
				Macro->PlateCenter
					- Macro->PlateDimensionsCM * 0.5f,
				Macro->PlateCenter
					+ Macro->PlateDimensionsCM * 0.5f);
			if (!Plate.IsInsideOrOn(
					Cell.RequiredSolidBounds.Min)
				|| !Plate.IsInsideOrOn(
					Cell.RequiredSolidBounds.Max))
			{
				OutError = FString::Printf(
					TEXT("DAG5BRequiredMacroOutsideEnvelope:%d"),
					Cell.RequiredMacroNodeId);
				return false;
			}
		}
		else if (Cell.Occupancy
			== EABTSM73DAG5BOccupancy::MustVoid)
		{
			++MustVoidCount;
			WFCDerivedMustVoidCount +=
				Cell.DerivationPath.Contains(
					TEXT("/PhysicalOpenAir"))
					? 1
					: 0;
			for (const FABTSM73DAGMacroLayout& Macro :
				Layout.MacroLayouts)
			{
				const FBox Plate(
					Macro.PlateCenter
						- Macro.PlateDimensionsCM * 0.5f,
					Macro.PlateCenter
						+ Macro.PlateDimensionsCM * 0.5f);
				if (BoxesOverlapWithVolume(
					Cell.LocalBounds,
					Plate))
				{
					OutError = FString::Printf(
						TEXT("DAG5BMustVoidPlateConflict:%d:%d,%d,%d:Semantic=%d"),
						Macro.MacroNodeId,
						Cell.Coordinate.X,
						Cell.Coordinate.Y,
						Cell.Coordinate.Z,
						static_cast<int32>(Cell.Semantic));
					return false;
				}
			}
		}
	}
	if (MustOccupyCount != Layout.MacroLayouts.Num()
		|| MustVoidCount < 1
		|| WFCDerivedMustVoidCount < 1
		|| InOutEnvelope.WeaknessSockets.IsEmpty())
	{
		OutError = FString::Printf(
			TEXT("DAG5BContractCardinalityMismatch:Occupy=%d:Macros=%d:Void=%d:WFCVoid=%d:Ports=%d:Supports=%d:Sockets=%d"),
			MustOccupyCount,
			Layout.MacroLayouts.Num(),
			MustVoidCount,
			WFCDerivedMustVoidCount,
			InOutEnvelope.SupportPorts.Num(),
			Layout.SelectedSupports.Num(),
			InOutEnvelope.WeaknessSockets.Num());
		return false;
	}
	for (const FABTSM73DAGSelectedSupport& Support :
		Layout.SelectedSupports)
	{
		const FABTSM73DAG5BSupportPortConstraint* Port = nullptr;
		int32 MatchingPortCount = 0;
		for (const FABTSM73DAG5BSupportPortConstraint& Candidate :
			InOutEnvelope.SupportPorts)
		{
			if (Candidate.SupportMacroNodeId
					== Support.SupportMacroNodeId
				&& Candidate.LoadMacroNodeId
					== Support.LoadMacroNodeId)
			{
				Port = &Candidate;
				++MatchingPortCount;
			}
		}
		if (MatchingPortCount != 1
			|| Support.RealizedColumnCenters.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("DAG5BSelectedSupportPortMissing:%d:%d"),
				Support.SupportMacroNodeId,
				Support.LoadMacroNodeId);
			return false;
		}
		if (!IsVerticalSupportSemantic(Port->SourceSemantic)
			|| Port->SourceCellMin.X < 0
			|| Port->SourceCellMin.Y < 0
			|| Port->SourceCellMin.Z < 0
			|| Port->SourceCellMax.X
				>= InOutEnvelope.GridSize.X
			|| Port->SourceCellMax.Y
				>= InOutEnvelope.GridSize.Y
			|| Port->SourceCellMax.Z
				>= InOutEnvelope.GridSize.Z
			|| Port->SourceCellMin.X > Port->SourceCellMax.X
			|| Port->SourceCellMin.Y > Port->SourceCellMax.Y
			|| Port->SourceCellMin.Z != Port->SourceCellMax.Z)
		{
			OutError = FString::Printf(
				TEXT("DAG5BSupportPortSourceInvalid:%d:%d"),
				Support.SupportMacroNodeId,
				Support.LoadMacroNodeId);
			return false;
		}
		const FABTSM73DAGMacroLayout* SupportMacro =
			FindMacroLayout(
				Layout,
				Support.SupportMacroNodeId);
		const FABTSM73DAGMacroLayout* LoadMacro =
			FindMacroLayout(
				Layout,
				Support.LoadMacroNodeId);
		if (SupportMacro == nullptr || LoadMacro == nullptr)
		{
			OutError = TEXT("DAG5BSupportPortMacroMissing");
			return false;
		}
		const float BottomZ =
			SupportMacro->PlateCenter.Z
			+ SupportMacro->PlateDimensionsCM.Z * 0.5f;
		const float TopZ =
			LoadMacro->PlateCenter.Z
			- LoadMacro->PlateDimensionsCM.Z * 0.5f;
		for (const FVector2D& Center :
			Support.RealizedColumnCenters)
		{
			const float HalfWidth =
				Support.RealizedColumnWidthCM * 0.5f;
			if (!Port->AllowedColumnRegion.IsInsideOrOn(
					Center - FVector2D(HalfWidth))
				|| !Port->AllowedColumnRegion.IsInsideOrOn(
					Center + FVector2D(HalfWidth)))
			{
				OutError = FString::Printf(
					TEXT("DAG5BColumnOutsideWFCSupportPort:%d:%d"),
					Support.SupportMacroNodeId,
					Support.LoadMacroNodeId);
				return false;
			}
			const FBox ColumnBounds(
				FVector(
					Center.X - HalfWidth,
					Center.Y - HalfWidth,
					BottomZ),
				FVector(
					Center.X + HalfWidth,
					Center.Y + HalfWidth,
					TopZ));
			for (const FABTSM73DAG5BSemanticCellRecord& Cell :
				InOutEnvelope.Cells)
			{
				if (Cell.Occupancy
						== EABTSM73DAG5BOccupancy::MustVoid
					&& BoxesOverlapWithVolume(
						Cell.LocalBounds,
						ColumnBounds))
				{
					OutError = FString::Printf(
						TEXT("DAG5BColumnIntersectsMustVoid:%d:%d:%d,%d,%d"),
						Support.SupportMacroNodeId,
						Support.LoadMacroNodeId,
						Cell.Coordinate.X,
						Cell.Coordinate.Y,
						Cell.Coordinate.Z);
					return false;
				}
			}
		}
	}
	return true;
}

bool FABTSM73DAG5BSemanticEnvelopeBuilder::
	ValidateSupportPortProvenance(
		const FABTSM73SemanticEnvelope& Envelope,
		const FABTSM73DAG5BSupportPortConstraint& Port,
		FString& OutError) const
{
	OutError.Reset();
	const int32 RawCellCount =
		Envelope.GridSize.X
		* Envelope.GridSize.Y
		* Envelope.GridSize.Z;
	if (Envelope.GridSize.X <= 0
		|| Envelope.GridSize.Y <= 0
		|| Envelope.GridSize.Z <= 0
		|| Envelope.Cells.Num() < RawCellCount
		|| Port.SourceCellMin.X < 0
		|| Port.SourceCellMin.Y < 0
		|| Port.SourceCellMin.Z < 0
		|| Port.SourceCellMax.X >= Envelope.GridSize.X
		|| Port.SourceCellMax.Y >= Envelope.GridSize.Y
		|| Port.SourceCellMax.Z >= Envelope.GridSize.Z
		|| Port.SourceCellMin.X > Port.SourceCellMax.X
		|| Port.SourceCellMin.Y > Port.SourceCellMax.Y
		|| Port.SourceCellMin.Z > Port.SourceCellMax.Z)
	{
		OutError = FString::Printf(
			TEXT("DAG5BSupportPortSourceRangeInvalid:%d:%d"),
			Port.SupportMacroNodeId,
			Port.LoadMacroNodeId);
		return false;
	}

	uint32 RecomputedHash = 0;
	bool bHasWallPier = false;
	for (int32 Z = Port.SourceCellMin.Z;
		Z <= Port.SourceCellMax.Z;
		++Z)
	{
		for (int32 Y = Port.SourceCellMin.Y;
			Y <= Port.SourceCellMax.Y;
			++Y)
		{
			for (int32 X = Port.SourceCellMin.X;
				X <= Port.SourceCellMax.X;
				++X)
			{
				const FIntVector Coordinate(X, Y, Z);
				const FABTSM73DAG5BSemanticCellRecord& Cell =
					Envelope.Cells[
						GridIndex(Coordinate, Envelope.GridSize)];
				if (!IsVerticalSupportSemantic(Cell.Semantic))
				{
					OutError = FString::Printf(
						TEXT("DAG5BSupportPortSourceNotStructural:%d:%d:%d,%d,%d"),
						Port.SupportMacroNodeId,
						Port.LoadMacroNodeId,
						X,
						Y,
						Z);
					return false;
				}
				bHasWallPier |= Cell.Semantic
					== EABTSM73DAG5BSemanticCell::WallPier;
				RecomputedHash = HashCombineFast(
					RecomputedHash,
					HashCombineFast(
						CoordinateHash(0x5b31, Coordinate),
						static_cast<uint32>(Cell.Semantic)));
			}
		}
	}
	const EABTSM73DAG5BSemanticCell ExpectedSemantic =
		bHasWallPier
			? EABTSM73DAG5BSemanticCell::WallPier
			: Port.SourceCellMin.Z == 0
				? EABTSM73DAG5BSemanticCell::Foundation
				: EABTSM73DAG5BSemanticCell::ColumnZone;
	const FABTSM73DAG5BSemanticCellRecord& First =
		Envelope.Cells[GridIndex(
			Port.SourceCellMin,
			Envelope.GridSize)];
	const FABTSM73DAG5BSemanticCellRecord& Last =
		Envelope.Cells[GridIndex(
			Port.SourceCellMax,
			Envelope.GridSize)];
	const FBox2D SourceBounds(
		FVector2D(
			First.LocalBounds.Min.X,
			First.LocalBounds.Min.Y),
		FVector2D(
			Last.LocalBounds.Max.X,
			Last.LocalBounds.Max.Y));
	const FABTSM73DAG5BMacroConstraint* SupportConstraint = nullptr;
	const FABTSM73DAG5BMacroConstraint* LoadConstraint = nullptr;
	int32 SupportConstraintCount = 0;
	int32 LoadConstraintCount = 0;
	for (const FABTSM73DAG5BMacroConstraint& Constraint :
		Envelope.MacroConstraints)
	{
		if (Constraint.MacroNodeId == Port.SupportMacroNodeId)
		{
			SupportConstraint = &Constraint;
			++SupportConstraintCount;
		}
		if (Constraint.MacroNodeId == Port.LoadMacroNodeId)
		{
			LoadConstraint = &Constraint;
			++LoadConstraintCount;
		}
	}
	const FVector EnvelopeSize = Envelope.LocalBounds.GetSize();
	FBox2D ExpectedPort(EForceInit::ForceInit);
	if (SupportConstraintCount != 1
		|| LoadConstraintCount != 1
		|| SupportConstraint == nullptr
		|| LoadConstraint == nullptr)
	{
		OutError = FString::Printf(
			TEXT("DAG5BSupportPortMacroConstraintInvalid:%d:%d"),
			Port.SupportMacroNodeId,
			Port.LoadMacroNodeId);
		return false;
	}
	const FVector2D SupportHalf(
		SupportConstraint->FootprintScale.X * EnvelopeSize.X * 0.5f,
		SupportConstraint->FootprintScale.Y * EnvelopeSize.Y * 0.5f);
	const FVector2D LoadHalf(
		LoadConstraint->FootprintScale.X * EnvelopeSize.X * 0.5f,
		LoadConstraint->FootprintScale.Y * EnvelopeSize.Y * 0.5f);
	FBox2D MacroIntersection(EForceInit::ForceInit);
	if (!Intersect2D(
			FBox2D(
				SupportConstraint->OffsetCM - SupportHalf,
				SupportConstraint->OffsetCM + SupportHalf),
			FBox2D(
				LoadConstraint->OffsetCM - LoadHalf,
				LoadConstraint->OffsetCM + LoadHalf),
			MacroIntersection)
		|| !Intersect2D(
			SourceBounds,
			MacroIntersection,
			ExpectedPort))
	{
		OutError = FString::Printf(
			TEXT("DAG5BSupportPortExpectedRegionMissing:%d:%d"),
			Port.SupportMacroNodeId,
			Port.LoadMacroNodeId);
		return false;
	}
	if (RecomputedHash != Port.SourceCellHash
		|| ExpectedSemantic != Port.SourceSemantic
		|| !Port.AllowedColumnRegion.bIsValid
		|| !Port.AllowedColumnRegion.Min.Equals(
			ExpectedPort.Min,
			KINDA_SMALL_NUMBER)
		|| !Port.AllowedColumnRegion.Max.Equals(
			ExpectedPort.Max,
			KINDA_SMALL_NUMBER))
	{
		OutError = FString::Printf(
			TEXT("DAG5BSupportPortProvenanceMismatch:%d:%d"),
			Port.SupportMacroNodeId,
			Port.LoadMacroNodeId);
		return false;
	}
	return true;
}

bool FABTSM73DAG5BEnvelopeAuditor::Audit(
	const FABTSM73SemanticEnvelope& Envelope,
	const FABTSM73StructureData& Data,
	FABTSM73DAG5BAuditResult& OutResult,
	FString& OutError) const
{
	OutResult = FABTSM73DAG5BAuditResult();
	OutError.Reset();
	if (!Envelope.bAccepted || Data.Bricks.IsEmpty())
	{
		OutError = TEXT("DAG5BAuditInputRejected");
		OutResult.RejectReason = OutError;
		return false;
	}
	FString Canonical = FString::Printf(
		TEXT("Envelope=%u"),
		Envelope.EnvelopeHash);
	FABTSM73DAG5BSemanticEnvelopeBuilder ContractBuilder;
	if (!ContractBuilder.ValidateEnvelopeIdentity(
		Envelope,
		OutError))
	{
		OutResult.RejectReason = OutError;
		return false;
	}
	for (const FABTSM73DAG5BSupportPortConstraint& Port :
		Envelope.SupportPorts)
	{
		if (!ContractBuilder.ValidateSupportPortProvenance(
			Envelope,
			Port,
			OutError))
		{
			OutResult.RejectReason = OutError;
			return false;
		}
	}
	TSet<int32> MappedBrickNodeIds;
	int32 SupportMappingCount = 0;
	int32 ShapeMacroMappingCount = 0;
	for (const FABTSM73DAG5BSemanticBrickMapping& Mapping :
		Data.DAG5BSemanticBrickMappings)
	{
		if (Mapping.BrickNodeIds.IsEmpty())
		{
			OutError = TEXT("DAG5BAuditSemanticMappingMismatch");
			OutResult.RejectReason = OutError;
			return false;
		}
		const FABTSM73DAG5BSupportPortConstraint* Port = nullptr;
		const FABTSM73DAGPhysicalSupportMapping* Physical = nullptr;
		if (Mapping.Kind
			== EABTSM73DAG5BSemanticMappingKind::SupportPort)
		{
			Port = Envelope.SupportPorts.FindByPredicate(
				[&Mapping](
					const FABTSM73DAG5BSupportPortConstraint& Candidate)
				{
					return Candidate.SupportMacroNodeId
							== Mapping.SupportMacroNodeId
						&& Candidate.LoadMacroNodeId
							== Mapping.LoadMacroNodeId
						&& Candidate.SourceCellHash
							== Mapping.SourceCellHash;
				});
			Physical =
				Data.DAGPhysicalSupportMappings.FindByPredicate(
					[&Mapping](
						const FABTSM73DAGPhysicalSupportMapping&
							Candidate)
					{
						return Candidate.SupportMacroNodeId
								== Mapping.SupportMacroNodeId
							&& Candidate.LoadMacroNodeId
								== Mapping.LoadMacroNodeId;
					});
			if (Port == nullptr
				|| Physical == nullptr
				|| Mapping.TargetMacroNodeId != INDEX_NONE
				|| Mapping.SourceSemantic != Port->SourceSemantic
				|| Mapping.SourceCellMin != Port->SourceCellMin
				|| Mapping.SourceCellMax != Port->SourceCellMax
				|| Mapping.BrickNodeIds != Physical->ColumnNodeIds)
			{
				OutError =
					TEXT("DAG5BAuditSemanticMappingMismatch");
				OutResult.RejectReason = OutError;
				return false;
			}
			++SupportMappingCount;
		}
		else if (Mapping.Kind
			== EABTSM73DAG5BSemanticMappingKind::ShapeMacro)
		{
			if (Mapping.SupportMacroNodeId != INDEX_NONE
				|| Mapping.LoadMacroNodeId != INDEX_NONE
				|| Mapping.TargetMacroNodeId == INDEX_NONE
				|| Mapping.SourceCellMin != Mapping.SourceCellMax
				|| Mapping.SourceCellMin.X < 0
				|| Mapping.SourceCellMin.Y < 0
				|| Mapping.SourceCellMin.Z < 0
				|| Mapping.SourceCellMin.X >= Envelope.GridSize.X
				|| Mapping.SourceCellMin.Y >= Envelope.GridSize.Y
				|| Mapping.SourceCellMin.Z >= Envelope.GridSize.Z)
			{
				OutError =
					TEXT("DAG5BAuditShapeMacroMappingInvalid");
				OutResult.RejectReason = OutError;
				return false;
			}
			const FABTSM73DAG5BSemanticCellRecord& Source =
				Envelope.Cells[GridIndex(
					Mapping.SourceCellMin,
					Envelope.GridSize)];
			const uint32 ExpectedSourceHash = HashCombineFast(
				CoordinateHash(0x5b61, Source.Coordinate),
				static_cast<uint32>(Source.Semantic));
			const FABTSM73DAG5BSemanticCellRecord* Contract =
				Envelope.Cells.FindByPredicate(
					[&Mapping](
						const FABTSM73DAG5BSemanticCellRecord&
							Candidate)
					{
						return Candidate.Occupancy
								== EABTSM73DAG5BOccupancy::MustOccupy
							&& Candidate.RequiredMacroNodeId
								== Mapping.TargetMacroNodeId
							&& Candidate.Semantic
								== Mapping.SourceSemantic;
					});
			if (Source.Semantic != Mapping.SourceSemantic
				|| !Source.bHardAnchor
				|| Mapping.SourceCellHash != ExpectedSourceHash
				|| Contract == nullptr
				|| Mapping.BrickNodeIds.Num() != 1
				|| !Data.Bricks.IsValidIndex(
					Mapping.BrickNodeIds[0])
				|| Data.Bricks[Mapping.BrickNodeIds[0]].MacroNodeId
					!= Mapping.TargetMacroNodeId
				|| !BoxesOverlapWithVolume(
					Source.LocalBounds,
					BrickBox(
						Data.Bricks[Mapping.BrickNodeIds[0]])))
			{
				OutError =
					TEXT("DAG5BAuditShapeMacroMappingMismatch");
				OutResult.RejectReason = OutError;
				return false;
			}
			++ShapeMacroMappingCount;
		}
		else
		{
			OutError = TEXT("DAG5BAuditSemanticMappingKindInvalid");
			OutResult.RejectReason = OutError;
			return false;
		}
		Canonical += FString::Printf(
			TEXT("|M=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u,%u"),
			static_cast<int32>(Mapping.Kind),
			Mapping.SupportMacroNodeId,
			Mapping.LoadMacroNodeId,
			Mapping.TargetMacroNodeId,
			static_cast<int32>(Mapping.SourceSemantic),
			Mapping.SourceCellMin.X,
			Mapping.SourceCellMin.Y,
			Mapping.SourceCellMin.Z,
			Mapping.SourceCellMax.X,
			Mapping.SourceCellMax.Y,
			Mapping.SourceCellMax.Z,
			Mapping.SourceCellHash,
			Mapping.MappingHash);
		FString MappingCanonical = FString::Printf(
			TEXT("K=%d|%d>%d|T=%d|S=%d|C=%d,%d,%d..%d,%d,%d|H=%u"),
			static_cast<int32>(Mapping.Kind),
			Mapping.SupportMacroNodeId,
			Mapping.LoadMacroNodeId,
			Mapping.TargetMacroNodeId,
			static_cast<int32>(Mapping.SourceSemantic),
			Mapping.SourceCellMin.X,
			Mapping.SourceCellMin.Y,
			Mapping.SourceCellMin.Z,
			Mapping.SourceCellMax.X,
			Mapping.SourceCellMax.Y,
			Mapping.SourceCellMax.Z,
			Mapping.SourceCellHash);
		for (const int32 BrickNodeId : Mapping.BrickNodeIds)
		{
			if (!Data.Bricks.IsValidIndex(BrickNodeId)
				|| MappedBrickNodeIds.Contains(BrickNodeId))
			{
				OutError =
					TEXT("DAG5BAuditSemanticMappingBrickInvalid");
				OutResult.RejectReason = OutError;
				return false;
			}
			const FABTSM73BrickNode& Brick =
				Data.Bricks[BrickNodeId];
			const FVector2D Center(
				Brick.LocalCenter.X,
				Brick.LocalCenter.Y);
			const FVector2D HalfExtent(
				Brick.DimensionsCM.X * 0.5f,
				Brick.DimensionsCM.Y * 0.5f);
			if (Mapping.Kind
					== EABTSM73DAG5BSemanticMappingKind::SupportPort
				&& (Brick.MacroNodeId != INDEX_NONE
					|| (Brick.SemanticRole
							!= EABTSM73BrickSemanticRole::Column
						&& Brick.SemanticRole
							!= EABTSM73BrickSemanticRole::WeakSupport)
					|| !FMath::IsNearlyEqual(
						Brick.DimensionsCM.X,
						Physical->RealizedColumnWidthCM)
					|| !FMath::IsNearlyEqual(
						Brick.DimensionsCM.Y,
						Physical->RealizedColumnWidthCM)
					|| !Port->AllowedColumnRegion.IsInsideOrOn(
						Center - HalfExtent)
					|| !Port->AllowedColumnRegion.IsInsideOrOn(
						Center + HalfExtent)))
			{
				OutError =
					TEXT("DAG5BAuditSemanticMappingGeometryMismatch");
				OutResult.RejectReason = OutError;
				return false;
			}
			MappedBrickNodeIds.Add(BrickNodeId);
			Canonical += FString::Printf(TEXT(",%d"), BrickNodeId);
			MappingCanonical += FString::Printf(
				TEXT("|B=%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f"),
				BrickNodeId,
				Brick.LocalCenter.X,
				Brick.LocalCenter.Y,
				Brick.LocalCenter.Z,
				Brick.DimensionsCM.X,
				Brick.DimensionsCM.Y,
				Brick.DimensionsCM.Z);
		}
		if (FCrc::StrCrc32(*MappingCanonical) != Mapping.MappingHash)
		{
			OutError =
				TEXT("DAG5BAuditSemanticMappingHashMismatch");
			OutResult.RejectReason = OutError;
			return false;
		}
	}
	int32 PhysicalColumnCount = 0;
	for (const FABTSM73DAGPhysicalSupportMapping& Physical :
		Data.DAGPhysicalSupportMappings)
	{
		PhysicalColumnCount += Physical.ColumnNodeIds.Num();
	}
	if (SupportMappingCount
			!= Data.DAGPhysicalSupportMappings.Num()
		|| ShapeMacroMappingCount < 1
		|| MappedBrickNodeIds.Num()
			!= PhysicalColumnCount + ShapeMacroMappingCount)
	{
		OutError = TEXT("DAG5BAuditSemanticMappingCardinality");
		OutResult.RejectReason = OutError;
		return false;
	}
	int32 WFCDerivedMustVoidCount = 0;
	for (const FABTSM73DAG5BSemanticCellRecord& Cell :
		Envelope.Cells)
	{
		if (Cell.Occupancy
			== EABTSM73DAG5BOccupancy::MustOccupy)
		{
			++OutResult.MustOccupyCount;
			bool bCovered = false;
			for (const FABTSM73BrickNode& Brick : Data.Bricks)
			{
				if (Cell.RequiredMacroNodeId != INDEX_NONE
					&& Brick.MacroNodeId
						!= Cell.RequiredMacroNodeId)
				{
					continue;
				}
				const FBox Box = BrickBox(Brick);
				if (Box.IsInsideOrOn(
					Cell.RequiredSolidBounds.GetCenter())
					&& Box.IsInsideOrOn(
						Cell.RequiredSolidBounds.Min)
					&& Box.IsInsideOrOn(
						Cell.RequiredSolidBounds.Max))
				{
					bCovered = true;
					break;
				}
			}
			if (!bCovered)
			{
				++OutResult.UncoveredMustOccupyCount;
			}
		}
		else if (Cell.Occupancy
			== EABTSM73DAG5BOccupancy::MustVoid)
		{
			++OutResult.MustVoidCount;
			WFCDerivedMustVoidCount +=
				Cell.DerivationPath.Contains(
					TEXT("/PhysicalOpenAir"))
					? 1
					: 0;
			for (const FABTSM73BrickNode& Brick : Data.Bricks)
			{
				if (BoxesOverlapWithVolume(
					Cell.LocalBounds,
					BrickBox(Brick)))
				{
					++OutResult.MustVoidViolationCount;
					break;
				}
			}
		}
	}
	for (const FABTSM73BrickNode& Brick : Data.Bricks)
	{
		const FBox Box = BrickBox(Brick);
		const FBox ExpandedEnvelope =
			Envelope.LocalBounds.ExpandBy(0.5f);
		if (!ExpandedEnvelope.IsInsideOrOn(Box.Min)
			|| !ExpandedEnvelope.IsInsideOrOn(Box.Max))
		{
			++OutResult.OutOfEnvelopeBrickCount;
		}
		bool bInsideShapeScope = false;
		const FVector EnvelopeSize =
			Envelope.LocalBounds.GetSize();
		int32 RequiredScopeMacroNodeId = Brick.MacroNodeId;
		if (RequiredScopeMacroNodeId == INDEX_NONE)
		{
			const FABTSM73DAG5BSemanticBrickMapping* SupportMapping =
				Data.DAG5BSemanticBrickMappings.FindByPredicate(
					[&Brick](
						const FABTSM73DAG5BSemanticBrickMapping&
							Mapping)
					{
						return Mapping.Kind
								== EABTSM73DAG5BSemanticMappingKind::
									SupportPort
							&& Mapping.BrickNodeIds.Contains(
								Brick.NodeId);
					});
			if (SupportMapping != nullptr)
			{
				RequiredScopeMacroNodeId =
					SupportMapping->LoadMacroNodeId;
			}
		}
		const FABTSM73DAG5BShapeScope* RequiredScope =
			Envelope.ShapeScopes.FindByPredicate(
				[RequiredScopeMacroNodeId](
					const FABTSM73DAG5BShapeScope& Scope)
				{
					return Scope.MacroNodeId
						== RequiredScopeMacroNodeId;
				});
		if (RequiredScope != nullptr
			&& RequiredScope->NormalizedBounds.IsValid)
		{
			const FBox LocalScope(
				FVector(
					RequiredScope->NormalizedBounds.Min.X
						* EnvelopeSize.X,
					RequiredScope->NormalizedBounds.Min.Y
						* EnvelopeSize.Y,
					Envelope.LocalBounds.Min.Z
						+ RequiredScope->NormalizedBounds.Min.Z
							* EnvelopeSize.Z),
				FVector(
					RequiredScope->NormalizedBounds.Max.X
						* EnvelopeSize.X,
					RequiredScope->NormalizedBounds.Max.Y
						* EnvelopeSize.Y,
					Envelope.LocalBounds.Min.Z
						+ RequiredScope->NormalizedBounds.Max.Z
							* EnvelopeSize.Z));
			const FBox ExpandedScope = LocalScope.ExpandBy(0.5f);
			if (ExpandedScope.IsInsideOrOn(Box.Min)
				&& ExpandedScope.IsInsideOrOn(Box.Max))
			{
				bInsideShapeScope = true;
			}
		}
		if (!bInsideShapeScope)
		{
			++OutResult.OutOfShapeScopeBrickCount;
		}
		Canonical += FString::Printf(
			TEXT("|B=%d,%d,%d,%d,%d,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f"),
			Brick.NodeId,
			Brick.MacroNodeId,
			static_cast<int32>(Brick.SemanticRole),
			static_cast<int32>(Brick.Material),
			static_cast<int32>(Brick.OriginalMaterial),
			Brick.StoreyIndex,
			Brick.LocalCenter.X,
			Brick.LocalCenter.Y,
			Brick.LocalCenter.Z,
			Brick.DimensionsCM.X,
			Brick.DimensionsCM.Y,
			Brick.DimensionsCM.Z);
	}
	for (const FABTSM73SupportEdge& Edge : Data.SupportEdges)
	{
		Canonical += FString::Printf(
			TEXT("|E=%d>%d,%.3f"),
			Edge.LowerNodeId,
			Edge.UpperNodeId,
			Edge.ContactAreaCM2);
	}
	for (const FABTSM73DAGPhysicalSupportMapping& Mapping :
		Data.DAGPhysicalSupportMappings)
	{
		Canonical += FString::Printf(
			TEXT("|P=%d,%d,%d,%d,%d,%.3f"),
			Mapping.SupportMacroNodeId,
			Mapping.LoadMacroNodeId,
			Mapping.SupportPlateNodeId,
			Mapping.LoadPlateNodeId,
			static_cast<int32>(Mapping.SupportPattern),
			Mapping.RealizedColumnWidthCM);
		for (int32 Index = 0;
			Index < Mapping.ColumnNodeIds.Num();
			++Index)
		{
			Canonical += FString::Printf(
				TEXT(",%d:%d"),
				Mapping.ColumnNodeIds[Index],
				Mapping.ColumnRoles.IsValidIndex(Index)
					? static_cast<int32>(Mapping.ColumnRoles[Index])
					: static_cast<int32>(
						EABTSM73DAGRealizedColumnRole::Ordinary));
		}
	}
	Canonical += FString::Printf(
		TEXT("|O=%d|V=%d|U=%d|X=%d|S=%d"),
		OutResult.MustOccupyCount,
		OutResult.MustVoidViolationCount,
		OutResult.UncoveredMustOccupyCount,
		OutResult.OutOfEnvelopeBrickCount,
		OutResult.OutOfShapeScopeBrickCount);
	OutResult.AuditHash = FCrc::StrCrc32(*Canonical);
	if (OutResult.MustOccupyCount < 1
		|| OutResult.MustVoidCount < 1
		|| WFCDerivedMustVoidCount < 1
		|| OutResult.MustVoidViolationCount > 0
		|| OutResult.UncoveredMustOccupyCount > 0
		|| OutResult.OutOfEnvelopeBrickCount > 0
		|| OutResult.OutOfShapeScopeBrickCount > 0)
	{
		OutError = FString::Printf(
			TEXT("DAG5BEnvelopeAuditRejected:Occupy=%d:Void=%d:VoidViolations=%d:Uncovered=%d:Outside=%d:ShapeOutside=%d"),
			OutResult.MustOccupyCount,
			OutResult.MustVoidCount,
			OutResult.MustVoidViolationCount,
			OutResult.UncoveredMustOccupyCount,
			OutResult.OutOfEnvelopeBrickCount,
			OutResult.OutOfShapeScopeBrickCount);
		OutResult.RejectReason = OutError;
		return false;
	}
	OutResult.bAccepted = true;
	return true;
}
