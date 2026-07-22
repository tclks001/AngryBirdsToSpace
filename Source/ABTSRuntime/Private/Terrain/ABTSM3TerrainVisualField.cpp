// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/ABTSM3TerrainVisualField.h"

#include "Planet/ABTSM2Planet.h"

void FABTSM3TerrainVisualField::Initialize(
	const float InBaseRadiusCM,
	const float InHeightScaleCM,
	const float InWaterDepthCM,
	const float InHeightBlendWidthCM,
	const float InColorBlendWidthCM,
	const TArray<FABTSM2Cell>& InCells,
	const TArray<FABTSM3CellState>& InCellStates,
	const TArray<FABTSM3CellEdgeState>& InEdgeStates,
	const float InStreamHalfWidthCM,
	const float InShallowRiverHalfWidthCM,
	const float InDeepRiverHalfWidthCM)
{
	BaseRadiusCM = InBaseRadiusCM;
	HeightScaleCM = FMath::Max(0.0f, InHeightScaleCM);
	WaterDepthCM = FMath::Max(0.0f, InWaterDepthCM);
	HeightBlendWidthCM = FMath::Max(1.0f, InHeightBlendWidthCM);
	ColorBlendWidthCM = FMath::Max(1.0f, InColorBlendWidthCM);
	Cells = &InCells;
	CellStates = &InCellStates;
	BuildBoundarySegments();
	BuildRiverSegments(InEdgeStates, InStreamHalfWidthCM, InShallowRiverHalfWidthCM, InDeepRiverHalfWidthCM);
}

int32 FABTSM3TerrainVisualField::FindNearestCell(const FVector& UnitDirection, int32 StartCellHint) const
{
	if (Cells == nullptr || Cells->IsEmpty()) return INDEX_NONE;
	const FVector Direction = UnitDirection.GetSafeNormal();
	int32 Current = Cells->IsValidIndex(StartCellHint) ? StartCellHint : 0;
	float CurrentDot = FVector::DotProduct((*Cells)[Current].UnitCenter, Direction);

	bool bImproved = true;
	while (bImproved)
	{
		bImproved = false;
		for (const int32 Neighbor : (*Cells)[Current].NeighborCellIds)
		{
			const float NeighborDot = FVector::DotProduct((*Cells)[Neighbor].UnitCenter, Direction);
			if (NeighborDot > CurrentDot + UE_KINDA_SMALL_NUMBER)
			{
				Current = Neighbor;
				CurrentDot = NeighborDot;
				bImproved = true;
				break;
			}
		}
	}
	return Current;
}

float FABTSM3TerrainVisualField::GetCellHeightCM(const int32 CellId) const
{
	if (CellStates == nullptr || !CellStates->IsValidIndex(CellId)) return 0.0f;
	const FABTSM3CellState& State = (*CellStates)[CellId];
	return State.LogicalHeight01 * HeightScaleCM;
}

float FABTSM3TerrainVisualField::GetDistanceToSegmentCM(
	const FVector& UnitDirection,
	const FABTSM3BoundarySegment& Segment) const
{
	const FVector SegmentVector = Segment.EndUnit - Segment.StartUnit;
	const float LengthSquared = SegmentVector.SizeSquared();
	if (LengthSquared <= SMALL_NUMBER) return FVector::Distance(UnitDirection, Segment.StartUnit) * BaseRadiusCM;
	const float Projection = FVector::DotProduct(UnitDirection - Segment.StartUnit, SegmentVector) / LengthSquared;
	const float ClampedProjection = FMath::Clamp(Projection, 0.0f, 1.0f);
	const FVector ClosestPoint = Segment.StartUnit + SegmentVector * ClampedProjection;
	return FVector::Distance(UnitDirection, ClosestPoint) * BaseRadiusCM;
}

