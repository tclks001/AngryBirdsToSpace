// Copyright Epic Games, Inc. All Rights Reserved.

#include "Calibration/ABTSSlingshotSatelliteCalibrationTypes.h"

#include "Algo/Sort.h"
#include "Physics/ABTSSweptCollision.h"

namespace ABTSSlingshotCalibrationPrivate
{
	constexpr uint64 FnvOffsetBasis64 = 14695981039346656037ull;
	constexpr uint64 FnvPrime64 = 1099511628211ull;

	void AppendHash(uint64& InOutHash, const int64 Value)
	{
		uint64 Bits = static_cast<uint64>(Value);
		for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
		{
			InOutHash ^= (Bits >> (ByteIndex * 8)) & 0xffull;
			InOutHash *= FnvPrime64;
		}
	}

	void AppendStringHash(uint64& InOutHash, const FString& Value)
	{
		const FTCHARToUTF8 Utf8(*Value);
		AppendHash(InOutHash, Utf8.Length());
		for (int32 ByteIndex = 0; ByteIndex < Utf8.Length(); ++ByteIndex)
		{
			InOutHash ^= static_cast<uint8>(Utf8.Get()[ByteIndex]);
			InOutHash *= FnvPrime64;
		}
	}

	int64 Quantize(const double Value, const double Scale = 1000.0)
	{
		return FMath::RoundToInt64(Value * Scale);
	}

	bool IsFiniteProfile(const FABTSM6LaunchProfile& Profile)
	{
		return FMath::IsFinite(Profile.MinimumSpeedCMPerSec)
			&& FMath::IsFinite(Profile.MaximumSpeedCMPerSec)
			&& FMath::IsFinite(Profile.PowerExponent)
			&& FMath::IsFinite(Profile.MinimumPullDistanceCM)
			&& FMath::IsFinite(Profile.MaximumPullDistanceCM)
			&& FMath::IsFinite(Profile.InitialPullAlpha)
			&& FMath::IsFinite(Profile.PullPowerWheelStep)
			&& FMath::IsFinite(Profile.AimSensitivityScale)
			&& FMath::IsFinite(Profile.MaximumAimPlaneOffsetCM)
			&& FMath::IsFinite(Profile.ComfortablePullMinimum)
			&& FMath::IsFinite(Profile.ComfortablePullMaximum);
	}

	float SampleRange(const float Minimum, const float Maximum, const int32 Index, const int32 Count)
	{
		return Count <= 1
			? (Minimum + Maximum) * 0.5f
			: FMath::Lerp(Minimum, Maximum, static_cast<float>(Index) / static_cast<float>(Count - 1));
	}

	bool IsValidLaunchFrame(const FABTSM6CalibrationLaunchFrame& Frame)
	{
		return !Frame.SlingCenterWorld.ContainsNaN()
			&& !Frame.RestPouchWorldLocation.ContainsNaN()
			&& !Frame.SlingUpWorld.ContainsNaN()
			&& !Frame.SlingForwardWorld.ContainsNaN()
			&& !Frame.SlingRightWorld.ContainsNaN()
			&& !Frame.AimPlaneNormalWorld.ContainsNaN()
			&& !Frame.AimInPlaneAxisWorld.ContainsNaN()
			&& !Frame.AimOutOfPlaneAxisWorld.ContainsNaN()
			&& !Frame.SlingUpWorld.IsNearlyZero()
			&& !Frame.SlingForwardWorld.IsNearlyZero()
			&& !Frame.SlingRightWorld.IsNearlyZero()
			&& !Frame.AimPlaneNormalWorld.IsNearlyZero()
			&& !Frame.AimInPlaneAxisWorld.IsNearlyZero()
			&& !Frame.AimOutOfPlaneAxisWorld.IsNearlyZero()
			&& FMath::IsFinite(Frame.BirdInPouchOffsetCM);
	}

	FQuat MakePulledPouchRotation(
		const FVector& LaunchDirection,
		const FVector& PreferredRight)
	{
		const FVector PouchForwardZ = LaunchDirection.GetSafeNormal();
		FVector PouchSideY =
			FVector::VectorPlaneProject(
				PreferredRight,
				PouchForwardZ).GetSafeNormal();
		if (PouchSideY.IsNearlyZero())
		{
			const FVector FallbackAxis =
				FMath::Abs(PouchForwardZ.Z) < 0.9f
					? FVector::UpVector
					: FVector::ForwardVector;
			PouchSideY =
				FVector::CrossProduct(
					PouchForwardZ,
					FallbackAxis).GetSafeNormal();
		}
		return FRotationMatrix::MakeFromYZ(
			PouchSideY,
			PouchForwardZ).ToQuat();
	}

	void BuildReachablePullSamples(
		const FABTSM6LaunchProfile& Profile,
		TArray<float>& OutPullSamples)
	{
		OutPullSamples.Reset();
		OutPullSamples.Add(0.0f);
		OutPullSamples.Add(1.0f);
		const float Step = Profile.PullPowerWheelStep;
		for (int32 Notch = -1000; Notch <= 1000; ++Notch)
		{
			const float Pull = Profile.InitialPullAlpha + Step * static_cast<float>(Notch);
			if (Pull < -KINDA_SMALL_NUMBER || Pull > 1.0f + KINDA_SMALL_NUMBER)
			{
				continue;
			}
			OutPullSamples.AddUnique(FMath::Clamp(Pull, 0.0f, 1.0f));
		}
		OutPullSamples.Sort();
	}

	int32 FlattenSweepIndex(
		const int32 PullIndex,
		const int32 VIndex,
		const int32 UIndex,
		const int32 VCount,
		const int32 UCount)
	{
		return (PullIndex * VCount + VIndex) * UCount + UIndex;
	}
}

