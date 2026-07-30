// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCG/ABTSM3MonthlyWitness.h"

#include "ABTSRuntime.h"

namespace ABTSM3MonthlyWitnessPrivate
{
constexpr uint64 FnvOffset = 14695981039346656037ull;
constexpr uint64 FnvPrime = 1099511628211ull;
constexpr int32 Quantization = 1000;

struct FHash64
{
	uint64 Value = FnvOffset;

	void AddByte(const uint8 Input)
	{
		Value ^= Input;
		Value *= FnvPrime;
	}

	void AddInt32(const int32 Input)
	{
		const uint32 Bits = static_cast<uint32>(Input);
		for (int32 Shift = 0; Shift < 32; Shift += 8)
		{
			AddByte(static_cast<uint8>((Bits >> Shift) & 0xffu));
		}
	}

	void AddInt64(const int64 Input)
	{
		const uint64 Bits = static_cast<uint64>(Input);
		for (int32 Shift = 0; Shift < 64; Shift += 8)
		{
			AddByte(static_cast<uint8>((Bits >> Shift) & 0xffull));
		}
	}

	void AddBool(const bool Input)
	{
		AddByte(Input ? 1u : 0u);
	}

	void AddName(const FName Input)
	{
		const FString Text = Input.ToString();
		AddInt32(Text.Len());
		for (const TCHAR Character : Text)
		{
			AddInt32(static_cast<int32>(Character));
		}
	}

	void AddVectorCM(const FVector& Input)
	{
		AddInt32(FMath::RoundToInt(Input.X));
		AddInt32(FMath::RoundToInt(Input.Y));
		AddInt32(FMath::RoundToInt(Input.Z));
	}

	void AddUnitVectorQ(const FVector& Input)
	{
		const FVector Unit = Input.GetSafeNormal();
		AddInt32(FMath::RoundToInt(Unit.X * Quantization));
		AddInt32(FMath::RoundToInt(Unit.Y * Quantization));
		AddInt32(FMath::RoundToInt(Unit.Z * Quantization));
	}

