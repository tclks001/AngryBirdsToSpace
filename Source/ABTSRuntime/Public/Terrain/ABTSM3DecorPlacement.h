// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UStaticMesh;

/** Conservative collision description derived from a mesh's simple Chaos geometry. */
struct ABTSRUNTIME_API FABTSM3DecorCollisionShape
{
	FBox LocalBounds = FBox(ForceInit);
	TArray<FVector> LocalSurfaceSamples;

	bool IsValid() const
	{
		return LocalBounds.IsValid != 0
			&& LocalBounds.GetExtent().GetMin() > UE_SMALL_NUMBER
			&& !LocalSurfaceSamples.IsEmpty();
	}
};

/** Uniformly-scaled oriented collision box used by the deterministic broad/narrow phase. */
struct ABTSRUNTIME_API FABTSM3DecorOrientedBounds
{
	FVector Center = FVector::ZeroVector;
	FVector Axes[3] = {
		FVector::ForwardVector,
		FVector::RightVector,
		FVector::UpVector};
	FVector HalfExtent = FVector::ZeroVector;
	FBox WorldAABB = FBox(ForceInit);

	bool IsValid() const
	{
		return WorldAABB.IsValid != 0
			&& HalfExtent.GetMin() > UE_SMALL_NUMBER;
	}
};

/**
 * Pure geometry kernel for M3 natural-decoration placement.
 * It never queries a physics scene, so generation is independent of BodyInstance timing.
 */
class ABTSRUNTIME_API FABTSM3DecorPlacementGeometry
{
public:
	static bool BuildCollisionShape(
		const UStaticMesh* Mesh,
		FABTSM3DecorCollisionShape& OutShape,
		FString& OutFailure);

	static bool TrySeatOnSurface(
		const FABTSM3DecorCollisionShape& Shape,
		const FVector& RadialUp,
		const FQuat& Rotation,
		float UniformScale,
		const FVector& InitialLocation,
		float GroundClearanceCM,
		TFunctionRef<float(const FVector&)> QuerySignedSurfaceDistanceCM,
		FTransform& OutTransform,
		float& OutCorrectionCM,
		float& OutMinimumClearanceCM);

	static FABTSM3DecorOrientedBounds BuildOrientedBounds(
		const FABTSM3DecorCollisionShape& Shape,
		const FTransform& Transform);

	/** Returns true when the two boxes violate the requested positive separation margin. */
	static bool OverlapsWithMargin(
		const FABTSM3DecorOrientedBounds& A,
		const FABTSM3DecorOrientedBounds& B,
		float SeparationMarginCM,
		float* OutMaximumSeparatingAxisGapCM = nullptr);

	static uint32 MakeAttemptSeed(
		int32 WorldSeed,
		int32 CellId,
		int32 Slot,
		int32 Attempt,
		uint32 VisualVariantSeed);
};

/** Spatial broad phase shared by forest and rock candidates. */
class ABTSRUNTIME_API FABTSM3DecorSpatialHash
{
public:
	explicit FABTSM3DecorSpatialHash(float InCellSizeCM);

	bool WouldOverlap(
		const FABTSM3DecorOrientedBounds& Candidate,
		float SeparationMarginCM,
		float& OutNearestAxisGapCM) const;

	void Add(const FABTSM3DecorOrientedBounds& Bounds);
	int32 Num() const { return AcceptedBounds.Num(); }

private:
	void GetCellRange(
		const FBox& Bounds,
		FIntVector& OutMin,
		FIntVector& OutMax) const;

	float CellSizeCM = 100.0f;
	TArray<FABTSM3DecorOrientedBounds> AcceptedBounds;
	TMap<FIntVector, TArray<int32>> IndicesByCell;
	mutable TArray<int32> VisitEpochByAcceptedIndex;
	mutable int32 CurrentVisitEpoch = 0;
};