void FABTSM3TerrainVisualField::FindTwoNearestTerrainFeatures(
	const FVector& UnitDirection,
	const int32 CellId,
	const FABTSM3BoundarySegment*& OutBest,
	float& OutBestDistanceCM,
	const FABTSM3BoundarySegment*& OutSecond,
	float& OutSecondDistanceCM) const
{
	OutBest = nullptr;
	OutSecond = nullptr;
	OutBestDistanceCM = TNumericLimits<float>::Max();
	OutSecondDistanceCM = TNumericLimits<float>::Max();
	const FABTSM3BoundarySegment* NearestByType[5] = {};
	float DistanceByType[5] = {
		TNumericLimits<float>::Max(), TNumericLimits<float>::Max(), TNumericLimits<float>::Max(),
		TNumericLimits<float>::Max(), TNumericLimits<float>::Max()};
	for (const FABTSM3BoundarySegment& Feature : GetBoundarySegments(CellId))
	{
		const int32 TypeIndex = static_cast<int32>(Feature.TerrainType);
		if (TypeIndex < 0 || TypeIndex >= UE_ARRAY_COUNT(DistanceByType)) continue;
		const float DistanceCM = GetDistanceToSegmentCM(UnitDirection, Feature);
		if (DistanceCM < DistanceByType[TypeIndex])
		{
			DistanceByType[TypeIndex] = DistanceCM;
			NearestByType[TypeIndex] = &Feature;
		}
	}
	for (int32 TypeIndex = 0; TypeIndex < UE_ARRAY_COUNT(DistanceByType); ++TypeIndex)
	{
		if (NearestByType[TypeIndex] == nullptr) continue;
		if (DistanceByType[TypeIndex] < OutBestDistanceCM)
		{
			OutSecond = OutBest;
			OutSecondDistanceCM = OutBestDistanceCM;
			OutBest = NearestByType[TypeIndex];
			OutBestDistanceCM = DistanceByType[TypeIndex];
		}
		else if (DistanceByType[TypeIndex] < OutSecondDistanceCM)
		{
			OutSecond = NearestByType[TypeIndex];
			OutSecondDistanceCM = DistanceByType[TypeIndex];
		}
	}
}

float FABTSM3TerrainVisualField::GetSurfaceRadius(const FVector& UnitDirection) const
{
	if (!IsReady()) return BaseRadiusCM;
	const FVector Direction = UnitDirection.GetSafeNormal();
	const int32 CellId = FindNearestCell(Direction);
	float HeightCM = GetCellHeightCM(CellId);
	// Water is an edge property. Deform only a narrow band around the generated
	// flow-centerline or barrier-dual segment; never lower an entire logical Cell (which creates a
	// hexagonal/ Voronoi-shaped river).
	float NearestRiverDistanceCM = TNumericLimits<float>::Max();
	float NearestRiverHalfWidthCM = 0.0f;
	if (RiverSegmentsByCell.IsValidIndex(CellId))
	for (const FABTSM3RiverVisualSegment& River : RiverSegmentsByCell[CellId])
	{
		const FVector SegmentVector = River.EndUnit - River.StartUnit;
		const float SegmentLengthSq = SegmentVector.SizeSquared();
		const float Projection = SegmentLengthSq > SMALL_NUMBER
			? FMath::Clamp(FVector::DotProduct(Direction - River.StartUnit, SegmentVector) / SegmentLengthSq, 0.0f, 1.0f)
			: 0.0f;
		const FVector Closest = River.StartUnit + Projection * SegmentVector;
		const float DistanceCM = FVector::Distance(Direction, Closest) * BaseRadiusCM;
		if (DistanceCM < NearestRiverDistanceCM)
		{
			NearestRiverDistanceCM = DistanceCM;
			NearestRiverHalfWidthCM = River.HalfWidthCM;
		}
	}
	if (NearestRiverDistanceCM < TNumericLimits<float>::Max())
	{
		const float RiverBlendWidthCM = FMath::Max(HeightBlendWidthCM, 1.0f);
		const float RiverAlpha = 1.0f - FMath::SmoothStep(NearestRiverHalfWidthCM, NearestRiverHalfWidthCM + RiverBlendWidthCM, NearestRiverDistanceCM);
		HeightCM -= WaterDepthCM * RiverAlpha;
	}
	const FABTSM3BoundarySegment* BestTerrain = nullptr;
	const FABTSM3BoundarySegment* SecondTerrain = nullptr;
	float BestTerrainDistanceCM = 0.0f;
	float SecondTerrainDistanceCM = 0.0f;
	FindTwoNearestTerrainFeatures(Direction, CellId, BestTerrain, BestTerrainDistanceCM, SecondTerrain, SecondTerrainDistanceCM);
	if (BestTerrain != nullptr && SecondTerrain != nullptr
		&& SecondTerrainDistanceCM - BestTerrainDistanceCM < HeightBlendWidthCM * 2.0f)
	{
		const float BestHeight = 0.5f * (GetCellHeightCM(BestTerrain->SourceCellAId) + GetCellHeightCM(BestTerrain->SourceCellBId));
		const float SecondHeight = 0.5f * (GetCellHeightCM(SecondTerrain->SourceCellAId) + GetCellHeightCM(SecondTerrain->SourceCellBId));
		const float Separation = SecondTerrainDistanceCM - BestTerrainDistanceCM;
		const float BestWeight = FMath::SmoothStep(0.0f, HeightBlendWidthCM * 2.0f, Separation);
		HeightCM = FMath::Lerp(0.5f * (BestHeight + SecondHeight), BestHeight, BestWeight);
	}
	return BaseRadiusCM + HeightCM;
}