	void AddFloatQ(const float Input)
	{
		AddInt32(FMath::RoundToInt(Input * Quantization));
	}
};

struct FTrajectoryAnalysis
{
	bool bValid = false;
	bool bGeometricTargetContact = false;
	bool bTargetHit = false;
	bool bForbiddenHit = false;
	float TargetMissCM = TNumericLimits<float>::Max();
	float MinimumClearanceCM = TNumericLimits<float>::Max();
	FVector ImpactWorldCM = FVector::ZeroVector;
};

bool IsFiniteWitnessVector(const FVector& Vector)
{
	return FMath::IsFinite(Vector.X)
		&& FMath::IsFinite(Vector.Y)
		&& FMath::IsFinite(Vector.Z);
}

bool IsValidBird(const EABTSBirdId Bird)
{
	return static_cast<uint8>(Bird)
		<= static_cast<uint8>(EABTSBirdId::Black);
}

bool GetV1EligibleBirds(
	const EABTSSlingshotTier Tier,
	TArray<EABTSBirdId>& OutBirds)
{
	OutBirds.Reset();
	switch (Tier)
	{
	case EABTSSlingshotTier::Simple:
		OutBirds = {
			EABTSBirdId::Red,
			EABTSBirdId::Blue,
			EABTSBirdId::Yellow
		};
		return true;
	case EABTSSlingshotTier::Reinforced:
		OutBirds = {
			EABTSBirdId::Red,
			EABTSBirdId::Blue,
			EABTSBirdId::Yellow,
			EABTSBirdId::Black
		};
		return true;
	default:
		return false;
	}
}

uint64 ComputeV1BirdCatalogHashValue()
{
	FHash64 Hash;
	Hash.AddInt32(1);
	for (const EABTSSlingshotTier Tier : {
		EABTSSlingshotTier::Simple,
		EABTSSlingshotTier::Reinforced })
	{
		TArray<EABTSBirdId> Birds;
		verify(GetV1EligibleBirds(Tier, Birds));
		Hash.AddByte(static_cast<uint8>(Tier));
		Hash.AddInt32(Birds.Num());
		for (const EABTSBirdId Bird : Birds)
		{
			Hash.AddByte(static_cast<uint8>(Bird));
		}
	}
	return Hash.Value;
}

bool IsValidTier(const EABTSSlingshotTier Tier)
{
	return static_cast<uint8>(Tier)
		<= static_cast<uint8>(EABTSSlingshotTier::Space);
}

bool IsValidAuthority(const EABTSM3WitnessAuthority Authority)
{
	return Authority == EABTSM3WitnessAuthority::Fixture
		|| Authority == EABTSM3WitnessAuthority::Integration;
}

bool IsValidProgressKey(const EABTSM3ProgressKey Key)
{
	return static_cast<uint8>(Key)
		<= static_cast<uint8>(EABTSM3ProgressKey::HaveCrystalCore);
}

void HashProgressKeys(
	FHash64& Hash,
	const TArray<EABTSM3ProgressKey>& Keys)
{
	Hash.AddInt32(Keys.Num());
	for (const EABTSM3ProgressKey Key : Keys)
	{
		Hash.AddByte(static_cast<uint8>(Key));
	}
}

void HashItemAmounts(
	FHash64& Hash,
	const TArray<FABTSM3WitnessItemAmount>& Items)
{
	Hash.AddInt32(Items.Num());
	for (const FABTSM3WitnessItemAmount& Item : Items)
	{
		Hash.AddByte(static_cast<uint8>(Item.ItemId));
		Hash.AddInt32(Item.Quantity);
	}
}

void HashLaunchInput(
	FHash64& Hash,
	const FABTSM3WitnessLaunchInput& Input)
{
	Hash.AddInt32(Input.EncounterId);
	Hash.AddInt32(Input.EncounterOrder);
	Hash.AddInt32(Input.SlotACellId);
	Hash.AddInt32(Input.SlotBCellId);
	Hash.AddByte(static_cast<uint8>(Input.Tier));
	Hash.AddByte(static_cast<uint8>(Input.Bird));
	Hash.AddInt32(Input.LaunchSideSign);
	Hash.AddInt32(Input.PullAlphaQ);
	Hash.AddInt32(Input.AimRightQ);
	Hash.AddInt32(Input.AimUpQ);
	Hash.AddBool(Input.bEnableSatelliteGravity);
}

void HashTrajectorySamples(
	FHash64& Hash,
	const TArray<FABTSM3WitnessTrajectorySample>& Samples)
{
	Hash.AddInt32(Samples.Num());
	for (const FABTSM3WitnessTrajectorySample& Sample : Samples)
	{
		Hash.AddFloatQ(Sample.TimeSeconds);
		Hash.AddVectorCM(Sample.PositionWorldCM);
		Hash.AddVectorCM(Sample.VelocityWorldCMPerSec);
	}
}

void HashForbiddenSphere(
	FHash64& Hash,
	const FABTSM3WitnessForbiddenSphere& Sphere)
{
	Hash.AddName(Sphere.VolumeId);
	Hash.AddVectorCM(Sphere.CenterWorldCM);
	Hash.AddFloatQ(Sphere.RadiusCM);
}

uint64 ComputeFaceHash(const FABTSM3WitnessAttackFace& Face)
{
	FHash64 Hash;
	Hash.AddName(Face.FaceId);
	Hash.AddVectorCM(Face.LocalCenterCM);
	Hash.AddUnitVectorQ(Face.LocalNormal);
	Hash.AddFloatQ(Face.RadiusCM);
	Hash.AddFloatQ(Face.MinimumImpactSpeedCMPerSec);
	Hash.AddBool(Face.bRequiresBird);
	Hash.AddByte(static_cast<uint8>(Face.RequiredBird));
	return Hash.Value;
}

uint64 ComputeDescriptorHash(
	const FABTSM3WitnessProfileDescriptor& Descriptor)
{
	FHash64 Hash;
	Hash.AddName(Descriptor.ProfileId);
	Hash.AddVectorCM(Descriptor.BoundsExtentCM);
	Hash.AddInt32(Descriptor.AttackFaces.Num());
	for (const FABTSM3WitnessAttackFace& Face :
		Descriptor.AttackFaces)
	{
		Hash.AddInt64(Face.FaceHash);
	}
	return Hash.Value;
}

uint64 ComputeProfileCatalogHashValue(
	const FABTSM3WitnessProfileCatalog& Catalog)
{
	FHash64 Hash;
	Hash.AddInt32(1);
	Hash.AddInt64(Catalog.SpatialSourceCatalogHash);
	Hash.AddInt32(Catalog.Descriptors.Num());
	for (const FABTSM3WitnessProfileDescriptor& Descriptor :
		Catalog.Descriptors)
	{
		Hash.AddInt64(Descriptor.DescriptorHash);
	}
	return Hash.Value;
}

uint64 ComputeGeometryHash(
	const FABTSM3ResolvedWitnessGeometry& Geometry)
{
	FHash64 Hash;
	Hash.AddInt32(Geometry.EncounterId);
	Hash.AddInt32(Geometry.EncounterOrder);
	Hash.AddInt32(Geometry.Slots.Num());
	for (const FABTSM3WitnessSlotGeometry& Slot : Geometry.Slots)
	{
		Hash.AddInt32(Slot.CellId);
		Hash.AddVectorCM(Slot.CordSocketWorldCM);
	}
	Hash.AddVectorCM(Geometry.TargetWorldTransform.GetLocation());
	Hash.AddUnitVectorQ(
		Geometry.TargetWorldTransform.GetRotation().GetForwardVector());
	Hash.AddUnitVectorQ(
		Geometry.TargetWorldTransform.GetRotation().GetRightVector());
	Hash.AddFloatQ(Geometry.TargetWorldTransform.GetScale3D().X);
	Hash.AddFloatQ(Geometry.TargetWorldTransform.GetScale3D().Y);
	Hash.AddFloatQ(Geometry.TargetWorldTransform.GetScale3D().Z);
	Hash.AddInt32(Geometry.ForbiddenSpheres.Num());
	for (const FABTSM3WitnessForbiddenSphere& Sphere :
		Geometry.ForbiddenSpheres)
	{
		HashForbiddenSphere(Hash, Sphere);
	}
	Hash.AddName(Geometry.M9SatelliteForbiddenVolumeId);
	return Hash.Value;
}

uint64 ComputeBridgeHash(
	const FABTSM3BridgeGateEvidence& Evidence)
{
	FHash64 Hash;
	Hash.AddInt32(Evidence.SourceRouteCandidateId);
	Hash.AddName(Evidence.BarrierId);
	Hash.AddInt32(Evidence.GateCellId);
	Hash.AddInt32(Evidence.PreBridgeCellId);
	Hash.AddInt32(Evidence.PostBridgeCellId);
	Hash.AddBool(Evidence.bBlockedBeforeBridge);
	Hash.AddBool(Evidence.bReachableAfterBridge);
	Hash.AddBool(Evidence.bNoBypassBeforeBridge);
	return Hash.Value;
}

uint64 ComputeProgressionHash(
	const FABTSM3WitnessProgressionSnapshot& Snapshot)
{
	FHash64 Hash;
	Hash.AddInt32(1);
	Hash.AddBool(Snapshot.bWorkbenchStationAvailable);
	Hash.AddBool(Snapshot.bFurnaceStationAvailable);
	HashItemAmounts(Hash, Snapshot.InitialItems);
	Hash.AddInt32(Snapshot.Recipes.Num());
	for (const FABTSM3WitnessRecipe& Recipe : Snapshot.Recipes)
	{
		Hash.AddName(Recipe.RecipeId);
		Hash.AddByte(static_cast<uint8>(Recipe.RequiredStation));
		HashItemAmounts(Hash, Recipe.Inputs);
		HashItemAmounts(Hash, Recipe.Outputs);
		HashProgressKeys(Hash, Recipe.RequiredKeys);
		HashProgressKeys(Hash, Recipe.GrantedKeys);
	}
	Hash.AddInt32(Snapshot.EncounterRewards.Num());
	for (const FABTSM3WitnessEncounterReward& Reward :
		Snapshot.EncounterRewards)
	{
		Hash.AddInt32(Reward.EncounterId);
		Hash.AddInt32(Reward.EncounterOrder);
		HashItemAmounts(Hash, Reward.Items);
		HashProgressKeys(Hash, Reward.RequiredKeys);
		HashProgressKeys(Hash, Reward.GrantedKeys);
	}
	return Hash.Value;
}

uint64 ComputeM9AblationEvidenceHashValue(
	const FABTSM3M9AblationEvidence& Evidence,
	const FABTSM3WitnessServiceIdentity& Identity)
{
	FHash64 Hash;
	HashLaunchInput(Hash, Evidence.LaunchInput);
	HashTrajectorySamples(Hash, Evidence.Samples);
	Hash.AddByte(static_cast<uint8>(Evidence.Termination));
	Hash.AddVectorCM(Evidence.LandingWorldCM);
	Hash.AddInt32(Evidence.TargetMissCM);
	Hash.AddInt32(Evidence.M9QueryCount);
	Hash.AddInt32(Evidence.M9NonZeroAccelerationCount);
	Hash.AddFloatQ(Evidence.PeakM9AccelerationCMPerSecSq);
	Hash.AddInt64(Evidence.SolverHashEcho);
	Hash.AddInt64(Evidence.GravitySnapshotHashEcho);
	Hash.AddInt64(Identity.SolverHash);
	Hash.AddInt64(Identity.GravitySnapshotHash);
	return Hash.Value;
}

float PointSegmentDistanceSquared(
	const FVector& Point,
	const FVector& SegmentStart,
	const FVector& SegmentEnd,
	float& OutSegmentAlpha)
{
	const FVector Delta = SegmentEnd - SegmentStart;
	const float LengthSquared = Delta.SizeSquared();
	if (LengthSquared <= UE_SMALL_NUMBER)
	{
		OutSegmentAlpha = 0.0f;
		return FVector::DistSquared(Point, SegmentStart);
	}
	OutSegmentAlpha = FMath::Clamp(
		FVector::DotProduct(Point - SegmentStart, Delta)
			/ LengthSquared,
		0.0f,
		1.0f);
	return FVector::DistSquared(
		Point,
		SegmentStart + Delta * OutSegmentAlpha);
}

bool FindFirstSegmentSphereIntersection(
	const FVector& SegmentStart,
	const FVector& SegmentEnd,
	const FVector& SphereCenter,
	const float SphereRadius,
	float& OutSegmentAlpha)
{
	OutSegmentAlpha = 0.0f;
	const FVector StartOffset = SegmentStart - SphereCenter;
	const float RadiusSquared = SphereRadius * SphereRadius;
	if (StartOffset.SizeSquared() <= RadiusSquared)
	{
		return true;
	}
	const FVector Delta = SegmentEnd - SegmentStart;
	const float A = Delta.SizeSquared();
	if (A <= UE_SMALL_NUMBER)
	{
		return false;
	}
	const float B = FVector::DotProduct(StartOffset, Delta);
	const float C = StartOffset.SizeSquared() - RadiusSquared;
	const float Discriminant = B * B - A * C;
	if (Discriminant < 0.0f)
	{
		return false;
	}
	const float EntryAlpha =
		(-B - FMath::Sqrt(Discriminant)) / A;
	if (EntryAlpha < 0.0f || EntryAlpha > 1.0f)
	{
		return false;
	}
	OutSegmentAlpha = EntryAlpha;
	return true;
}

FTrajectoryAnalysis AnalyzeTrajectory(
	const FABTSM3WitnessTrajectoryRequest& Request,
	const FABTSM3WitnessTrajectoryResult& Result,
	const FABTSM3WitnessAttackFace& Face,
	const FABTSM3ResolvedWitnessGeometry& Geometry,
	const int32 MinimumForbiddenClearanceCM)
{
	FTrajectoryAnalysis Analysis;
	if (Result.Samples.Num() < 2
		|| (Result.Termination
				!= EABTSM3TrajectoryTermination::TargetHit
			&& Result.Termination
				!= EABTSM3TrajectoryTermination::WorldHit
			&& Result.Termination
				!= EABTSM3TrajectoryTermination::TimeLimit)
		|| Result.SolverHashEcho != Request.SolverHash
		|| Result.GravitySnapshotHashEcho
			!= Request.GravitySnapshotHash
		|| !IsFiniteWitnessVector(Result.LandingWorldCM)
		|| Result.M9QueryCount < 0
		|| Result.M9NonZeroAccelerationCount < 0
		|| Result.M9NonZeroAccelerationCount
			> Result.M9QueryCount
		|| !FMath::IsFinite(
			Result.PeakM9AccelerationCMPerSecSq)
		|| Result.PeakM9AccelerationCMPerSecSq < 0.0f
		|| !Result.LandingWorldCM.Equals(
			Result.Samples.Last().PositionWorldCM,
			0.01))
	{
		return Analysis;
	}

	float PreviousTime = -1.0f;
	for (int32 SampleIndex = 0;
		SampleIndex < Result.Samples.Num();
		++SampleIndex)
	{
		const FABTSM3WitnessTrajectorySample& Sample =
			Result.Samples[SampleIndex];
		if (!FMath::IsFinite(Sample.TimeSeconds)
			|| (SampleIndex == 0 && Sample.TimeSeconds < 0.0f)
			|| Sample.TimeSeconds <= PreviousTime
			|| !IsFiniteWitnessVector(Sample.PositionWorldCM)
			|| !IsFiniteWitnessVector(
				Sample.VelocityWorldCMPerSec))
		{
			return Analysis;
		}
		PreviousTime = Sample.TimeSeconds;
	}

	Analysis.bValid = true;
	Analysis.MinimumClearanceCM =
		TNumericLimits<float>::Max();
	for (int32 Index = 1; Index < Result.Samples.Num(); ++Index)
	{
		const FABTSM3WitnessTrajectorySample& A =
			Result.Samples[Index - 1];
		const FABTSM3WitnessTrajectorySample& B =
			Result.Samples[Index];
		float TargetAlpha = 0.0f;
		const float TargetDistanceSquared =
			PointSegmentDistanceSquared(
				Request.TargetCenterWorldCM,
				A.PositionWorldCM,
				B.PositionWorldCM,
				TargetAlpha);
		const float TargetDistance =
			FMath::Sqrt(TargetDistanceSquared);
		Analysis.TargetMissCM = FMath::Min(
			Analysis.TargetMissCM,
			FMath::Max(
				0.0f,
				TargetDistance - Request.TargetRadiusCM));
		float EntryAlpha = 0.0f;
		if (!Analysis.bGeometricTargetContact
			&& FindFirstSegmentSphereIntersection(
				A.PositionWorldCM,
				B.PositionWorldCM,
				Request.TargetCenterWorldCM,
				Request.TargetRadiusCM,
				EntryAlpha))
		{
			Analysis.bGeometricTargetContact = true;
			Analysis.ImpactWorldCM = FMath::Lerp(
				A.PositionWorldCM,
				B.PositionWorldCM,
				EntryAlpha);
			if (FMath::Lerp(
					A.VelocityWorldCMPerSec,
					B.VelocityWorldCMPerSec,
					EntryAlpha).Size()
				>= Face.MinimumImpactSpeedCMPerSec)
			{
				Analysis.bTargetHit = true;
			}
		}

		for (const FABTSM3WitnessForbiddenSphere& Sphere :
			Geometry.ForbiddenSpheres)
		{
			float VolumeAlpha = 0.0f;
			const float Distance = FMath::Sqrt(
				PointSegmentDistanceSquared(
					Sphere.CenterWorldCM,
					A.PositionWorldCM,
					B.PositionWorldCM,
					VolumeAlpha));
			const float Clearance = Distance - Sphere.RadiusCM;
			Analysis.MinimumClearanceCM = FMath::Min(
				Analysis.MinimumClearanceCM,
				Clearance);
			if (Clearance
				< static_cast<float>(
					MinimumForbiddenClearanceCM))
			{
				Analysis.bForbiddenHit = true;
			}
		}
	}
	if (Geometry.ForbiddenSpheres.IsEmpty())
	{
		Analysis.MinimumClearanceCM = 1000000.0f;
	}
	const bool bReportedTargetHit =
		Result.Termination
			== EABTSM3TrajectoryTermination::TargetHit;
	if (Analysis.bGeometricTargetContact
		!= bReportedTargetHit)
	{
		Analysis.bValid = false;
	}
	return Analysis;
}

uint64 ComputeWitnessHashValue(
	const FABTSM3BallisticWitness& Witness,
	const FABTSM3WitnessServiceIdentity& Identity)
{
	FHash64 Hash;
	HashLaunchInput(Hash, Witness.LaunchInput);
	Hash.AddName(Witness.ProfileId);
	Hash.AddName(Witness.AttackFaceId);
	Hash.AddInt64(Witness.ResolvedGeometryHash);
	Hash.AddInt64(Witness.ProfileDescriptorHash);
	Hash.AddInt64(Witness.AttackFaceHash);
	HashTrajectorySamples(Hash, Witness.Samples);
	Hash.AddByte(static_cast<uint8>(Witness.Termination));
	Hash.AddVectorCM(Witness.PredictedImpactWorldCM);
	Hash.AddInt32(Witness.MinimumClearanceCM);
	Hash.AddInt32(Witness.SlotDistanceCM);
	Hash.AddInt32(Witness.M9AblationMissCM);
	Hash.AddInt64(Witness.M9AblationEvidence.EvidenceHash);
	HashForbiddenSphere(
		Hash,
		Witness.M9SatelliteForbiddenSphere);
	Hash.AddInt32(Witness.SearchEvaluationCount);
	Hash.AddInt32(Witness.M9QueryCount);
	Hash.AddInt32(Witness.M9NonZeroAccelerationCount);
	Hash.AddFloatQ(Witness.PeakM9AccelerationCMPerSecSq);
	Hash.AddInt64(Identity.SolverHash);
	Hash.AddInt64(Identity.GravitySnapshotHash);
	return Hash.Value;
}

uint64 ComputeCertificateHashValue(
	const FABTSM3PriorTierInfeasibilityCertificate& Certificate,
	const FABTSM3WitnessServiceIdentity& Identity)
{
	FHash64 Hash;
	Hash.AddByte(static_cast<uint8>(Certificate.State));
	Hash.AddByte(static_cast<uint8>(Certificate.Tier));
	Hash.AddInt32(Certificate.PlannedInputCount);
	Hash.AddInt32(Certificate.CompletedInputCount);
	Hash.AddInt32(Certificate.ClosestMissCM);
	Hash.AddInt64(Certificate.EligibleBirdCatalogHash);
	Hash.AddInt64(Certificate.InputDomainHash);
	Hash.AddInt64(Certificate.ResolvedGeometryHash);
	Hash.AddInt64(Certificate.ProfileDescriptorHash);
	Hash.AddInt64(Certificate.AttackFaceHash);
	Hash.AddInt64(Certificate.SolverHash);
	Hash.AddInt64(Certificate.GravitySnapshotHash);
	Hash.AddInt64(Identity.SolverHash);
	Hash.AddInt64(Identity.GravitySnapshotHash);
	Hash.AddInt64(Identity.BirdCatalogHash);
	return Hash.Value;
}

bool ValidateWitnessShape(
	const FABTSM3BallisticWitness& Witness)
{
	if (Witness.LaunchInput.EncounterId < 0
		|| Witness.LaunchInput.EncounterOrder < 0
		|| Witness.LaunchInput.EncounterOrder >= 6
		|| Witness.LaunchInput.SlotACellId < 0
		|| Witness.LaunchInput.SlotBCellId < 0
		|| Witness.LaunchInput.SlotACellId
			>= Witness.LaunchInput.SlotBCellId
		|| !IsValidTier(Witness.LaunchInput.Tier)
		|| !IsValidBird(Witness.LaunchInput.Bird)
		|| FMath::Abs(Witness.LaunchInput.LaunchSideSign) != 1
		|| Witness.LaunchInput.PullAlphaQ < 0
		|| Witness.LaunchInput.PullAlphaQ > Quantization
		|| Witness.Samples.Num() < 2
		|| Witness.SlotDistanceCM <= 0
		|| !IsFiniteWitnessVector(
			Witness.PredictedImpactWorldCM)
		|| !FMath::IsFinite(
			Witness.PeakM9AccelerationCMPerSecSq)
		|| Witness.SearchEvaluationCount <= 0
		|| Witness.M9QueryCount < 0
		|| Witness.M9NonZeroAccelerationCount < 0
		|| Witness.M9NonZeroAccelerationCount
			> Witness.M9QueryCount)
	{
		return false;
	}
	float PreviousTime = -1.0f;
	for (int32 SampleIndex = 0;
		SampleIndex < Witness.Samples.Num();
		++SampleIndex)
	{
		const FABTSM3WitnessTrajectorySample& Sample =
			Witness.Samples[SampleIndex];
		if (!FMath::IsFinite(Sample.TimeSeconds)
			|| (SampleIndex == 0 && Sample.TimeSeconds < 0.0f)
			|| Sample.TimeSeconds <= PreviousTime
			|| !IsFiniteWitnessVector(Sample.PositionWorldCM)
			|| !IsFiniteWitnessVector(
				Sample.VelocityWorldCMPerSec))
		{
			return false;
		}
		PreviousTime = Sample.TimeSeconds;
	}
	return true;
}

bool LaunchInputsMatchExceptM9(
	const FABTSM3WitnessLaunchInput& A,
	const FABTSM3WitnessLaunchInput& B)
{
	return A.EncounterId == B.EncounterId
		&& A.EncounterOrder == B.EncounterOrder
		&& A.SlotACellId == B.SlotACellId
		&& A.SlotBCellId == B.SlotBCellId
		&& A.Tier == B.Tier
		&& A.Bird == B.Bird
		&& A.LaunchSideSign == B.LaunchSideSign
		&& A.PullAlphaQ == B.PullAlphaQ
		&& A.AimRightQ == B.AimRightQ
		&& A.AimUpQ == B.AimUpQ;
}

bool ValidateM9AblationEvidence(
	const FABTSM3BallisticWitness& Witness,
	const FABTSM3WitnessServiceIdentity& Identity,
	const int32 MinimumMissCM)
{
	const FABTSM3M9AblationEvidence& Evidence =
		Witness.M9AblationEvidence;
	if (Evidence.EvidenceHash == 0
		|| Evidence.EvidenceHash
			!= static_cast<int64>(
				ComputeM9AblationEvidenceHashValue(
					Evidence,
					Identity))
		|| !LaunchInputsMatchExceptM9(
			Witness.LaunchInput,
			Evidence.LaunchInput)
		|| !Witness.LaunchInput.bEnableSatelliteGravity
		|| Evidence.LaunchInput.bEnableSatelliteGravity
		|| Evidence.Samples.Num() < 2
		|| (Evidence.Termination
				!= EABTSM3TrajectoryTermination::WorldHit
			&& Evidence.Termination
				!= EABTSM3TrajectoryTermination::TimeLimit)
		|| Evidence.TargetMissCM < MinimumMissCM
		|| Evidence.TargetMissCM
			!= Witness.M9AblationMissCM
		|| Evidence.M9QueryCount != 0
		|| Evidence.M9NonZeroAccelerationCount != 0
		|| !FMath::IsFinite(
			Evidence.PeakM9AccelerationCMPerSecSq)
		|| Evidence.PeakM9AccelerationCMPerSecSq != 0.0f
		|| Evidence.SolverHashEcho != Identity.SolverHash
		|| Evidence.GravitySnapshotHashEcho
			!= Identity.GravitySnapshotHash
		|| !IsFiniteWitnessVector(Evidence.LandingWorldCM)
		|| !Evidence.LandingWorldCM.Equals(
			Evidence.Samples.Last().PositionWorldCM,
			0.01))
	{
		return false;
	}
	float PreviousTime = -1.0f;
	for (int32 SampleIndex = 0;
		SampleIndex < Evidence.Samples.Num();
		++SampleIndex)
	{
		const FABTSM3WitnessTrajectorySample& Sample =
			Evidence.Samples[SampleIndex];
		if (!FMath::IsFinite(Sample.TimeSeconds)
			|| (SampleIndex == 0 && Sample.TimeSeconds < 0.0f)
			|| Sample.TimeSeconds <= PreviousTime
			|| !IsFiniteWitnessVector(Sample.PositionWorldCM)
			|| !IsFiniteWitnessVector(
				Sample.VelocityWorldCMPerSec))
		{
			return false;
		}
		PreviousTime = Sample.TimeSeconds;
	}
	return true;
}

bool IsDefaultM9AblationEvidence(
	const FABTSM3M9AblationEvidence& Evidence)
{
	return Evidence.LaunchInput.EncounterId == INDEX_NONE
		&& Evidence.LaunchInput.EncounterOrder == INDEX_NONE
		&& Evidence.LaunchInput.SlotACellId == INDEX_NONE
		&& Evidence.LaunchInput.SlotBCellId == INDEX_NONE
		&& Evidence.LaunchInput.Tier
			== EABTSSlingshotTier::Simple
		&& Evidence.LaunchInput.Bird == EABTSBirdId::Red
		&& Evidence.LaunchInput.LaunchSideSign == 1
		&& Evidence.LaunchInput.PullAlphaQ == 0
		&& Evidence.LaunchInput.AimRightQ == 0
		&& Evidence.LaunchInput.AimUpQ == 0
		&& Evidence.LaunchInput.bEnableSatelliteGravity
		&& Evidence.Samples.IsEmpty()
		&& Evidence.Termination
			== EABTSM3TrajectoryTermination::None
		&& Evidence.LandingWorldCM.IsZero()
		&& Evidence.TargetMissCM == 0
		&& Evidence.M9QueryCount == 0
		&& Evidence.M9NonZeroAccelerationCount == 0
		&& Evidence.PeakM9AccelerationCMPerSecSq == 0.0f
		&& Evidence.SolverHashEcho == 0
		&& Evidence.GravitySnapshotHashEcho == 0
		&& Evidence.EvidenceHash == 0;
}

bool ValidateM9ForbiddenSphereClearance(
	const FABTSM3BallisticWitness& Witness,
	const int32 MinimumClearanceCM)
{
	const FABTSM3WitnessForbiddenSphere& Sphere =
		Witness.M9SatelliteForbiddenSphere;
	if (Sphere.VolumeId.IsNone()
		|| !IsFiniteWitnessVector(Sphere.CenterWorldCM)
		|| !FMath::IsFinite(Sphere.RadiusCM)
		|| Sphere.RadiusCM <= 0.0f)
	{
		return false;
	}
	float BestClearance = TNumericLimits<float>::Max();
	for (int32 Index = 1; Index < Witness.Samples.Num(); ++Index)
	{
		float Alpha = 0.0f;
		const float Distance = FMath::Sqrt(
			PointSegmentDistanceSquared(
				Sphere.CenterWorldCM,
				Witness.Samples[Index - 1].PositionWorldCM,
				Witness.Samples[Index].PositionWorldCM,
				Alpha));
		BestClearance = FMath::Min(
			BestClearance,
			Distance - Sphere.RadiusCM);
	}
	return BestClearance
		>= static_cast<float>(MinimumClearanceCM);
}

bool IsDefaultForbiddenSphere(
	const FABTSM3WitnessForbiddenSphere& Sphere)
{
	return Sphere.VolumeId.IsNone()
		&& Sphere.CenterWorldCM.IsZero()
		&& Sphere.RadiusCM == 0.0f;
}

bool IsAttackEffectSatisfied(
	const FABTSM3WitnessAttackFace& Face,
	const EABTSBirdId Bird)
{
	return !Face.bRequiresBird || Face.RequiredBird == Bird;
}

void BuildAimSamples(
	const int32 AxisSampleCount,
	TArray<FIntPoint>& OutSamples)
{
	OutSamples.Reset();
	OutSamples.Add(FIntPoint::ZeroValue);
	const int32 Half = AxisSampleCount / 2;
	for (int32 Y = -Half; Y <= Half; ++Y)
	{
		for (int32 X = -Half; X <= Half; ++X)
		{
			if ((X == 0 && Y == 0)
				|| X * X + Y * Y > Half * Half)
			{
				continue;
			}
			OutSamples.Add(FIntPoint(
				FMath::RoundToInt(
					static_cast<float>(X) * Quantization
						/ static_cast<float>(Half)),
				FMath::RoundToInt(
					static_cast<float>(Y) * Quantization
						/ static_cast<float>(Half))));
		}
	}
}

void BuildPullSamples(
	const int32 SampleCount,
	TArray<int32>& OutSamples)
{
	OutSamples.Reset();
	for (int32 Index = 0; Index < SampleCount; ++Index)
	{
		OutSamples.Add(FMath::RoundToInt(
			static_cast<float>(SampleCount - 1 - Index)
				* Quantization
				/ static_cast<float>(SampleCount - 1)));
	}
}

struct FReachablePair
{
	const FABTSM3WitnessSlotGeometry* A = nullptr;
	const FABTSM3WitnessSlotGeometry* B = nullptr;
};

bool BuildReachablePairs(
	const FABTSM3MonthlySlingshotField& Field,
	const FABTSM3ResolvedWitnessGeometry& Geometry,
	const int32 MaxCordLengthCM,
	TArray<FReachablePair>& OutPairs,
	FString& OutFailure)
{
	OutPairs.Reset();
	if (Geometry.EncounterId != Field.EncounterId
		|| Geometry.EncounterOrder < 0
		|| Geometry.EncounterOrder >= 6
		|| Geometry.Slots.Num() != Field.SlotCellIds.Num()
		|| Geometry.GeometryHash == 0
		|| static_cast<uint64>(Geometry.GeometryHash)
			!= ComputeGeometryHash(Geometry)
		|| Geometry.TargetWorldTransform.ContainsNaN()
		|| !Geometry.TargetWorldTransform.GetScale3D().Equals(
			FVector::OneVector,
			UE_KINDA_SMALL_NUMBER))
	{
		OutFailure = TEXT("GeometryIdentity");
		return false;
	}

	TArray<const FABTSM3WitnessSlotGeometry*> OrderedSlots;
	TSet<int32> GeometryCells;
	for (const FABTSM3WitnessSlotGeometry& Slot : Geometry.Slots)
	{
		if (Slot.CellId < 0
			|| !IsFiniteWitnessVector(Slot.CordSocketWorldCM)
			|| GeometryCells.Contains(Slot.CellId)
			|| !Field.SlotCellIds.Contains(Slot.CellId))
		{
			OutFailure = TEXT("GeometrySlot");
			return false;
		}
		GeometryCells.Add(Slot.CellId);
		OrderedSlots.Add(&Slot);
	}
	for (const int32 CellId : Field.SlotCellIds)
	{
		if (!GeometryCells.Contains(CellId))
		{
			OutFailure = TEXT("GeometryCoverage");
			return false;
		}
	}
	TSet<FName> ForbiddenIds;
	int32 M9SatelliteVolumeMatches = 0;
	for (const FABTSM3WitnessForbiddenSphere& Sphere :
		Geometry.ForbiddenSpheres)
	{
		if (Sphere.VolumeId.IsNone()
			|| ForbiddenIds.Contains(Sphere.VolumeId)
			|| !IsFiniteWitnessVector(Sphere.CenterWorldCM)
			|| !FMath::IsFinite(Sphere.RadiusCM)
			|| Sphere.RadiusCM <= 0.0f)
		{
			OutFailure = TEXT("ForbiddenSphere");
			return false;
		}
		ForbiddenIds.Add(Sphere.VolumeId);
		M9SatelliteVolumeMatches +=
			Sphere.VolumeId
				== Geometry.M9SatelliteForbiddenVolumeId
			? 1
			: 0;
	}
	if ((!Geometry.M9SatelliteForbiddenVolumeId.IsNone()
			&& M9SatelliteVolumeMatches != 1)
		|| (Geometry.M9SatelliteForbiddenVolumeId.IsNone()
			&& M9SatelliteVolumeMatches != 0))
	{
		OutFailure = TEXT("M9ForbiddenVolumeIdentity");
		return false;
	}
	OrderedSlots.Sort([](
		const FABTSM3WitnessSlotGeometry& A,
		const FABTSM3WitnessSlotGeometry& B)
	{
		return A.CellId < B.CellId;
	});
	for (int32 AIndex = 0; AIndex < OrderedSlots.Num(); ++AIndex)
	{
		for (int32 BIndex = AIndex + 1;
			BIndex < OrderedSlots.Num();
			++BIndex)
		{
			if (FVector::Distance(
					OrderedSlots[AIndex]->CordSocketWorldCM,
					OrderedSlots[BIndex]->CordSocketWorldCM)
				<= static_cast<float>(MaxCordLengthCM))
			{
				OutPairs.Add({
					OrderedSlots[AIndex],
					OrderedSlots[BIndex]
				});
			}
		}
	}
	if (OutPairs.IsEmpty())
	{
		OutFailure = TEXT("NoReachablePair");
		return false;
	}
	return true;
}

const FABTSM3WitnessProfileDescriptor* FindProfile(
	const FABTSM3WitnessProfileCatalog& Catalog,
	const FName ProfileId)
{
	const FABTSM3WitnessProfileDescriptor* Found = nullptr;
	for (const FABTSM3WitnessProfileDescriptor& Descriptor :
		Catalog.Descriptors)
	{
		if (Descriptor.ProfileId == ProfileId)
		{
			if (Found != nullptr)
			{
				return nullptr;
			}
			Found = &Descriptor;
		}
	}
	return Found;
}

const FABTSM3WitnessAttackFace* SelectAttackFace(
	const FABTSM3MonthlySpatialEncounter& Encounter,
	const FABTSM3WitnessProfileDescriptor& Descriptor,
	const FTransform& TargetTransform)
{
	const FABTSM3WitnessAttackFace* Best = nullptr;
	float BestDot = -2.0f;
	for (const FABTSM3WitnessAttackFace& Face :
		Descriptor.AttackFaces)
	{
		const FVector WorldNormal = TargetTransform.TransformVectorNoScale(
			Face.LocalNormal).GetSafeNormal();
		const float Dot = FVector::DotProduct(
			WorldNormal,
			Encounter.AttackFaceDirection.GetSafeNormal());
		if (Best == nullptr
			|| Dot > BestDot + UE_KINDA_SMALL_NUMBER
			|| (FMath::IsNearlyEqual(Dot, BestDot)
				&& Face.FaceId.LexicalLess(Best->FaceId)))
		{
			Best = &Face;
			BestDot = Dot;
		}
	}
	return Best;
}

bool ValidateProfileCatalog(
	const FABTSM3WitnessServiceIdentity& Identity,
	const FABTSM3WitnessProfileCatalog& Catalog,
	const FABTSM3MonthlySpatialResult& SpatialResult,
	FString& OutFailure)
{
	if (Catalog.FullCatalogHash == 0
		|| Catalog.FullCatalogHash != Identity.ProfileCatalogHash
		|| static_cast<uint64>(Catalog.FullCatalogHash)
			!= ComputeProfileCatalogHashValue(Catalog)
		|| Catalog.SpatialSourceCatalogHash
			!= SpatialResult.ProfileCatalogHash
		|| Catalog.Descriptors.IsEmpty())
	{
		OutFailure = TEXT("ProfileCatalogIdentity");
		return false;
	}
	TSet<FName> ProfileIds;
	for (const FABTSM3WitnessProfileDescriptor& Descriptor :
		Catalog.Descriptors)
	{
		if (Descriptor.ProfileId.IsNone()
			|| ProfileIds.Contains(Descriptor.ProfileId)
			|| !IsFiniteWitnessVector(Descriptor.BoundsExtentCM)
			|| Descriptor.BoundsExtentCM.GetMin() <= 0.0
			|| Descriptor.AttackFaces.IsEmpty()
			|| Descriptor.DescriptorHash == 0
			|| static_cast<uint64>(Descriptor.DescriptorHash)
				!= ComputeDescriptorHash(Descriptor))
		{
			OutFailure = TEXT("ProfileDescriptor");
			return false;
		}
		ProfileIds.Add(Descriptor.ProfileId);
		TSet<FName> FaceIds;
		for (const FABTSM3WitnessAttackFace& Face :
			Descriptor.AttackFaces)
		{
			if (Face.FaceId.IsNone()
				|| FaceIds.Contains(Face.FaceId)
				|| !IsFiniteWitnessVector(Face.LocalCenterCM)
				|| !IsFiniteWitnessVector(Face.LocalNormal)
				|| Face.LocalNormal.IsNearlyZero()
				|| !FMath::IsFinite(Face.RadiusCM)
				|| Face.RadiusCM <= 0.0f
				|| !FMath::IsFinite(
					Face.MinimumImpactSpeedCMPerSec)
				|| Face.MinimumImpactSpeedCMPerSec <= 0.0f
				|| !IsValidBird(Face.RequiredBird)
				|| Face.FaceHash == 0
				|| static_cast<uint64>(Face.FaceHash)
					!= ComputeFaceHash(Face))
			{
				OutFailure = TEXT("AttackFace");
				return false;
			}
			FaceIds.Add(Face.FaceId);
		}
	}
	return true;
}

bool GetStableEligibleBirds(
	const IABTSM3MonthlyWitnessServices& Services,
	const EABTSSlingshotTier Tier,
	TArray<EABTSBirdId>& OutBirds,
	FString& OutFailure)
{
	OutBirds.Reset();
	TArray<EABTSBirdId> ExpectedBirds;
	if (!GetV1EligibleBirds(Tier, ExpectedBirds))
	{
		OutFailure = TEXT("EligibleBirdTier");
		return false;
	}
	if (!Services.GetEligibleBirds(Tier, OutBirds, OutFailure)
		|| OutBirds.IsEmpty())
	{
		return false;
	}
	OutBirds.Sort([](
		const EABTSBirdId A,
		const EABTSBirdId B)
	{
		return static_cast<uint8>(A) < static_cast<uint8>(B);
	});
	for (int32 Index = 0; Index < OutBirds.Num(); ++Index)
	{
		if (!IsValidBird(OutBirds[Index])
			|| (Index > 0
				&& OutBirds[Index] == OutBirds[Index - 1]))
		{
			OutFailure = TEXT("EligibleBirdCatalog");
			return false;
		}
	}
	if (OutBirds != ExpectedBirds)
	{
		OutFailure = TEXT("EligibleBirdCatalog");
		return false;
	}
	return true;
}

FABTSM3WitnessTrajectoryRequest MakeRequest(
	const int32 SourceCandidateId,
	const FABTSM3WitnessLaunchInput& Input,
	const FReachablePair& Pair,
	const FVector TargetCenter,
	const float TargetRadiusCM,
	const FABTSM3WitnessServiceIdentity& Identity)
{
	FABTSM3WitnessTrajectoryRequest Request;
	Request.SourceRouteCandidateId = SourceCandidateId;
	Request.LaunchInput = Input;
	Request.SlotAWorldCM = Pair.A->CordSocketWorldCM;
	Request.SlotBWorldCM = Pair.B->CordSocketWorldCM;
	Request.TargetCenterWorldCM = TargetCenter;
	Request.TargetRadiusCM = TargetRadiusCM;
	Request.SolverHash = Identity.SolverHash;
	Request.GravitySnapshotHash = Identity.GravitySnapshotHash;
	return Request;
}

bool SearchPriorTierDomain(
	const FABTSM3MonthlyWitnessConfig& Config,
	const IABTSM3MonthlyWitnessServices& Services,
	const FABTSM3WitnessServiceIdentity& Identity,
	const int32 SourceCandidateId,
	const int32 EncounterId,
	const int32 EncounterOrder,
	const EABTSSlingshotTier Tier,
	const FABTSM3WitnessAttackFace& Face,
	const FABTSM3ResolvedWitnessGeometry& Geometry,
	const TArray<FReachablePair>& Pairs,
	const TArray<int32>& PullSamples,
	const TArray<FIntPoint>& AimSamples,
	int32& InOutEvaluationCount,
	FABTSM3PriorTierInfeasibilityCertificate& OutCertificate,
	EABTSM3MonthlyWitnessRejectReason& OutReason,
	FString& OutFailure)
{
	OutCertificate = FABTSM3PriorTierInfeasibilityCertificate();
	OutCertificate.State =
		EABTSM3PriorTierCertificateState::Incomplete;
	OutCertificate.Tier = Tier;

	TArray<EABTSBirdId> Birds;
	if (!GetStableEligibleBirds(
		Services,
		Tier,
		Birds,
		OutFailure))
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::
				ProviderIdentityMismatch;
		return false;
	}
	OutCertificate.PlannedInputCount =
		Birds.Num() * Pairs.Num() * 2
		* PullSamples.Num() * AimSamples.Num();
	OutCertificate.EligibleBirdCatalogHash =
		static_cast<int64>(ComputeV1BirdCatalogHashValue());
	FHash64 DomainHash;
	DomainHash.AddInt32(OutCertificate.PlannedInputCount);
	float ClosestMissCM = TNumericLimits<float>::Max();

