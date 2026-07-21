// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/ABTSM3TerrainVisualField.h"

#include "Planet/ABTSM2Planet.h"

void FABTSM3TerrainVisualField::Initialize(
	const float InBaseRadiusCM,
	const float InHeightScaleCM,
	const float InWaterDepthCM,
	const float InBlendWidthCM,
	const TArray<FABTSM2Cell>& InCells,
	const TArray<FABTSM3CellState>& InCellStates)
{
	BaseRadiusCM = InBaseRadiusCM;
	HeightScaleCM = FMath::Max(0.0f, InHeightScaleCM);
	WaterDepthCM = FMath::Max(0.0f, InWaterDepthCM);
	BlendWidthCM = FMath::Max(1.0f, InBlendWidthCM);
	Cells = &InCells;
	CellStates = &InCellStates;
	BuildBoundarySegments();
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
	return State.LogicalHeight01 * HeightScaleCM - (State.bWater ? WaterDepthCM : 0.0f);
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

float FABTSM3TerrainVisualField::GetSurfaceRadius(const FVector& UnitDirection) const
{
	if (!IsReady()) return BaseRadiusCM;
	const FVector Direction = UnitDirection.GetSafeNormal();
	const int32 CellId = FindNearestCell(Direction);
	float HeightCM = GetCellHeightCM(CellId);
	float NearestDistanceCM = TNumericLimits<float>::Max();
	int32 OtherCellId = INDEX_NONE;
	for (const FABTSM3BoundarySegment& Segment : BoundarySegmentsByCell[CellId])
	{
		const float DistanceCM = GetDistanceToSegmentCM(Direction, Segment);
		if (DistanceCM < NearestDistanceCM)
		{
			NearestDistanceCM = DistanceCM;
			OtherCellId = Segment.OtherCellId;
		}
	}

	if (OtherCellId != INDEX_NONE && NearestDistanceCM < BlendWidthCM)
	{
		const float Alpha = FMath::SmoothStep(0.0f, BlendWidthCM, NearestDistanceCM);
		const float BoundaryHeight = 0.5f * (HeightCM + GetCellHeightCM(OtherCellId));
		HeightCM = FMath::Lerp(BoundaryHeight, HeightCM, Alpha);
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

FLinearColor FABTSM3TerrainVisualField::GetDebugTerrainColor(const FVector& UnitDirection) const
{
	const FVector Direction = UnitDirection.GetSafeNormal();
	const int32 CellId = FindNearestCell(Direction);
	FLinearColor Color = GetCellColor(CellId);
	float NearestDistanceCM = TNumericLimits<float>::Max();
	int32 OtherCellId = INDEX_NONE;
	for (const FABTSM3BoundarySegment& Segment : BoundarySegmentsByCell[CellId])
	{
		const float DistanceCM = GetDistanceToSegmentCM(Direction, Segment);
		if (DistanceCM < NearestDistanceCM)
		{
			NearestDistanceCM = DistanceCM;
			OtherCellId = Segment.OtherCellId;
		}
	}
	if (OtherCellId != INDEX_NONE && NearestDistanceCM < BlendWidthCM)
	{
		const float Alpha = FMath::SmoothStep(0.0f, BlendWidthCM, NearestDistanceCM);
		Color = FMath::Lerp(0.5f * (Color + GetCellColor(OtherCellId)), Color, Alpha);
	}
	return Color;
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
	BoundarySegmentsByCell.SetNum(Cells->Num());

	for (int32 CellA = 0; CellA < Cells->Num(); ++CellA)
	{
		for (const int32 CellB : (*Cells)[CellA].NeighborCellIds)
		{
			if (CellB < CellA || (*CellStates)[CellA].TerrainType == (*CellStates)[CellB].TerrainType) continue;
			TArray<int32, TInlineAllocator<2>> CommonNeighbors;
			for (const int32 Candidate : (*Cells)[CellA].NeighborCellIds)
			{
				if (Candidate != CellB && (*Cells)[CellB].NeighborCellIds.Contains(Candidate)) CommonNeighbors.Add(Candidate);
			}
			if (CommonNeighbors.Num() != 2) continue;
			const FVector Start = ((*Cells)[CellA].UnitCenter + (*Cells)[CellB].UnitCenter + (*Cells)[CommonNeighbors[0]].UnitCenter).GetSafeNormal();
			const FVector End = ((*Cells)[CellA].UnitCenter + (*Cells)[CellB].UnitCenter + (*Cells)[CommonNeighbors[1]].UnitCenter).GetSafeNormal();
			BoundarySegmentsByCell[CellA].Add({Start, End, CellB});
			BoundarySegmentsByCell[CellB].Add({Start, End, CellA});
		}
	}
}

