// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/ABTSM3TerrainVisualField.h"

#include "Planet/ABTSM2Planet.h"

void FABTSM3TerrainVisualField::Initialize(
	const float InBaseRadiusCM,
	const float InHeightScaleCM,
	const float InWaterDepthCM,
	const float InHeightBlendWidthCM,
	const float InColorBlendWidthCM,
	const float InNormalSmoothingDistanceCM,
	const TArray<FABTSM2Cell>& InCells,
	const TArray<FABTSM3CellState>& InCellStates,
	const TArray<FABTSM3CellEdgeState>& InEdgeStates,
	const float InTrailRoadHalfWidthCM,
	const float InMainRoadHalfWidthCM,
	const float InStreamHalfWidthCM,
	const float InShallowRiverHalfWidthCM,
	const float InDeepRiverHalfWidthCM)
{
	BaseRadiusCM = InBaseRadiusCM;
	HeightScaleCM = FMath::Max(0.0f, InHeightScaleCM);
	WaterDepthCM = FMath::Max(0.0f, InWaterDepthCM);
	HeightBlendWidthCM = FMath::Max(1.0f, InHeightBlendWidthCM);
	ColorBlendWidthCM = FMath::Max(1.0f, InColorBlendWidthCM);
	NormalSmoothingDistanceCM = FMath::Max(1.0f, InNormalSmoothingDistanceCM);
	TrailRoadHalfWidthCM = FMath::Max(1.0f, InTrailRoadHalfWidthCM);
	MainRoadHalfWidthCM = FMath::Max(1.0f, InMainRoadHalfWidthCM);
	Cells = &InCells;
	CellStates = &InCellStates;
	BuildBoundarySegments();
	BuildRiverSegments(InEdgeStates, InStreamHalfWidthCM, InShallowRiverHalfWidthCM, InDeepRiverHalfWidthCM);
	BuildRoadSegments(InEdgeStates);
}

void FABTSM3TerrainVisualField::SetBuildingPads(const TArray<FABTSM3BuildingSpawnSite>& InSites)
{
	BuildingPads.Reset();
	for (const FABTSM3BuildingSpawnSite& Site : InSites)
	{
		if (!Site.bTerrainPadApplied || Site.PadTargetRadiusCM <= 0.0f
			|| Site.PadHalfExtentCM.X <= 0.0f || Site.PadHalfExtentCM.Y <= 0.0f)
		{
			continue;
		}
		BuildingPads.Add(Site);
	}
}