	for (const EABTSBirdId Bird : Birds)
	{
		for (const FReachablePair& Pair : Pairs)
		{
			for (const int32 Side : { -1, 1 })
			{
				for (const int32 PullQ : PullSamples)
				{
					for (const FIntPoint AimQ : AimSamples)
					{
						FABTSM3WitnessLaunchInput Input;
						Input.EncounterId = EncounterId;
						Input.EncounterOrder = EncounterOrder;
						Input.SlotACellId = Pair.A->CellId;
						Input.SlotBCellId = Pair.B->CellId;
						Input.Tier = Tier;
						Input.Bird = Bird;
						Input.LaunchSideSign = Side;
						Input.PullAlphaQ = PullQ;
						Input.AimRightQ = AimQ.X;
						Input.AimUpQ = AimQ.Y;
						Input.bEnableSatelliteGravity = true;
						HashLaunchInput(DomainHash, Input);

						if (InOutEvaluationCount
							>= Config.
								MaxWitnessEvaluationsPerEncounter)
						{
							OutCertificate.InputDomainHash =
								static_cast<int64>(DomainHash.Value);
							OutReason =
								EABTSM3MonthlyWitnessRejectReason::
									SearchBudgetExceeded;
							OutFailure = TEXT("PriorTierBudget");
							return false;
						}

						const FVector TargetCenter =
							Geometry.TargetWorldTransform.
								TransformPosition(
									Face.LocalCenterCM);
						const FABTSM3WitnessTrajectoryRequest Request =
							MakeRequest(
								SourceCandidateId,
								Input,
								Pair,
								TargetCenter,
								Face.RadiusCM,
								Identity);
						FABTSM3WitnessTrajectoryResult Trajectory;
						if (!Services.EvaluateTrajectory(
							Request,
							Trajectory,
							OutFailure))
						{
							OutReason =
								EABTSM3MonthlyWitnessRejectReason::
									ProviderIdentityMismatch;
							return false;
						}
						++InOutEvaluationCount;
						++OutCertificate.CompletedInputCount;
						const FTrajectoryAnalysis Analysis =
							AnalyzeTrajectory(
								Request,
								Trajectory,
								Face,
								Geometry,
								Config.
									MinimumForbiddenClearanceCM);
						if (!Analysis.bValid)
						{
							OutReason =
								EABTSM3MonthlyWitnessRejectReason::
									ProviderIdentityMismatch;
							OutFailure =
								TEXT("PriorTrajectoryInvalid");
							return false;
						}
						ClosestMissCM = FMath::Min(
							ClosestMissCM,
							Analysis.TargetMissCM);
						if (Analysis.bTargetHit
							&& !Analysis.bForbiddenHit
							&& IsAttackEffectSatisfied(
								Face,
								Bird))
						{
							OutReason =
								EABTSM3MonthlyWitnessRejectReason::
									PriorDomainIncomplete;
							OutFailure =
								TEXT("PriorTierHasPositive");
							return false;
						}
					}
				}
			}
		}
	}
	if (OutCertificate.CompletedInputCount
		!= OutCertificate.PlannedInputCount)
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::
				PriorDomainIncomplete;
		OutFailure = TEXT("PriorTierCoverage");
		return false;
	}
	OutCertificate.State =
		EABTSM3PriorTierCertificateState::CompleteInfeasible;
	OutCertificate.ClosestMissCM = FMath::RoundToInt(
		FMath::IsFinite(ClosestMissCM) ? ClosestMissCM : 0.0f);
	OutCertificate.InputDomainHash =
		static_cast<int64>(DomainHash.Value);
	return true;
}

bool SearchPositiveWitness(
	const FABTSM3MonthlyWitnessConfig& Config,
	const IABTSM3MonthlyWitnessServices& Services,
	const FABTSM3WitnessServiceIdentity& Identity,
	const int32 SourceCandidateId,
	const int32 EncounterId,
	const int32 EncounterOrder,
	const EABTSSlingshotTier Tier,
	const bool bRequireM9Causality,
	const FABTSM3WitnessAttackFace& Face,
	const FABTSM3ResolvedWitnessGeometry& Geometry,
	const TArray<FReachablePair>& Pairs,
	const TArray<int32>& PullSamples,
	const TArray<FIntPoint>& AimSamples,
	int32& InOutEvaluationCount,
	FABTSM3BallisticWitness& OutWitness,
	EABTSM3MonthlyWitnessRejectReason& OutReason,
	FString& OutFailure)
{
	OutWitness = FABTSM3BallisticWitness();
	TArray<EABTSBirdId> Birds;
	if (!GetStableEligibleBirds(
		Services,
		Tier,
		Birds,
		OutFailure))
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::
				ProviderIdentityMismatch;
		return false;
	}
	if (Face.bRequiresBird)
	{
		const int32 RequiredIndex =
			Birds.IndexOfByKey(Face.RequiredBird);
		if (RequiredIndex != INDEX_NONE && RequiredIndex > 0)
		{
			const EABTSBirdId Required = Birds[RequiredIndex];
			Birds.RemoveAt(RequiredIndex);
			Birds.Insert(Required, 0);
		}
	}
	const FVector TargetCenter =
		Geometry.TargetWorldTransform.TransformPosition(
			Face.LocalCenterCM);
	bool bSawNonCausalM9Hit = false;

	for (const EABTSBirdId Bird : Birds)
	{
		if (!IsAttackEffectSatisfied(Face, Bird))
		{
			continue;
		}
		for (const FReachablePair& Pair : Pairs)
		{
			for (const int32 Side : { -1, 1 })
			{
				for (const int32 PullQ : PullSamples)
				{
					for (const FIntPoint AimQ : AimSamples)
					{
						if (InOutEvaluationCount
							>= Config.
								MaxWitnessEvaluationsPerEncounter)
						{
							OutReason =
								EABTSM3MonthlyWitnessRejectReason::
									SearchBudgetExceeded;
							OutFailure = TEXT("PositiveBudget");
							return false;
						}
						FABTSM3WitnessLaunchInput Input;
						Input.EncounterId = EncounterId;
						Input.EncounterOrder = EncounterOrder;
						Input.SlotACellId = Pair.A->CellId;
						Input.SlotBCellId = Pair.B->CellId;
						Input.Tier = Tier;
						Input.Bird = Bird;
						Input.LaunchSideSign = Side;
						Input.PullAlphaQ = PullQ;
						Input.AimRightQ = AimQ.X;
						Input.AimUpQ = AimQ.Y;
						Input.bEnableSatelliteGravity = true;

						const FABTSM3WitnessTrajectoryRequest Request =
							MakeRequest(
								SourceCandidateId,
								Input,
								Pair,
								TargetCenter,
								Face.RadiusCM,
								Identity);
						FABTSM3WitnessTrajectoryResult Trajectory;
						if (!Services.EvaluateTrajectory(
							Request,
							Trajectory,
							OutFailure))
						{
							OutReason =
								EABTSM3MonthlyWitnessRejectReason::
									ProviderIdentityMismatch;
							return false;
						}
						++InOutEvaluationCount;
						const FTrajectoryAnalysis Analysis =
							AnalyzeTrajectory(
								Request,
								Trajectory,
								Face,
								Geometry,
								Config.
									MinimumForbiddenClearanceCM);
						if (!Analysis.bValid)
						{
							OutReason =
								EABTSM3MonthlyWitnessRejectReason::
									ProviderIdentityMismatch;
							OutFailure =
								TEXT("PositiveTrajectoryInvalid");
							return false;
						}
						if (!Analysis.bTargetHit
							|| Analysis.bForbiddenHit
							|| !IsAttackEffectSatisfied(
								Face,
								Bird))
						{
							continue;
						}

						int32 AblationMissCM = 0;
						FABTSM3M9AblationEvidence
							AblationEvidence;
						if (bRequireM9Causality)
						{
							if (Trajectory.M9QueryCount <= 0
								|| Trajectory.
									M9NonZeroAccelerationCount <= 0
								|| Trajectory.
									PeakM9AccelerationCMPerSecSq <= 0.0f)
							{
								bSawNonCausalM9Hit = true;
								continue;
							}
							if (InOutEvaluationCount
								>= Config.
									MaxWitnessEvaluationsPerEncounter)
							{
								OutReason =
									EABTSM3MonthlyWitnessRejectReason::
										SearchBudgetExceeded;
								OutFailure = TEXT("M9AblationBudget");
								return false;
							}
							FABTSM3WitnessTrajectoryRequest
								AblationRequest = Request;
							AblationRequest.LaunchInput.
								bEnableSatelliteGravity = false;
							FABTSM3WitnessTrajectoryResult
								AblationTrajectory;
							if (!Services.EvaluateTrajectory(
								AblationRequest,
								AblationTrajectory,
								OutFailure))
							{
								OutReason =
									EABTSM3MonthlyWitnessRejectReason::
										ProviderIdentityMismatch;
								return false;
							}
							++InOutEvaluationCount;
							const FTrajectoryAnalysis
								AblationAnalysis =
									AnalyzeTrajectory(
										AblationRequest,
										AblationTrajectory,
										Face,
										Geometry,
										Config.
											MinimumForbiddenClearanceCM);
							if (!AblationAnalysis.bValid)
							{
								OutReason =
									EABTSM3MonthlyWitnessRejectReason::
										ProviderIdentityMismatch;
								OutFailure =
									TEXT("M9AblationTrajectoryInvalid");
								return false;
							}
							if (AblationTrajectory.M9QueryCount != 0
								|| AblationTrajectory.
									M9NonZeroAccelerationCount != 0
								|| AblationTrajectory.
									PeakM9AccelerationCMPerSecSq
										!= 0.0f)
							{
								OutReason =
									EABTSM3MonthlyWitnessRejectReason::
										ProviderIdentityMismatch;
								OutFailure =
									TEXT("M9DisableIgnored");
								return false;
							}
							AblationMissCM = FMath::RoundToInt(
								AblationAnalysis.TargetMissCM);
							if (AblationAnalysis.bTargetHit
								|| AblationMissCM
									< Config.
										MinimumM9AblationMissCM)
							{
								bSawNonCausalM9Hit = true;
								continue;
							}
							AblationEvidence.LaunchInput =
								AblationRequest.LaunchInput;
							AblationEvidence.Samples =
								AblationTrajectory.Samples;
							AblationEvidence.Termination =
								AblationTrajectory.Termination;
							AblationEvidence.LandingWorldCM =
								AblationTrajectory.LandingWorldCM;
							AblationEvidence.TargetMissCM =
								AblationMissCM;
							AblationEvidence.M9QueryCount =
								AblationTrajectory.M9QueryCount;
							AblationEvidence.
								M9NonZeroAccelerationCount =
									AblationTrajectory.
										M9NonZeroAccelerationCount;
							AblationEvidence.
								PeakM9AccelerationCMPerSecSq =
									AblationTrajectory.
										PeakM9AccelerationCMPerSecSq;
							AblationEvidence.SolverHashEcho =
								AblationTrajectory.SolverHashEcho;
							AblationEvidence.
								GravitySnapshotHashEcho =
									AblationTrajectory.
										GravitySnapshotHashEcho;
							AblationEvidence.EvidenceHash =
								static_cast<int64>(
									ComputeM9AblationEvidenceHashValue(
										AblationEvidence,
										Identity));
						}

						OutWitness.LaunchInput = Input;
						OutWitness.ProfileId = NAME_None;
						OutWitness.AttackFaceId = Face.FaceId;
						OutWitness.Samples = Trajectory.Samples;
						OutWitness.Termination =
							Trajectory.Termination;
						OutWitness.PredictedImpactWorldCM =
							Analysis.ImpactWorldCM;
						OutWitness.MinimumClearanceCM =
							FMath::RoundToInt(
								Analysis.MinimumClearanceCM);
						OutWitness.SlotDistanceCM =
							FMath::RoundToInt(
								FVector::Distance(
									Pair.A->CordSocketWorldCM,
									Pair.B->CordSocketWorldCM));
						OutWitness.M9AblationMissCM =
							AblationMissCM;
						OutWitness.M9AblationEvidence =
							MoveTemp(AblationEvidence);
						OutWitness.SearchEvaluationCount =
							InOutEvaluationCount;
						OutWitness.M9QueryCount =
							Trajectory.M9QueryCount;
						OutWitness.M9NonZeroAccelerationCount =
							Trajectory.
								M9NonZeroAccelerationCount;
						OutWitness.PeakM9AccelerationCMPerSecSq =
							Trajectory.
								PeakM9AccelerationCMPerSecSq;
						return true;
					}
				}
			}
		}
	}
	OutReason = bRequireM9Causality && bSawNonCausalM9Hit
		? EABTSM3MonthlyWitnessRejectReason::M9EvidenceMissing
		: EABTSM3MonthlyWitnessRejectReason::
			PositiveWitnessNotFound;
	OutFailure = bRequireM9Causality && bSawNonCausalM9Hit
		? TEXT("M9CausalWitness")
		: TEXT("PositiveWitness");
	return false;
}

uint64 ComputeEncounterGameplayHash(
	const FABTSM3MonthlyEncounterGameplay& Encounter)
{
	FHash64 Hash;
	Hash.AddInt32(Encounter.EncounterId);
	Hash.AddInt32(Encounter.EncounterOrder);
	Hash.AddName(Encounter.ResolvedProfileId);
	Hash.AddInt64(Encounter.ProfileDescriptorHash);
	Hash.AddName(Encounter.AttackFaceId);
	Hash.AddInt64(Encounter.PositiveWitness.WitnessHash);
	Hash.AddInt64(
		Encounter.PriorTierCertificate.CertificateHash);
	Hash.AddByte(static_cast<uint8>(
		Encounter.PriorTierCertificate.State));
	Hash.AddInt32(Encounter.TotalTrajectoryEvaluations);
	return Hash.Value;
}

void SortProgressKeys(TArray<EABTSM3ProgressKey>& Keys)
{
	Keys.Sort([](
		const EABTSM3ProgressKey A,
		const EABTSM3ProgressKey B)
	{
		return static_cast<uint8>(A) < static_cast<uint8>(B);
	});
}

void SortItemAmounts(TArray<FABTSM3WitnessItemAmount>& Items)
{
	Items.Sort([](
		const FABTSM3WitnessItemAmount& A,
		const FABTSM3WitnessItemAmount& B)
	{
		return static_cast<uint8>(A.ItemId)
			< static_cast<uint8>(B.ItemId);
	});
}

uint64 ComputeLedgerHash(
	const TMap<EABTSItemId, int32>& Ledger,
	const TSet<EABTSM3ProgressKey>& Keys)
{
	TArray<FABTSM3WitnessItemAmount> Items;
	for (const TPair<EABTSItemId, int32>& Pair : Ledger)
	{
		if (Pair.Value > 0)
		{
			FABTSM3WitnessItemAmount Item;
			Item.ItemId = Pair.Key;
			Item.Quantity = Pair.Value;
			Items.Add(Item);
		}
	}
	SortItemAmounts(Items);
	TArray<EABTSM3ProgressKey> OrderedKeys = Keys.Array();
	SortProgressKeys(OrderedKeys);
	FHash64 Hash;
	HashItemAmounts(Hash, Items);
	HashProgressKeys(Hash, OrderedKeys);
	return Hash.Value;
}

void DeriveInventoryKeys(
	const TMap<EABTSItemId, int32>& Ledger,
	TSet<EABTSM3ProgressKey>& Keys)
{
	const int32 Wood = Ledger.FindRef(EABTSItemId::Wood);
	if (Wood > 0)
	{
		Keys.Add(EABTSM3ProgressKey::HaveWood);
	}
	else
	{
		Keys.Remove(EABTSM3ProgressKey::HaveWood);
	}
	const int32 Core = Ledger.FindRef(EABTSItemId::CrystalCore);
	if (Core > 0)
	{
		Keys.Add(EABTSM3ProgressKey::HaveCrystalCore);
	}
	else
	{
		Keys.Remove(EABTSM3ProgressKey::HaveCrystalCore);
	}
}

