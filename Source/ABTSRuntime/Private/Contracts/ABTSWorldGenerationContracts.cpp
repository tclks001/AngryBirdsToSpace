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

bool IsFiniteWorldGenerationContractBox(const FBox& Box)
{
	return Box.IsValid
		&& IsFiniteContractVector(Box.Min)
		&& IsFiniteContractVector(Box.Max);
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

bool FABTSJuryDemoFixedSixBuildingSite::IsUsable(
	const double Tolerance) const
{
	const double SafeTolerance = FMath::Max(Tolerance, UE_DOUBLE_SMALL_NUMBER);
	const FVector Scale = WorldTransform.GetScale3D();
	if (ManifestEntryId.IsNone()
		|| EncounterIndex < 0
		|| EncounterIndex >= FABTSJuryDemoFixedSixContract::ExpectedSiteCount
		|| DifficultyTier != EncounterIndex
		|| DeterministicSeed <= 0
		|| DescriptorHash == 0
		|| !WorldTransform.IsValid()
		|| !Scale.Equals(FVector::OneVector, SafeTolerance)
		|| !FMath::IsFinite(PadHalfExtentCM.X)
		|| !FMath::IsFinite(PadHalfExtentCM.Y)
		|| PadHalfExtentCM.X <= 0.0
		|| PadHalfExtentCM.Y <= 0.0
		|| !IsFiniteWorldGenerationContractBox(LocalBounds)
		|| LocalBounds.Max.X < LocalBounds.Min.X
		|| LocalBounds.Max.Y < LocalBounds.Min.Y
		|| LocalBounds.Max.Z <= LocalBounds.Min.Z
		|| LocalBounds.Min.Z < -SafeTolerance)
	{
		return false;
	}

	const double RequiredHalfExtentX = FMath::Max(
		FMath::Abs(LocalBounds.Min.X),
		FMath::Abs(LocalBounds.Max.X));
	const double RequiredHalfExtentY = FMath::Max(
		FMath::Abs(LocalBounds.Min.Y),
		FMath::Abs(LocalBounds.Max.Y));
	return PadHalfExtentCM.X + SafeTolerance >= RequiredHalfExtentX
		&& PadHalfExtentCM.Y + SafeTolerance >= RequiredHalfExtentY;
}

bool FABTSJuryDemoFixedSixContract::IsEmpty() const
{
	return ContractVersion == 0
		&& PlacementSchemaVersion == 0
		&& DemoManifestVersion == 0
		&& DemoManifestHash == 0
		&& PlacementCatalogHash == 0
		&& WorldSeed == 0
		&& CandidateId == INDEX_NONE
		&& LayoutHash == 0
		&& Sites.IsEmpty();
}

bool FABTSJuryDemoFixedSixContract::IsUsable(const double Tolerance) const
{
	if (ContractVersion != CurrentContractVersion
		|| PlacementSchemaVersion != FrozenPlacementSchemaVersion
		|| DemoManifestVersion != FrozenDemoManifestVersion
		|| DemoManifestHash != FrozenDemoManifestHash
		|| PlacementCatalogHash != FrozenPlacementCatalogHash
		|| WorldSeed != FrozenWorldSeed
		|| CandidateId != FrozenCandidateId
		|| LayoutHash != FrozenLayoutHash
		|| Sites.Num() != ExpectedSiteCount)
	{
		return false;
	}

	TSet<FName> ManifestEntryIds;
	TSet<uint64> DescriptorHashes;
	for (int32 Index = 0; Index < Sites.Num(); ++Index)
	{
		const FABTSJuryDemoFixedSixBuildingSite& Site = Sites[Index];
		if (!Site.IsUsable(Tolerance)
			|| Site.EncounterIndex != Index
			|| ManifestEntryIds.Contains(Site.ManifestEntryId)
			|| DescriptorHashes.Contains(Site.DescriptorHash))
		{
			return false;
		}
		ManifestEntryIds.Add(Site.ManifestEntryId);
		DescriptorHashes.Add(Site.DescriptorHash);
	}
	return true;
}

bool FABTSBuildingGenerationContract::IsUsable(const double Tolerance) const
{
	if (!Identity.IsUsable()
		|| Sites.IsEmpty()
		|| (!JuryDemoFixedSix.IsEmpty()
			&& (!JuryDemoFixedSix.IsUsable(Tolerance)
				|| JuryDemoFixedSix.WorldSeed != Identity.WorldSeed)))
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