FVector FABTSM3TerrainVisualField::GetSurfaceNormal(const FVector& UnitDirection) const
{
	const FVector Up = UnitDirection.GetSafeNormal();
	FVector TangentX = FVector::CrossProduct(FVector::UpVector, Up).GetSafeNormal();
	if (TangentX.IsNearlyZero()) TangentX = FVector::ForwardVector;
	const FVector TangentY = FVector::CrossProduct(Up, TangentX).GetSafeNormal();
	constexpr float SampleAngle = 0.0008f;
	const FVector X0 = (Up - TangentX * SampleAngle).GetSafeNormal();
	const FVector X1 = (Up + TangentX * SampleAngle).GetSafeNormal();
	const FVector Y0 = (Up - TangentY * SampleAngle).GetSafeNormal();
	const FVector Y1 = (Up + TangentY * SampleAngle).GetSafeNormal();
	const FVector DX = X1 * GetSurfaceRadius(X1) - X0 * GetSurfaceRadius(X0);
	const FVector DY = Y1 * GetSurfaceRadius(Y1) - Y0 * GetSurfaceRadius(Y0);
	FVector Normal = FVector::CrossProduct(DX, DY).GetSafeNormal();
	if (FVector::DotProduct(Normal, Up) < 0.0f) Normal *= -1.0f;
	return Normal.IsNearlyZero() ? Up : Normal;
}

FLinearColor FABTSM3TerrainVisualField::GetCellColor(const int32 CellId) const
{
	if (CellStates == nullptr || !CellStates->IsValidIndex(CellId)) return FLinearColor::Gray;
	switch ((*CellStates)[CellId].TerrainType)
	{
	case EABTSM3TerrainType::Forest: return FLinearColor(0.08f, 0.28f, 0.10f);
	case EABTSM3TerrainType::Highland: return FLinearColor(0.45f, 0.34f, 0.18f);
	case EABTSM3TerrainType::Mountain: return FLinearColor(0.32f, 0.30f, 0.28f);
	case EABTSM3TerrainType::Water: return FLinearColor(0.03f, 0.20f, 0.36f);
	default: return FLinearColor(0.28f, 0.46f, 0.18f);
	}
}

FLinearColor FABTSM3TerrainVisualField::GetCellLandColor(const int32 CellId) const
{
	if (CellStates == nullptr || !CellStates->IsValidIndex(CellId)) return FLinearColor::Gray;
	// bWater is a compatibility/cache flag derived from river edges. It must not
	// turn the entire logical Cell into a hexagonal water polygon in the material.
	if ((*CellStates)[CellId].TerrainType != EABTSM3TerrainType::Water) return GetCellColor(CellId);
	if ((*CellStates)[CellId].LogicalHeight01 >= 0.62f) return FLinearColor(0.32f, 0.30f, 0.28f);
	if ((*CellStates)[CellId].LogicalHeight01 >= 0.38f) return FLinearColor(0.45f, 0.34f, 0.18f);
	return (*CellStates)[CellId].Moisture01 >= 0.48f ? FLinearColor(0.08f, 0.28f, 0.10f) : FLinearColor(0.28f, 0.46f, 0.18f);
}

FLinearColor FABTSM3TerrainVisualField::GetDebugTerrainColor(const FVector& UnitDirection) const
{
	const FVector Direction = UnitDirection.GetSafeNormal();
	const int32 CellId = FindNearestCell(Direction);
	const FABTSM3BoundarySegment* Best = nullptr;
	const FABTSM3BoundarySegment* Second = nullptr;
	float BestDistance = 0.0f, SecondDistance = 0.0f;
	FindTwoNearestTerrainFeatures(Direction, CellId, Best, BestDistance, Second, SecondDistance);
	if (Best == nullptr) return GetCellColor(CellId);
	const FLinearColor BestColor = GetCellColor(Best->SourceCellAId);
	if (Second == nullptr) return BestColor;
	const FLinearColor SecondColor = GetCellColor(Second->SourceCellAId);
	const float BestWeight = FMath::SmoothStep(0.0f, ColorBlendWidthCM * 2.0f, SecondDistance - BestDistance);
	return FMath::Lerp(0.5f * (BestColor + SecondColor), BestColor, BestWeight);
}