bool HasKeys(
	const TSet<EABTSM3ProgressKey>& Keys,
	const TArray<EABTSM3ProgressKey>& Required)
{
	for (const EABTSM3ProgressKey Key : Required)
	{
		if (!Keys.Contains(Key))
		{
			return false;
		}
	}
	return true;
}

bool ValidateKeyList(
	const TArray<EABTSM3ProgressKey>& Keys)
{
	TSet<EABTSM3ProgressKey> Unique;
	for (const EABTSM3ProgressKey Key : Keys)
	{
		if (Key == EABTSM3ProgressKey::None
			|| !IsValidProgressKey(Key)
			|| Unique.Contains(Key))
		{
			return false;
		}
		Unique.Add(Key);
	}
	return true;
}

bool HasExactKeySet(
	const TArray<EABTSM3ProgressKey>& Actual,
	const std::initializer_list<EABTSM3ProgressKey> ExpectedValues)
{
	TArray<EABTSM3ProgressKey> Expected;
	for (const EABTSM3ProgressKey Key : ExpectedValues)
	{
		Expected.Add(Key);
	}
	TArray<EABTSM3ProgressKey> OrderedActual = Actual;
	SortProgressKeys(Expected);
	SortProgressKeys(OrderedActual);
	return OrderedActual == Expected;
}

int32 FindItemQuantity(
	const TArray<FABTSM3WitnessItemAmount>& Items,
	const EABTSItemId ItemId)
{
	const FABTSM3WitnessItemAmount* Found =
		Items.FindByPredicate(
			[ItemId](const FABTSM3WitnessItemAmount& Item)
			{
				return Item.ItemId == ItemId;
			});
	return Found != nullptr ? Found->Quantity : 0;
}

bool ValidateItemList(
	const TArray<FABTSM3WitnessItemAmount>& Items,
	const bool bAllowZero)
{
	TSet<uint8> Unique;
	for (const FABTSM3WitnessItemAmount& Item : Items)
	{
		const uint8 ItemValue = static_cast<uint8>(Item.ItemId);
		if (Unique.Contains(ItemValue)
			|| ItemValue > static_cast<uint8>(EABTSItemId::SpaceCord)
			|| Item.Quantity < 0
			|| (!bAllowZero && Item.Quantity == 0))
		{
			return false;
		}
		Unique.Add(ItemValue);
	}
	return true;
}

bool ValidateItemDeltaList(
	const TArray<FABTSM3WitnessItemAmount>& Deltas)
{
	TSet<uint8> Unique;
	for (const FABTSM3WitnessItemAmount& Delta : Deltas)
	{
		const uint8 ItemValue =
			static_cast<uint8>(Delta.ItemId);
		if (Unique.Contains(ItemValue)
			|| ItemValue
				> static_cast<uint8>(EABTSItemId::SpaceCord)
			|| Delta.Quantity == 0)
		{
			return false;
		}
		Unique.Add(ItemValue);
	}
	return true;
}

TArray<FABTSM3WitnessItemAmount> BuildRecipeItemDeltas(
	const FABTSM3WitnessRecipe& Recipe)
{
	TMap<EABTSItemId, int32> DeltaByItem;
	for (const FABTSM3WitnessItemAmount& Input : Recipe.Inputs)
	{
		DeltaByItem.FindOrAdd(Input.ItemId) -= Input.Quantity;
	}
	for (const FABTSM3WitnessItemAmount& Output : Recipe.Outputs)
	{
		DeltaByItem.FindOrAdd(Output.ItemId) += Output.Quantity;
	}
	TArray<FABTSM3WitnessItemAmount> Deltas;
	for (const TPair<EABTSItemId, int32>& Pair : DeltaByItem)
	{
		if (Pair.Value != 0)
		{
			FABTSM3WitnessItemAmount Delta;
			Delta.ItemId = Pair.Key;
			Delta.Quantity = Pair.Value;
			Deltas.Add(Delta);
		}
	}
	SortItemAmounts(Deltas);
	return Deltas;
}

bool HasExactItemSet(
	const TArray<FABTSM3WitnessItemAmount>& Actual,
	const std::initializer_list<
		TPair<EABTSItemId, int32>> Expected)
{
	if (Actual.Num() != static_cast<int32>(Expected.size()))
	{
		return false;
	}
	for (const TPair<EABTSItemId, int32>& Pair : Expected)
	{
		if (FindItemQuantity(Actual, Pair.Key) != Pair.Value)
		{
			return false;
		}
	}
	return true;
}

bool IsValidCraftingStation(
	const EABTSCraftingStationType Station)
{
	return static_cast<uint8>(Station)
		<= static_cast<uint8>(
			EABTSCraftingStationType::Furnace);
}

bool IsCraftingStationAvailable(
	const EABTSCraftingStationType Station,
	const bool bWorkbenchAvailable,
	const bool bFurnaceAvailable)
{
	switch (Station)
	{
	case EABTSCraftingStationType::None:
		return true;
	case EABTSCraftingStationType::Workbench:
		return bWorkbenchAvailable;
	case EABTSCraftingStationType::Furnace:
		return bFurnaceAvailable;
	default:
		return false;
	}
}

const FABTSM3WitnessRecipe* FindRecipe(
	const FABTSM3WitnessProgressionSnapshot& Snapshot,
	const FName RecipeId)
{
	const FABTSM3WitnessRecipe* Found = nullptr;
	for (const FABTSM3WitnessRecipe& Recipe : Snapshot.Recipes)
	{
		if (Recipe.RecipeId == RecipeId)
		{
			if (Found != nullptr)
			{
				return nullptr;
			}
			Found = &Recipe;
		}
	}
	return Found;
}

const FABTSM3WitnessEncounterReward* FindReward(
	const FABTSM3WitnessProgressionSnapshot& Snapshot,
	const int32 EncounterOrder)
{
	const FABTSM3WitnessEncounterReward* Found = nullptr;
	for (const FABTSM3WitnessEncounterReward& Reward :
		Snapshot.EncounterRewards)
	{
		if (Reward.EncounterOrder == EncounterOrder)
		{
			if (Found != nullptr)
			{
				return nullptr;
			}
			Found = &Reward;
		}
	}
	return Found;
}

void AddFlowStep(
	FABTSM3MonthlyFlowClosure& Flow,
	const EABTSM3FlowStepKind Kind,
	const FName StepId,
	const int32 EncounterId,
	const int32 EncounterOrder,
	const TArray<EABTSM3ProgressKey>& RequiredKeys,
	const TArray<EABTSM3ProgressKey>& GrantedKeys,
	const EABTSCraftingStationType RequiredStation,
	const TArray<FABTSM3WitnessItemAmount>& ItemDeltas,
	const TMap<EABTSItemId, int32>& Ledger,
	const TSet<EABTSM3ProgressKey>& Keys)
{
	FABTSM3WitnessFlowStep Step;
	Step.StepIndex = Flow.Steps.Num();
	Step.Kind = Kind;
	Step.StepId = StepId;
	Step.EncounterId = EncounterId;
	Step.EncounterOrder = EncounterOrder;
	Step.RequiredKeys = RequiredKeys;
	Step.GrantedKeys = GrantedKeys;
	Step.RequiredStation = RequiredStation;
	Step.ItemDeltas = ItemDeltas;
	SortProgressKeys(Step.RequiredKeys);
	SortProgressKeys(Step.GrantedKeys);
	SortItemAmounts(Step.ItemDeltas);
	Step.LedgerHashAfterStep =
		static_cast<int64>(ComputeLedgerHash(Ledger, Keys));
	Flow.Steps.Add(MoveTemp(Step));
}

bool ApplyRecipe(
	const FABTSM3WitnessRecipe& Recipe,
	const bool bWorkbenchStationAvailable,
	const bool bFurnaceStationAvailable,
	TMap<EABTSItemId, int32>& Ledger,
	TSet<EABTSM3ProgressKey>& Keys,
	FABTSM3MonthlyFlowClosure& Flow,
	FString& OutFailure)
{
	if (!HasKeys(Keys, Recipe.RequiredKeys))
	{
		OutFailure = FString::Printf(
			TEXT("RecipeKey:%s"),
			*Recipe.RecipeId.ToString());
		return false;
	}
	if (!IsCraftingStationAvailable(
		Recipe.RequiredStation,
		bWorkbenchStationAvailable,
		bFurnaceStationAvailable))
	{
		OutFailure = FString::Printf(
			TEXT("RecipeStation:%s"),
			*Recipe.RecipeId.ToString());
		return false;
	}
	for (const FABTSM3WitnessItemAmount& Input : Recipe.Inputs)
	{
		if (Ledger.FindRef(Input.ItemId) < Input.Quantity)
		{
			OutFailure = FString::Printf(
				TEXT("RecipeItem:%s"),
				*Recipe.RecipeId.ToString());
			return false;
		}
	}
	for (const FABTSM3WitnessItemAmount& Input : Recipe.Inputs)
	{
		Ledger.FindOrAdd(Input.ItemId) -= Input.Quantity;
	}
	for (const FABTSM3WitnessItemAmount& Output : Recipe.Outputs)
	{
		Ledger.FindOrAdd(Output.ItemId) += Output.Quantity;
	}
	for (const EABTSM3ProgressKey Key : Recipe.GrantedKeys)
	{
		Keys.Add(Key);
	}
	DeriveInventoryKeys(Ledger, Keys);
	AddFlowStep(
		Flow,
		EABTSM3FlowStepKind::Recipe,
		Recipe.RecipeId,
		INDEX_NONE,
		INDEX_NONE,
		Recipe.RequiredKeys,
		Recipe.GrantedKeys,
		Recipe.RequiredStation,
		BuildRecipeItemDeltas(Recipe),
		Ledger,
		Keys);
	return true;
}

bool ApplyReward(
	const FABTSM3WitnessEncounterReward& Reward,
	TMap<EABTSItemId, int32>& Ledger,
	TSet<EABTSM3ProgressKey>& Keys,
	FABTSM3MonthlyFlowClosure& Flow,
	FString& OutFailure)
{
	if (!HasKeys(Keys, Reward.RequiredKeys))
	{
		OutFailure = FString::Printf(
			TEXT("RewardKey:E%d"),
			Reward.EncounterOrder + 1);
		return false;
	}
	for (const FABTSM3WitnessItemAmount& Item : Reward.Items)
	{
		Ledger.FindOrAdd(Item.ItemId) += Item.Quantity;
	}
	for (const EABTSM3ProgressKey Key : Reward.GrantedKeys)
	{
		Keys.Add(Key);
	}
	DeriveInventoryKeys(Ledger, Keys);
	AddFlowStep(
		Flow,
		EABTSM3FlowStepKind::EncounterReward,
		FName(*FString::Printf(
			TEXT("Encounter%d"),
			Reward.EncounterOrder + 1)),
		Reward.EncounterId,
		Reward.EncounterOrder,
		Reward.RequiredKeys,
		Reward.GrantedKeys,
		EABTSCraftingStationType::None,
		Reward.Items,
		Ledger,
		Keys);
	return true;
}

uint64 ComputeFlowHash(const FABTSM3MonthlyFlowClosure& Flow)
{
	FHash64 Hash;
	Hash.AddBool(Flow.bWorkbenchStationAvailable);
	Hash.AddBool(Flow.bFurnaceStationAvailable);
	Hash.AddInt32(Flow.Steps.Num());
	for (const FABTSM3WitnessFlowStep& Step : Flow.Steps)
	{
		Hash.AddInt32(Step.StepIndex);
		Hash.AddByte(static_cast<uint8>(Step.Kind));
		Hash.AddName(Step.StepId);
		Hash.AddInt32(Step.EncounterId);
		Hash.AddInt32(Step.EncounterOrder);
		HashProgressKeys(Hash, Step.RequiredKeys);
		HashProgressKeys(Hash, Step.GrantedKeys);
		Hash.AddByte(static_cast<uint8>(Step.RequiredStation));
		HashItemAmounts(Hash, Step.ItemDeltas);
		Hash.AddInt64(Step.LedgerHashAfterStep);
	}
	HashItemAmounts(Hash, Flow.FinalItems);
	HashProgressKeys(Hash, Flow.FinalKeys);
	Hash.AddInt64(Flow.BridgeEvidence.EvidenceHash);
	Hash.AddBool(Flow.bBridgeBlockedBefore);
	Hash.AddBool(Flow.bBridgeReachableAfter);
	Hash.AddBool(Flow.bBridgeNoBypass);
	Hash.AddInt32(Flow.BranchCount);
	Hash.AddByte(static_cast<uint8>(Flow.BranchUtility));
	Hash.AddBool(Flow.bFlowValid);
	return Hash.Value;
}

bool ValidateFlowShape(
	const FABTSM3MonthlyFlowClosure& Flow,
	const TArray<FABTSM3MonthlyEncounterGameplay>& Encounters,
	const int32 SourceRouteCandidateId)
{
	static const EABTSM3FlowStepKind ExpectedKinds[] = {
		EABTSM3FlowStepKind::InitialState,
		EABTSM3FlowStepKind::Recipe,
		EABTSM3FlowStepKind::Recipe,
		EABTSM3FlowStepKind::EncounterReward,
		EABTSM3FlowStepKind::EncounterReward,
		EABTSM3FlowStepKind::EncounterReward,
		EABTSM3FlowStepKind::Recipe,
		EABTSM3FlowStepKind::BridgeGate,
		EABTSM3FlowStepKind::Recipe,
		EABTSM3FlowStepKind::EncounterReward,
		EABTSM3FlowStepKind::EncounterReward,
		EABTSM3FlowStepKind::EncounterReward,
		EABTSM3FlowStepKind::Recipe,
		EABTSM3FlowStepKind::Recipe,
		EABTSM3FlowStepKind::FinaleEntry
	};
	static const FName ExpectedIds[] = {
		FName(TEXT("Initial")),
		FName(TEXT("M3R4_Workbench")),
		FName(TEXT("M3R4_SimpleSlingshot")),
		FName(TEXT("Encounter1")),
		FName(TEXT("Encounter2")),
		FName(TEXT("Encounter3")),
		FName(TEXT("M3R4_Bridge")),
		NAME_None,
		FName(TEXT("M3R4_ReinforcedSlingshot")),
		FName(TEXT("Encounter4")),
		FName(TEXT("Encounter5")),
		FName(TEXT("Encounter6")),
		FName(TEXT("SpaceStakePair")),
		FName(TEXT("SpaceCord")),
		FName(TEXT("FinaleLaunch"))
	};
	static const EABTSCraftingStationType ExpectedStations[] = {
		EABTSCraftingStationType::None,
		EABTSCraftingStationType::None,
		EABTSCraftingStationType::None,
		EABTSCraftingStationType::None,
		EABTSCraftingStationType::None,
		EABTSCraftingStationType::None,
		EABTSCraftingStationType::None,
		EABTSCraftingStationType::None,
		EABTSCraftingStationType::None,
		EABTSCraftingStationType::None,
		EABTSCraftingStationType::None,
		EABTSCraftingStationType::None,
		EABTSCraftingStationType::Furnace,
		EABTSCraftingStationType::Furnace,
		EABTSCraftingStationType::None
	};
	if (!Flow.bFlowValid
		|| !Flow.bWorkbenchStationAvailable
		|| !Flow.bFurnaceStationAvailable
		|| Encounters.Num()
			!= FABTSM3MonthlyWitnessBuilder::
				RequiredEncounterCount
		|| Flow.BridgeEvidence.SourceRouteCandidateId
			!= SourceRouteCandidateId
		|| Flow.BridgeEvidence.BarrierId.IsNone()
		|| Flow.BridgeEvidence.GateCellId < 0
		|| Flow.BridgeEvidence.PreBridgeCellId < 0
		|| Flow.BridgeEvidence.PostBridgeCellId < 0
		|| Flow.BridgeEvidence.EvidenceHash == 0
		|| static_cast<uint64>(
				Flow.BridgeEvidence.EvidenceHash)
			!= ComputeBridgeHash(Flow.BridgeEvidence)
		|| !Flow.bBridgeBlockedBefore
		|| !Flow.bBridgeReachableAfter
		|| !Flow.bBridgeNoBypass
		|| Flow.bBridgeBlockedBefore
			!= Flow.BridgeEvidence.bBlockedBeforeBridge
		|| Flow.bBridgeReachableAfter
			!= Flow.BridgeEvidence.bReachableAfterBridge
		|| Flow.bBridgeNoBypass
			!= Flow.BridgeEvidence.bNoBypassBeforeBridge
		|| Flow.BranchCount != 0
		|| Flow.BranchUtility
			!= EABTSM3BranchUtilityState::NotRequired
		|| Flow.Steps.Num() != UE_ARRAY_COUNT(ExpectedKinds)
		|| FindItemQuantity(
			Flow.FinalItems,
			EABTSItemId::SpaceStake) != 2
		|| FindItemQuantity(
			Flow.FinalItems,
			EABTSItemId::SpaceCord) != 1
		|| FindItemQuantity(
			Flow.FinalItems,
			EABTSItemId::MetalParts) != 0
		|| FindItemQuantity(
			Flow.FinalItems,
			EABTSItemId::Wood) != 0
		|| FindItemQuantity(
			Flow.FinalItems,
			EABTSItemId::CrystalCore) != 0
		|| Flow.FinalKeys.Contains(
			EABTSM3ProgressKey::HaveCrystalCore)
		|| !Flow.FinalKeys.Contains(
			EABTSM3ProgressKey::SatelliteShotSolved)
		|| !Flow.FinalKeys.Contains(
			EABTSM3ProgressKey::ReinforcedSlingshotReady))
	{
		return false;
	}
	for (int32 Index = 0; Index < Flow.Steps.Num(); ++Index)
	{
		const FABTSM3WitnessFlowStep& Step =
			Flow.Steps[Index];
		const bool bReward = Step.Kind
			== EABTSM3FlowStepKind::EncounterReward;
		const int32 RewardOrder =
			Index >= 3 && Index <= 5
			? Index - 3
			: (Index >= 9 && Index <= 11
				? Index - 6
				: INDEX_NONE);
		if (Step.StepIndex != Index
			|| Step.Kind != ExpectedKinds[Index]
			|| (Index == 7
				? Step.StepId
					!= Flow.BridgeEvidence.BarrierId
				: Step.StepId != ExpectedIds[Index])
			|| Step.RequiredStation != ExpectedStations[Index]
			|| Step.LedgerHashAfterStep == 0
			|| (bReward
				&& (RewardOrder == INDEX_NONE
					|| Step.EncounterOrder != RewardOrder
					|| Step.EncounterId
						!= Encounters[RewardOrder].
							EncounterId))
			|| (!bReward
				&& (Step.EncounterId != INDEX_NONE
					|| Step.EncounterOrder != INDEX_NONE))
			|| !ValidateItemDeltaList(Step.ItemDeltas))
		{
			return false;
		}
		switch (Index)
		{
		case 0:
		case 1:
			if (!HasExactKeySet(Step.RequiredKeys, {})
				|| !HasExactKeySet(
					Step.GrantedKeys,
					Index == 1
						? std::initializer_list<
							EABTSM3ProgressKey>{
								EABTSM3ProgressKey::
									BuildWorkbench }
						: std::initializer_list<
							EABTSM3ProgressKey>{}))
			{
				return false;
			}
			break;
		case 2:
			if (!HasExactKeySet(
					Step.RequiredKeys,
					{ EABTSM3ProgressKey::BuildWorkbench })
				|| !HasExactKeySet(
					Step.GrantedKeys,
					{ EABTSM3ProgressKey::
						SimpleSlingshotReady }))
			{
				return false;
			}
			break;
		case 3:
			if (!HasExactKeySet(
					Step.RequiredKeys,
					{ EABTSM3ProgressKey::
						SimpleSlingshotReady })
				|| !HasExactKeySet(
					Step.GrantedKeys,
					{ EABTSM3ProgressKey::
						TargetDestroyed }))
			{
				return false;
			}
			break;
		case 4:
		case 5:
			if (!HasExactKeySet(
					Step.RequiredKeys,
					{ EABTSM3ProgressKey::TargetDestroyed })
				|| !HasExactKeySet(Step.GrantedKeys, {}))
			{
				return false;
			}
			break;
		case 6:
		case 7:
			if (!HasExactKeySet(
					Step.RequiredKeys,
					{ EABTSM3ProgressKey::TargetDestroyed,
						EABTSM3ProgressKey::HaveWood })
				|| !HasExactKeySet(
					Step.GrantedKeys,
					{ EABTSM3ProgressKey::BridgeBuilt }))
			{
				return false;
			}
			break;
		case 8:
			if (!HasExactKeySet(
					Step.RequiredKeys,
					{ EABTSM3ProgressKey::BridgeBuilt })
				|| !HasExactKeySet(
					Step.GrantedKeys,
					{ EABTSM3ProgressKey::
						ReinforcedSlingshotReady }))
			{
				return false;
			}
			break;
		case 9:
		case 10:
			if (!HasExactKeySet(
					Step.RequiredKeys,
					{ EABTSM3ProgressKey::
						ReinforcedSlingshotReady })
				|| !HasExactKeySet(
					Step.GrantedKeys,
					Index == 10
						? std::initializer_list<
							EABTSM3ProgressKey>{
								EABTSM3ProgressKey::
									SatelliteShotSolved }
						: std::initializer_list<
							EABTSM3ProgressKey>{}))
			{
				return false;
			}
			break;
		case 11:
			if (!HasExactKeySet(
					Step.RequiredKeys,
					{ EABTSM3ProgressKey::
						SatelliteShotSolved })
				|| !HasExactKeySet(Step.GrantedKeys, {}))
			{
				return false;
			}
			break;
		case 12:
			if (!HasExactKeySet(
					Step.RequiredKeys,
					{ EABTSM3ProgressKey::
							ReinforcedSlingshotReady,
						EABTSM3ProgressKey::
							SatelliteShotSolved })
				|| !HasExactKeySet(Step.GrantedKeys, {}))
			{
				return false;
			}
			break;
		case 13:
			if (!HasExactKeySet(
					Step.RequiredKeys,
					{ EABTSM3ProgressKey::
							ReinforcedSlingshotReady,
						EABTSM3ProgressKey::
							SatelliteShotSolved,
						EABTSM3ProgressKey::
							HaveCrystalCore })
				|| !HasExactKeySet(Step.GrantedKeys, {}))
			{
				return false;
			}
			break;
		case 14:
			if (!HasExactKeySet(
					Step.RequiredKeys,
					{ EABTSM3ProgressKey::
							ReinforcedSlingshotReady,
						EABTSM3ProgressKey::
							SatelliteShotSolved })
				|| !HasExactKeySet(Step.GrantedKeys, {}))
			{
				return false;
			}
			break;
		default:
			return false;
		}
		switch (Index)
		{
		case 4:
			if (!HasExactItemSet(
				Step.ItemDeltas,
				{ { EABTSItemId::Wood, 6 } }))
			{
				return false;
			}
			break;
		case 6:
			if (!HasExactItemSet(
				Step.ItemDeltas,
				{ { EABTSItemId::Wood, -1 } }))
			{
				return false;
			}
			break;
		case 10:
			if (!HasExactItemSet(
				Step.ItemDeltas,
				{ { EABTSItemId::CrystalCore, 1 } }))
			{
				return false;
			}
			break;
		case 11:
			if (!HasExactItemSet(
				Step.ItemDeltas,
				{ { EABTSItemId::MetalParts, 8 } }))
			{
				return false;
			}
			break;
		case 12:
			if (!HasExactItemSet(
				Step.ItemDeltas,
				{ { EABTSItemId::MetalParts, -6 },
					{ EABTSItemId::Wood, -5 },
					{ EABTSItemId::SpaceStake, 2 } }))
			{
				return false;
			}
			break;
		case 13:
			if (!HasExactItemSet(
				Step.ItemDeltas,
				{ { EABTSItemId::MetalParts, -2 },
					{ EABTSItemId::CrystalCore, -1 },
					{ EABTSItemId::SpaceCord, 1 } }))
			{
				return false;
			}
			break;
		default:
			if (!HasExactItemSet(Step.ItemDeltas, {}))
			{
				return false;
			}
			break;
		}
	}
	TMap<EABTSItemId, int32> ReplayLedger;
	TSet<EABTSM3ProgressKey> ReplayKeys;
	for (const FABTSM3WitnessFlowStep& Step : Flow.Steps)
	{
		if (!HasKeys(ReplayKeys, Step.RequiredKeys))
		{
			return false;
		}
		if (!IsCraftingStationAvailable(
			Step.RequiredStation,
			Flow.bWorkbenchStationAvailable,
			Flow.bFurnaceStationAvailable))
		{
			return false;
		}
		for (const FABTSM3WitnessItemAmount& Delta :
			Step.ItemDeltas)
		{
			int32& Quantity =
				ReplayLedger.FindOrAdd(Delta.ItemId);
			Quantity += Delta.Quantity;
			if (Quantity < 0)
			{
				return false;
			}
		}
		for (const EABTSM3ProgressKey Key :
			Step.GrantedKeys)
		{
			ReplayKeys.Add(Key);
		}
		DeriveInventoryKeys(ReplayLedger, ReplayKeys);
		if (Step.LedgerHashAfterStep
			!= static_cast<int64>(
				ComputeLedgerHash(
					ReplayLedger,
					ReplayKeys)))
		{
			return false;
		}
	}
	TArray<FABTSM3WitnessItemAmount> ReplayItems;
	for (const TPair<EABTSItemId, int32>& Pair :
		ReplayLedger)
	{
		if (Pair.Value > 0)
		{
			FABTSM3WitnessItemAmount Item;
			Item.ItemId = Pair.Key;
			Item.Quantity = Pair.Value;
			ReplayItems.Add(Item);
		}
	}
	SortItemAmounts(ReplayItems);
	if (ReplayItems.Num() != Flow.FinalItems.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < ReplayItems.Num(); ++Index)
	{
		if (ReplayItems[Index].ItemId
				!= Flow.FinalItems[Index].ItemId
			|| ReplayItems[Index].Quantity
				!= Flow.FinalItems[Index].Quantity)
		{
			return false;
		}
	}
	TArray<EABTSM3ProgressKey> ReplayFinalKeys =
		ReplayKeys.Array();
	SortProgressKeys(ReplayFinalKeys);
	if (ReplayFinalKeys != Flow.FinalKeys)
	{
		return false;
	}
	return true;
}

