// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/ABTSM3DecorPlacement.h"

#include "Engine/StaticMesh.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BodySetup.h"

namespace
{
void AddABTSM3DecorBoxSamples(
	const FBox& Box,
	TArray<FVector>& OutSamples)
{
	const FVector Center = Box.GetCenter();
	const FVector Extent = Box.GetExtent();
	for (int32 X = -1; X <= 1; X += 2)
	{
		for (int32 Y = -1; Y <= 1; Y += 2)
		{
			for (int32 Z = -1; Z <= 1; Z += 2)
			{
				OutSamples.Add(Center + FVector(X, Y, Z) * Extent);
			}
		}
	}
	OutSamples.Add(Center + FVector(Extent.X, 0.0f, 0.0f));
	OutSamples.Add(Center - FVector(Extent.X, 0.0f, 0.0f));
	OutSamples.Add(Center + FVector(0.0f, Extent.Y, 0.0f));
	OutSamples.Add(Center - FVector(0.0f, Extent.Y, 0.0f));
	OutSamples.Add(Center + FVector(0.0f, 0.0f, Extent.Z));
	OutSamples.Add(Center - FVector(0.0f, 0.0f, Extent.Z));
}

float ProjectABTSM3DecorRadius(
	const FABTSM3DecorOrientedBounds& Bounds,
	const FVector& Axis,
	const float ExtentInflationCM)
{
	return FMath::Abs(FVector::DotProduct(Bounds.Axes[0], Axis))
			* (Bounds.HalfExtent.X + ExtentInflationCM)
		+ FMath::Abs(FVector::DotProduct(Bounds.Axes[1], Axis))
			* (Bounds.HalfExtent.Y + ExtentInflationCM)
		+ FMath::Abs(FVector::DotProduct(Bounds.Axes[2], Axis))
			* (Bounds.HalfExtent.Z + ExtentInflationCM);
}
}

bool FABTSM3DecorPlacementGeometry::BuildCollisionShape(
	const UStaticMesh* Mesh,
	FABTSM3DecorCollisionShape& OutShape,
	FString& OutFailure)
{
	OutShape = FABTSM3DecorCollisionShape();
	OutFailure.Reset();
	if (Mesh == nullptr)
	{
		OutFailure = TEXT("MeshUnavailable");
		return false;
	}
	const UBodySetup* BodySetup = Mesh->GetBodySetup();
	if (BodySetup == nullptr)
	{
		OutFailure = TEXT("BodySetupUnavailable");
		return false;
	}
	const FKAggregateGeom& Aggregate = BodySetup->AggGeom;
	const int32 SimpleShapeCount = Aggregate.SphereElems.Num()
		+ Aggregate.BoxElems.Num()
		+ Aggregate.SphylElems.Num()
		+ Aggregate.ConvexElems.Num()
		+ Aggregate.TaperedCapsuleElems.Num();
	if (SimpleShapeCount <= 0)
	{
		OutFailure = TEXT("SimpleCollisionUnavailable");
		return false;
	}

	OutShape.LocalBounds = Aggregate.CalcAABB(FTransform::Identity);
	if (OutShape.LocalBounds.IsValid == 0)
	{
		OutFailure = TEXT("SimpleCollisionBoundsInvalid");
		return false;
	}

	// The production tree and rock both use convex simple collision. Sample every
	// convex vertex plus lower-hull face centroids so seating follows the Chaos
	// hull instead of a render bound or mesh pivot convention. Upper-hull face
	// centroids cannot become ground support while local +Z follows surface Up.
	const double LowerHullCentroidMaximumZ = OutShape.LocalBounds.Min.Z
		+ OutShape.LocalBounds.GetSize().Z * 0.2;
	for (const FKConvexElem& Convex : Aggregate.ConvexElems)
	{
		const FTransform ElementTransform = Convex.GetTransform();
		for (const FVector& Vertex : Convex.VertexData)
		{
			OutShape.LocalSurfaceSamples.Add(
				ElementTransform.TransformPosition(Vertex));
		}
		for (int32 Index = 0; Index + 2 < Convex.IndexData.Num(); Index += 3)
		{
			const int32 A = Convex.IndexData[Index];
			const int32 B = Convex.IndexData[Index + 1];
			const int32 C = Convex.IndexData[Index + 2];
			if (Convex.VertexData.IsValidIndex(A)
				&& Convex.VertexData.IsValidIndex(B)
				&& Convex.VertexData.IsValidIndex(C))
			{
				const FVector FaceCentroid = ElementTransform.TransformPosition(
					(Convex.VertexData[A]
						+ Convex.VertexData[B]
						+ Convex.VertexData[C]) / 3.0f);
				if (FaceCentroid.Z <= LowerHullCentroidMaximumZ)
				{
					OutShape.LocalSurfaceSamples.Add(FaceCentroid);
				}
			}
		}
	}
	if (!OutShape.LocalSurfaceSamples.IsEmpty())
	{
		const FVector BoundsCenter = OutShape.LocalBounds.GetCenter();
		OutShape.LocalSurfaceSamples.Add(FVector(
			BoundsCenter.X,
			BoundsCenter.Y,
			OutShape.LocalBounds.Min.Z));
	}
	// Primitive-only preview/fallback meshes still fail closed against their
	// simple-collision envelope. This path is intentionally conservative.
	if (OutShape.LocalSurfaceSamples.IsEmpty())
	{
		AddABTSM3DecorBoxSamples(
			OutShape.LocalBounds,
			OutShape.LocalSurfaceSamples);
	}
	if (!OutShape.IsValid())
	{
		OutFailure = TEXT("CollisionDescriptionInvalid");
		OutShape = FABTSM3DecorCollisionShape();
		return false;
	}
	return true;
}