bool FABTSM3TerrainVisualField::IsInsideBuildingPad(const FVector& UnitDirection) const
{
	if (BuildingPads.IsEmpty() || UnitDirection.IsNearlyZero()) return false;
	const float Radius = GetUnpaddedSurfaceRadius(UnitDirection.GetSafeNormal());
	for (const FABTSM3BuildingSpawnSite& Pad : BuildingPads)
	{
		if (GetBuildingPadSignedDistanceCM(UnitDirection, Radius, Pad) <= 0.0f) return true;
	}
	return false;
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

float FABTSM3TerrainVisualField::GetInterpolatedHeightCM(const FVector& UnitDirection, const int32 NearestCellId) const
{
	if (Cells == nullptr || !Cells->IsValidIndex(NearestCellId)) return 0.0f;
	const FVector Direction = UnitDirection.GetSafeNormal();
	const FABTSM2Cell& CenterCell = (*Cells)[NearestCellId];
	float BestMinimumWeight = -TNumericLimits<float>::Max();
	float BestHeightCM = GetCellHeightCM(NearestCellId);
	bool bFoundTriangle = false;

	for (int32 FirstIndex = 0; FirstIndex < CenterCell.NeighborCellIds.Num(); ++FirstIndex)
	{
		const int32 CellB = CenterCell.NeighborCellIds[FirstIndex];
		for (int32 SecondIndex = FirstIndex + 1; SecondIndex < CenterCell.NeighborCellIds.Num(); ++SecondIndex)
		{
			const int32 CellC = CenterCell.NeighborCellIds[SecondIndex];
			if (!(*Cells)[CellB].NeighborCellIds.Contains(CellC)) continue;
			const FVector A = CenterCell.UnitCenter;
			const FVector B = (*Cells)[CellB].UnitCenter;
			const FVector C = (*Cells)[CellC].UnitCenter;
			const FVector Edge0 = B - A;
			const FVector Edge1 = C - A;
			const FVector PlaneNormal = FVector::CrossProduct(Edge0, Edge1);
			const float RayDenominator = FVector::DotProduct(PlaneNormal, Direction);
			if (FMath::Abs(RayDenominator) <= SMALL_NUMBER) continue;
			const float RayDistance = FVector::DotProduct(PlaneNormal, A) / RayDenominator;
			if (RayDistance <= 0.0f) continue;
			const FVector Point = Direction * RayDistance;
			const FVector ToPoint = Point - A;
			const float D00 = FVector::DotProduct(Edge0, Edge0);
			const float D01 = FVector::DotProduct(Edge0, Edge1);
			const float D11 = FVector::DotProduct(Edge1, Edge1);
			const float D20 = FVector::DotProduct(ToPoint, Edge0);
			const float D21 = FVector::DotProduct(ToPoint, Edge1);
			const float Denominator = D00 * D11 - D01 * D01;
			if (FMath::Abs(Denominator) <= SMALL_NUMBER) continue;
			const float WeightB = (D11 * D20 - D01 * D21) / Denominator;
			const float WeightC = (D00 * D21 - D01 * D20) / Denominator;
			const float WeightA = 1.0f - WeightB - WeightC;
			const float MinimumWeight = FMath::Min3(WeightA, WeightB, WeightC);
			if (MinimumWeight < -0.002f || MinimumWeight <= BestMinimumWeight) continue;
			const float ClampedA = FMath::Max(0.0f, WeightA);
			const float ClampedB = FMath::Max(0.0f, WeightB);
			const float ClampedC = FMath::Max(0.0f, WeightC);
			const float WeightSum = FMath::Max(ClampedA + ClampedB + ClampedC, UE_SMALL_NUMBER);
			BestHeightCM = (ClampedA * GetCellHeightCM(NearestCellId)
				+ ClampedB * GetCellHeightCM(CellB)
				+ ClampedC * GetCellHeightCM(CellC)) / WeightSum;
			BestMinimumWeight = MinimumWeight;
			bFoundTriangle = true;
		}
	}

	if (bFoundTriangle) return BestHeightCM;
	// Defensive fallback at numerical seams: inverse chord-distance weighting over
	// the nearest Cell and its one-ring neighbors remains continuous enough to
	// avoid returning the old piecewise-constant Cell height.
	float WeightedHeight = 0.0f;
	float WeightSum = 0.0f;
	const auto Accumulate = [&](const int32 CellId)
	{
		const float DistanceSquared = FVector::DistSquared(Direction, (*Cells)[CellId].UnitCenter);
		const float Weight = 1.0f / FMath::Max(DistanceSquared, 1.e-6f);
		WeightedHeight += Weight * GetCellHeightCM(CellId);
		WeightSum += Weight;
	};
	Accumulate(NearestCellId);
	for (const int32 NeighborId : CenterCell.NeighborCellIds) Accumulate(NeighborId);
	return WeightSum > 0.0f ? WeightedHeight / WeightSum : GetCellHeightCM(NearestCellId);
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

float FABTSM3TerrainVisualField::GetUnpaddedSurfaceRadius(const FVector& UnitDirection) const
{
	if (!IsReady()) return BaseRadiusCM;
	const FVector Direction = UnitDirection.GetSafeNormal();
	const int32 CellId = FindNearestCell(Direction);
	return GetUnpaddedSurfaceRadiusForCell(Direction, CellId);
}

float FABTSM3TerrainVisualField::GetUnpaddedSurfaceRadiusForCell(
	const FVector& UnitDirection,
	const int32 CellId) const
{
	if (!IsReady() || !Cells->IsValidIndex(CellId)) return BaseRadiusCM;
	const FVector Direction = UnitDirection.GetSafeNormal();
	float HeightCM = GetInterpolatedHeightCM(Direction, CellId);
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
	return BaseRadiusCM + HeightCM;
}

float FABTSM3TerrainVisualField::GetBuildingPadSignedDistanceCM(
	const FVector& UnitDirection,
	const float RadiusCM,
	const FABTSM3BuildingSpawnSite& Pad) const
{
	const FVector Direction = UnitDirection.GetSafeNormal();
	const FVector Anchor = Pad.AnchorDirection.GetSafeNormal() * Pad.PadTargetRadiusCM;
	const FVector Point = Direction * RadiusCM;
	const FVector Offset = Point - Anchor;
	const float LocalX = FVector::DotProduct(Offset, Pad.TangentForward.GetSafeNormal());
	const float LocalY = FVector::DotProduct(Offset, Pad.TangentRight.GetSafeNormal());
	const FVector2D Delta(FMath::Abs(LocalX) - Pad.PadHalfExtentCM.X, FMath::Abs(LocalY) - Pad.PadHalfExtentCM.Y);
	const FVector2D Outside(FMath::Max(0.0f, Delta.X), FMath::Max(0.0f, Delta.Y));
	return Outside.Size() + FMath::Min(FMath::Max(Delta.X, Delta.Y), 0.0f);
}

float FABTSM3TerrainVisualField::ApplyCompatibilityBuildingPadRadius(
	const FVector& UnitDirection,
	float UnpaddedRadiusCM) const
{
	const FVector Direction = UnitDirection.GetSafeNormal();
	// Preserve the established TaskGraph construction-pad behavior, including
	// both cut and fill. Terrain-only jury pads are composed separately below.
	for (const FABTSM3BuildingSpawnSite& Pad : BuildingPads)
	{
		if (Pad.TaskId == INDEX_NONE)
		{
			continue;
		}
		const float Denominator = FVector::DotProduct(Pad.AnchorDirection.GetSafeNormal(), Direction);
		if (Denominator <= 0.25f) continue;
		const float SignedDistance = GetBuildingPadSignedDistanceCM(Direction, UnpaddedRadiusCM, Pad);
		const float BlendWidth = FMath::Max(1.0f, Pad.PadEdgeBlendWidthCM);
		const float BlendAlpha = 1.0f - FMath::SmoothStep(0.0f, BlendWidth, FMath::Max(0.0f, SignedDistance));
		if (BlendAlpha <= 0.0f) continue;
		const float TangentPlaneRadiusCM = Pad.PadTargetRadiusCM / Denominator;
		UnpaddedRadiusCM = FMath::Lerp(
			UnpaddedRadiusCM,
			TangentPlaneRadiusCM,
			BlendAlpha);
	}
	return UnpaddedRadiusCM;
}

float FABTSM3TerrainVisualField::GetCompatibilityPaddedSurfaceRadius(
	const FVector& UnitDirection) const
{
	const FVector Direction = UnitDirection.GetSafeNormal();
	return ApplyCompatibilityBuildingPadRadius(
		Direction,
		GetUnpaddedSurfaceRadius(Direction));
}

float FABTSM3TerrainVisualField::ApplyBuildingPadRadius(
	const FVector& UnitDirection,
	float UnpaddedRadiusCM) const
{
	const FVector Direction = UnitDirection.GetSafeNormal();
	const float CompatibilityPaddedRadiusCM =
		ApplyCompatibilityBuildingPadRadius(Direction, UnpaddedRadiusCM);
	float ResolvedRadiusCM = CompatibilityPaddedRadiusCM;
	for (const FABTSM3BuildingSpawnSite& Pad : BuildingPads)
	{
		if (Pad.TaskId != INDEX_NONE)
		{
			continue;
		}
		const float Denominator = FVector::DotProduct(Pad.AnchorDirection.GetSafeNormal(), Direction);
		if (Denominator <= 0.25f) continue;
		const float SignedDistance = GetBuildingPadSignedDistanceCM(
			Direction,
			CompatibilityPaddedRadiusCM,
			Pad);
		const float BlendWidth = FMath::Max(1.0f, Pad.PadEdgeBlendWidthCM);
		const float BlendAlpha = 1.0f - FMath::SmoothStep(
			0.0f,
			BlendWidth,
			FMath::Max(0.0f, SignedDistance));
		if (BlendAlpha <= 0.0f) continue;
		const float TangentPlaneRadiusCM = Pad.PadTargetRadiusCM / Denominator;
		const float CandidateRadiusCM = FMath::Lerp(
			CompatibilityPaddedRadiusCM,
			TangentPlaneRadiusCM,
			BlendAlpha);
		// All fixed-six work pads grade downward from the production terrain.
		// Their lower envelope is continuous, order independent and guarantees
		// that a remote overlapping skirt cannot lift or re-cut an inner plane.
		ResolvedRadiusCM = FMath::Min(ResolvedRadiusCM, CandidateRadiusCM);
	}
	return ResolvedRadiusCM;
}

float FABTSM3TerrainVisualField::GetSurfaceRadius(const FVector& UnitDirection) const
{
	const FVector Direction = UnitDirection.GetSafeNormal();
	return ApplyBuildingPadRadius(Direction, GetUnpaddedSurfaceRadius(Direction));
}

float FABTSM3TerrainVisualField::GetSurfaceRadiusWithHint(
	const FVector& UnitDirection,
	const int32 StartCellHint,
	int32& OutCellId) const
{
	const FVector Direction = UnitDirection.GetSafeNormal();
	OutCellId = FindNearestCell(Direction, StartCellHint);
	return ApplyBuildingPadRadius(
		Direction,
		GetUnpaddedSurfaceRadiusForCell(Direction, OutCellId));
}

FVector FABTSM3TerrainVisualField::GetSurfaceNormal(const FVector& UnitDirection) const
{
	const FVector Up = UnitDirection.GetSafeNormal();
	FVector TangentX = FVector::CrossProduct(FVector::UpVector, Up).GetSafeNormal();
	if (TangentX.IsNearlyZero()) TangentX = FVector::ForwardVector;
	const FVector TangentY = FVector::CrossProduct(Up, TangentX).GetSafeNormal();
	const float SampleAngle = FMath::Clamp(NormalSmoothingDistanceCM / FMath::Max(BaseRadiusCM, 1.0f), 0.0001f, 0.08f);
	const FVector X0 = (Up - TangentX * SampleAngle).GetSafeNormal();
	const FVector X1 = (Up + TangentX * SampleAngle).GetSafeNormal();
	const FVector Y0 = (Up - TangentY * SampleAngle).GetSafeNormal();
	const FVector Y1 = (Up + TangentY * SampleAngle).GetSafeNormal();
	const FVector DX = X1 * GetSurfaceRadius(X1) - X0 * GetSurfaceRadius(X0);
	const FVector DY = Y1 * GetSurfaceRadius(Y1) - Y0 * GetSurfaceRadius(Y0);
	const FVector Diagonal0 = (TangentX + TangentY).GetSafeNormal();
	const FVector Diagonal1 = (-TangentX + TangentY).GetSafeNormal();
	const FVector D00 = (Up - Diagonal0 * SampleAngle).GetSafeNormal();
	const FVector D01 = (Up + Diagonal0 * SampleAngle).GetSafeNormal();
	const FVector D10 = (Up - Diagonal1 * SampleAngle).GetSafeNormal();
	const FVector D11 = (Up + Diagonal1 * SampleAngle).GetSafeNormal();
	const FVector DD0 = D01 * GetSurfaceRadius(D01) - D00 * GetSurfaceRadius(D00);
	const FVector DD1 = D11 * GetSurfaceRadius(D11) - D10 * GetSurfaceRadius(D10);
	FVector NormalXY = FVector::CrossProduct(DX, DY).GetSafeNormal();
	FVector NormalDiagonal = FVector::CrossProduct(DD0, DD1).GetSafeNormal();
	if (FVector::DotProduct(NormalXY, Up) < 0.0f) NormalXY *= -1.0f;
	if (FVector::DotProduct(NormalDiagonal, Up) < 0.0f) NormalDiagonal *= -1.0f;
	FVector Normal = (NormalXY + NormalDiagonal).GetSafeNormal();
	if (FVector::DotProduct(Normal, Up) < 0.0f) Normal *= -1.0f;
	return Normal.IsNearlyZero() ? Up : Normal;
}
bool FABTSM3TerrainVisualField::QueryContinuousSurfaceSample(
	const FVector& UnitDirection,
	const int32 StartCellHint,
	FABTSM3ContinuousSurfaceSample& OutSample) const
{
	if (!QueryContinuousSurfaceBaseSample(
			UnitDirection, StartCellHint, OutSample))
	{
		return false;
	}
	OutSample.SurfaceNormal = GetSurfaceNormal(UnitDirection);
	return true;
}

bool FABTSM3TerrainVisualField::QueryContinuousSurfaceBaseSample(
	const FVector& UnitDirection,
	const int32 StartCellHint,
	FABTSM3ContinuousSurfaceSample& OutSample) const
{
	OutSample = FABTSM3ContinuousSurfaceSample();
	if (!IsReady() || UnitDirection.IsNearlyZero()) return false;
	const FVector Direction = UnitDirection.GetSafeNormal();
	OutSample.SurfaceRadiusCM = GetSurfaceRadiusWithHint(
		Direction, StartCellHint, OutSample.CellId);
	if (OutSample.CellId == INDEX_NONE) return false;
	OutSample.TerrainColor = GetDebugTerrainColorForCell(
		Direction,
		OutSample.CellId);
	return true;
}

bool FABTSM3TerrainVisualField::QuerySurfaceGeometry(
	const FVector& UnitDirection,
	const int32 StartCellHint,
	int32& OutCellId,
	float& OutSurfaceRadiusCM,
	FVector& OutSurfaceNormal) const
{
	OutCellId = INDEX_NONE;
	OutSurfaceRadiusCM = 0.0f;
	OutSurfaceNormal = FVector::UpVector;
	if (!IsReady() || UnitDirection.IsNearlyZero()) return false;
	const FVector Direction = UnitDirection.GetSafeNormal();
	OutSurfaceRadiusCM = GetSurfaceRadiusWithHint(
		Direction, StartCellHint, OutCellId);
	if (OutCellId == INDEX_NONE) return false;
	// Keep the eight offset probes on their original Cell-0 canonical path.
	// Production mesh generation invokes this normal path serially because its
	// exact oracle proved worker-thread evaluation can drift in the low bits.
	OutSurfaceNormal = GetSurfaceNormal(Direction);
	return true;
}

bool FABTSM3TerrainVisualField::QuerySurfaceSDF(
	const FVector& UnitDirection,
	const float PhysicsBlendWidthCM,
	FABTSM3SurfaceSDFSample& OutSample) const
{
	OutSample = FABTSM3SurfaceSDFSample();
	if (!IsReady() || UnitDirection.IsNearlyZero() || CellStates == nullptr) return false;

	const FVector Direction = UnitDirection.GetSafeNormal();
	const int32 CellId = FindNearestCell(Direction);
	if (!CellStates->IsValidIndex(CellId)) return false;
	OutSample.CellId = CellId;
	OutSample.PrimaryTerrain = FABTSM3TerrainFeatureVisualBuilder::ResolveLandType((*CellStates)[CellId]);
	OutSample.SecondaryTerrain = OutSample.PrimaryTerrain;
	OutSample.SurfaceNormal = GetSurfaceNormal(Direction);

	const FABTSM3BoundarySegment* Best = nullptr;
	const FABTSM3BoundarySegment* Second = nullptr;
	float BestDistanceCM = 0.0f;
	float SecondDistanceCM = 0.0f;
	FindTwoNearestTerrainFeatures(Direction, CellId, Best, BestDistanceCM, Second, SecondDistanceCM);
	if (Best != nullptr && CellStates->IsValidIndex(Best->SourceCellAId))
	{
		OutSample.PrimaryTerrain = FABTSM3TerrainFeatureVisualBuilder::ResolveLandType((*CellStates)[Best->SourceCellAId]);
		if (Second != nullptr && CellStates->IsValidIndex(Second->SourceCellAId))
		{
			OutSample.SecondaryTerrain = FABTSM3TerrainFeatureVisualBuilder::ResolveLandType((*CellStates)[Second->SourceCellAId]);
			const float WidthCM = FMath::Max(1.0f, PhysicsBlendWidthCM);
			OutSample.PrimaryTerrainWeight = FMath::SmoothStep(0.0f, WidthCM * 2.0f, SecondDistanceCM - BestDistanceCM);
		}
	}

	const auto ComputeLinearMask = [&](const TArray<TArray<FABTSM3RiverVisualSegment>>& SegmentsByCell)
	{
		if (!SegmentsByCell.IsValidIndex(CellId)) return 0.0f;
		float BestMask = 0.0f;
		for (const FABTSM3RiverVisualSegment& Segment : SegmentsByCell[CellId])
		{
			const float DistanceCM = FABTSM3RiverVisualBuilder::GetDistanceToSegmentCM(Direction, Segment, BaseRadiusCM);
			const float Mask = 1.0f - FMath::SmoothStep(
				Segment.HalfWidthCM,
				Segment.HalfWidthCM + FMath::Max(1.0f, PhysicsBlendWidthCM),
				DistanceCM);
			BestMask = FMath::Max(BestMask, Mask);
		}
		return BestMask;
	};
	OutSample.RoadWeight = ComputeLinearMask(RoadSegmentsByCell);
	OutSample.RiverWeight = ComputeLinearMask(RiverSegmentsByCell);
	return true;
}

FLinearColor FABTSM3TerrainVisualField::GetTerrainBaseColor(
	const EABTSM3TerrainType TerrainType)
{
	switch (TerrainType)
	{
	case EABTSM3TerrainType::Forest: return FLinearColor(0.08f, 0.28f, 0.10f);
	case EABTSM3TerrainType::Highland: return FLinearColor(0.45f, 0.34f, 0.18f);
	case EABTSM3TerrainType::Mountain: return FLinearColor(0.32f, 0.30f, 0.28f);
	case EABTSM3TerrainType::Water: return FLinearColor(0.03f, 0.20f, 0.36f);
	default: return FLinearColor(0.28f, 0.46f, 0.18f);
	}
}

FLinearColor FABTSM3TerrainVisualField::GetCellColor(const int32 CellId) const
{
	if (CellStates == nullptr || !CellStates->IsValidIndex(CellId)) return FLinearColor::Gray;
	return GetTerrainBaseColor((*CellStates)[CellId].TerrainType);
}

FLinearColor FABTSM3TerrainVisualField::GetCellBaseLandColor(
	const int32 CellId) const
{
	if (CellStates == nullptr || !CellStates->IsValidIndex(CellId)) return FLinearColor::Gray;
	// bWater is a compatibility/cache flag derived from river edges. It must not
	// turn the entire logical Cell into a hexagonal water polygon in the material.
	return GetTerrainBaseColor(
		FABTSM3TerrainFeatureVisualBuilder::ResolveLandType(
			(*CellStates)[CellId]));
}

FLinearColor FABTSM3TerrainVisualField::GetDebugTerrainColor(const FVector& UnitDirection) const
{
	const FVector Direction = UnitDirection.GetSafeNormal();
	const int32 CellId = FindNearestCell(Direction);
	return GetDebugTerrainColorForCell(Direction, CellId);
}

FLinearColor FABTSM3TerrainVisualField::GetDebugTerrainColorForCell(
	const FVector& UnitDirection,
	const int32 CellId) const
{
	const FVector Direction = UnitDirection.GetSafeNormal();
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
	if (Best == nullptr) return GetCellBaseLandColor(CellId);
	const FLinearColor BestColor = GetCellBaseLandColor(Best->SourceCellAId);
	if (Second == nullptr) return BestColor;
	const FLinearColor SecondColor = GetCellBaseLandColor(Second->SourceCellAId);
	const float BestWeight = FMath::SmoothStep(0.0f, ColorBlendWidthCM * 2.0f, SecondDistance - BestDistance);
	return FMath::Lerp(0.5f * (BestColor + SecondColor), BestColor, BestWeight);
}

bool FABTSM3TerrainVisualField::QueryScoutMapColor(
	const FVector& UnitDirection,
	const FLinearColor& RoadColor,
	const FLinearColor& RiverColor,
	const int32 StartCellHint,
	int32& OutCellId,
	FLinearColor& OutColor) const
{
	OutCellId = INDEX_NONE;
	OutColor = FLinearColor::Black;
	if (!IsReady() || UnitDirection.IsNearlyZero()) return false;
	const FVector Direction = UnitDirection.GetSafeNormal();
	OutCellId = FindNearestCell(Direction, StartCellHint);
	if (OutCellId == INDEX_NONE) return false;

	const FABTSM3BoundarySegment* Best = nullptr;
	const FABTSM3BoundarySegment* Second = nullptr;
	float BestDistanceCM = 0.0f;
	float SecondDistanceCM = 0.0f;
	FindTwoNearestTerrainFeatures(Direction, OutCellId, Best, BestDistanceCM, Second, SecondDistanceCM);
	if (Best == nullptr)
	{
		OutColor = GetCellBaseLandColor(OutCellId);
	}
	else
	{
		const FLinearColor BestColor = GetCellBaseLandColor(Best->SourceCellAId);
		if (Second == nullptr)
		{
			OutColor = BestColor;
		}
		else
		{
			const FLinearColor SecondColor = GetCellBaseLandColor(Second->SourceCellAId);
			const float BestWeight = FMath::SmoothStep(
				0.0f, ColorBlendWidthCM * 2.0f, SecondDistanceCM - BestDistanceCM);
			OutColor = FMath::Lerp(0.5f * (BestColor + SecondColor), BestColor, BestWeight);
		}
	}

	const auto ComputeLinearMask = [&](const TArray<TArray<FABTSM3RiverVisualSegment>>& SegmentsByCell)
	{
		if (!SegmentsByCell.IsValidIndex(OutCellId)) return 0.0f;
		float BestMask = 0.0f;
		for (const FABTSM3RiverVisualSegment& Segment : SegmentsByCell[OutCellId])
		{
			const float DistanceCM = FABTSM3RiverVisualBuilder::GetDistanceToSegmentCM(
				Direction, Segment, BaseRadiusCM);
			const float Mask = 1.0f - FMath::SmoothStep(
				Segment.HalfWidthCM,
				Segment.HalfWidthCM + FMath::Max(1.0f, ColorBlendWidthCM),
				DistanceCM);
			BestMask = FMath::Max(BestMask, Mask);
		}
		return BestMask;
	};
	OutColor = FMath::Lerp(OutColor, RoadColor, ComputeLinearMask(RoadSegmentsByCell));
	OutColor = FMath::Lerp(OutColor, RiverColor, ComputeLinearMask(RiverSegmentsByCell));
	OutColor.A = 1.0f;
	return true;
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

void FABTSM3TerrainVisualField::BuildRoadSegments(const TArray<FABTSM3CellEdgeState>& EdgeStates)
{
	RoadSegmentsByCell.Reset();
	if (Cells == nullptr) return;
	TArray<FABTSM3RiverVisualSegment> Segments;
	// These widths are visual defaults only. Physics has its own transition width,
	// while the road centerlines remain the same CellTopo transport edges.
	FABTSM3RiverVisualBuilder::BuildRoadSegments(*Cells, EdgeStates, TrailRoadHalfWidthCM, MainRoadHalfWidthCM, Segments);
	TArray<TArray<int32>> SegmentIndicesByCell;
	int32 DroppedReferences = 0;
	FABTSM3RiverVisualBuilder::BuildLocalSegmentIndices(*Cells, Segments, BaseRadiusCM, ColorBlendWidthCM, 0, SegmentIndicesByCell, DroppedReferences);
	RoadSegmentsByCell.SetNum(Cells->Num());
	for (int32 CellId = 0; CellId < SegmentIndicesByCell.Num(); ++CellId)
	{
		for (const int32 SegmentIndex : SegmentIndicesByCell[CellId]) RoadSegmentsByCell[CellId].Add(Segments[SegmentIndex]);
	}
}