bool BuildFlowClosure(
	const FABTSM3WitnessProgressionSnapshot& Snapshot,
	const TArray<FABTSM3MonthlyEncounterGameplay>& Encounters,
	const int32 SourceRouteCandidateId,
	FABTSM3MonthlyFlowClosure& OutFlow,
	EABTSM3MonthlyWitnessRejectReason& OutReason,
	FString& OutFailure)
{
	OutFlow = FABTSM3MonthlyFlowClosure();
	if (Snapshot.CatalogHash == 0
		|| static_cast<uint64>(Snapshot.CatalogHash)
			!= ComputeProgressionHash(Snapshot)
		|| !Snapshot.bWorkbenchStationAvailable
		|| !Snapshot.bFurnaceStationAvailable
		|| !ValidateItemList(Snapshot.InitialItems, true)
		|| !Snapshot.InitialItems.IsEmpty()
		|| Snapshot.EncounterRewards.Num() != 6
		|| Encounters.Num() != 6)
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::
				ProgressionInvalid;
		OutFailure = TEXT("ProgressionCatalog");
		return false;
	}
	OutFlow.bWorkbenchStationAvailable =
		Snapshot.bWorkbenchStationAvailable;
	OutFlow.bFurnaceStationAvailable =
		Snapshot.bFurnaceStationAvailable;
	for (const FABTSM3WitnessRecipe& Recipe : Snapshot.Recipes)
	{
		if (Recipe.RecipeId.IsNone()
			|| !IsValidCraftingStation(
				Recipe.RequiredStation)
			|| !ValidateItemList(Recipe.Inputs, false)
			|| !ValidateItemList(Recipe.Outputs, false)
			|| !ValidateKeyList(Recipe.RequiredKeys)
			|| !ValidateKeyList(Recipe.GrantedKeys))
		{
			OutReason =
				EABTSM3MonthlyWitnessRejectReason::
					ProgressionInvalid;
			OutFailure = TEXT("RecipeCatalog");
			return false;
		}
	}
	for (int32 EncounterOrder = 0;
		EncounterOrder < 6;
		++EncounterOrder)
	{
		const FABTSM3WitnessEncounterReward* Reward =
			FindReward(Snapshot, EncounterOrder);
		if (Reward == nullptr
			|| Reward->EncounterOrder != EncounterOrder
			|| Reward->EncounterId
				!= Encounters[EncounterOrder].EncounterId
			|| !ValidateItemList(Reward->Items, true)
			|| !ValidateKeyList(Reward->RequiredKeys)
			|| !ValidateKeyList(Reward->GrantedKeys))
		{
			OutReason =
				EABTSM3MonthlyWitnessRejectReason::
					ProgressionInvalid;
			OutFailure = TEXT("RewardCatalog");
			return false;
		}
	}
	const FABTSM3BridgeGateEvidence& Bridge =
		Snapshot.BridgeEvidence;
	if (Bridge.SourceRouteCandidateId
			!= SourceRouteCandidateId
		|| Bridge.BarrierId.IsNone()
		|| Bridge.GateCellId < 0
		|| Bridge.PreBridgeCellId < 0
		|| Bridge.PostBridgeCellId < 0
		|| !Bridge.bBlockedBeforeBridge
		|| !Bridge.bReachableAfterBridge
		|| !Bridge.bNoBypassBeforeBridge
		|| Bridge.EvidenceHash == 0
		|| static_cast<uint64>(Bridge.EvidenceHash)
			!= ComputeBridgeHash(Bridge))
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::
				BridgeEvidenceInvalid;
		OutFailure = TEXT("BridgeEvidence");
		return false;
	}

	const FName WorkbenchId(TEXT("M3R4_Workbench"));
	const FName SimpleId(TEXT("M3R4_SimpleSlingshot"));
	const FName BridgeId(TEXT("M3R4_Bridge"));
	const FName ReinforcedId(TEXT("M3R4_ReinforcedSlingshot"));
	const FName SpaceStakePairId(TEXT("SpaceStakePair"));
	const FName SpaceCordId(TEXT("SpaceCord"));
	const FABTSM3WitnessRecipe* Workbench =
		FindRecipe(Snapshot, WorkbenchId);
	const FABTSM3WitnessRecipe* Simple =
		FindRecipe(Snapshot, SimpleId);
	const FABTSM3WitnessRecipe* BridgeRecipe =
		FindRecipe(Snapshot, BridgeId);
	const FABTSM3WitnessRecipe* Reinforced =
		FindRecipe(Snapshot, ReinforcedId);
	const FABTSM3WitnessRecipe* SpaceStakePair =
		FindRecipe(Snapshot, SpaceStakePairId);
	const FABTSM3WitnessRecipe* SpaceCord =
		FindRecipe(Snapshot, SpaceCordId);
	if (Workbench == nullptr
		|| Simple == nullptr
		|| BridgeRecipe == nullptr
		|| Reinforced == nullptr
		|| SpaceStakePair == nullptr
		|| SpaceCord == nullptr)
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::
				ProgressionInvalid;
		OutFailure = TEXT("RequiredRecipe");
		return false;
	}
	const FABTSM3WitnessEncounterReward* Rewards[6] = {};
	for (int32 EncounterOrder = 0;
		EncounterOrder < 6;
		++EncounterOrder)
	{
		Rewards[EncounterOrder] =
			FindReward(Snapshot, EncounterOrder);
	}
	if (!HasExactKeySet(Workbench->RequiredKeys, {})
		|| Workbench->RequiredStation
			!= EABTSCraftingStationType::None
		|| !HasExactItemSet(Workbench->Inputs, {})
		|| !HasExactItemSet(Workbench->Outputs, {})
		|| !HasExactKeySet(
			Workbench->GrantedKeys,
			{ EABTSM3ProgressKey::BuildWorkbench })
		|| Simple->RequiredStation
			!= EABTSCraftingStationType::None
		|| !HasExactItemSet(Simple->Inputs, {})
		|| !HasExactItemSet(Simple->Outputs, {})
		|| !HasExactKeySet(
			Simple->RequiredKeys,
			{ EABTSM3ProgressKey::BuildWorkbench })
		|| !HasExactKeySet(
			Simple->GrantedKeys,
			{ EABTSM3ProgressKey::SimpleSlingshotReady })
		|| !HasExactKeySet(
			BridgeRecipe->RequiredKeys,
			{ EABTSM3ProgressKey::TargetDestroyed,
				EABTSM3ProgressKey::HaveWood })
		|| BridgeRecipe->RequiredStation
			!= EABTSCraftingStationType::None
		|| !HasExactItemSet(
			BridgeRecipe->Inputs,
			{ { EABTSItemId::Wood, 1 } })
		|| !HasExactItemSet(BridgeRecipe->Outputs, {})
		|| !HasExactKeySet(
			BridgeRecipe->GrantedKeys,
			{ EABTSM3ProgressKey::BridgeBuilt })
		|| Reinforced->RequiredStation
			!= EABTSCraftingStationType::None
		|| !HasExactItemSet(Reinforced->Inputs, {})
		|| !HasExactItemSet(Reinforced->Outputs, {})
		|| !HasExactKeySet(
			Reinforced->RequiredKeys,
			{ EABTSM3ProgressKey::BridgeBuilt })
		|| !HasExactKeySet(
			Reinforced->GrantedKeys,
			{ EABTSM3ProgressKey::ReinforcedSlingshotReady })
		|| !HasExactKeySet(
			SpaceStakePair->RequiredKeys,
			{ EABTSM3ProgressKey::ReinforcedSlingshotReady,
				EABTSM3ProgressKey::SatelliteShotSolved })
		|| SpaceStakePair->RequiredStation
			!= EABTSCraftingStationType::Furnace
		|| !HasExactItemSet(
			SpaceStakePair->Inputs,
			{ { EABTSItemId::MetalParts, 6 },
				{ EABTSItemId::Wood, 5 } })
		|| !HasExactItemSet(
			SpaceStakePair->Outputs,
			{ { EABTSItemId::SpaceStake, 2 } })
		|| !HasExactKeySet(
			SpaceStakePair->GrantedKeys,
			{})
		|| !HasExactKeySet(
			SpaceCord->RequiredKeys,
			{ EABTSM3ProgressKey::ReinforcedSlingshotReady,
				EABTSM3ProgressKey::SatelliteShotSolved,
				EABTSM3ProgressKey::HaveCrystalCore })
		|| SpaceCord->RequiredStation
			!= EABTSCraftingStationType::Furnace
		|| !HasExactItemSet(
			SpaceCord->Inputs,
			{ { EABTSItemId::MetalParts, 2 },
				{ EABTSItemId::CrystalCore, 1 } })
		|| !HasExactItemSet(
			SpaceCord->Outputs,
			{ { EABTSItemId::SpaceCord, 1 } })
		|| !HasExactKeySet(SpaceCord->GrantedKeys, {})
		|| !HasExactKeySet(
			Rewards[0]->RequiredKeys,
			{ EABTSM3ProgressKey::SimpleSlingshotReady })
		|| !HasExactKeySet(
			Rewards[0]->GrantedKeys,
			{ EABTSM3ProgressKey::TargetDestroyed })
		|| !HasExactItemSet(Rewards[0]->Items, {})
		|| !HasExactKeySet(
			Rewards[1]->RequiredKeys,
			{ EABTSM3ProgressKey::TargetDestroyed })
		|| !HasExactItemSet(
			Rewards[1]->Items,
			{ { EABTSItemId::Wood, 6 } })
		|| !Rewards[1]->GrantedKeys.IsEmpty()
		|| !HasExactKeySet(
			Rewards[2]->RequiredKeys,
			{ EABTSM3ProgressKey::TargetDestroyed })
		|| !HasExactItemSet(Rewards[2]->Items, {})
		|| !Rewards[2]->GrantedKeys.IsEmpty()
		|| !HasExactKeySet(
			Rewards[3]->RequiredKeys,
			{ EABTSM3ProgressKey::ReinforcedSlingshotReady })
		|| !HasExactItemSet(Rewards[3]->Items, {})
		|| !Rewards[3]->GrantedKeys.IsEmpty()
		|| !HasExactKeySet(
			Rewards[4]->RequiredKeys,
			{ EABTSM3ProgressKey::ReinforcedSlingshotReady })
		|| !HasExactKeySet(
			Rewards[4]->GrantedKeys,
			{ EABTSM3ProgressKey::SatelliteShotSolved })
		|| !HasExactItemSet(
			Rewards[4]->Items,
			{ { EABTSItemId::CrystalCore, 1 } })
		|| !HasExactKeySet(
			Rewards[5]->RequiredKeys,
			{ EABTSM3ProgressKey::SatelliteShotSolved })
		|| !HasExactItemSet(
			Rewards[5]->Items,
			{ { EABTSItemId::MetalParts, 8 } })
		|| !Rewards[5]->GrantedKeys.IsEmpty())
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::
				ProgressionInvalid;
		OutFailure = TEXT("ProgressionSchedule");
		return false;
	}

	TMap<EABTSItemId, int32> Ledger;
	for (const FABTSM3WitnessItemAmount& Item : Snapshot.InitialItems)
	{
		Ledger.Add(Item.ItemId, Item.Quantity);
	}
	TSet<EABTSM3ProgressKey> Keys;
	DeriveInventoryKeys(Ledger, Keys);
	AddFlowStep(
		OutFlow,
		EABTSM3FlowStepKind::InitialState,
		FName(TEXT("Initial")),
		INDEX_NONE,
		INDEX_NONE,
		{},
		{},
		EABTSCraftingStationType::None,
		Snapshot.InitialItems,
		Ledger,
		Keys);

	if (!ApplyRecipe(
			*Workbench,
			Snapshot.bWorkbenchStationAvailable,
			Snapshot.bFurnaceStationAvailable,
			Ledger,
			Keys,
			OutFlow,
			OutFailure)
		|| !Keys.Contains(EABTSM3ProgressKey::BuildWorkbench)
		|| !ApplyRecipe(
			*Simple,
			Snapshot.bWorkbenchStationAvailable,
			Snapshot.bFurnaceStationAvailable,
			Ledger,
			Keys,
			OutFlow,
			OutFailure)
		|| !Keys.Contains(
			EABTSM3ProgressKey::SimpleSlingshotReady))
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::
				ProgressionInvalid;
		return false;
	}
	for (int32 EncounterId = 0; EncounterId <= 2; ++EncounterId)
	{
		if (!ApplyReward(
			*FindReward(Snapshot, EncounterId),
			Ledger,
			Keys,
			OutFlow,
			OutFailure))
		{
			OutReason =
				EABTSM3MonthlyWitnessRejectReason::
					ProgressionInvalid;
			return false;
		}
	}
	if (!Keys.Contains(EABTSM3ProgressKey::TargetDestroyed)
		|| !Keys.Contains(EABTSM3ProgressKey::HaveWood)
		|| !ApplyRecipe(
			*BridgeRecipe,
			Snapshot.bWorkbenchStationAvailable,
			Snapshot.bFurnaceStationAvailable,
			Ledger,
			Keys,
			OutFlow,
			OutFailure)
		|| !Keys.Contains(EABTSM3ProgressKey::BridgeBuilt))
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::
				ProgressionInvalid;
		return false;
	}
	OutFlow.bBridgeBlockedBefore = Bridge.bBlockedBeforeBridge;
	OutFlow.bBridgeReachableAfter = Bridge.bReachableAfterBridge;
	OutFlow.bBridgeNoBypass = Bridge.bNoBypassBeforeBridge;
	AddFlowStep(
		OutFlow,
		EABTSM3FlowStepKind::BridgeGate,
		Bridge.BarrierId,
		INDEX_NONE,
		INDEX_NONE,
		{ EABTSM3ProgressKey::TargetDestroyed,
			EABTSM3ProgressKey::HaveWood },
		{ EABTSM3ProgressKey::BridgeBuilt },
		EABTSCraftingStationType::None,
		{},
		Ledger,
		Keys);
	if (!ApplyRecipe(
		*Reinforced,
		Snapshot.bWorkbenchStationAvailable,
		Snapshot.bFurnaceStationAvailable,
		Ledger,
		Keys,
		OutFlow,
		OutFailure)
		|| !Keys.Contains(
			EABTSM3ProgressKey::ReinforcedSlingshotReady))
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::
				ProgressionInvalid;
		return false;
	}
	for (int32 EncounterId = 3; EncounterId <= 5; ++EncounterId)
	{
		if (!ApplyReward(
			*FindReward(Snapshot, EncounterId),
			Ledger,
			Keys,
			OutFlow,
			OutFailure))
		{
			OutReason =
				EABTSM3MonthlyWitnessRejectReason::
					ProgressionInvalid;
			return false;
		}
	}
	if (!Keys.Contains(
			EABTSM3ProgressKey::SatelliteShotSolved)
		|| !Keys.Contains(EABTSM3ProgressKey::HaveCrystalCore)
		|| !ApplyRecipe(
			*SpaceStakePair,
			Snapshot.bWorkbenchStationAvailable,
			Snapshot.bFurnaceStationAvailable,
			Ledger,
			Keys,
			OutFlow,
			OutFailure)
		|| !ApplyRecipe(
			*SpaceCord,
			Snapshot.bWorkbenchStationAvailable,
			Snapshot.bFurnaceStationAvailable,
			Ledger,
			Keys,
			OutFlow,
			OutFailure)
		|| Ledger.FindRef(EABTSItemId::SpaceStake) != 2
		|| Ledger.FindRef(EABTSItemId::SpaceCord) != 1
		|| Ledger.FindRef(EABTSItemId::MetalParts) != 0
		|| Ledger.FindRef(EABTSItemId::Wood) != 0
		|| Ledger.FindRef(EABTSItemId::CrystalCore) != 0)
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::
				ProgressionInvalid;
		OutFailure = TEXT("FinaleResources");
		return false;
	}
	AddFlowStep(
		OutFlow,
		EABTSM3FlowStepKind::FinaleEntry,
		FName(TEXT("FinaleLaunch")),
		INDEX_NONE,
		INDEX_NONE,
		{ EABTSM3ProgressKey::SatelliteShotSolved,
			EABTSM3ProgressKey::ReinforcedSlingshotReady },
		{},
		EABTSCraftingStationType::None,
		{},
		Ledger,
		Keys);

	for (const TPair<EABTSItemId, int32>& Pair : Ledger)
	{
		if (Pair.Value < 0)
		{
			OutReason =
				EABTSM3MonthlyWitnessRejectReason::
					ProgressionInvalid;
			OutFailure = TEXT("NegativeLedger");
			return false;
		}
		if (Pair.Value > 0)
		{
			FABTSM3WitnessItemAmount Item;
			Item.ItemId = Pair.Key;
			Item.Quantity = Pair.Value;
			OutFlow.FinalItems.Add(Item);
		}
	}
	SortItemAmounts(OutFlow.FinalItems);
	OutFlow.FinalKeys = Keys.Array();
	SortProgressKeys(OutFlow.FinalKeys);
	OutFlow.BridgeEvidence = Bridge;
	OutFlow.BranchCount = 0;
	OutFlow.BranchUtility =
		EABTSM3BranchUtilityState::NotRequired;
	OutFlow.bFlowValid = true;
	OutFlow.FlowHash =
		static_cast<int64>(ComputeFlowHash(OutFlow));
	if (!ValidateFlowShape(
			OutFlow,
			Encounters,
			SourceRouteCandidateId))
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::
				ProgressionInvalid;
		OutFailure = TEXT("FlowReplay");
		return false;
	}
	return true;
}