bool FABTSM3DecorPlacementGeometry::TrySeatOnSurface(
	const FABTSM3DecorCollisionShape& Shape,
	const FVector& RadialUp,
	const FQuat& Rotation,
	const float UniformScale,
	const FVector& InitialLocation,
	const float GroundClearanceCM,
	TFunctionRef<float(const FVector&)> QuerySignedSurfaceDistanceCM,
	FTransform& OutTransform,
	float& OutCorrectionCM,
	float& OutMinimumClearanceCM)
{
	OutTransform = FTransform::Identity;
	OutCorrectionCM = 0.0f;
	OutMinimumClearanceCM = -TNumericLimits<float>::Max();
	const FVector ResolvedUp = RadialUp.GetSafeNormal();
	if (!Shape.IsValid()
		|| ResolvedUp.IsNearlyZero()
		|| !Rotation.IsNormalized()
		|| !FMath::IsFinite(UniformScale)
		|| UniformScale <= UE_SMALL_NUMBER
		|| !FMath::IsFinite(GroundClearanceCM)
		|| GroundClearanceCM < 0.0f)
	{
		return false;
	}

	FVector Location = InitialLocation;
	const FVector Scale(UniformScale);
	constexpr int32 MaximumSeatIterations = 4;
	constexpr float SeatToleranceCM = 0.01f;
	for (int32 Iteration = 0; Iteration < MaximumSeatIterations; ++Iteration)
	{
		const FTransform Candidate(Rotation, Location, Scale);
		float MinimumSignedDistanceCM = TNumericLimits<float>::Max();
		for (const FVector& LocalSample : Shape.LocalSurfaceSamples)
		{
			const float SignedDistanceCM = QuerySignedSurfaceDistanceCM(
				Candidate.TransformPosition(LocalSample));
			if (!FMath::IsFinite(SignedDistanceCM))
			{
				return false;
			}
			MinimumSignedDistanceCM = FMath::Min(
				MinimumSignedDistanceCM,
				SignedDistanceCM);
		}
		OutMinimumClearanceCM = MinimumSignedDistanceCM;
		const float RequiredCorrectionCM = GroundClearanceCM
			- MinimumSignedDistanceCM;
		if (RequiredCorrectionCM <= SeatToleranceCM)
		{
			OutTransform = Candidate;
			return true;
		}
		Location += ResolvedUp * RequiredCorrectionCM;
		OutCorrectionCM += RequiredCorrectionCM;
	}

	OutTransform = FTransform(Rotation, Location, Scale);
	OutMinimumClearanceCM = TNumericLimits<float>::Max();
	for (const FVector& LocalSample : Shape.LocalSurfaceSamples)
	{
		const float SignedDistanceCM = QuerySignedSurfaceDistanceCM(
			OutTransform.TransformPosition(LocalSample));
		if (!FMath::IsFinite(SignedDistanceCM))
		{
			return false;
		}
		OutMinimumClearanceCM = FMath::Min(
			OutMinimumClearanceCM,
			SignedDistanceCM);
	}
	return OutMinimumClearanceCM + SeatToleranceCM >= GroundClearanceCM;
}