FABTSM6LaunchProfileCatalog
FABTSSlingshotSatelliteCalibrationModel::MakeFrozenLaunchProfileCatalogV0()
{
	FABTSM6LaunchProfileCatalog Catalog;
	Catalog.Version = 1;
	Catalog.FlightAirDragPerSecond = 0.08f;
	Catalog.AimCameraDistanceCM = 1500.0f;
	Catalog.AimCameraPitchDegrees = -3.0f;
	Catalog.AimTargetForwardDistanceCM = 900.0f;
	Catalog.AimTargetHeightCM = 245.0f;
	const auto AddProfile = [&Catalog](
		const EABTSSlingshotTier Tier,
		const float MinimumSpeed,
		const float MaximumSpeed,
		const float PowerExponent,
		const float PullPowerWheelStep)
	{
		FABTSM6LaunchProfile& Profile = Catalog.Profiles.AddDefaulted_GetRef();
		Profile.Tier = Tier;
		Profile.MinimumSpeedCMPerSec = MinimumSpeed;
		Profile.MaximumSpeedCMPerSec = MaximumSpeed;
		Profile.PowerExponent = PowerExponent;
		Profile.MinimumPullDistanceCM = 120.0f;
		Profile.MaximumPullDistanceCM = 430.0f;
		Profile.InitialPullAlpha = 0.55f;
		Profile.PullPowerWheelStep = PullPowerWheelStep;
		Profile.AimSensitivityScale = 1.0f;
		Profile.MaximumAimPlaneOffsetCM = 260.0f;
		Profile.ComfortablePullMinimum = 0.60f;
		Profile.ComfortablePullMaximum = 0.85f;
	};
	AddProfile(EABTSSlingshotTier::Twig, 700.0f, 1700.0f, 1.15f, 0.04f);
	AddProfile(EABTSSlingshotTier::Simple, 900.0f, 2300.0f, 1.08f, 0.02f);
	AddProfile(EABTSSlingshotTier::Reinforced, 1050.0f, 3300.0f, 1.00f, 0.01f);
	return Catalog;
}

FABTSSatellitePracticePreset
FABTSSlingshotSatelliteCalibrationModel::MakeFrozenSatellitePracticePresetV0()
{
	FABTSSatellitePracticePreset Preset;
	Preset.Version = 2;
	Preset.SatelliteRadiusPrimaryRatio = 0.125f;
	Preset.SatelliteAnchorArcDegrees = 30.0f;
	Preset.SatelliteAnchorAzimuthDegrees = 0.0f;
	Preset.SatelliteCenterClearancePrimaryRatio = 0.55f;
	Preset.SatelliteSurfaceGravityPrimaryRatio = 2.0f;
	Preset.TargetBody = TEXT("PracticeSatellite");
	Preset.BacksideAngleDeg = 178.0f;
	Preset.TargetLocalAzimuthDeg = 20.0f;
	Preset.TargetProxyRadiusCM = 420.0f;
	Preset.BirdCollisionRadiusCM = 42.0f;
	Preset.TargetSatelliteClearanceCM = 20.0f;
	Preset.RangeTargetProxyRadiusCM = 150.0f;
	Preset.AimInPlaneMinimumCM = -260.0f;
	Preset.AimInPlaneMaximumCM = 260.0f;
	Preset.AimInPlaneSampleCount = 41;
	Preset.AimOutOfPlaneMinimumCM = -80.0f;
	Preset.AimOutOfPlaneMaximumCM = 80.0f;
	Preset.AimOutOfPlaneSampleCount = 5;
	Preset.PullMinimum = 0.75f;
	Preset.PullMaximum = 1.0f;
	Preset.IntegrationStepSeconds = 0.04f;
	Preset.MaximumFlightSeconds = 30.0f;
	Preset.MinimumSuccessIslandSamples = 3;
	Preset.GravityOffMinimumMissCM = 60.0f;
	return Preset;
}

FABTSM6LaunchProfileCatalog
FABTSSlingshotSatelliteCalibrationModel::MakeCandidateCatalogV0()
{
	return MakeFrozenLaunchProfileCatalogV0();
}

FABTSSatellitePracticePreset
FABTSSlingshotSatelliteCalibrationModel::MakeCandidatePracticePresetV0()
{
	return MakeFrozenSatellitePracticePresetV0();
}

bool FABTSSlingshotSatelliteCalibrationModel::ResolveCatalog(
	const FABTSM6LaunchProfileCatalog& Source,
	FABTSM6LaunchProfileCatalog& OutResolved,
	FString* OutFailureReason)
{
	OutResolved = FABTSM6LaunchProfileCatalog();
	const auto Fail = [OutFailureReason](const TCHAR* Reason)
	{
		if (OutFailureReason) *OutFailureReason = Reason;
		return false;
	};
	if (Source.Version <= 0 || !FMath::IsFinite(Source.FlightAirDragPerSecond)
		|| !FMath::IsFinite(Source.AimCameraDistanceCM)
		|| !FMath::IsFinite(Source.AimCameraPitchDegrees)
		|| !FMath::IsFinite(Source.AimTargetForwardDistanceCM)
		|| !FMath::IsFinite(Source.AimTargetHeightCM)
		|| Source.FlightAirDragPerSecond < 0.0f
		|| Source.AimCameraDistanceCM < 100.0f
		|| Source.AimCameraPitchDegrees < -10.0f
		|| Source.AimCameraPitchDegrees > 75.0f
		|| Source.AimTargetForwardDistanceCM < 0.0f
		|| Source.Profiles.Num() != 3)
	{
		return Fail(TEXT("Catalog header or profile count is invalid."));
	}
	OutResolved = Source;
	OutResolved.Profiles.Sort([](const FABTSM6LaunchProfile& A, const FABTSM6LaunchProfile& B)
	{
		return static_cast<uint8>(A.Tier) < static_cast<uint8>(B.Tier);
	});
	const EABTSSlingshotTier ExpectedTiers[] =
	{
		EABTSSlingshotTier::Twig,
		EABTSSlingshotTier::Simple,
		EABTSSlingshotTier::Reinforced
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ExpectedTiers); ++Index)
	{
		const FABTSM6LaunchProfile& Profile = OutResolved.Profiles[Index];
		if (Profile.Tier != ExpectedTiers[Index] || !ABTSSlingshotCalibrationPrivate::IsFiniteProfile(Profile))
		{
			return Fail(TEXT("Catalog tiers are missing, duplicated or non-finite."));
		}
		if (Profile.MinimumSpeedCMPerSec < 100.0f
			|| Profile.MaximumSpeedCMPerSec <= Profile.MinimumSpeedCMPerSec
			|| Profile.PowerExponent <= 0.0f
			|| Profile.MinimumPullDistanceCM < 10.0f
			|| Profile.MaximumPullDistanceCM <= Profile.MinimumPullDistanceCM
			|| Profile.InitialPullAlpha < 0.0f
			|| Profile.InitialPullAlpha > 1.0f
			|| Profile.PullPowerWheelStep < 0.01f
			|| Profile.PullPowerWheelStep > 1.0f
			|| Profile.AimSensitivityScale <= 0.0f
			|| Profile.MaximumAimPlaneOffsetCM < 20.0f
			|| Profile.ComfortablePullMinimum < 0.0f
			|| Profile.ComfortablePullMaximum > 1.0f
			|| Profile.ComfortablePullMinimum >= Profile.ComfortablePullMaximum)
		{
			return Fail(TEXT("Catalog contains an out-of-domain profile value."));
		}
	}
	if (OutFailureReason) OutFailureReason->Reset();
	return true;
}