bool BuildCandidate(
	const FABTSM3MonthlyWitnessConfig& Config,
	const IABTSM3MonthlyWitnessServices& Services,
	const FABTSM3WitnessServiceIdentity& Identity,
	const FABTSM3WitnessProfileCatalog& Catalog,
	const FABTSM3MonthlySpatialCandidate& SpatialCandidate,
	const FABTSM3MonthlySlingshotFieldCandidate& FieldCandidate,
	const int32 MaxCordLengthCM,
	FABTSM3MonthlyGameplayCandidate& OutCandidate,
	EABTSM3MonthlyWitnessRejectReason& OutReason,
	FString& OutFailure)
{
	OutCandidate = FABTSM3MonthlyGameplayCandidate();
	OutCandidate.SourceRouteCandidateId =
		SpatialCandidate.SourceRouteCandidateId;
	OutCandidate.SourceSpatialCandidateHash =
		SpatialCandidate.SpatialCandidateHash;
	OutCandidate.SourceSlingshotFieldCandidateHash =
		FieldCandidate.CandidateHash;
	OutCandidate.SpatialScore = SpatialCandidate.SpatialScore;
	OutCandidate.RouteScore =
		SpatialCandidate.RecomputedRoute.RouteScore;

	if (SpatialCandidate.Encounters.Num()
			!= FABTSM3MonthlyWitnessBuilder::
				RequiredEncounterCount)
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::InvalidSource;
		OutFailure = TEXT("EncounterCount");
		return false;
	}
	TArray<int32> PullSamples;
	TArray<FIntPoint> AimSamples;
	BuildPullSamples(Config.PullAlphaSampleCount, PullSamples);
	BuildAimSamples(Config.AimAxisSampleCount, AimSamples);
	for (int32 EncounterOrder = 0;
		EncounterOrder < SpatialCandidate.Encounters.Num();
		++EncounterOrder)
	{
		const FABTSM3MonthlySpatialEncounter& Encounter =
			SpatialCandidate.Encounters[EncounterOrder];
		if (Encounter.Contract.OrderIndex != EncounterOrder
			|| Encounter.Contract.EncounterId < 0)
		{
			OutReason =
				EABTSM3MonthlyWitnessRejectReason::InvalidSource;
			OutFailure = TEXT("EncounterOrder");
			return false;
		}
		const FABTSM3MonthlySlingshotField* Field = nullptr;
		for (const FABTSM3MonthlySlingshotField& CandidateField :
			FieldCandidate.Fields)
		{
			if (CandidateField.Kind
					== EABTSM3MonthlySlingshotFieldKind::
						EncounterRequired
				&& CandidateField.EncounterId
					== Encounter.Contract.EncounterId)
			{
				if (Field != nullptr)
				{
					OutReason =
						EABTSM3MonthlyWitnessRejectReason::
							CandidateJoinMismatch;
					OutFailure = TEXT("DuplicateEncounterField");
					return false;
				}
				Field = &CandidateField;
			}
		}
		if (Field == nullptr)
		{
			OutReason =
				EABTSM3MonthlyWitnessRejectReason::
					CandidateJoinMismatch;
			OutFailure = TEXT("MissingEncounterField");
			return false;
		}

		const FABTSM3WitnessProfileDescriptor* Descriptor =
			FindProfile(
				Catalog,
				Encounter.ResolvedFixtureProfileId);
		if (Descriptor == nullptr
			|| Encounter.ProfileCatalogHash
				!= Catalog.SpatialSourceCatalogHash
			|| !Descriptor->BoundsExtentCM.Equals(
				Encounter.ProfileBoundsExtentCM,
				0.01))
		{
			OutReason =
				EABTSM3MonthlyWitnessRejectReason::
					ProfileMismatch;
			OutFailure = TEXT("ProfileFreeze");
			return false;
		}

		FABTSM3ResolvedWitnessGeometry Geometry;
		if (!Services.ResolveEncounterGeometry(
			SpatialCandidate.SourceRouteCandidateId,
			Encounter,
			*Field,
			Geometry,
			OutFailure))
		{
			OutReason =
				EABTSM3MonthlyWitnessRejectReason::
					GeometryInvalid;
			return false;
		}
		if (Geometry.EncounterId
				!= Encounter.Contract.EncounterId
			|| Geometry.EncounterOrder != EncounterOrder)
		{
			OutReason =
				EABTSM3MonthlyWitnessRejectReason::
					GeometryInvalid;
			OutFailure = TEXT("GeometryEncounterIdentity");
			return false;
		}
		const FABTSM3WitnessForbiddenSphere*
			M9SatelliteForbiddenSphere = nullptr;
		if (EncounterOrder == Config.M9PracticeEncounterOrder)
		{
			if (Geometry.M9SatelliteForbiddenVolumeId.IsNone())
			{
				OutReason =
					EABTSM3MonthlyWitnessRejectReason::
						GeometryInvalid;
				OutFailure = TEXT("M9ForbiddenVolumeMissing");
				return false;
			}
			M9SatelliteForbiddenSphere =
				Geometry.ForbiddenSpheres.FindByPredicate(
					[&Geometry](
						const FABTSM3WitnessForbiddenSphere&
							Sphere)
					{
						return Sphere.VolumeId
							== Geometry.
								M9SatelliteForbiddenVolumeId;
					});
			if (M9SatelliteForbiddenSphere == nullptr)
			{
				OutReason =
					EABTSM3MonthlyWitnessRejectReason::
						GeometryInvalid;
				OutFailure = TEXT("M9ForbiddenVolumeMissing");
				return false;
			}
		}
		else if (!Geometry.M9SatelliteForbiddenVolumeId.IsNone())
		{
			OutReason =
				EABTSM3MonthlyWitnessRejectReason::
					GeometryInvalid;
			OutFailure = TEXT("UnexpectedM9ForbiddenVolume");
			return false;
		}
		TArray<FReachablePair> Pairs;
		if (!BuildReachablePairs(
			*Field,
			Geometry,
			MaxCordLengthCM,
			Pairs,
			OutFailure))
		{
			OutReason =
				EABTSM3MonthlyWitnessRejectReason::
					GeometryInvalid;
			return false;
		}
		const FABTSM3WitnessAttackFace* Face =
			SelectAttackFace(
				Encounter,
				*Descriptor,
				Geometry.TargetWorldTransform);
		if (Face == nullptr)
		{
			OutReason =
				EABTSM3MonthlyWitnessRejectReason::
					ProfileMismatch;
			OutFailure = TEXT("AttackFaceSelection");
			return false;
		}

		FABTSM3MonthlyEncounterGameplay Gameplay;
		Gameplay.EncounterId = Encounter.Contract.EncounterId;
		Gameplay.EncounterOrder = EncounterOrder;
		Gameplay.ResolvedProfileId = Descriptor->ProfileId;
		Gameplay.ProfileDescriptorHash =
			Descriptor->DescriptorHash;
		Gameplay.AttackFaceId = Face->FaceId;
		int32 EvaluationCount = 0;
		if (Config.PriorTierRequiredEncounterOrders.Contains(
			EncounterOrder))
		{
			if (!SearchPriorTierDomain(
				Config,
				Services,
				Identity,
				SpatialCandidate.SourceRouteCandidateId,
				Encounter.Contract.EncounterId,
				EncounterOrder,
				Config.PriorTier,
				*Face,
				Geometry,
				Pairs,
				PullSamples,
				AimSamples,
				EvaluationCount,
				Gameplay.PriorTierCertificate,
				OutReason,
				OutFailure))
			{
				return false;
			}
		}
		else
		{
			Gameplay.PriorTierCertificate.State =
				EABTSM3PriorTierCertificateState::NotRequired;
			Gameplay.PriorTierCertificate.Tier =
				Config.PriorTier;
		}
		if (!SearchPositiveWitness(
			Config,
			Services,
			Identity,
			SpatialCandidate.SourceRouteCandidateId,
			Encounter.Contract.EncounterId,
			EncounterOrder,
			Config.EncounterTiers[EncounterOrder],
			EncounterOrder == Config.M9PracticeEncounterOrder,
			*Face,
			Geometry,
			Pairs,
			PullSamples,
			AimSamples,
			EvaluationCount,
			Gameplay.PositiveWitness,
			OutReason,
			OutFailure))
		{
			return false;
		}
		if (Gameplay.PriorTierCertificate.State
			== EABTSM3PriorTierCertificateState::
				CompleteInfeasible)
		{
			Gameplay.PriorTierCertificate.ResolvedGeometryHash =
				Geometry.GeometryHash;
			Gameplay.PriorTierCertificate.ProfileDescriptorHash =
				Descriptor->DescriptorHash;
			Gameplay.PriorTierCertificate.AttackFaceHash =
				Face->FaceHash;
			Gameplay.PriorTierCertificate.SolverHash =
				Identity.SolverHash;
			Gameplay.PriorTierCertificate.GravitySnapshotHash =
				Identity.GravitySnapshotHash;
			Gameplay.PriorTierCertificate.CertificateHash =
				static_cast<int64>(
					ComputeCertificateHashValue(
						Gameplay.PriorTierCertificate,
						Identity));
		}
		Gameplay.PositiveWitness.ProfileId =
			Descriptor->ProfileId;
		Gameplay.PositiveWitness.ResolvedGeometryHash =
			Geometry.GeometryHash;
		Gameplay.PositiveWitness.ProfileDescriptorHash =
			Descriptor->DescriptorHash;
		Gameplay.PositiveWitness.AttackFaceHash =
			Face->FaceHash;
		if (M9SatelliteForbiddenSphere != nullptr)
		{
			Gameplay.PositiveWitness.
				M9SatelliteForbiddenSphere =
					*M9SatelliteForbiddenSphere;
		}
		Gameplay.PositiveWitness.WitnessHash =
			static_cast<int64>(ComputeWitnessHashValue(
				Gameplay.PositiveWitness,
				Identity));
		Gameplay.TotalTrajectoryEvaluations = EvaluationCount;
		Gameplay.EncounterGameplayHash =
			static_cast<int64>(
				ComputeEncounterGameplayHash(Gameplay));
		OutCandidate.Encounters.Add(MoveTemp(Gameplay));
	}

	FABTSM3WitnessProgressionSnapshot Progression;
	if (!Services.GetProgressionSnapshot(
			SpatialCandidate.SourceRouteCandidateId,
			Progression,
			OutFailure)
		|| Progression.CatalogHash
			!= Identity.ProgressionCatalogHash)
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::
				ProgressionInvalid;
		if (OutFailure.IsEmpty())
		{
			OutFailure = TEXT("ProgressionIdentity");
		}
		return false;
	}
	if (!BuildFlowClosure(
		Progression,
		OutCandidate.Encounters,
		SpatialCandidate.SourceRouteCandidateId,
		OutCandidate.FlowClosure,
		OutReason,
		OutFailure))
	{
		return false;
	}
	int32 TotalEvaluations = 0;
	int32 ClearanceScore = 0;
	for (const FABTSM3MonthlyEncounterGameplay& Encounter :
		OutCandidate.Encounters)
	{
		TotalEvaluations += Encounter.TotalTrajectoryEvaluations;
		ClearanceScore += FMath::Clamp(
			Encounter.PositiveWitness.MinimumClearanceCM,
			0,
			10000);
	}
	OutCandidate.GameplayScore =
		1000000 - TotalEvaluations + ClearanceScore / 10;
	OutCandidate.bHardPass = true;
	OutCandidate.RejectReason =
		EABTSM3MonthlyWitnessRejectReason::None;
	OutCandidate.CandidateHash = static_cast<int64>(
		FABTSM3MonthlyWitnessBuilder::ComputeCandidateHash(
			OutCandidate));
	return true;
}

bool ValidateConfig(
	const FABTSM3MonthlyWitnessConfig& Config,
	FString& OutFailure)
{
	if (Config.MaxWitnessEvaluationsPerEncounter <= 0
		|| Config.MaxWitnessEvaluationsPerEncounter
			> FABTSM3MonthlyWitnessBuilder::
				MaximumEvaluationBudget
		|| Config.PullAlphaSampleCount < 2
		|| Config.PullAlphaSampleCount > 16
		|| Config.AimAxisSampleCount < 3
		|| Config.AimAxisSampleCount > 9
		|| Config.AimAxisSampleCount % 2 == 0
		|| Config.MinimumForbiddenClearanceCM < 0
		|| Config.MinimumM9AblationMissCM <= 0
		|| Config.MaximumRetainedCandidates <= 0
		|| Config.MaximumRetainedCandidates > 8
		|| Config.EncounterTiers.Num() != 6
		|| !IsValidTier(Config.PriorTier)
		|| Config.PriorTier != EABTSSlingshotTier::Simple
		|| Config.PriorTierRequiredEncounterOrders.Num() != 2
		|| Config.PriorTierRequiredEncounterOrders[0] != 3
		|| Config.PriorTierRequiredEncounterOrders[1] != 4
		|| Config.M9PracticeEncounterOrder < 0
		|| Config.M9PracticeEncounterOrder >= 6
		|| Config.M9PracticeEncounterOrder != 4
		|| Config.WitnessPlannerVersion != 1
		|| Config.FlowValidatorVersion != 1)
	{
		OutFailure = TEXT("WitnessConfig");
		return false;
	}
	const EABTSSlingshotTier ExpectedTiers[] = {
		EABTSSlingshotTier::Simple,
		EABTSSlingshotTier::Simple,
		EABTSSlingshotTier::Simple,
		EABTSSlingshotTier::Reinforced,
		EABTSSlingshotTier::Reinforced,
		EABTSSlingshotTier::Reinforced
	};
	for (int32 EncounterId = 0;
		EncounterId < Config.EncounterTiers.Num();
		++EncounterId)
	{
		const EABTSSlingshotTier Tier =
			Config.EncounterTiers[EncounterId];
		if (!IsValidTier(Tier)
			|| Tier != ExpectedTiers[EncounterId])
		{
			OutFailure = TEXT("EncounterTier");
			return false;
		}
	}
	TSet<int32> PriorIds;
	int32 PreviousPriorId = INDEX_NONE;
	for (const int32 EncounterId :
		Config.PriorTierRequiredEncounterOrders)
	{
		if (EncounterId < 0
			|| EncounterId >= 6
			|| PriorIds.Contains(EncounterId)
			|| (PreviousPriorId != INDEX_NONE
				&& EncounterId <= PreviousPriorId))
		{
			OutFailure = TEXT("PriorTierSchedule");
			return false;
		}
		PriorIds.Add(EncounterId);
		PreviousPriorId = EncounterId;
	}
	return true;
}

bool ValidateSourceJoinSets(
	const FABTSM3MonthlySpatialResult& SpatialResult,
	const FABTSM3MonthlySlingshotFieldResult& FieldResult,
	FString& OutFailure)
{
	if (SpatialResult.RetainedCandidates.Num()
		!= FieldResult.RetainedCandidates.Num())
	{
		OutFailure = TEXT("CandidateJoinCount");
		return false;
	}
	TSet<int32> SpatialRouteIds;
	TSet<int64> SpatialHashes;
	for (const FABTSM3MonthlySpatialCandidate& Spatial :
		SpatialResult.RetainedCandidates)
	{
		if (SpatialRouteIds.Contains(Spatial.SourceRouteCandidateId)
			|| SpatialHashes.Contains(Spatial.SpatialCandidateHash)
			|| Spatial.SpatialCandidateHash == 0)
		{
			OutFailure = TEXT("SpatialCandidateIdentity");
			return false;
		}
		SpatialRouteIds.Add(Spatial.SourceRouteCandidateId);
		SpatialHashes.Add(Spatial.SpatialCandidateHash);
		int32 MatchCount = 0;
		for (const FABTSM3MonthlySlingshotFieldCandidate& Field :
			FieldResult.RetainedCandidates)
		{
			MatchCount += Field.SourceRouteCandidateId
					== Spatial.SourceRouteCandidateId
				&& Field.SourceSpatialCandidateHash
					== Spatial.SpatialCandidateHash
				? 1
				: 0;
		}
		if (MatchCount != 1)
		{
			OutFailure = TEXT("SpatialToFieldJoin");
			return false;
		}
	}
	TSet<int32> FieldRouteIds;
	TSet<int64> FieldHashes;
	for (const FABTSM3MonthlySlingshotFieldCandidate& Field :
		FieldResult.RetainedCandidates)
	{
		if (FieldRouteIds.Contains(Field.SourceRouteCandidateId)
			|| FieldHashes.Contains(Field.CandidateHash)
			|| Field.CandidateHash == 0
			|| !SpatialRouteIds.Contains(
				Field.SourceRouteCandidateId)
			|| !SpatialHashes.Contains(
				Field.SourceSpatialCandidateHash))
		{
			OutFailure = TEXT("FieldCandidateIdentity");
			return false;
		}
		FieldRouteIds.Add(Field.SourceRouteCandidateId);
		FieldHashes.Add(Field.CandidateHash);
	}
	return true;
}