FABTSM3DecorOrientedBounds
FABTSM3DecorPlacementGeometry::BuildOrientedBounds(
	const FABTSM3DecorCollisionShape& Shape,
	const FTransform& Transform)
{
	FABTSM3DecorOrientedBounds Result;
	if (!Shape.IsValid() || !Transform.IsValid())
	{
		return Result;
	}
	const FVector AbsScale = Transform.GetScale3D().GetAbs();
	Result.Center = Transform.TransformPosition(Shape.LocalBounds.GetCenter());
	Result.Axes[0] = Transform.GetRotation().GetAxisX().GetSafeNormal();
	Result.Axes[1] = Transform.GetRotation().GetAxisY().GetSafeNormal();
	Result.Axes[2] = Transform.GetRotation().GetAxisZ().GetSafeNormal();
	Result.HalfExtent = Shape.LocalBounds.GetExtent() * AbsScale;
	const FVector AABBExtent(
		FMath::Abs(Result.Axes[0].X) * Result.HalfExtent.X
			+ FMath::Abs(Result.Axes[1].X) * Result.HalfExtent.Y
			+ FMath::Abs(Result.Axes[2].X) * Result.HalfExtent.Z,
		FMath::Abs(Result.Axes[0].Y) * Result.HalfExtent.X
			+ FMath::Abs(Result.Axes[1].Y) * Result.HalfExtent.Y
			+ FMath::Abs(Result.Axes[2].Y) * Result.HalfExtent.Z,
		FMath::Abs(Result.Axes[0].Z) * Result.HalfExtent.X
			+ FMath::Abs(Result.Axes[1].Z) * Result.HalfExtent.Y
			+ FMath::Abs(Result.Axes[2].Z) * Result.HalfExtent.Z);
	Result.WorldAABB = FBox(Result.Center - AABBExtent, Result.Center + AABBExtent);
	return Result;
}

bool FABTSM3DecorPlacementGeometry::OverlapsWithMargin(
	const FABTSM3DecorOrientedBounds& A,
	const FABTSM3DecorOrientedBounds& B,
	const float SeparationMarginCM,
	float* OutMaximumSeparatingAxisGapCM)
{
	if (OutMaximumSeparatingAxisGapCM != nullptr)
	{
		*OutMaximumSeparatingAxisGapCM = -TNumericLimits<float>::Max();
	}
	if (!A.IsValid() || !B.IsValid() || SeparationMarginCM < 0.0f)
	{
		return true;
	}

	FVector Axes[15] = {
		A.Axes[0], A.Axes[1], A.Axes[2],
		B.Axes[0], B.Axes[1], B.Axes[2]};
	int32 AxisCount = 6;
	for (int32 AIndex = 0; AIndex < 3; ++AIndex)
	{
		for (int32 BIndex = 0; BIndex < 3; ++BIndex)
		{
			const FVector Cross = FVector::CrossProduct(
				A.Axes[AIndex],
				B.Axes[BIndex]);
			if (!Cross.IsNearlyZero())
			{
				Axes[AxisCount++] = Cross.GetSafeNormal();
			}
		}
	}

	const FVector CenterDelta = B.Center - A.Center;
	const float ExtentInflationCM = SeparationMarginCM * 0.5f;
	float MaximumOriginalGapCM = -TNumericLimits<float>::Max();
	for (int32 AxisIndex = 0; AxisIndex < AxisCount; ++AxisIndex)
	{
		const FVector Axis = Axes[AxisIndex];
		const float CenterDistanceCM = FMath::Abs(
			FVector::DotProduct(CenterDelta, Axis));
		const float OriginalRadiusCM = ProjectABTSM3DecorRadius(A, Axis, 0.0f)
			+ ProjectABTSM3DecorRadius(B, Axis, 0.0f);
		MaximumOriginalGapCM = FMath::Max(
			MaximumOriginalGapCM,
			CenterDistanceCM - OriginalRadiusCM);
		const float InflatedRadiusCM = ProjectABTSM3DecorRadius(
			A, Axis, ExtentInflationCM)
			+ ProjectABTSM3DecorRadius(B, Axis, ExtentInflationCM);
		if (CenterDistanceCM > InflatedRadiusCM)
		{
			if (OutMaximumSeparatingAxisGapCM != nullptr)
			{
				*OutMaximumSeparatingAxisGapCM = MaximumOriginalGapCM;
			}
			return false;
		}
	}
	if (OutMaximumSeparatingAxisGapCM != nullptr)
	{
		*OutMaximumSeparatingAxisGapCM = MaximumOriginalGapCM;
	}
	return true;
}