const FABTSM6LaunchProfile* FABTSSlingshotSatelliteCalibrationModel::FindProfile(
	const FABTSM6LaunchProfileCatalog& ResolvedCatalog,
	const EABTSSlingshotTier Tier)
{
	return ResolvedCatalog.Profiles.FindByPredicate([Tier](const FABTSM6LaunchProfile& Profile)
	{
		return Profile.Tier == Tier;
	});
}

float FABTSSlingshotSatelliteCalibrationModel::EvaluateLaunchSpeed(
	const FABTSM6LaunchProfile& Profile,
	const float PullAlpha)
{
	const float CurveAlpha = FMath::Pow(FMath::Clamp(PullAlpha, 0.0f, 1.0f), Profile.PowerExponent);
	return FMath::Lerp(Profile.MinimumSpeedCMPerSec, Profile.MaximumSpeedCMPerSec, CurveAlpha);
}

bool FABTSSlingshotSatelliteCalibrationModel::BuildM6LaunchSample(
	const FABTSM6CalibrationLaunchFrame& LaunchFrame,
	const FABTSM6LaunchProfile& Profile,
	const float AimInPlaneOffsetCM,
	const float AimOutOfPlaneOffsetCM,
	const float PullAlpha,
	FVector& OutBirdWorldLocation,
	FVector& OutInitialWorldVelocity)
{
	using namespace ABTSSlingshotCalibrationPrivate;
	OutBirdWorldLocation = FVector::ZeroVector;
	OutInitialWorldVelocity = FVector::ZeroVector;
	if (!IsValidLaunchFrame(LaunchFrame)
		|| !IsFiniteProfile(Profile)
		|| !FMath::IsFinite(AimInPlaneOffsetCM)
		|| !FMath::IsFinite(AimOutOfPlaneOffsetCM)
		|| !FMath::IsFinite(PullAlpha))
	{
		return false;
	}
	const FVector Up = LaunchFrame.SlingUpWorld.GetSafeNormal();
	const FVector Forward = LaunchFrame.SlingForwardWorld.GetSafeNormal();
	const FVector Right = LaunchFrame.SlingRightWorld.GetSafeNormal();
	FVector AimPlaneOffset =
		LaunchFrame.AimInPlaneAxisWorld.GetSafeNormal()
			* AimInPlaneOffsetCM
		+ LaunchFrame.AimOutOfPlaneAxisWorld.GetSafeNormal()
			* AimOutOfPlaneOffsetCM;
	AimPlaneOffset =
		AimPlaneOffset.GetClampedToMaxSize(
			FMath::Max(20.0f, Profile.MaximumAimPlaneOffsetCM));
	const float PullDistance = FMath::Lerp(
		Profile.MinimumPullDistanceCM,
		Profile.MaximumPullDistanceCM,
		FMath::Clamp(PullAlpha, 0.0f, 1.0f));
	const FVector PouchLocation =
		LaunchFrame.RestPouchWorldLocation
		+ AimPlaneOffset
		- Forward * PullDistance;
	const FVector Direction =
		(LaunchFrame.SlingCenterWorld + Up * 65.0f - PouchLocation)
			.GetSafeNormal();
	if (Direction.IsNearlyZero()) return false;
	const FQuat PouchRotation =
		MakePulledPouchRotation(Direction, Right);
	OutBirdWorldLocation =
		PouchLocation
		+ PouchRotation.RotateVector(
			FVector(0.0f, 0.0f, LaunchFrame.BirdInPouchOffsetCM));
	OutInitialWorldVelocity =
		Direction * EvaluateLaunchSpeed(Profile, PullAlpha);
	return true;
}

uint64 FABTSSlingshotSatelliteCalibrationModel::ComputeLaunchProfileHash(
	const FABTSM6LaunchProfileCatalog& ResolvedCatalog)
{
	using namespace ABTSSlingshotCalibrationPrivate;
	uint64 Hash = FnvOffsetBasis64;
	AppendHash(Hash, ResolvedCatalog.Version);
	AppendHash(Hash, Quantize(ResolvedCatalog.FlightAirDragPerSecond));
	AppendHash(Hash, Quantize(ResolvedCatalog.AimCameraDistanceCM));
	AppendHash(Hash, Quantize(ResolvedCatalog.AimCameraPitchDegrees));
	AppendHash(Hash, Quantize(ResolvedCatalog.AimTargetForwardDistanceCM));
	AppendHash(Hash, Quantize(ResolvedCatalog.AimTargetHeightCM));
	for (const FABTSM6LaunchProfile& Profile : ResolvedCatalog.Profiles)
	{
		AppendHash(Hash, static_cast<int64>(Profile.Tier));
		AppendHash(Hash, Quantize(Profile.MinimumSpeedCMPerSec));
		AppendHash(Hash, Quantize(Profile.MaximumSpeedCMPerSec));
		AppendHash(Hash, Quantize(Profile.PowerExponent));
		AppendHash(Hash, Quantize(Profile.MinimumPullDistanceCM));
		AppendHash(Hash, Quantize(Profile.MaximumPullDistanceCM));
		AppendHash(Hash, Quantize(Profile.InitialPullAlpha));
		AppendHash(Hash, Quantize(Profile.PullPowerWheelStep));
		AppendHash(Hash, Quantize(Profile.AimSensitivityScale));
		AppendHash(Hash, Quantize(Profile.MaximumAimPlaneOffsetCM));
		AppendHash(Hash, Quantize(Profile.ComfortablePullMinimum));
		AppendHash(Hash, Quantize(Profile.ComfortablePullMaximum));
	}
	return Hash;
}

