// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slingshot/ABTSSlingshotVisualTypes.h"

#include "Engine/StaticMesh.h"

namespace
{
	constexpr float MinimumMeshDimensionCM = 0.01f;

	FVector DivideDimensionsSafely(const FVector& Numerator, const FVector& Denominator)
	{
		return FVector(
			Numerator.X / FMath::Max(MinimumMeshDimensionCM, FMath::Abs(Denominator.X)),
			Numerator.Y / FMath::Max(MinimumMeshDimensionCM, FMath::Abs(Denominator.Y)),
			Numerator.Z / FMath::Max(MinimumMeshDimensionCM, FMath::Abs(Denominator.Z)));
	}
}

FTransform ABTSMakeSlingshotVisualTransform(
	const UStaticMesh* Mesh,
	const FVector& TargetAnchorWorld,
	const FQuat& TargetRotation,
	const FVector& TargetSizeCM,
	const FABTSSlingshotVisualSlot& VisualSlot,
	const EABTSSlingshotVisualAnchor Anchor)
{
	const FBoxSphereBounds Bounds = Mesh
		? Mesh->GetBounds()
		: FBoxSphereBounds(FVector::ZeroVector, FVector(50.0f), 86.60254f);
	const FVector SourceSizeCM = Bounds.BoxExtent * 2.0f;
	const FVector FitScale = DivideDimensionsSafely(TargetSizeCM.GetAbs(), SourceSizeCM);
	const FVector LocalFineScale(
		FMath::Max(0.001f, FMath::Abs(VisualSlot.LocalScale.X)),
		FMath::Max(0.001f, FMath::Abs(VisualSlot.LocalScale.Y)),
		FMath::Max(0.001f, FMath::Abs(VisualSlot.LocalScale.Z)));
	const FVector FinalScale = FitScale * LocalFineScale;
	const FQuat FinalRotation = TargetRotation * VisualSlot.LocalRotation.Quaternion();

	FVector SourceAnchor = Bounds.Origin;
	if (Anchor == EABTSSlingshotVisualAnchor::BoundsBottomCenter)
	{
		SourceAnchor.Z -= Bounds.BoxExtent.Z;
	}

	const FVector OffsetWorld = TargetRotation.RotateVector(VisualSlot.LocalOffsetCM);
	const FVector PivotCorrectionWorld = FinalRotation.RotateVector(SourceAnchor * FinalScale);
	return FTransform(
		FinalRotation,
		TargetAnchorWorld + OffsetWorld - PivotCorrectionWorld,
		FinalScale);
}

FVector ABTSScaleSlingshotPouchConnectionOffset(
	const FVector& AuthoredOffsetCM,
	const FABTSSlingshotVisualSlot& PouchVisualSlot)
{
	return AuthoredOffsetCM * FVector(
		FMath::Max(0.001f, FMath::Abs(PouchVisualSlot.LocalScale.X)),
		FMath::Max(0.001f, FMath::Abs(PouchVisualSlot.LocalScale.Y)),
		FMath::Max(0.001f, FMath::Abs(PouchVisualSlot.LocalScale.Z)));
}