uint32 FABTSM3DecorPlacementGeometry::MakeAttemptSeed(
	const int32 WorldSeed,
	const int32 CellId,
	const int32 Slot,
	const int32 Attempt,
	const uint32 VisualVariantSeed)
{
	uint32 Seed = HashCombineFast(
		GetTypeHash(WorldSeed),
		GetTypeHash(CellId));
	Seed = HashCombineFast(Seed, GetTypeHash(Slot));
	Seed = HashCombineFast(Seed, GetTypeHash(Attempt));
	return HashCombineFast(Seed, VisualVariantSeed);
}

FABTSM3DecorSpatialHash::FABTSM3DecorSpatialHash(
	const float InCellSizeCM)
	: CellSizeCM(FMath::Max(InCellSizeCM, 1.0f))
{
}

void FABTSM3DecorSpatialHash::GetCellRange(
	const FBox& Bounds,
	FIntVector& OutMin,
	FIntVector& OutMax) const
{
	OutMin = FIntVector(
		FMath::FloorToInt(Bounds.Min.X / CellSizeCM),
		FMath::FloorToInt(Bounds.Min.Y / CellSizeCM),
		FMath::FloorToInt(Bounds.Min.Z / CellSizeCM));
	OutMax = FIntVector(
		FMath::FloorToInt(Bounds.Max.X / CellSizeCM),
		FMath::FloorToInt(Bounds.Max.Y / CellSizeCM),
		FMath::FloorToInt(Bounds.Max.Z / CellSizeCM));
}

bool FABTSM3DecorSpatialHash::WouldOverlap(
	const FABTSM3DecorOrientedBounds& Candidate,
	const float SeparationMarginCM,
	float& OutNearestAxisGapCM) const
{
	OutNearestAxisGapCM = TNumericLimits<float>::Max();
	if (!Candidate.IsValid())
	{
		return true;
	}
	const FBox QueryBounds = Candidate.WorldAABB.ExpandBy(
		FMath::Max(SeparationMarginCM, 0.0f));
	FIntVector MinCell;
	FIntVector MaxCell;
	GetCellRange(QueryBounds, MinCell, MaxCell);
	++CurrentVisitEpoch;
	if (CurrentVisitEpoch == MAX_int32)
	{
		VisitEpochByAcceptedIndex.Init(0, AcceptedBounds.Num());
		CurrentVisitEpoch = 1;
	}
	for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
	{
		for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
		{
			for (int32 Z = MinCell.Z; Z <= MaxCell.Z; ++Z)
			{
				const TArray<int32>* Bucket = IndicesByCell.Find(
					FIntVector(X, Y, Z));
				if (Bucket == nullptr)
				{
					continue;
				}
				for (const int32 AcceptedIndex : *Bucket)
				{
					check(VisitEpochByAcceptedIndex.IsValidIndex(AcceptedIndex));
					if (VisitEpochByAcceptedIndex[AcceptedIndex]
						== CurrentVisitEpoch)
					{
						continue;
					}
					VisitEpochByAcceptedIndex[AcceptedIndex] = CurrentVisitEpoch;
					float AxisGapCM = 0.0f;
					if (FABTSM3DecorPlacementGeometry::OverlapsWithMargin(
							Candidate,
							AcceptedBounds[AcceptedIndex],
							SeparationMarginCM,
							&AxisGapCM))
					{
						return true;
					}
					OutNearestAxisGapCM = FMath::Min(
						OutNearestAxisGapCM,
						AxisGapCM);
				}
			}
		}
	}
	return false;
}

void FABTSM3DecorSpatialHash::Add(
	const FABTSM3DecorOrientedBounds& Bounds)
{
	check(Bounds.IsValid());
	const int32 AcceptedIndex = AcceptedBounds.Add(Bounds);
	VisitEpochByAcceptedIndex.Add(0);
	FIntVector MinCell;
	FIntVector MaxCell;
	GetCellRange(Bounds.WorldAABB, MinCell, MaxCell);
	for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
	{
		for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
		{
			for (int32 Z = MinCell.Z; Z <= MaxCell.Z; ++Z)
			{
				IndicesByCell.FindOrAdd(FIntVector(X, Y, Z)).Add(
					AcceptedIndex);
			}
		}
	}
}