FABTSM6ReachEnvelope FABTSSlingshotSatelliteCalibrationModel::EstimateReachEnvelope(
	const FABTSM6LaunchProfile& Profile,
	const float PrimaryRadiusCM,
	const float PrimarySurfaceGravityCMPerSec2,
	const float FlightAirDragPerSecond,
	const float BirdCollisionRadiusCM)
{
	FABTSM6ReachEnvelope Envelope;
	Envelope.Tier = Profile.Tier;
	const float RadiusCM = FMath::Max(1000.0f, PrimaryRadiusCM);
	const float SurfaceGravity = FMath::Max(1.0f, PrimarySurfaceGravityCMPerSec2);
	const float Mu = SurfaceGravity * FMath::Square(RadiusCM);
	const float StepSeconds = 0.04f;
	const int32 MaximumSteps = FMath::CeilToInt(20.0f / StepSeconds);
	const float BirdRadiusCM = FMath::Max(1.0f, BirdCollisionRadiusCM);
	const FVector Start(RadiusCM + 250.0f, 0.0f, 0.0f);
	const auto MeasureMaximumReach = [&](const float PullAlpha)
	{
		float BestReachCM = 0.0f;
		const float Speed = EvaluateLaunchSpeed(Profile, PullAlpha);
		for (int32 ElevationDegrees = 5; ElevationDegrees <= 80; ++ElevationDegrees)
		{
			const float ElevationRadians = FMath::DegreesToRadians(static_cast<float>(ElevationDegrees));
			FVector Position = Start;
			FVector Velocity(
				FMath::Sin(ElevationRadians) * Speed,
				FMath::Cos(ElevationRadians) * Speed,
				0.0f);
			for (int32 StepIndex = 0; StepIndex < MaximumSteps; ++StepIndex)
			{
				const float Distance = FMath::Max(Position.Size(), 1.0f);
				const FVector Acceleration =
					-Position / Distance * (Mu / FMath::Square(Distance))
					- Velocity * FMath::Max(0.0f, FlightAirDragPerSecond);
				Velocity += Acceleration * StepSeconds;
				const FVector NextPosition = Position + Velocity * StepSeconds;
				if (NextPosition.Size() <= RadiusCM + BirdRadiusCM)
				{
					const float Dot = FMath::Clamp(
						FVector::DotProduct(Start.GetSafeNormal(), NextPosition.GetSafeNormal()),
						-1.0f,
						1.0f);
					BestReachCM = FMath::Max(BestReachCM, FMath::Acos(Dot) * RadiusCM);
					break;
				}
				Position = NextPosition;
			}
		}
		return BestReachCM;
	};
	Envelope.ComfortableReachCM = MeasureMaximumReach(Profile.ComfortablePullMaximum);
	Envelope.MaximumReachCM = MeasureMaximumReach(1.0f);
	Envelope.ComfortableReachPrimaryRadiusRatio = Envelope.ComfortableReachCM / RadiusCM;
	Envelope.MaximumReachPrimaryRadiusRatio = Envelope.MaximumReachCM / RadiusCM;
	return Envelope;
}

uint64 FABTSSlingshotSatelliteCalibrationModel::ComputeSatellitePracticePresetHash(
	const FABTSSatellitePracticePreset& Preset)
{
	using namespace ABTSSlingshotCalibrationPrivate;
	uint64 Hash = FnvOffsetBasis64;
	AppendHash(Hash, Preset.Version);
	AppendHash(Hash, Quantize(Preset.SatelliteRadiusPrimaryRatio));
	AppendHash(Hash, Quantize(Preset.SatelliteAnchorArcDegrees));
	AppendHash(Hash, Quantize(Preset.SatelliteAnchorAzimuthDegrees));
	AppendHash(Hash, Quantize(Preset.SatelliteCenterClearancePrimaryRatio));
	AppendHash(Hash, Quantize(Preset.SatelliteSurfaceGravityPrimaryRatio));
	AppendStringHash(Hash, Preset.TargetBody.ToString());
	AppendHash(Hash, Quantize(Preset.BacksideAngleDeg));
	AppendHash(Hash, Quantize(Preset.TargetLocalAzimuthDeg));
	AppendHash(Hash, Quantize(Preset.TargetProxyRadiusCM));
	AppendHash(Hash, Quantize(Preset.BirdCollisionRadiusCM));
	AppendHash(Hash, Quantize(Preset.TargetSatelliteClearanceCM));
	AppendHash(Hash, Quantize(Preset.RangeTargetProxyRadiusCM));
	AppendHash(Hash, Quantize(Preset.AimInPlaneMinimumCM));
	AppendHash(Hash, Quantize(Preset.AimInPlaneMaximumCM));
	AppendHash(Hash, Preset.AimInPlaneSampleCount);
	AppendHash(Hash, Quantize(Preset.AimOutOfPlaneMinimumCM));
	AppendHash(Hash, Quantize(Preset.AimOutOfPlaneMaximumCM));
	AppendHash(Hash, Preset.AimOutOfPlaneSampleCount);
	AppendHash(Hash, Quantize(Preset.PullMinimum));
	AppendHash(Hash, Quantize(Preset.PullMaximum));
	AppendHash(Hash, Quantize(Preset.IntegrationStepSeconds));
	AppendHash(Hash, Quantize(Preset.MaximumFlightSeconds));
	AppendHash(Hash, Preset.MinimumSuccessIslandSamples);
	AppendHash(Hash, Quantize(Preset.GravityOffMinimumMissCM));
	return Hash;
}

uint64 FABTSSlingshotSatelliteCalibrationModel::ComputeGravitySnapshotHash(
	const FABTSCalibrationGravitySnapshot& Snapshot)
{
	using namespace ABTSSlingshotCalibrationPrivate;
	uint64 Hash = FnvOffsetBasis64;
	const FVector RelativeSatellite =
		Snapshot.SatelliteCenterWorld - Snapshot.PrimaryCenterWorld;
	AppendHash(Hash, Quantize(Snapshot.PrimaryRadiusCM));
	AppendHash(Hash, Quantize(Snapshot.PrimarySurfaceGravityCMPerSec2));
	AppendHash(Hash, Quantize(RelativeSatellite.X));
	AppendHash(Hash, Quantize(RelativeSatellite.Y));
	AppendHash(Hash, Quantize(RelativeSatellite.Z));
	AppendHash(Hash, Quantize(Snapshot.SatelliteRadiusCM));
	AppendHash(Hash, Quantize(Snapshot.SatelliteSurfaceGravityCMPerSec2));
	AppendHash(Hash, Quantize(Snapshot.FlightAirDragPerSecond));
	AppendHash(Hash, Snapshot.bSatelliteGravityEnabled ? 1 : 0);
	return Hash;
}

bool FABTSSlingshotSatelliteCalibrationModel::BuildSatelliteTargetWorldLocation(
	const FVector& LaunchWorldLocation,
	const FABTSCalibrationGravitySnapshot& Snapshot,
	const FABTSSatellitePracticePreset& Preset,
	FVector& OutTargetWorldLocation,
	FString* OutFailureReason)
{
	FTransform TargetTransform;
	if (!BuildSatelliteTargetWorldTransform(
		LaunchWorldLocation,
		Snapshot,
		Preset,
		TargetTransform,
		OutFailureReason))
	{
		return false;
	}
	OutTargetWorldLocation = TargetTransform.GetLocation();
	return true;
}