FLinearColor FABTSM3TerrainVisualField::GetDebugLandColor(const FVector& UnitDirection) const
{
	const FVector Direction = UnitDirection.GetSafeNormal();
	const int32 CellId = FindNearestCell(Direction);
	const FABTSM3BoundarySegment* Best = nullptr;
	const FABTSM3BoundarySegment* Second = nullptr;
	float BestDistance = 0.0f, SecondDistance = 0.0f;
	FindTwoNearestTerrainFeatures(Direction, CellId, Best, BestDistance, Second, SecondDistance);
	if (Best == nullptr) return GetCellLandColor(CellId);
	const FLinearColor BestColor = GetCellLandColor(Best->SourceCellAId);
	if (Second == nullptr) return BestColor;
	const FLinearColor SecondColor = GetCellLandColor(Second->SourceCellAId);
	const float BestWeight = FMath::SmoothStep(0.0f, ColorBlendWidthCM * 2.0f, SecondDistance - BestDistance);
	return FMath::Lerp(0.5f * (BestColor + SecondColor), BestColor, BestWeight);
}

const TArray<FABTSM3BoundarySegment>& FABTSM3TerrainVisualField::GetBoundarySegments(const int32 CellId) const
{
	static const TArray<FABTSM3BoundarySegment> Empty;
	return BoundarySegmentsByCell.IsValidIndex(CellId) ? BoundarySegmentsByCell[CellId] : Empty;
}

void FABTSM3TerrainVisualField::BuildBoundarySegments()
{
	BoundarySegmentsByCell.Reset();
	if (Cells == nullptr || CellStates == nullptr || Cells->Num() != CellStates->Num()) return;
	TArray<FABTSM3TerrainFeatureVisualSegment> TerrainFeatures;
	FABTSM3TerrainFeatureVisualBuilder::BuildSegments(*Cells, *CellStates, TerrainFeatures);
	TArray<TArray<int32>> SegmentIndicesByCell;
	int32 DroppedReferences = 0;
	FABTSM3TerrainFeatureVisualBuilder::BuildLocalSegmentIndices(*Cells, TerrainFeatures, 3, 32, BaseRadiusCM, SegmentIndicesByCell, DroppedReferences);
	BoundarySegmentsByCell.SetNum(Cells->Num());
	for (int32 CellId = 0; CellId < SegmentIndicesByCell.Num(); ++CellId)
	{
		for (const int32 SegmentIndex : SegmentIndicesByCell[CellId])
		{
			const FABTSM3TerrainFeatureVisualSegment& Segment = TerrainFeatures[SegmentIndex];
			BoundarySegmentsByCell[CellId].Add({Segment.StartUnit, Segment.EndUnit, Segment.TerrainType, Segment.SourceCellAId, Segment.SourceCellBId});
		}
	}
}

void FABTSM3TerrainVisualField::BuildRiverSegments(const TArray<FABTSM3CellEdgeState>& EdgeStates, const float StreamHalfWidthCM, const float ShallowRiverHalfWidthCM, const float DeepRiverHalfWidthCM)
{
	RiverSegmentsByCell.Reset();
	if (Cells == nullptr) return;
	TArray<FABTSM3RiverVisualSegment> Segments;
	FABTSM3RiverVisualBuilder::BuildSegments(*Cells, EdgeStates, StreamHalfWidthCM, ShallowRiverHalfWidthCM, DeepRiverHalfWidthCM, Segments);
	TArray<TArray<int32>> SegmentIndicesByCell;
	int32 DroppedReferences = 0;
	FABTSM3RiverVisualBuilder::BuildLocalSegmentIndices(*Cells, Segments, BaseRadiusCM, FMath::Max(ColorBlendWidthCM, HeightBlendWidthCM), 0, SegmentIndicesByCell, DroppedReferences);
	RiverSegmentsByCell.SetNum(Cells->Num());
	for (int32 CellId = 0; CellId < SegmentIndicesByCell.Num(); ++CellId)
	{
		RiverSegmentsByCell[CellId].Reserve(SegmentIndicesByCell[CellId].Num());
		for (const int32 SegmentIndex : SegmentIndicesByCell[CellId]) RiverSegmentsByCell[CellId].Add(Segments[SegmentIndex]);
	}
}