bool ValidateSourcePayloadHashes(
	const FABTSM3MonthlySpatialResult& SpatialResult,
	const FABTSM3MonthlySlingshotFieldResult& FieldResult,
	FString& OutFailure)
{
	if (SpatialResult.SpatialResultHash == 0
		|| static_cast<uint64>(
				SpatialResult.SpatialResultHash)
			!= FABTSM3MonthlyEncounterBuilder::
				ComputeResultHash(SpatialResult))
	{
		OutFailure = TEXT("SpatialResultPayloadHash");
		return false;
	}
	for (const FABTSM3MonthlySpatialCandidate& Candidate :
		SpatialResult.RetainedCandidates)
	{
		if (Candidate.SpatialCandidateHash == 0
			|| static_cast<uint64>(
					Candidate.SpatialCandidateHash)
				!= FABTSM3MonthlyEncounterBuilder::
					ComputeCandidateHash(Candidate)
			|| Candidate.RoadContextHash == 0
			|| static_cast<uint64>(Candidate.RoadContextHash)
				!= FABTSM3MonthlyRouteBuilder::
					ComputeRoadContextHash(
						Candidate.RoadContext)
			|| Candidate.RecomputedRoute.CandidateHash == 0
			|| static_cast<uint64>(
					Candidate.RecomputedRoute.CandidateHash)
				!= FABTSM3MonthlyRouteBuilder::
					ComputeCandidateHash(
						Candidate.RecomputedRoute))
		{
			OutFailure = TEXT("SpatialCandidatePayloadHash");
			return false;
		}
		for (const FABTSM3MonthlySpatialEncounter& Encounter :
			Candidate.Encounters)
		{
			if (Encounter.EncounterHash == 0
				|| static_cast<uint64>(
						Encounter.EncounterHash)
					!= FABTSM3MonthlyEncounterBuilder::
						ComputeEncounterHash(Encounter))
			{
				OutFailure = TEXT("EncounterPayloadHash");
				return false;
			}
		}
	}
	if (FieldResult.ResultHash == 0
		|| static_cast<uint64>(FieldResult.ResultHash)
			!= FABTSM3MonthlySlingshotFieldBuilder::
				ComputeResultHash(FieldResult))
	{
		OutFailure = TEXT("SlingshotResultPayloadHash");
		return false;
	}
	for (const FABTSM3MonthlySlingshotFieldCandidate& Candidate :
		FieldResult.RetainedCandidates)
	{
		if (Candidate.CandidateHash == 0
			|| static_cast<uint64>(Candidate.CandidateHash)
				!= FABTSM3MonthlySlingshotFieldBuilder::
					ComputeCandidateHash(Candidate))
		{
			OutFailure = TEXT("SlingshotCandidatePayloadHash");
			return false;
		}
		for (const FABTSM3MonthlySlingshotField& Field :
			Candidate.Fields)
		{
			if (Field.FieldHash == 0
				|| static_cast<uint64>(Field.FieldHash)
					!= FABTSM3MonthlySlingshotFieldBuilder::
						ComputeFieldHash(Field))
			{
				OutFailure = TEXT("SlingshotFieldPayloadHash");
				return false;
			}
		}
	}
	return true;
}

bool IsGameplayCandidateBefore(
	const FABTSM3MonthlyGameplayCandidate& A,
	const FABTSM3MonthlyGameplayCandidate& B)
{
	if (A.GameplayScore != B.GameplayScore)
	{
		return A.GameplayScore > B.GameplayScore;
	}
	if (A.SpatialScore != B.SpatialScore)
	{
		return A.SpatialScore > B.SpatialScore;
	}
	if (A.RouteScore != B.RouteScore)
	{
		return A.RouteScore > B.RouteScore;
	}
	if (A.SourceRouteCandidateId != B.SourceRouteCandidateId)
	{
		return A.SourceRouteCandidateId
			< B.SourceRouteCandidateId;
	}
	return static_cast<uint64>(A.CandidateHash)
		< static_cast<uint64>(B.CandidateHash);
}

uint64 ComputeGameplayLayoutHashValue(
	const FABTSM3MonthlyWitnessResult& Result)
{
	FHash64 Hash;
	Hash.AddInt64(Result.SourceSpatialResultHash);
	Hash.AddInt64(Result.SourceSlingshotFieldResultHash);
	Hash.AddInt64(Result.ConfigHash);
	Hash.AddInt64(Result.ServiceIdentity.SolverHash);
	Hash.AddInt64(Result.ServiceIdentity.GeometryHash);
	Hash.AddInt64(Result.ServiceIdentity.GravitySnapshotHash);
	Hash.AddInt64(Result.ServiceIdentity.ProfileCatalogHash);
	Hash.AddInt64(Result.ServiceIdentity.ProgressionCatalogHash);
	Hash.AddInt64(Result.ServiceIdentity.BirdCatalogHash);
	Hash.AddInt32(Result.SelectedCandidateId);
	Hash.AddInt32(Result.RetainedCandidates.Num());
	for (const FABTSM3MonthlyGameplayCandidate& Candidate :
		Result.RetainedCandidates)
	{
		Hash.AddInt64(Candidate.CandidateHash);
	}
	return Hash.Value;
}

bool RejectBuild(
	FABTSM3MonthlyWitnessResult& Result,
	const EABTSM3MonthlyWitnessRejectReason Reason,
	const FString& Failure,
	FString& OutFailure)
{
	Result.bGameplayFinalizeValid = false;
	Result.bExternalInputsCertified = false;
	Result.bMonthlyWorldAccepted = false;
	Result.RejectReason = Reason;
	Result.ResultHash = static_cast<int64>(
		FABTSM3MonthlyWitnessBuilder::ComputeResultHash(Result));
	OutFailure = Failure;
	return false;
}
}

FABTSM3MonthlyWitnessConfig::FABTSM3MonthlyWitnessConfig()
{
	EncounterTiers = {
		EABTSSlingshotTier::Simple,
		EABTSSlingshotTier::Simple,
		EABTSSlingshotTier::Simple,
		EABTSSlingshotTier::Reinforced,
		EABTSSlingshotTier::Reinforced,
		EABTSSlingshotTier::Reinforced
	};
	PriorTierRequiredEncounterOrders = { 3, 4 };
}

bool FABTSM3MonthlyWitnessBuilder::Build(
	const int32 WorldSeed,
	const FABTSM3MonthlyWitnessConfig& Config,
	const FABTSM3MonthlySpatialResult& SpatialResult,
	const FABTSM3MonthlySlingshotFieldResult& FieldResult,
	const IABTSM3MonthlyWitnessServices* Services,
	FABTSM3MonthlyWitnessResult& OutResult,
	FString& OutFailure)
{
	using namespace ABTSM3MonthlyWitnessPrivate;
	OutResult = FABTSM3MonthlyWitnessResult();
	OutFailure.Reset();
	OutResult.SchemaVersion = SchemaVersion;
	OutResult.GeneratorVersion = GeneratorVersion;
	OutResult.LayoutPolicyVersion = MonthlyLayoutPolicyVersion;
	OutResult.WorldSeed = WorldSeed;
	OutResult.SourceSpatialResultHash =
		SpatialResult.SpatialResultHash;
	OutResult.SourceSlingshotFieldResultHash =
		FieldResult.ResultHash;
	OutResult.ConfigHash =
		static_cast<int64>(ComputeConfigHash(Config));
	if (!ValidateConfig(Config, OutFailure))
	{
		return RejectBuild(
			OutResult,
			EABTSM3MonthlyWitnessRejectReason::InvalidConfig,
			OutFailure,
			OutFailure);
	}
	if (!Config.bBuildGameplayFinalize)
	{
		OutResult.RejectReason =
			EABTSM3MonthlyWitnessRejectReason::NotEvaluated;
		OutResult.ResultHash =
			static_cast<int64>(ComputeResultHash(OutResult));
		return true;
	}
	if (!SpatialResult.bSpatialResultValid
		|| SpatialResult.WorldSeed != WorldSeed
		|| FieldResult.WorldSeed != WorldSeed
		|| SpatialResult.SchemaVersion
			!= FABTSM3MonthlyEncounterBuilder::
				SpatialSchemaVersion
		|| FieldResult.SchemaVersion
			!= FABTSM3MonthlySlingshotFieldBuilder::
				SchemaVersion
		|| SpatialResult.GeneratorVersion != GeneratorVersion
		|| FieldResult.GeneratorVersion != GeneratorVersion
		|| SpatialResult.LayoutPolicyVersion
			!= MonthlyLayoutPolicyVersion
		|| FieldResult.LayoutPolicyVersion
			!= MonthlyLayoutPolicyVersion
		|| SpatialResult.TopologyHash != FieldResult.TopologyHash
		|| SpatialResult.RejectReason
			!= EABTSM3MonthlySpatialRejectReason::None
		|| SpatialResult.RetainedCandidates.IsEmpty()
		|| !FieldResult.bSlingshotFieldResultValid
		|| FieldResult.RejectReason
			!= EABTSM3MonthlySlingshotFieldRejectReason::None
		|| FieldResult.RetainedCandidates.IsEmpty()
		|| FieldResult.SourceSpatialResultHash
			!= SpatialResult.SpatialResultHash)
	{
		return RejectBuild(
			OutResult,
			EABTSM3MonthlyWitnessRejectReason::InvalidSource,
			TEXT("SourceResult"),
			OutFailure);
	}
	if (!ValidateSourcePayloadHashes(
			SpatialResult,
			FieldResult,
			OutFailure))
	{
		return RejectBuild(
			OutResult,
			EABTSM3MonthlyWitnessRejectReason::InvalidSource,
			OutFailure,
			OutFailure);
	}
	if (!ValidateSourceJoinSets(
		SpatialResult,
		FieldResult,
		OutFailure))
	{
		return RejectBuild(
			OutResult,
			EABTSM3MonthlyWitnessRejectReason::
				CandidateJoinMismatch,
			OutFailure,
			OutFailure);
	}
	if (Services == nullptr)
	{
		return RejectBuild(
			OutResult,
			EABTSM3MonthlyWitnessRejectReason::
				ProviderUnavailable,
			TEXT("ProviderUnavailable"),
			OutFailure);
	}

	FABTSM3WitnessServiceIdentity Identity;
	if (!Services->GetIdentity(Identity, OutFailure)
		|| !IsValidAuthority(Identity.Authority)
		|| Identity.Authority
			!= EABTSM3WitnessAuthority::Fixture
		|| Identity.ServiceSchemaVersion != 1
		|| Identity.SolverHash == 0
		|| Identity.GeometryHash == 0
		|| Identity.GravitySnapshotHash == 0
		|| Identity.ProfileCatalogHash == 0
		|| Identity.ProgressionCatalogHash == 0
		|| Identity.BirdCatalogHash
			!= static_cast<int64>(
				ComputeV1BirdCatalogHashValue())
		|| Identity.bCertified)
	{
		return RejectBuild(
			OutResult,
			EABTSM3MonthlyWitnessRejectReason::
				ProviderIdentityMismatch,
			OutFailure.IsEmpty()
				? TEXT("ProviderIdentity")
				: OutFailure,
			OutFailure);
	}
	OutResult.ServiceIdentity = Identity;
	OutResult.Authority = Identity.Authority;

	FABTSM3WitnessProfileCatalog Catalog;
	if (!Services->GetProfileCatalog(Catalog, OutFailure)
		|| !ValidateProfileCatalog(
			Identity,
			Catalog,
			SpatialResult,
			OutFailure))
	{
		return RejectBuild(
			OutResult,
			EABTSM3MonthlyWitnessRejectReason::
				ProfileCatalogMismatch,
			OutFailure,
			OutFailure);
	}
	EABTSM3MonthlyWitnessRejectReason LastReason =
		EABTSM3MonthlyWitnessRejectReason::NoAcceptedCandidate;
	FString LastFailure(TEXT("NoAcceptedCandidate"));
	for (const FABTSM3MonthlySpatialCandidate& SpatialCandidate :
		SpatialResult.RetainedCandidates)
	{
		++OutResult.AttemptedCandidateCount;
		const FABTSM3MonthlySlingshotFieldCandidate*
			JoinedField = nullptr;
		for (const FABTSM3MonthlySlingshotFieldCandidate&
			FieldCandidate : FieldResult.RetainedCandidates)
		{
			if (FieldCandidate.SourceRouteCandidateId
					== SpatialCandidate.SourceRouteCandidateId
				&& FieldCandidate.SourceSpatialCandidateHash
					== SpatialCandidate.SpatialCandidateHash)
			{
				if (JoinedField != nullptr)
				{
					JoinedField = nullptr;
					LastReason =
						EABTSM3MonthlyWitnessRejectReason::
							CandidateJoinMismatch;
					LastFailure = TEXT("DuplicateCandidateJoin");
					break;
				}
				JoinedField = &FieldCandidate;
			}
		}
		if (JoinedField == nullptr)
		{
			LastReason =
				EABTSM3MonthlyWitnessRejectReason::
					CandidateJoinMismatch;
			if (LastFailure != TEXT("DuplicateCandidateJoin"))
			{
				LastFailure = TEXT("MissingCandidateJoin");
			}
			continue;
		}
		FABTSM3MonthlyGameplayCandidate Candidate;
		EABTSM3MonthlyWitnessRejectReason CandidateReason =
			EABTSM3MonthlyWitnessRejectReason::None;
		FString CandidateFailure;
		if (!BuildCandidate(
			Config,
			*Services,
			Identity,
			Catalog,
			SpatialCandidate,
			*JoinedField,
			FieldResult.MaxCordLengthCM,
			Candidate,
			CandidateReason,
			CandidateFailure))
		{
			LastReason = CandidateReason;
			LastFailure = CandidateFailure;
			continue;
		}
		OutResult.RetainedCandidates.Add(MoveTemp(Candidate));
	}
	if (OutResult.RetainedCandidates.IsEmpty())
	{
		return RejectBuild(
			OutResult,
			LastReason,
			LastFailure,
			OutFailure);
	}
	OutResult.RetainedCandidates.Sort([](
		const FABTSM3MonthlyGameplayCandidate& A,
		const FABTSM3MonthlyGameplayCandidate& B)
	{
		return IsGameplayCandidateBefore(A, B);
	});
	OutResult.HardPassCandidateCount =
		OutResult.RetainedCandidates.Num();
	if (OutResult.RetainedCandidates.Num()
		> Config.MaximumRetainedCandidates)
	{
		OutResult.RetainedCandidates.SetNum(
			Config.MaximumRetainedCandidates);
	}
	OutResult.SelectedCandidateId =
		OutResult.RetainedCandidates[0].SourceRouteCandidateId;
	OutResult.bGameplayFinalizeValid = true;
	OutResult.bExternalInputsCertified = false;
	OutResult.bMonthlyWorldAccepted = false;
	OutResult.RejectReason =
		EABTSM3MonthlyWitnessRejectReason::None;
	OutResult.GameplayLayoutHash =
		static_cast<int64>(
			ComputeGameplayLayoutHashValue(OutResult));
	OutResult.ResultHash =
		static_cast<int64>(ComputeResultHash(OutResult));
	if (Config.bEmitWitnessLogs)
	{
		LogSummary(OutResult);
	}
	return true;
}