bool FABTSSlingshotSatelliteCalibrationModel::BuildSatelliteTargetWorldTransform(
	const FVector& LaunchWorldLocation,
	const FABTSCalibrationGravitySnapshot& Snapshot,
	const FABTSSatellitePracticePreset& Preset,
	FTransform& OutTargetWorldTransform,
	FString* OutFailureReason)
{
	const auto Fail = [OutFailureReason](const TCHAR* Reason)
	{
		if (OutFailureReason) *OutFailureReason = Reason;
		return false;
	};
	if (Preset.TargetBody != TEXT("PracticeSatellite"))
	{
		return Fail(TEXT("TargetBody does not resolve to the practice satellite."));
	}
	if (!FMath::IsFinite(Preset.BacksideAngleDeg)
		|| !FMath::IsFinite(Preset.TargetLocalAzimuthDeg)
		|| !FMath::IsFinite(Preset.TargetProxyRadiusCM)
		|| !FMath::IsFinite(Preset.TargetSatelliteClearanceCM)
		|| Preset.BacksideAngleDeg <= 90.0f
		|| Preset.BacksideAngleDeg >= 180.0f
		|| Preset.TargetProxyRadiusCM <= 0.0f
		|| Preset.TargetSatelliteClearanceCM < 0.0f
		|| Snapshot.SatelliteRadiusCM <= 0.0f)
	{
		return Fail(TEXT("Backside E5 surface frame is invalid."));
	}
	const FVector FacingLaunch =
		(LaunchWorldLocation - Snapshot.SatelliteCenterWorld).GetSafeNormal();
	if (FacingLaunch.IsNearlyZero()) return Fail(TEXT("Launch origin and satellite centre coincide."));
	FVector ReferenceUp = (Snapshot.SatelliteCenterWorld - Snapshot.PrimaryCenterWorld).GetSafeNormal();
	ReferenceUp = FVector::VectorPlaneProject(ReferenceUp, FacingLaunch).GetSafeNormal();
	if (ReferenceUp.IsNearlyZero())
	{
		ReferenceUp = FVector::VectorPlaneProject(
			FMath::Abs(FacingLaunch.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector,
			FacingLaunch).GetSafeNormal();
	}
	const FVector ReferenceRight = FVector::CrossProduct(ReferenceUp, FacingLaunch).GetSafeNormal();
	const float AzimuthRadians = FMath::DegreesToRadians(Preset.TargetLocalAzimuthDeg);
	const FVector AzimuthTangent =
		ReferenceUp * FMath::Cos(AzimuthRadians)
		+ ReferenceRight * FMath::Sin(AzimuthRadians);
	const float BacksideRadians = FMath::DegreesToRadians(Preset.BacksideAngleDeg);
	const FVector TargetDirection =
		(FacingLaunch * FMath::Cos(BacksideRadians)
			+ AzimuthTangent * FMath::Sin(BacksideRadians)).GetSafeNormal();
	FVector TargetForward =
		FVector::VectorPlaneProject(
			FacingLaunch,
			TargetDirection).GetSafeNormal();
	if (TargetForward.IsNearlyZero())
	{
		TargetForward =
			FVector::VectorPlaneProject(
				ReferenceUp,
				TargetDirection).GetSafeNormal();
	}
	if (TargetForward.IsNearlyZero())
	{
		return Fail(TEXT("Backside E5 tangent frame is degenerate."));
	}
	const FVector TargetCenter =
		Snapshot.SatelliteCenterWorld
		+ TargetDirection
			* (Snapshot.SatelliteRadiusCM
				+ Preset.TargetProxyRadiusCM
				+ Preset.TargetSatelliteClearanceCM);
	OutTargetWorldTransform = FTransform(
		FRotationMatrix::MakeFromXZ(
			TargetForward,
			TargetDirection).ToQuat(),
		TargetCenter);
	if (OutFailureReason) OutFailureReason->Reset();
	return true;
}

FABTSCalibrationTrajectoryResult FABTSSlingshotSatelliteCalibrationModel::IntegrateTrajectory(
	const FABTSCalibrationScenario& Scenario,
	const FVector& InitialWorldVelocity,
	const FABTSSatellitePracticePreset& Preset,
	const bool bSatelliteGravityEnabled)
{
	using namespace ABTSSlingshotCalibrationPrivate;
	FABTSCalibrationTrajectoryResult Result;
	FVector Position = Scenario.LaunchWorldLocation;
	FVector Velocity = InitialWorldVelocity;
	const float StepSeconds = FMath::Clamp(Preset.IntegrationStepSeconds, 0.01f, 0.2f);
	const int32 MaximumSteps = FMath::Max(
		1,
		FMath::CeilToInt(FMath::Clamp(Preset.MaximumFlightSeconds, 2.0f, 60.0f) / StepSeconds));
	const float BirdRadiusCM = FMath::Max(1.0f, Preset.BirdCollisionRadiusCM);
	FTransform TargetTransform = Scenario.TargetWorldTransform;
	TargetTransform.SetLocation(Scenario.TargetWorldLocation);
	TargetTransform.SetScale3D(FVector::OneVector);
	const FVector TargetHalfExtentCM =
		Scenario.TargetHalfExtentCM.GetAbs().ComponentMax(
			FVector(FMath::Max(1.0f, Scenario.TargetProxyRadiusCM)));
	const float SatelliteBodyRadiusCM =
		FMath::Max(1.0f, Scenario.Gravity.SatelliteRadiusCM + BirdRadiusCM);
	const float PrimaryBodyRadiusCM =
		FMath::Max(1.0f, Scenario.Gravity.PrimaryRadiusCM + BirdRadiusCM);
	const float PrimaryMu = FMath::Max(0.0f, Scenario.Gravity.PrimarySurfaceGravityCMPerSec2)
		* FMath::Square(FMath::Max(1.0f, Scenario.Gravity.PrimaryRadiusCM));
	const float SatelliteMu = FMath::Max(0.0f, Scenario.Gravity.SatelliteSurfaceGravityCMPerSec2)
		* FMath::Square(FMath::Max(1.0f, Scenario.Gravity.SatelliteRadiusCM));
	Result.ClosestTargetClearanceCM =
		ABTSSweptCollision::PointExpandedOrientedBoxClearance(
			Position,
			TargetTransform,
			TargetHalfExtentCM,
			BirdRadiusCM);

	for (int32 StepIndex = 0; StepIndex < MaximumSteps; ++StepIndex)
	{
		const FVector ToPrimary = Scenario.Gravity.PrimaryCenterWorld - Position;
		const float PrimaryDistance = FMath::Max(ToPrimary.Size(), 1.0f);
		FVector Acceleration = ToPrimary / PrimaryDistance
			* (PrimaryMu / FMath::Square(PrimaryDistance));
		if (bSatelliteGravityEnabled)
		{
			const FVector ToSatellite = Scenario.Gravity.SatelliteCenterWorld - Position;
			const float SatelliteDistance = FMath::Max(
				ToSatellite.Size(),
				FMath::Max(1.0f, Scenario.Gravity.SatelliteRadiusCM));
			Acceleration += ToSatellite / SatelliteDistance
				* (SatelliteMu / FMath::Square(SatelliteDistance));
		}
		Acceleration -= Velocity * FMath::Max(0.0f, Scenario.Gravity.FlightAirDragPerSecond);
		Velocity += Acceleration * StepSeconds;
		const FVector NextPosition = Position + Velocity * StepSeconds;
		Result.PathLengthCM += FVector::Distance(Position, NextPosition);
		Result.ApexAltitudeAbovePrimaryCM = FMath::Max(
			Result.ApexAltitudeAbovePrimaryCM,
			FVector::Distance(NextPosition, Scenario.Gravity.PrimaryCenterWorld)
				- Scenario.Gravity.PrimaryRadiusCM);
		Result.ClosestTargetClearanceCM = FMath::Min(
			Result.ClosestTargetClearanceCM,
			ABTSSweptCollision::
				SegmentExpandedOrientedBoxMinimumClearance(
					Position,
					NextPosition,
					TargetTransform,
					TargetHalfExtentCM,
					BirdRadiusCM));

		float TargetAlpha = BIG_NUMBER;
		float SatelliteAlpha = BIG_NUMBER;
		float PrimaryAlpha = BIG_NUMBER;
		const bool bTargetHit =
			ABTSSweptCollision::SegmentExpandedOrientedBoxFirstAlpha(
				Position,
				NextPosition,
				TargetTransform,
				TargetHalfExtentCM,
				BirdRadiusCM,
				TargetAlpha);
		const bool bSatelliteHit = ABTSSweptCollision::SegmentSphereFirstAlpha(
			Position, NextPosition, Scenario.Gravity.SatelliteCenterWorld,
			SatelliteBodyRadiusCM, SatelliteAlpha);
		const bool bPrimaryHit = ABTSSweptCollision::SegmentSphereFirstAlpha(
			Position, NextPosition, Scenario.Gravity.PrimaryCenterWorld,
			PrimaryBodyRadiusCM, PrimaryAlpha);
		const float FirstAlpha = FMath::Min3(
			bTargetHit ? TargetAlpha : BIG_NUMBER,
			bSatelliteHit ? SatelliteAlpha : BIG_NUMBER,
			bPrimaryHit ? PrimaryAlpha : BIG_NUMBER);
		if (FirstAlpha < BIG_NUMBER)
		{
			Result.FlightTimeSeconds = (static_cast<float>(StepIndex) + FirstAlpha) * StepSeconds;
			if (bTargetHit && TargetAlpha <= FirstAlpha + KINDA_SMALL_NUMBER)
			{
				Result.Outcome = EABTSCalibrationTrajectoryOutcome::TargetHit;
			}
			else if (bSatelliteHit && SatelliteAlpha <= FirstAlpha + KINDA_SMALL_NUMBER)
			{
				Result.Outcome = EABTSCalibrationTrajectoryOutcome::SatelliteBodyHit;
			}
			else
			{
				Result.Outcome = EABTSCalibrationTrajectoryOutcome::PrimaryBodyHit;
			}
			return Result;
		}
		Position = NextPosition;
	}
	Result.FlightTimeSeconds = static_cast<float>(MaximumSteps) * StepSeconds;
	return Result;
}

FABTSCalibrationSweepSummary FABTSSlingshotSatelliteCalibrationModel::RunSuccessIslandSweep(
	const FABTSCalibrationScenario& Scenario,
	const FABTSM6LaunchProfileCatalog& ResolvedCatalog,
	const FABTSSatellitePracticePreset& Preset)
{
	using namespace ABTSSlingshotCalibrationPrivate;
	FABTSCalibrationSweepSummary Summary;
	const FABTSM6LaunchProfile* Reinforced = FindProfile(
		ResolvedCatalog, EABTSSlingshotTier::Reinforced);
	const FABTSM6LaunchProfile* Simple = FindProfile(
		ResolvedCatalog, EABTSSlingshotTier::Simple);
	if (Reinforced == nullptr
		|| Simple == nullptr
		|| !IsValidLaunchFrame(Scenario.LaunchFrame))
	{
		return Summary;
	}
	const int32 UCount =
		FMath::Clamp(Preset.AimInPlaneSampleCount, 5, 161);
	const int32 VCount =
		FMath::Clamp(Preset.AimOutOfPlaneSampleCount, 1, 31);
	TArray<float> ReachablePulls;
	BuildReachablePullSamples(*Reinforced, ReachablePulls);
	Summary.ReinforcedReachablePullSamples = ReachablePulls.Num();
	TArray<float> CertifiedPulls;
	for (const float Pull : ReachablePulls)
	{
		if (Pull + KINDA_SMALL_NUMBER >= Preset.PullMinimum
			&& Pull <= Preset.PullMaximum + KINDA_SMALL_NUMBER)
		{
			CertifiedPulls.Add(Pull);
		}
	}
	Summary.ReinforcedCertifiedPullSamples = CertifiedPulls.Num();
	const int32 PullCount = CertifiedPulls.Num();
	if (PullCount <= 0) return Summary;
	const int32 TotalCount = UCount * VCount * PullCount;
	TBitArray<> GravityDependentSuccess(false, TotalCount);
	TArray<float> AimInPlaneByIndex;
	AimInPlaneByIndex.SetNumUninitialized(UCount);
	Summary.MinimumGravityOffMissCM = BIG_NUMBER;
	Summary.MinimumGravityOnTargetClearanceCM = BIG_NUMBER;

	for (int32 PullIndex = 0; PullIndex < PullCount; ++PullIndex)
	{
		const float PullAlpha = CertifiedPulls[PullIndex];
		for (int32 VIndex = 0; VIndex < VCount; ++VIndex)
		{
			const float AimOutOfPlaneCM = SampleRange(
				Preset.AimOutOfPlaneMinimumCM,
				Preset.AimOutOfPlaneMaximumCM,
				VIndex,
				VCount);
			for (int32 UIndex = 0; UIndex < UCount; ++UIndex)
			{
				const float AimInPlaneCM = SampleRange(
					Preset.AimInPlaneMinimumCM,
					Preset.AimInPlaneMaximumCM,
					UIndex,
					UCount);
				AimInPlaneByIndex[UIndex] = AimInPlaneCM;
				if (FVector2D(
					AimInPlaneCM,
					AimOutOfPlaneCM).Size()
					> Reinforced->MaximumAimPlaneOffsetCM
						+ KINDA_SMALL_NUMBER)
				{
					continue;
				}
				FVector BirdWorldLocation;
				FVector InitialWorldVelocity;
				if (!BuildM6LaunchSample(
					Scenario.LaunchFrame,
					*Reinforced,
					AimInPlaneCM,
					AimOutOfPlaneCM,
					PullAlpha,
					BirdWorldLocation,
					InitialWorldVelocity))
				{
					continue;
				}
				++Summary.ReinforcedSampleCount;
				FABTSCalibrationScenario SampleScenario = Scenario;
				SampleScenario.LaunchWorldLocation = BirdWorldLocation;
				const FABTSCalibrationTrajectoryResult GravityOn = IntegrateTrajectory(
					SampleScenario,
					InitialWorldVelocity,
					Preset,
					true);
				if (GravityOn.ClosestTargetClearanceCM
					< Summary.MinimumGravityOnTargetClearanceCM)
				{
					Summary.MinimumGravityOnTargetClearanceCM =
						GravityOn.ClosestTargetClearanceCM;
					Summary.BestGravityOnAimInPlaneCM = AimInPlaneCM;
					Summary.BestGravityOnAimOutOfPlaneCM = AimOutOfPlaneCM;
					Summary.BestGravityOnPullAlpha = PullAlpha;
				}
				switch (GravityOn.Outcome)
				{
				case EABTSCalibrationTrajectoryOutcome::SatelliteBodyHit:
					++Summary.ReinforcedSatelliteBodyHits;
					break;
				case EABTSCalibrationTrajectoryOutcome::PrimaryBodyHit:
					++Summary.ReinforcedPrimaryBodyHits;
					break;
				case EABTSCalibrationTrajectoryOutcome::Timeout:
					++Summary.ReinforcedTimeouts;
					break;
				default:
					break;
				}
				if (GravityOn.Outcome != EABTSCalibrationTrajectoryOutcome::TargetHit) continue;
				++Summary.ReinforcedGravityOnHits;
				const FABTSCalibrationTrajectoryResult GravityOff = IntegrateTrajectory(
					SampleScenario,
					InitialWorldVelocity,
					Preset,
					false);
				if (GravityOff.Outcome == EABTSCalibrationTrajectoryOutcome::TargetHit
					|| GravityOff.ClosestTargetClearanceCM + KINDA_SMALL_NUMBER
						< Preset.GravityOffMinimumMissCM)
				{
					continue;
				}
				Summary.MinimumGravityOffMissCM = FMath::Min(
					Summary.MinimumGravityOffMissCM,
					GravityOff.ClosestTargetClearanceCM);
				const int32 FlatIndex = FlattenSweepIndex(
					PullIndex, VIndex, UIndex, VCount, UCount);
				GravityDependentSuccess[FlatIndex] = true;
				++Summary.GravityDependentHits;
			}
		}
	}

	TBitArray<> Remaining = GravityDependentSuccess;
	TArray<int32> Stack;
	TArray<int32> Component;
	TArray<int32> LargestComponent;
	for (TConstSetBitIterator<> It(Remaining); It; ++It)
	{
		const int32 SeedIndex = It.GetIndex();
		if (!Remaining[SeedIndex]) continue;
		Remaining[SeedIndex] = false;
		Stack.Reset();
		Component.Reset();
		Stack.Add(SeedIndex);
		while (!Stack.IsEmpty())
		{
			const int32 FlatIndex = Stack.Pop(EAllowShrinking::No);
			Component.Add(FlatIndex);
			const int32 UIndex = FlatIndex % UCount;
			const int32 PullAndV = FlatIndex / UCount;
			const int32 VIndex = PullAndV % VCount;
			const int32 PullIndex = PullAndV / VCount;
			const int32 NeighborCoordinates[][3] =
			{
				{PullIndex - 1, VIndex, UIndex},
				{PullIndex + 1, VIndex, UIndex},
				{PullIndex, VIndex - 1, UIndex},
				{PullIndex, VIndex + 1, UIndex},
				{PullIndex, VIndex, UIndex - 1},
				{PullIndex, VIndex, UIndex + 1}
			};
			for (const int32* Neighbor : NeighborCoordinates)
			{
				if (Neighbor[0] < 0 || Neighbor[0] >= PullCount
					|| Neighbor[1] < 0 || Neighbor[1] >= VCount
					|| Neighbor[2] < 0 || Neighbor[2] >= UCount)
				{
					continue;
				}
				const int32 NeighborIndex = FlattenSweepIndex(
					Neighbor[0], Neighbor[1], Neighbor[2], VCount, UCount);
				if (!Remaining[NeighborIndex]) continue;
				Remaining[NeighborIndex] = false;
				Stack.Add(NeighborIndex);
			}
		}
		if (Component.Num() > LargestComponent.Num())
		{
			LargestComponent = Component;
		}
	}

	Summary.LargestSuccessIslandSamples = LargestComponent.Num();
	if (!LargestComponent.IsEmpty())
	{
		int32 MinPullIndex = MAX_int32;
		int32 MaxPullIndex = MIN_int32;
		int32 MinUIndex = MAX_int32;
		int32 MaxUIndex = MIN_int32;
		for (const int32 FlatIndex : LargestComponent)
		{
			const int32 UIndex = FlatIndex % UCount;
			const int32 PullAndV = FlatIndex / UCount;
			const int32 PullIndex = PullAndV / VCount;
			MinPullIndex = FMath::Min(MinPullIndex, PullIndex);
			MaxPullIndex = FMath::Max(MaxPullIndex, PullIndex);
			MinUIndex = FMath::Min(MinUIndex, UIndex);
			MaxUIndex = FMath::Max(MaxUIndex, UIndex);
		}
		Summary.SuccessPullMinimum = CertifiedPulls[MinPullIndex];
		Summary.SuccessPullMaximum = CertifiedPulls[MaxPullIndex];
		Summary.SuccessAimInPlaneMinimumCM =
			AimInPlaneByIndex[MinUIndex];
		Summary.SuccessAimInPlaneMaximumCM =
			AimInPlaneByIndex[MaxUIndex];
		Summary.bIslandSpansPullNeighbors = MaxPullIndex > MinPullIndex;
		Summary.bIslandSpansAimNeighbors = MaxUIndex > MinUIndex;
	}

	for (int32 VIndex = 0; VIndex < VCount; ++VIndex)
	{
		const float AimOutOfPlaneCM = SampleRange(
			Preset.AimOutOfPlaneMinimumCM,
			Preset.AimOutOfPlaneMaximumCM,
			VIndex,
			VCount);
		for (int32 UIndex = 0; UIndex < UCount; ++UIndex)
		{
			const float AimInPlaneCM = SampleRange(
				Preset.AimInPlaneMinimumCM,
				Preset.AimInPlaneMaximumCM,
				UIndex,
				UCount);
			if (FVector2D(
				AimInPlaneCM,
				AimOutOfPlaneCM).Size()
				> Simple->MaximumAimPlaneOffsetCM + KINDA_SMALL_NUMBER)
			{
				continue;
			}
			FVector BirdWorldLocation;
			FVector InitialWorldVelocity;
			if (!BuildM6LaunchSample(
				Scenario.LaunchFrame,
				*Simple,
				AimInPlaneCM,
				AimOutOfPlaneCM,
				1.0f,
				BirdWorldLocation,
				InitialWorldVelocity))
			{
				continue;
			}
			FABTSCalibrationScenario SampleScenario = Scenario;
			SampleScenario.LaunchWorldLocation = BirdWorldLocation;
			if (IntegrateTrajectory(
				SampleScenario,
				InitialWorldVelocity,
				Preset,
				true).Outcome
				== EABTSCalibrationTrajectoryOutcome::TargetHit)
			{
				++Summary.SimpleFullPowerHits;
			}
		}
	}

	for (const float PullAlpha : ReachablePulls)
	{
		if (PullAlpha + KINDA_SMALL_NUMBER >= Preset.PullMinimum
			&& PullAlpha <= Preset.PullMaximum + KINDA_SMALL_NUMBER)
		{
			continue;
		}
		for (int32 VIndex = 0; VIndex < VCount; ++VIndex)
		{
			const float AimOutOfPlaneCM = SampleRange(
				Preset.AimOutOfPlaneMinimumCM,
				Preset.AimOutOfPlaneMaximumCM,
				VIndex,
				VCount);
			for (int32 UIndex = 0; UIndex < UCount; ++UIndex)
			{
				const float AimInPlaneCM = SampleRange(
					Preset.AimInPlaneMinimumCM,
					Preset.AimInPlaneMaximumCM,
					UIndex,
					UCount);
				if (FVector2D(
					AimInPlaneCM,
					AimOutOfPlaneCM).Size()
					> Reinforced->MaximumAimPlaneOffsetCM
						+ KINDA_SMALL_NUMBER)
				{
					continue;
				}
				FVector BirdWorldLocation;
				FVector InitialWorldVelocity;
				if (!BuildM6LaunchSample(
					Scenario.LaunchFrame,
					*Reinforced,
					AimInPlaneCM,
					AimOutOfPlaneCM,
					PullAlpha,
					BirdWorldLocation,
					InitialWorldVelocity))
				{
					continue;
				}
				FABTSCalibrationScenario SampleScenario = Scenario;
				SampleScenario.LaunchWorldLocation = BirdWorldLocation;
				if (IntegrateTrajectory(
					SampleScenario,
					InitialWorldVelocity,
					Preset,
					true).Outcome
					== EABTSCalibrationTrajectoryOutcome::TargetHit)
				{
					++Summary.ReinforcedOutsideCertifiedPullHits;
					// Preserve a few actionable witnesses without allowing a
					// deliberately bad candidate to amplify this sweep into
					// thousands of warning lines. The summary retains the total.
					if (Summary.ReinforcedOutsideCertifiedPullHits <= 8)
					{
						UE_LOG(
							LogABTSRuntime,
							Warning,
							TEXT("[ABTS][Calibration][OutsidePullHit] Witness=%d Pull=%.3f AimInPlaneCM=%.1f AimOutOfPlaneCM=%.1f"),
							Summary.ReinforcedOutsideCertifiedPullHits,
							PullAlpha,
							AimInPlaneCM,
							AimOutOfPlaneCM);
					}
				}
			}
		}
	}
	if (Summary.MinimumGravityOffMissCM == BIG_NUMBER)
	{
		Summary.MinimumGravityOffMissCM = 0.0f;
	}
	if (Summary.MinimumGravityOnTargetClearanceCM == BIG_NUMBER)
	{
		Summary.MinimumGravityOnTargetClearanceCM = 0.0f;
	}
	Summary.bPassed =
		Summary.LargestSuccessIslandSamples >= FMath::Max(1, Preset.MinimumSuccessIslandSamples)
		&& Summary.bIslandSpansAimNeighbors
		&& Summary.bIslandSpansPullNeighbors
		&& Summary.GravityDependentHits > 0
		&& Summary.SimpleFullPowerHits == 0
		&& Summary.ReinforcedOutsideCertifiedPullHits == 0
		&& Summary.SuccessPullMinimum + KINDA_SMALL_NUMBER >= Preset.PullMinimum
		&& Summary.SuccessPullMaximum <= Preset.PullMaximum + KINDA_SMALL_NUMBER;

	uint64 Hash = FnvOffsetBasis64;
	AppendHash(Hash, Summary.ReinforcedSampleCount);
	AppendHash(Hash, Summary.ReinforcedReachablePullSamples);
	AppendHash(Hash, Summary.ReinforcedCertifiedPullSamples);
	AppendHash(Hash, Summary.ReinforcedGravityOnHits);
	AppendHash(Hash, Summary.ReinforcedSatelliteBodyHits);
	AppendHash(Hash, Summary.ReinforcedPrimaryBodyHits);
	AppendHash(Hash, Summary.ReinforcedTimeouts);
	AppendHash(Hash, Summary.GravityDependentHits);
	AppendHash(Hash, Summary.LargestSuccessIslandSamples);
	AppendHash(Hash, Summary.SimpleFullPowerHits);
	AppendHash(Hash, Summary.ReinforcedOutsideCertifiedPullHits);
	AppendHash(Hash, Quantize(Summary.SuccessPullMinimum));
	AppendHash(Hash, Quantize(Summary.SuccessPullMaximum));
	AppendHash(Hash, Quantize(Summary.SuccessAimInPlaneMinimumCM));
	AppendHash(Hash, Quantize(Summary.SuccessAimInPlaneMaximumCM));
	AppendHash(Hash, Quantize(Summary.MinimumGravityOffMissCM));
	AppendHash(Hash, Quantize(Summary.MinimumGravityOnTargetClearanceCM));
	AppendHash(Hash, Quantize(Summary.BestGravityOnAimInPlaneCM));
	AppendHash(Hash, Quantize(Summary.BestGravityOnAimOutOfPlaneCM));
	AppendHash(Hash, Quantize(Summary.BestGravityOnPullAlpha));
	AppendHash(Hash, Summary.bPassed ? 1 : 0);
	Summary.ResultHash = Hash;
	return Summary;
}
