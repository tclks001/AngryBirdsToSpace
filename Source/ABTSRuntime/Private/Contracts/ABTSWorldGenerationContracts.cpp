// Copyright Epic Games, Inc. All Rights Reserved.

#include "Contracts/ABTSWorldGenerationContracts.h"

namespace
{
bool IsFiniteContractVector(const FVector& Value)
{
	return FMath::IsFinite(Value.X)
		&& FMath::IsFinite(Value.Y)
		&& FMath::IsFinite(Value.Z);
}
}

bool FABTSGeneratedWorldIdentity::IsUsable() const
{
	return ContractVersion == CurrentContractVersion
		&& GeneratorVersion > 0
		&& GenerationAttempt >= 0
		&& bSourceWorldAccepted;
}

bool FABTSGeneratedBuildingSite::IsUsable(const double Tolerance) const
{
	const double SafeTolerance = FMath::Max(Tolerance, UE_DOUBLE_SMALL_NUMBER);
	const FVector Scale = WorldTransform.GetScale3D();
	if (SiteId == MAX_uint64
		|| TaskId < 0
		|| CellId < 0
		|| Purpose >= EABTSGeneratedBuildingPurpose::Count
		|| !WorldTransform.IsValid()
		|| !Scale.Equals(FVector::OneVector, SafeTolerance)
		|| !FMath::IsFinite(MaxSlopeDegrees)
		|| MaxSlopeDegrees < 0.0f
		|| MaxSlopeDegrees > 180.0f
		|| !IsFiniteContractVector(AnchorDirection)
		|| !IsFiniteContractVector(TangentForward)
		|| !IsFiniteContractVector(TangentRight)
		|| !FMath::IsFinite(PadHalfExtentCM.X)
		|| !FMath::IsFinite(PadHalfExtentCM.Y)
		|| PadHalfExtentCM.X <= 0.0f
		|| PadHalfExtentCM.Y <= 0.0f
		|| !FMath::IsFinite(PadEdgeBlendWidthCM)
		|| PadEdgeBlendWidthCM < 0.0f
		|| !FMath::IsFinite(PadTargetRadiusCM)
		|| PadTargetRadiusCM <= 0.0f)
	{
		return false;
	}

	const FVector Up = AnchorDirection.GetSafeNormal();
	const FVector Forward = TangentForward.GetSafeNormal();
	const FVector Right = TangentRight.GetSafeNormal();
	return FMath::IsNearlyEqual(
			AnchorDirection.SizeSquared(),
			1.0,
			SafeTolerance)
		&& FMath::IsNearlyEqual(
			TangentForward.SizeSquared(),
			1.0,
			SafeTolerance)
		&& FMath::IsNearlyEqual(
			TangentRight.SizeSquared(),
			1.0,
			SafeTolerance)
		&& !Up.IsNearlyZero(SafeTolerance)
		&& !Forward.IsNearlyZero(SafeTolerance)
		&& !Right.IsNearlyZero(SafeTolerance)
		&& FMath::Abs(FVector::DotProduct(Up, Forward)) <= SafeTolerance
		&& FMath::Abs(FVector::DotProduct(Up, Right)) <= SafeTolerance
		&& FMath::Abs(FVector::DotProduct(Forward, Right)) <= SafeTolerance
		&& FVector::DotProduct(
			FVector::CrossProduct(Forward, Right),
			Up) >= 1.0 - SafeTolerance
		&& FVector::DotProduct(
			WorldTransform.GetUnitAxis(EAxis::X),
			Forward) >= 1.0 - SafeTolerance
		&& FVector::DotProduct(
			WorldTransform.GetUnitAxis(EAxis::Y),
			Right) >= 1.0 - SafeTolerance
		&& FVector::DotProduct(
			WorldTransform.GetUnitAxis(EAxis::Z),
			Up) >= 1.0 - SafeTolerance;
}

bool FABTSBuildingGenerationContract::IsUsable(const double Tolerance) const
{
	if (!Identity.IsUsable() || Sites.IsEmpty())
	{
		return false;
	}

	TSet<uint64> SiteIds;
	TSet<uint64> TaskCellPairs;
	for (const FABTSGeneratedBuildingSite& Site : Sites)
	{
		const uint64 TaskCellPair =
			(static_cast<uint64>(static_cast<uint32>(Site.TaskId)) << 32)
			| static_cast<uint32>(Site.CellId);
		if (!Site.IsUsable(Tolerance)
			|| SiteIds.Contains(Site.SiteId)
			|| TaskCellPairs.Contains(TaskCellPair))
		{
			return false;
		}
		SiteIds.Add(Site.SiteId);
		TaskCellPairs.Add(TaskCellPair);
	}
	return true;
}

bool FABTSFinaleWorldContract::IsUsable(const double Tolerance) const
{
	return Identity.IsUsable()
		&& FMath::IsFinite(PrimaryRadiusCM)
		&& PrimaryRadiusCM > 0.0
		&& LaunchFrame.IsUsable(Tolerance)
		&& LaunchFrame.WorldTransform.GetScale3D().Equals(
			FVector::OneVector,
			FMath::Max(Tolerance, UE_DOUBLE_SMALL_NUMBER));
}