bool FABTSM3MonthlyWitnessBuilder::Validate(
	const FABTSM3MonthlyWitnessConfig& Config,
	const FABTSM3MonthlySpatialResult& SpatialResult,
	const FABTSM3MonthlySlingshotFieldResult& FieldResult,
	const FABTSM3MonthlyWitnessResult& Result,
	EABTSM3MonthlyWitnessRejectReason& OutReason,
	FString& OutFailure)
{
	using namespace ABTSM3MonthlyWitnessPrivate;
	OutReason = EABTSM3MonthlyWitnessRejectReason::None;
	OutFailure.Reset();
	if (!ValidateConfig(Config, OutFailure))
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::InvalidConfig;
		return false;
	}
	if (Result.SchemaVersion != SchemaVersion
		|| Result.GeneratorVersion != GeneratorVersion
		|| Result.LayoutPolicyVersion
			!= MonthlyLayoutPolicyVersion
		|| Result.SourceSpatialResultHash
			!= SpatialResult.SpatialResultHash
		|| Result.SourceSlingshotFieldResultHash
			!= FieldResult.ResultHash
		|| Result.ConfigHash
			!= static_cast<int64>(ComputeConfigHash(Config))
		|| Result.bMonthlyWorldAccepted
		|| Result.ResultHash
			!= static_cast<int64>(ComputeResultHash(Result)))
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::HashMismatch;
		OutFailure = TEXT("ResultIdentity");
		return false;
	}
	if (!Config.bBuildGameplayFinalize)
	{
		if (Result.bGameplayFinalizeValid
			|| Result.bExternalInputsCertified
			|| Result.RejectReason
				!= EABTSM3MonthlyWitnessRejectReason::NotEvaluated
			|| !Result.RetainedCandidates.IsEmpty()
			|| Result.SelectedCandidateId != INDEX_NONE
			|| Result.GameplayLayoutHash != 0
			|| Result.Authority
				!= EABTSM3WitnessAuthority::None
			|| Result.ServiceIdentity.Authority
				!= EABTSM3WitnessAuthority::None
			|| Result.ServiceIdentity.ServiceSchemaVersion != 0
			|| Result.ServiceIdentity.SolverHash != 0
			|| Result.ServiceIdentity.GeometryHash != 0
			|| Result.ServiceIdentity.GravitySnapshotHash != 0
			|| Result.ServiceIdentity.ProfileCatalogHash != 0
			|| Result.ServiceIdentity.ProgressionCatalogHash != 0
			|| Result.ServiceIdentity.BirdCatalogHash != 0
			|| Result.ServiceIdentity.bCertified
			|| Result.AttemptedCandidateCount != 0
			|| Result.HardPassCandidateCount != 0)
		{
			OutReason =
				EABTSM3MonthlyWitnessRejectReason::HashMismatch;
			OutFailure = TEXT("DisabledResult");
			return false;
		}
		return true;
	}
	if (Result.WorldSeed != SpatialResult.WorldSeed
		|| Result.WorldSeed != FieldResult.WorldSeed
		|| SpatialResult.SchemaVersion
			!= FABTSM3MonthlyEncounterBuilder::
				SpatialSchemaVersion
		|| FieldResult.SchemaVersion
			!= FABTSM3MonthlySlingshotFieldBuilder::
				SchemaVersion
		|| SpatialResult.GeneratorVersion != GeneratorVersion
		|| FieldResult.GeneratorVersion != GeneratorVersion
		|| SpatialResult.LayoutPolicyVersion
			!= MonthlyLayoutPolicyVersion
		|| FieldResult.LayoutPolicyVersion
			!= MonthlyLayoutPolicyVersion
		|| SpatialResult.TopologyHash != FieldResult.TopologyHash
		|| !SpatialResult.bSpatialResultValid
		|| SpatialResult.RejectReason
			!= EABTSM3MonthlySpatialRejectReason::None
		|| !FieldResult.bSlingshotFieldResultValid
		|| FieldResult.RejectReason
			!= EABTSM3MonthlySlingshotFieldRejectReason::None
		|| FieldResult.SourceSpatialResultHash
			!= SpatialResult.SpatialResultHash
		|| !ValidateSourcePayloadHashes(
			SpatialResult,
			FieldResult,
			OutFailure)
		|| !ValidateSourceJoinSets(
			SpatialResult,
			FieldResult,
			OutFailure))
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::
				CandidateJoinMismatch;
		return false;
	}
	if (!Result.bGameplayFinalizeValid
		|| Result.RejectReason
			!= EABTSM3MonthlyWitnessRejectReason::None
		|| !IsValidAuthority(Result.Authority)
		|| Result.Authority
			!= EABTSM3WitnessAuthority::Fixture
		|| Result.Authority
			!= Result.ServiceIdentity.Authority
		|| Result.ServiceIdentity.ServiceSchemaVersion != 1
		|| Result.ServiceIdentity.SolverHash == 0
		|| Result.ServiceIdentity.GeometryHash == 0
		|| Result.ServiceIdentity.GravitySnapshotHash == 0
		|| Result.ServiceIdentity.ProfileCatalogHash == 0
		|| Result.ServiceIdentity.ProgressionCatalogHash == 0
		|| Result.ServiceIdentity.BirdCatalogHash
			!= static_cast<int64>(
				ComputeV1BirdCatalogHashValue())
		|| Result.bExternalInputsCertified
		|| Result.ServiceIdentity.bCertified
		|| Result.RetainedCandidates.IsEmpty()
		|| Result.RetainedCandidates.Num()
			> Config.MaximumRetainedCandidates
		|| Result.SelectedCandidateId
			!= Result.RetainedCandidates[0].
				SourceRouteCandidateId
		|| Result.AttemptedCandidateCount
			!= SpatialResult.RetainedCandidates.Num()
		|| Result.HardPassCandidateCount
			< Result.RetainedCandidates.Num()
		|| Result.HardPassCandidateCount
			> Result.AttemptedCandidateCount
		|| Result.GameplayLayoutHash == 0
		|| Result.GameplayLayoutHash
			!= static_cast<int64>(
				ComputeGameplayLayoutHashValue(Result)))
	{
		OutReason =
			EABTSM3MonthlyWitnessRejectReason::HashMismatch;
		OutFailure = TEXT("AcceptedResult");
		return false;
	}
	TSet<int32> RetainedSourceIds;
	for (int32 CandidateIndex = 0;
		CandidateIndex < Result.RetainedCandidates.Num();
		++CandidateIndex)
	{
		const FABTSM3MonthlyGameplayCandidate& Candidate =
			Result.RetainedCandidates[CandidateIndex];
		const FABTSM3MonthlySpatialCandidate* SourceSpatial =
			SpatialResult.RetainedCandidates.FindByPredicate(
				[&Candidate](
					const FABTSM3MonthlySpatialCandidate& Source)
				{
					return Source.SourceRouteCandidateId
							== Candidate.SourceRouteCandidateId
						&& Source.SpatialCandidateHash
							== Candidate.
								SourceSpatialCandidateHash;
				});
		const FABTSM3MonthlySlingshotFieldCandidate* SourceField =
			FieldResult.RetainedCandidates.FindByPredicate(
				[&Candidate](
					const FABTSM3MonthlySlingshotFieldCandidate&
						Source)
				{
					return Source.SourceRouteCandidateId
							== Candidate.SourceRouteCandidateId
						&& Source.SourceSpatialCandidateHash
							== Candidate.
								SourceSpatialCandidateHash
						&& Source.CandidateHash
							== Candidate.
								SourceSlingshotFieldCandidateHash;
				});
		if (!Candidate.bHardPass
			|| Candidate.RejectReason
				!= EABTSM3MonthlyWitnessRejectReason::None
			|| Candidate.Encounters.Num() != RequiredEncounterCount
			|| !ValidateFlowShape(
				Candidate.FlowClosure,
				Candidate.Encounters,
				Candidate.SourceRouteCandidateId)
			|| Candidate.FlowClosure.FlowHash
				!= static_cast<int64>(
					ComputeFlowHash(Candidate.FlowClosure))
			|| Candidate.CandidateHash
				!= static_cast<int64>(
					ComputeCandidateHash(Candidate))
			|| SourceSpatial == nullptr
			|| SourceField == nullptr
			|| SourceSpatial->Encounters.Num()
				!= RequiredEncounterCount
			|| Candidate.SpatialScore
				!= SourceSpatial->SpatialScore
			|| Candidate.RouteScore
				!= SourceSpatial->RecomputedRoute.RouteScore)
		{
			OutReason =
				EABTSM3MonthlyWitnessRejectReason::HashMismatch;
			OutFailure = TEXT("Candidate");
			return false;
		}
		if (RetainedSourceIds.Contains(
				Candidate.SourceRouteCandidateId)
			|| (CandidateIndex > 0
				&& IsGameplayCandidateBefore(
					Candidate,
					Result.RetainedCandidates[
						CandidateIndex - 1])))
		{
			OutReason =
				EABTSM3MonthlyWitnessRejectReason::HashMismatch;
			OutFailure = TEXT("CandidateOrder");
			return false;
		}
		RetainedSourceIds.Add(Candidate.SourceRouteCandidateId);
		for (int32 EncounterOrder = 0;
			EncounterOrder < Candidate.Encounters.Num();
			++EncounterOrder)
		{
			const FABTSM3MonthlyEncounterGameplay& Encounter =
				Candidate.Encounters[EncounterOrder];
			const FABTSM3MonthlySpatialEncounter&
				SourceEncounter =
					SourceSpatial->Encounters[EncounterOrder];
			const int32 ExpectedStableEncounterId =
				SourceEncounter.Contract.EncounterId;
			const FABTSM3MonthlySlingshotField*
				SourceEncounterField =
					SourceField->Fields.FindByPredicate(
						[ExpectedStableEncounterId](
							const FABTSM3MonthlySlingshotField&
								Field)
						{
							return Field.Kind
									== EABTSM3MonthlySlingshotFieldKind::
										EncounterRequired
								&& Field.EncounterId
									== ExpectedStableEncounterId;
						});
			const bool bPriorRequired =
				Config.PriorTierRequiredEncounterOrders.Contains(
					EncounterOrder);
			const FABTSM3BallisticWitness& Witness =
				Encounter.PositiveWitness;
			const FABTSM3PriorTierInfeasibilityCertificate&
				Certificate =
					Encounter.PriorTierCertificate;
			if (Encounter.EncounterId != ExpectedStableEncounterId
				|| Encounter.EncounterOrder != EncounterOrder
				|| Encounter.ResolvedProfileId
					!= SourceEncounter.
						ResolvedFixtureProfileId
				|| Encounter.AttackFaceId.IsNone()
				|| SourceEncounterField == nullptr
				|| Witness.ProfileId
					!= Encounter.ResolvedProfileId
				|| Witness.AttackFaceId
					!= Encounter.AttackFaceId
				|| Witness.LaunchInput.EncounterId
					!= Encounter.EncounterId
				|| Witness.LaunchInput.EncounterOrder
					!= EncounterOrder
				|| Witness.LaunchInput.Tier
					!= Config.EncounterTiers[EncounterOrder]
				|| !Witness.LaunchInput.
					bEnableSatelliteGravity
				|| !SourceEncounterField->SlotCellIds.Contains(
					Witness.LaunchInput.SlotACellId)
				|| !SourceEncounterField->SlotCellIds.Contains(
					Witness.LaunchInput.SlotBCellId)
				|| Witness.SlotDistanceCM
					> FieldResult.MaxCordLengthCM
				|| Witness.ResolvedGeometryHash == 0
				|| Witness.ProfileDescriptorHash
					!= Encounter.ProfileDescriptorHash
				|| Witness.AttackFaceHash == 0
				|| Witness.Termination
					!= EABTSM3TrajectoryTermination::TargetHit
				|| !ValidateWitnessShape(Witness)
				|| Witness.SearchEvaluationCount
					!= Encounter.TotalTrajectoryEvaluations
				|| Witness.WitnessHash == 0
				|| Witness.WitnessHash
					!= static_cast<int64>(
						ComputeWitnessHashValue(
							Witness,
							Result.ServiceIdentity))
				|| Encounter.EncounterGameplayHash
					!= static_cast<int64>(
						ComputeEncounterGameplayHash(Encounter))
				|| Encounter.TotalTrajectoryEvaluations <= 0
				|| Encounter.TotalTrajectoryEvaluations
					> Config.
						MaxWitnessEvaluationsPerEncounter
				|| (bPriorRequired
					&& (Certificate.State
						!= EABTSM3PriorTierCertificateState::
							CompleteInfeasible
						|| Certificate.Tier
							!= Config.PriorTier
						|| Certificate.PlannedInputCount <= 0
						|| Certificate.CompletedInputCount
							!= Certificate.PlannedInputCount
						|| Certificate.EligibleBirdCatalogHash
							!= Result.ServiceIdentity.
								BirdCatalogHash
						|| Certificate.InputDomainHash == 0
						|| Certificate.ResolvedGeometryHash
							!= Witness.ResolvedGeometryHash
						|| Certificate.ProfileDescriptorHash
							!= Encounter.ProfileDescriptorHash
						|| Certificate.AttackFaceHash
							!= Witness.AttackFaceHash
						|| Certificate.SolverHash
							!= Result.ServiceIdentity.SolverHash
						|| Certificate.GravitySnapshotHash
							!= Result.ServiceIdentity.
								GravitySnapshotHash
						|| Certificate.CertificateHash == 0
						|| Certificate.CertificateHash
							!= static_cast<int64>(
								ComputeCertificateHashValue(
									Certificate,
									Result.ServiceIdentity))))
				|| (!bPriorRequired
					&& (Certificate.State
							!= EABTSM3PriorTierCertificateState::
								NotRequired
						|| Certificate.Tier
							!= Config.PriorTier
						|| Certificate.PlannedInputCount != 0
						|| Certificate.CompletedInputCount != 0
						|| Certificate.ClosestMissCM != 0
						|| Certificate.EligibleBirdCatalogHash != 0
						|| Certificate.InputDomainHash != 0
						|| Certificate.ResolvedGeometryHash != 0
						|| Certificate.ProfileDescriptorHash != 0
						|| Certificate.AttackFaceHash != 0
						|| Certificate.SolverHash != 0
						|| Certificate.GravitySnapshotHash != 0
						|| Certificate.CertificateHash != 0))
				|| (EncounterOrder
						== Config.M9PracticeEncounterOrder
					&& (Witness.M9QueryCount <= 0
						|| Witness.M9NonZeroAccelerationCount <= 0
						|| Witness.PeakM9AccelerationCMPerSecSq
							<= 0.0f
						|| !ValidateM9AblationEvidence(
							Witness,
							Result.ServiceIdentity,
							Config.MinimumM9AblationMissCM)
						|| !ValidateM9ForbiddenSphereClearance(
							Witness,
							Config.
								MinimumForbiddenClearanceCM)))
				|| (EncounterOrder
						!= Config.M9PracticeEncounterOrder
					&& (Witness.M9AblationMissCM != 0
						|| !IsDefaultM9AblationEvidence(
							Witness.M9AblationEvidence)
						|| !IsDefaultForbiddenSphere(
							Witness.
								M9SatelliteForbiddenSphere))))
			{
				OutReason =
					EABTSM3MonthlyWitnessRejectReason::HashMismatch;
				OutFailure = TEXT("Encounter");
				return false;
			}
		}
	}
	return true;
}

uint64 FABTSM3MonthlyWitnessBuilder::ComputeConfigHash(
	const FABTSM3MonthlyWitnessConfig& Config)
{
	using namespace ABTSM3MonthlyWitnessPrivate;
	FHash64 Hash;
	Hash.AddBool(Config.bBuildGameplayFinalize);
	Hash.AddInt32(Config.MaxWitnessEvaluationsPerEncounter);
	Hash.AddInt32(Config.PullAlphaSampleCount);
	Hash.AddInt32(Config.AimAxisSampleCount);
	Hash.AddInt32(Config.MinimumForbiddenClearanceCM);
	Hash.AddInt32(Config.MinimumM9AblationMissCM);
	Hash.AddInt32(Config.MaximumRetainedCandidates);
	Hash.AddInt32(Config.EncounterTiers.Num());
	for (const EABTSSlingshotTier Tier : Config.EncounterTiers)
	{
		Hash.AddByte(static_cast<uint8>(Tier));
	}
	Hash.AddInt32(Config.PriorTierRequiredEncounterOrders.Num());
	for (const int32 EncounterId :
		Config.PriorTierRequiredEncounterOrders)
	{
		Hash.AddInt32(EncounterId);
	}
	Hash.AddByte(static_cast<uint8>(Config.PriorTier));
	Hash.AddInt32(Config.M9PracticeEncounterOrder);
	Hash.AddInt32(Config.WitnessPlannerVersion);
	Hash.AddInt32(Config.FlowValidatorVersion);
	return Hash.Value;
}

uint64 FABTSM3MonthlyWitnessBuilder::ComputeAttackFaceHash(
	const FABTSM3WitnessAttackFace& Face)
{
	return ABTSM3MonthlyWitnessPrivate::ComputeFaceHash(Face);
}

uint64 FABTSM3MonthlyWitnessBuilder::ComputeProfileDescriptorHash(
	const FABTSM3WitnessProfileDescriptor& Descriptor)
{
	return ABTSM3MonthlyWitnessPrivate::ComputeDescriptorHash(
		Descriptor);
}

uint64 FABTSM3MonthlyWitnessBuilder::ComputeProfileCatalogHash(
	const FABTSM3WitnessProfileCatalog& Catalog)
{
	return ABTSM3MonthlyWitnessPrivate::
		ComputeProfileCatalogHashValue(Catalog);
}

uint64 FABTSM3MonthlyWitnessBuilder::ComputeResolvedGeometryHash(
	const FABTSM3ResolvedWitnessGeometry& Geometry)
{
	return ABTSM3MonthlyWitnessPrivate::ComputeGeometryHash(
		Geometry);
}

uint64 FABTSM3MonthlyWitnessBuilder::ComputeBridgeEvidenceHash(
	const FABTSM3BridgeGateEvidence& Evidence)
{
	return ABTSM3MonthlyWitnessPrivate::ComputeBridgeHash(Evidence);
}

uint64 FABTSM3MonthlyWitnessBuilder::ComputeProgressionCatalogHash(
	const FABTSM3WitnessProgressionSnapshot& Snapshot)
{
	return ABTSM3MonthlyWitnessPrivate::ComputeProgressionHash(
		Snapshot);
}

uint64 FABTSM3MonthlyWitnessBuilder::ComputeV1BirdCatalogHash()
{
	return ABTSM3MonthlyWitnessPrivate::
		ComputeV1BirdCatalogHashValue();
}

uint64 FABTSM3MonthlyWitnessBuilder::ComputeFlowClosureHash(
	const FABTSM3MonthlyFlowClosure& Flow)
{
	return ABTSM3MonthlyWitnessPrivate::ComputeFlowHash(Flow);
}

uint64 FABTSM3MonthlyWitnessBuilder::ComputeCandidateHash(
	const FABTSM3MonthlyGameplayCandidate& Candidate)
{
	using namespace ABTSM3MonthlyWitnessPrivate;
	FHash64 Hash;
	Hash.AddInt32(Candidate.SourceRouteCandidateId);
	Hash.AddInt64(Candidate.SourceSpatialCandidateHash);
	Hash.AddInt64(Candidate.SourceSlingshotFieldCandidateHash);
	Hash.AddInt32(Candidate.Encounters.Num());
	for (const FABTSM3MonthlyEncounterGameplay& Encounter :
		Candidate.Encounters)
	{
		Hash.AddInt64(Encounter.EncounterGameplayHash);
	}
	Hash.AddInt64(Candidate.FlowClosure.FlowHash);
	Hash.AddInt32(Candidate.GameplayScore);
	Hash.AddInt32(Candidate.SpatialScore);
	Hash.AddInt32(Candidate.RouteScore);
	Hash.AddBool(Candidate.bHardPass);
	Hash.AddByte(static_cast<uint8>(Candidate.RejectReason));
	return Hash.Value;
}

uint64 FABTSM3MonthlyWitnessBuilder::ComputeGameplayLayoutHash(
	const FABTSM3MonthlyWitnessResult& Result)
{
	return ABTSM3MonthlyWitnessPrivate::
		ComputeGameplayLayoutHashValue(Result);
}

uint64 FABTSM3MonthlyWitnessBuilder::ComputeResultHash(
	const FABTSM3MonthlyWitnessResult& Result)
{
	using namespace ABTSM3MonthlyWitnessPrivate;
	FHash64 Hash;
	Hash.AddInt32(Result.SchemaVersion);
	Hash.AddInt32(Result.GeneratorVersion);
	Hash.AddInt32(Result.LayoutPolicyVersion);
	Hash.AddInt32(Result.WorldSeed);
	Hash.AddInt64(Result.SourceSpatialResultHash);
	Hash.AddInt64(Result.SourceSlingshotFieldResultHash);
	Hash.AddInt64(Result.ConfigHash);
	Hash.AddByte(static_cast<uint8>(
		Result.ServiceIdentity.Authority));
	Hash.AddInt32(Result.ServiceIdentity.ServiceSchemaVersion);
	Hash.AddInt64(Result.ServiceIdentity.SolverHash);
	Hash.AddInt64(Result.ServiceIdentity.GeometryHash);
	Hash.AddInt64(Result.ServiceIdentity.GravitySnapshotHash);
	Hash.AddInt64(Result.ServiceIdentity.ProfileCatalogHash);
	Hash.AddInt64(Result.ServiceIdentity.ProgressionCatalogHash);
	Hash.AddInt64(Result.ServiceIdentity.BirdCatalogHash);
	Hash.AddBool(Result.ServiceIdentity.bCertified);
	Hash.AddByte(static_cast<uint8>(Result.Authority));
	Hash.AddBool(Result.bGameplayFinalizeValid);
	Hash.AddBool(Result.bExternalInputsCertified);
	Hash.AddBool(Result.bMonthlyWorldAccepted);
	Hash.AddByte(static_cast<uint8>(Result.RejectReason));
	Hash.AddInt32(Result.AttemptedCandidateCount);
	Hash.AddInt32(Result.HardPassCandidateCount);
	Hash.AddInt32(Result.SelectedCandidateId);
	Hash.AddInt32(Result.RetainedCandidates.Num());
	for (const FABTSM3MonthlyGameplayCandidate& Candidate :
		Result.RetainedCandidates)
	{
		Hash.AddInt64(Candidate.CandidateHash);
	}
	Hash.AddInt64(Result.GameplayLayoutHash);
	return Hash.Value;
}

void FABTSM3MonthlyWitnessBuilder::LogSummary(
	const FABTSM3MonthlyWitnessResult& Result)
{
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R4][Witness] Seed=%d Authority=%d GameplayValid=%d ExternalCertified=%d MonthlyAccepted=%d Attempted=%d HardPass=%d Selected=%d Layout=%016llX Result=%016llX Reject=%s"),
		Result.WorldSeed,
		static_cast<int32>(Result.Authority),
		Result.bGameplayFinalizeValid ? 1 : 0,
		Result.bExternalInputsCertified ? 1 : 0,
		Result.bMonthlyWorldAccepted ? 1 : 0,
		Result.AttemptedCandidateCount,
		Result.HardPassCandidateCount,
		Result.SelectedCandidateId,
		static_cast<unsigned long long>(
			Result.GameplayLayoutHash),
		static_cast<unsigned long long>(Result.ResultHash),
		GetRejectReasonName(Result.RejectReason));
}

const TCHAR* FABTSM3MonthlyWitnessBuilder::GetRejectReasonName(
	const EABTSM3MonthlyWitnessRejectReason Reason)
{
	switch (Reason)
	{
	case EABTSM3MonthlyWitnessRejectReason::None:
		return TEXT("None");
	case EABTSM3MonthlyWitnessRejectReason::NotEvaluated:
		return TEXT("NotEvaluated");
	case EABTSM3MonthlyWitnessRejectReason::InvalidConfig:
		return TEXT("InvalidConfig");
	case EABTSM3MonthlyWitnessRejectReason::InvalidSource:
		return TEXT("InvalidSource");
	case EABTSM3MonthlyWitnessRejectReason::ProviderUnavailable:
		return TEXT("ProviderUnavailable");
	case EABTSM3MonthlyWitnessRejectReason::ProviderIdentityMismatch:
		return TEXT("ProviderIdentityMismatch");
	case EABTSM3MonthlyWitnessRejectReason::ProfileCatalogMismatch:
		return TEXT("ProfileCatalogMismatch");
	case EABTSM3MonthlyWitnessRejectReason::CandidateJoinMismatch:
		return TEXT("CandidateJoinMismatch");
	case EABTSM3MonthlyWitnessRejectReason::ProfileMismatch:
		return TEXT("ProfileMismatch");
	case EABTSM3MonthlyWitnessRejectReason::GeometryInvalid:
		return TEXT("GeometryInvalid");
	case EABTSM3MonthlyWitnessRejectReason::SearchBudgetExceeded:
		return TEXT("SearchBudgetExceeded");
	case EABTSM3MonthlyWitnessRejectReason::PositiveWitnessNotFound:
		return TEXT("PositiveWitnessNotFound");
	case EABTSM3MonthlyWitnessRejectReason::PriorDomainIncomplete:
		return TEXT("PriorDomainIncomplete");
	case EABTSM3MonthlyWitnessRejectReason::M9EvidenceMissing:
		return TEXT("M9EvidenceMissing");
	case EABTSM3MonthlyWitnessRejectReason::ProgressionInvalid:
		return TEXT("ProgressionInvalid");
	case EABTSM3MonthlyWitnessRejectReason::BridgeEvidenceInvalid:
		return TEXT("BridgeEvidenceInvalid");
	case EABTSM3MonthlyWitnessRejectReason::HashMismatch:
		return TEXT("HashMismatch");
	case EABTSM3MonthlyWitnessRejectReason::NoAcceptedCandidate:
		return TEXT("NoAcceptedCandidate");
	default:
		return TEXT("Unknown");
	}
}
