// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleLayoutTypes.h"

#include "World/ABTSM11GravityAssistSolver.h"

namespace
{
	// Frozen M11-B v1 manifest outputs. Keep the three values together: any
	// certified geometry/contact/scan change must regenerate all affected
	// identities before the runtime boundary can reopen.
	constexpr uint64 CertifiedCertificationHashV1 =
		0x941684a72e11b27dull;
	constexpr uint64 CertifiedNominalTrajectoryHashV1 =
		0x185d3b673c1d52afull;
	constexpr uint64 CertifiedPhysicalPlaybackTrajectoryHashV1 =
		0xcac902c4183084afull;
	constexpr uint64 CertifiedBundleHashV1 =
		0xa219d69cf3f92af0ull;
	constexpr uint64 PhysicalPlaybackScenarioDomainV1 =
		0x504c41594241434bull;

	bool Reject(FString* OutFailure, const TCHAR* Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	}

	bool IsFiniteFinaleLayoutVector(const FVector3d& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	int32 InclusiveGridCount(
		const double Minimum,
		const double Maximum,
		const double Step)
	{
		if (!FMath::IsFinite(Minimum)
			|| !FMath::IsFinite(Maximum)
			|| !FMath::IsFinite(Step)
			|| Maximum < Minimum
			|| Step <= 0.0)
		{
			return 0;
		}
		return FMath::FloorToInt((Maximum - Minimum) / Step + 0.5) + 1;
	}

	bool StepTilesClosedInterval(
		const double Minimum,
		const double Maximum,
		const double Step)
	{
		if (!FMath::IsFinite(Minimum)
			|| !FMath::IsFinite(Maximum)
			|| !FMath::IsFinite(Step)
			|| Maximum < Minimum
			|| Step <= 0.0)
		{
			return false;
		}
		const double IntervalCount = (Maximum - Minimum) / Step;
		const double RoundedIntervalCount =
			FMath::RoundToDouble(IntervalCount);
		const double Tolerance =
			1.0e-9 * FMath::Max(1.0, FMath::Abs(IntervalCount));
		return FMath::Abs(IntervalCount - RoundedIntervalCount)
			<= Tolerance;
	}

	FABTSM11GravityBodySpec MakePrimary()
	{
		FABTSM11GravityBodySpec Body;
		Body.BodyId = 1100;
		Body.Role = EABTSM110FinaleGravityRole::Primary;
		Body.CenterCM = FVector3d(0.0, 0.0, -10000.0);
		Body.GravitationalParameterCM3PerSec2 = 5.665e9;
		Body.MinimumEvaluationRadiusCM = 1000.0;
		Body.VisualRadiusCM = 10000.0;
		Body.CollisionRadiusCM = 10000.0;
		Body.MaximumSimulationRadiusCM = 650000.0;
		Body.DebugColor = FLinearColor(0.1f, 0.35f, 0.7f);
		return Body;
	}

	FABTSM11GravityBodySpec MakeAssist(
		const int32 AssistIndex,
		const FVector3d& CenterCM)
	{
		FABTSM11GravityBodySpec Body;
		Body.BodyId = 1100 + AssistIndex;
		Body.Role = static_cast<EABTSM110FinaleGravityRole>(AssistIndex);
		Body.CenterCM = CenterCM;
		Body.GravitationalParameterCM3PerSec2 = AssistIndex == 1
			? 8.0e7
			: AssistIndex == 2 ? 1.0e8 : 1.3e8;
		Body.MinimumEvaluationRadiusCM = 500.0;
		Body.VisualRadiusCM = 1300.0 + AssistIndex * 150.0;
		Body.CollisionRadiusCM = 800.0;
		Body.InfluenceRadiusCM = AssistIndex == 1
			? 15000.0
			: AssistIndex == 2 ? 22000.0 : 30000.0;
		Body.InfluenceBlendWidthCM = Body.InfluenceRadiusCM * 0.10;
		Body.AssistReferenceRadiusCM =
			Body.InfluenceRadiusCM - Body.InfluenceBlendWidthCM;
		Body.VirtualOrbitalVelocityCMPerSec = FVector3d::ZeroVector;
		Body.BPlaneReferenceNormal = FVector3d(0.0, 0.0, 1.0);
		Body.BPlaneFallbackAxis = FVector3d(0.0, 1.0, 0.0);
		Body.BPlaneTargetTCM = 0.0;
		Body.BPlaneTargetRCM = 0.0;
		Body.BPlaneSigmaTCM = Body.InfluenceRadiusCM * 0.42;
		Body.BPlaneSigmaRCM = Body.InfluenceRadiusCM * 0.42;
		Body.BPlaneOuterChiSquared = 4.0;
		Body.AllowedPassSide = EABTSM11AllowedPassSide::Any;
		Body.MinimumEnergyChangeCM2PerSec2 = -250000.0;
		Body.MaximumEnergyChangeCM2PerSec2 = 250000.0;
		Body.DebugColor = AssistIndex == 1
			? FLinearColor(0.8f, 0.15f, 0.08f)
			: AssistIndex == 2
				? FLinearColor(0.75f, 0.55f, 0.12f)
				: FLinearColor(0.55f, 0.35f, 0.75f);
		return Body;
	}
}

bool FABTSM11FinaleLaunchInput::IsFinite() const
{
	return FMath::IsFinite(YawDegrees)
		&& FMath::IsFinite(PitchDegrees)
		&& FMath::IsFinite(Power);
}

bool FABTSM11FinaleLaunchModel::IsValid(FString* OutFailure) const
{
	if (LaunchModelVersion != 1)
	{
		return Reject(OutFailure, TEXT("UnsupportedLaunchModelVersion"));
	}
	if (!IsFiniteFinaleLayoutVector(PouchLocalPositionCM)
		|| !FMath::IsFinite(MinimumYawDegrees)
		|| !FMath::IsFinite(MaximumYawDegrees)
		|| !FMath::IsFinite(MinimumPitchDegrees)
		|| !FMath::IsFinite(MaximumPitchDegrees)
		|| !FMath::IsFinite(MinimumPower)
		|| !FMath::IsFinite(MaximumPower)
		|| MinimumYawDegrees >= MaximumYawDegrees
		|| MinimumPitchDegrees >= MaximumPitchDegrees
		|| MinimumPower >= MaximumPower)
	{
		return Reject(OutFailure, TEXT("InvalidLaunchInputDomain"));
	}
	if (!FMath::IsFinite(MinimumLaunchSpeedCMPerSec)
		|| !FMath::IsFinite(MaximumLaunchSpeedCMPerSec)
		|| !FMath::IsFinite(MaximumSimulationTimeSeconds)
		|| MinimumLaunchSpeedCMPerSec <= 0.0
		|| MinimumLaunchSpeedCMPerSec >= MaximumLaunchSpeedCMPerSec
		|| MaximumSimulationTimeSeconds <= 0.0)
	{
		return Reject(OutFailure, TEXT("InvalidLaunchSpeedOrTime"));
	}
	return true;
}

bool FABTSM11FinaleLaunchModel::Contains(
	const FABTSM11FinaleLaunchInput& Input) const
{
	return Input.IsFinite()
		&& Input.YawDegrees >= MinimumYawDegrees
		&& Input.YawDegrees <= MaximumYawDegrees
		&& Input.PitchDegrees >= MinimumPitchDegrees
		&& Input.PitchDegrees <= MaximumPitchDegrees
		&& Input.Power >= MinimumPower
		&& Input.Power <= MaximumPower;
}

FVector3d FABTSM11FinaleLaunchModel::MapDirection(
	const FABTSM11FinaleLaunchInput& Input) const
{
	const double YawRadians = FMath::DegreesToRadians(Input.YawDegrees);
	const double PitchRadians = FMath::DegreesToRadians(Input.PitchDegrees);
	const double CosPitch = FMath::Cos(PitchRadians);
	return FVector3d(
		CosPitch * FMath::Cos(YawRadians),
		CosPitch * FMath::Sin(YawRadians),
		FMath::Sin(PitchRadians)).GetSafeNormal();
}

double FABTSM11FinaleLaunchModel::MapSpeedCMPerSec(
	const FABTSM11FinaleLaunchInput& Input) const
{
	const double Alpha = FMath::Clamp(
		(Input.Power - MinimumPower) / (MaximumPower - MinimumPower),
		0.0,
		1.0);
	return FMath::Lerp(
		MinimumLaunchSpeedCMPerSec,
		MaximumLaunchSpeedCMPerSec,
		Alpha);
}

bool FABTSM11FinaleLaunchModel::ApplyToRequest(
	const FABTSM11FinaleLaunchInput& Input,
	FABTSM11TrajectoryRequest& InOutRequest,
	FString* OutFailure) const
{
	if (!IsValid(OutFailure) || !Contains(Input))
	{
		return Reject(OutFailure, TEXT("LaunchInputOutsideCertifiedDomain"));
	}
	InOutRequest.InitialPositionCM = PouchLocalPositionCM;
	InOutRequest.InitialVelocityCMPerSec =
		MapDirection(Input) * MapSpeedCMPerSec(Input);
	InOutRequest.InitialTimeSeconds = 0.0;
	InOutRequest.InitialExpectedAssistIndex = 1;
	InOutRequest.Config.MaximumSimulationTimeSeconds =
		MaximumSimulationTimeSeconds;
	return true;
}

bool FABTSM11PrefixTrustRegion::IsValid(
	const FABTSM11FinaleLaunchModel& LaunchModel) const
{
	return PrefixLevel >= 1
		&& PrefixLevel <= FABTSM11GravityScenario::AssistCount
		&& LaunchModel.Contains(Minimum)
		&& LaunchModel.Contains(Maximum)
		&& Minimum.YawDegrees <= Maximum.YawDegrees
		&& Minimum.PitchDegrees <= Maximum.PitchDegrees
		&& Minimum.Power <= Maximum.Power
		&& FMath::IsFinite(CaptureMarginCells)
		&& FMath::IsFinite(ReleaseMarginCells)
		&& CaptureMarginCells >= 0.0
		&& ReleaseMarginCells >= CaptureMarginCells;
}

bool FABTSM11PrefixTrustRegion::Contains(
	const FABTSM11FinaleLaunchInput& Input) const
{
	return Input.YawDegrees >= Minimum.YawDegrees
		&& Input.YawDegrees <= Maximum.YawDegrees
		&& Input.PitchDegrees >= Minimum.PitchDegrees
		&& Input.PitchDegrees <= Maximum.PitchDegrees
		&& Input.Power >= Minimum.Power
		&& Input.Power <= Maximum.Power;
}

bool FABTSM11PrefixTrustRegion::Contains(
	const FABTSM11PrefixTrustRegion& Other) const
{
	return Contains(Other.Minimum) && Contains(Other.Maximum);
}

bool FABTSM11LayoutScanContract::IsValid(
	const FABTSM11FinaleLaunchModel& LaunchModel,
	FString* OutFailure) const
{
	const bool bLegacyV2 = ScanContractVersion == 2
		&& Connectivity == 6
		&& BridgeClosurePolicy.IsDisabled();
	const bool bBridgeV3 = ScanContractVersion == 3
		&& Connectivity == 18
		&& DiscoveryPolicyVersion == 2
		&& BridgeClosurePolicy.IsValid(OutFailure)
		&& FMath::IsNearlyEqual(
			BridgeClosurePolicy.FinalYawPrecisionDegrees,
			FinalYawPrecisionDegrees,
			1.0e-12)
		&& FMath::IsNearlyEqual(
			BridgeClosurePolicy.FinalPitchPrecisionDegrees,
			FinalPitchPrecisionDegrees,
			1.0e-12)
		&& FMath::IsNearlyEqual(
			BridgeClosurePolicy.FinalPowerPrecision,
			FinalPowerPrecision,
			1.0e-12);
	if ((!bLegacyV2 && !bBridgeV3)
		|| !bIncludeHalfCellOffsetPass)
	{
		return Reject(OutFailure, TEXT("UnsupportedScanContract"));
	}
	if (!LaunchModel.IsValid(OutFailure))
	{
		return false;
	}
	if (!FMath::IsFinite(YawStepDegrees)
		|| !FMath::IsFinite(PitchStepDegrees)
		|| !FMath::IsFinite(PowerStep)
		|| YawStepDegrees <= 0.0
		|| PitchStepDegrees <= 0.0
		|| PowerStep <= 0.0
		|| GetSampleCount(LaunchModel) <= 0)
	{
		return Reject(OutFailure, TEXT("InvalidScanGrid"));
	}
	if (BoundaryRefinementDepth < 0
		|| BoundaryRefinementDepth > 10
		|| DiscoveryPolicyVersion != (bBridgeV3 ? 2 : 1)
		|| RefinementHaloCoarseCells < 1
		|| MaximumRefinementIterations < 1
		|| MaximumRefinementIterations > 16
		|| MaximumRefinementSampleCount < 1
		|| !FMath::IsFinite(FinalYawPrecisionDegrees)
		|| !FMath::IsFinite(FinalPitchPrecisionDegrees)
		|| !FMath::IsFinite(FinalPowerPrecision)
		|| FinalYawPrecisionDegrees <= 0.0
		|| FinalPitchPrecisionDegrees <= 0.0
		|| FinalPowerPrecision <= 0.0
		|| MaximumCompletePrimaryOrbits < 0)
	{
		return Reject(OutFailure, TEXT("InvalidBoundaryOrOrbitPolicy"));
	}
	const double RefinementScale =
		static_cast<double>(1 << BoundaryRefinementDepth);
	if (!FMath::IsNearlyEqual(
			YawStepDegrees,
			FinalYawPrecisionDegrees * RefinementScale,
			1.0e-12)
		|| !FMath::IsNearlyEqual(
			PitchStepDegrees,
			FinalPitchPrecisionDegrees * RefinementScale,
			1.0e-12)
		|| !FMath::IsNearlyEqual(
			PowerStep,
			FinalPowerPrecision * RefinementScale,
			1.0e-12))
	{
		return Reject(OutFailure, TEXT("NonIntegralRefinementScale"));
	}
	if (!FMath::IsFinite(MinimumF4YawWidthDegrees)
		|| !FMath::IsFinite(MinimumF4PitchWidthDegrees)
		|| !FMath::IsFinite(MaximumLockedPowerDeficitFromFullPower)
		|| !FMath::IsFinite(MinimumF1OnsetPower)
		|| !FMath::IsFinite(MaximumF1OnsetPower)
		|| !FMath::IsFinite(TrustErosionCells)
		|| MinimumF4YawWidthDegrees < FinalYawPrecisionDegrees
		|| MinimumF4PitchWidthDegrees < FinalPitchPrecisionDegrees
		|| MinimumPlayableF4PowerSliceCount < 2
		|| MaximumLockedPowerDeficitFromFullPower <= 0.0
		|| MaximumLockedPowerDeficitFromFullPower
			>= LaunchModel.MaximumPower - LaunchModel.MinimumPower
		|| MinimumF1OnsetPower < LaunchModel.MinimumPower
		|| MaximumF1OnsetPower > LaunchModel.MaximumPower
		|| MinimumF1OnsetPower >= MaximumF1OnsetPower
		|| TrustErosionCells < 0.0)
	{
		return Reject(OutFailure, TEXT("InvalidPlayableWidthPolicy"));
	}
	if (ReferenceResolutionX <= 0
		|| ReferenceResolutionY <= 0
		|| !FMath::IsFinite(ReferenceDPIScale)
		|| !FMath::IsFinite(MinimumScreenTrustWidthPixels)
		|| ReferenceDPIScale <= 0.0
		|| MinimumScreenTrustWidthPixels <= 0.0)
	{
		return Reject(OutFailure, TEXT("InvalidDisplayMapping"));
	}
	return true;
}

bool FABTSM11LayoutScanContract::UsesBridgeClosureV3() const
{
	return ScanContractVersion == 3
		&& Connectivity == 18
		&& !BridgeClosurePolicy.IsDisabled();
}

FABTSM11LayoutScanContract FABTSM11LayoutScanContract::MakeBridgeClosureV3(
	const FABTSM11LayoutScanContract& LegacyV2)
{
	FABTSM11LayoutScanContract Contract = LegacyV2;
	Contract.ScanContractVersion = 3;
	Contract.Connectivity = 18;
	Contract.DiscoveryPolicyVersion = 2;
	Contract.MaximumRefinementSampleCount =
		BridgeClosureV3MaximumRefinementSampleCount;
	Contract.BridgeClosurePolicy = FABTSM11BridgeClosurePolicy::MakeV1(
		Contract.FinalYawPrecisionDegrees,
		Contract.FinalPitchPrecisionDegrees,
		Contract.FinalPowerPrecision);
	return Contract;
}

int32 FABTSM11LayoutScanContract::GetYawCount(
	const FABTSM11FinaleLaunchModel& LaunchModel) const
{
	return InclusiveGridCount(
		LaunchModel.MinimumYawDegrees,
		LaunchModel.MaximumYawDegrees,
		YawStepDegrees);
}

int32 FABTSM11LayoutScanContract::GetPitchCount(
	const FABTSM11FinaleLaunchModel& LaunchModel) const
{
	return InclusiveGridCount(
		LaunchModel.MinimumPitchDegrees,
		LaunchModel.MaximumPitchDegrees,
		PitchStepDegrees);
}

int32 FABTSM11LayoutScanContract::GetPowerCount(
	const FABTSM11FinaleLaunchModel& LaunchModel) const
{
	return InclusiveGridCount(
		LaunchModel.MinimumPower,
		LaunchModel.MaximumPower,
		PowerStep);
}

int32 FABTSM11LayoutScanContract::GetSampleCount(
	const FABTSM11FinaleLaunchModel& LaunchModel) const
{
	const int64 Count =
		static_cast<int64>(GetYawCount(LaunchModel))
		* GetPitchCount(LaunchModel)
		* GetPowerCount(LaunchModel);
	return Count > 0 && Count <= MAX_int32 ? static_cast<int32>(Count) : 0;
}

FABTSM11FinaleLaunchInput FABTSM11LayoutScanContract::GetInput(
	const FABTSM11FinaleLaunchModel& LaunchModel,
	const int32 YawIndex,
	const int32 PitchIndex,
	const int32 PowerIndex) const
{
	FABTSM11FinaleLaunchInput Input;
	Input.YawDegrees = FMath::Min(
		LaunchModel.MaximumYawDegrees,
		LaunchModel.MinimumYawDegrees + YawIndex * YawStepDegrees);
	Input.PitchDegrees = FMath::Min(
		LaunchModel.MaximumPitchDegrees,
		LaunchModel.MinimumPitchDegrees + PitchIndex * PitchStepDegrees);
	Input.Power = FMath::Min(
		LaunchModel.MaximumPower,
		LaunchModel.MinimumPower + PowerIndex * PowerStep);
	return Input;
}

bool FABTSM11InputGrid::IsValid(
	const FABTSM11FinaleLaunchModel& LaunchModel,
	FString* OutFailure) const
{
	if (!Minimum.IsFinite()
		|| !Maximum.IsFinite()
		|| !LaunchModel.Contains(Minimum)
		|| !LaunchModel.Contains(Maximum)
		|| Minimum.YawDegrees > Maximum.YawDegrees
		|| Minimum.PitchDegrees > Maximum.PitchDegrees
		|| Minimum.Power > Maximum.Power
		|| !FMath::IsFinite(YawStepDegrees)
		|| !FMath::IsFinite(PitchStepDegrees)
		|| !FMath::IsFinite(PowerStep)
		|| YawStepDegrees <= 0.0
		|| PitchStepDegrees <= 0.0
		|| PowerStep <= 0.0
		|| GetSampleCount() <= 0)
	{
		return Reject(OutFailure, TEXT("InvalidInputGrid"));
	}
	if (!StepTilesClosedInterval(
			Minimum.YawDegrees,
			Maximum.YawDegrees,
			YawStepDegrees)
		|| !StepTilesClosedInterval(
			Minimum.PitchDegrees,
			Maximum.PitchDegrees,
			PitchStepDegrees)
		|| !StepTilesClosedInterval(
			Minimum.Power,
			Maximum.Power,
			PowerStep))
	{
		return Reject(OutFailure, TEXT("NonIntegralInputGrid"));
	}
	return true;
}

int32 FABTSM11InputGrid::GetYawCount() const
{
	return InclusiveGridCount(
		Minimum.YawDegrees, Maximum.YawDegrees, YawStepDegrees);
}

int32 FABTSM11InputGrid::GetPitchCount() const
{
	return InclusiveGridCount(
		Minimum.PitchDegrees, Maximum.PitchDegrees, PitchStepDegrees);
}

int32 FABTSM11InputGrid::GetPowerCount() const
{
	return InclusiveGridCount(Minimum.Power, Maximum.Power, PowerStep);
}

int32 FABTSM11InputGrid::GetSampleCount() const
{
	const int64 Count =
		static_cast<int64>(GetYawCount())
		* GetPitchCount()
		* GetPowerCount();
	return Count > 0 && Count <= MAX_int32 ? static_cast<int32>(Count) : 0;
}

FABTSM11FinaleLaunchInput FABTSM11InputGrid::GetInput(
	const int32 YawIndex,
	const int32 PitchIndex,
	const int32 PowerIndex) const
{
	FABTSM11FinaleLaunchInput Input;
	Input.YawDegrees = YawIndex == GetYawCount() - 1
		? Maximum.YawDegrees
		: Minimum.YawDegrees + YawIndex * YawStepDegrees;
	Input.PitchDegrees = PitchIndex == GetPitchCount() - 1
		? Maximum.PitchDegrees
		: Minimum.PitchDegrees + PitchIndex * PitchStepDegrees;
	Input.Power = PowerIndex == GetPowerCount() - 1
		? Maximum.Power
		: Minimum.Power + PowerIndex * PowerStep;
	return Input;
}

FABTSM11InputGrid FABTSM11InputGrid::MakeFullDomain(
	const FABTSM11FinaleLaunchModel& LaunchModel,
	const FABTSM11LayoutScanContract& ScanContract)
{
	FABTSM11InputGrid Grid;
	Grid.Minimum = FABTSM11FinaleLaunchInput{
		LaunchModel.MinimumYawDegrees,
		LaunchModel.MinimumPitchDegrees,
		LaunchModel.MinimumPower};
	Grid.Maximum = FABTSM11FinaleLaunchInput{
		LaunchModel.MaximumYawDegrees,
		LaunchModel.MaximumPitchDegrees,
		LaunchModel.MaximumPower};
	Grid.YawStepDegrees = ScanContract.YawStepDegrees;
	Grid.PitchStepDegrees = ScanContract.PitchStepDegrees;
	Grid.PowerStep = ScanContract.PowerStep;
	return Grid;
}

FABTSM11FinaleLayoutPreset::FABTSM11FinaleLayoutPreset()
{
	for (int32 Index = 0; Index < AssistCount; ++Index)
	{
		MinimumCertifiedCorridorQuality[Index] = 0.05;
		MinimumCertifiedEnergyGainCM2PerSec2[Index] = 20000.0;
		PrefixTrustRegions[Index].PrefixLevel = Index + 1;
		PrefixTrustRegions[Index].Minimum =
			FABTSM11FinaleLaunchInput{-1.0, 29.0, 0.94};
		PrefixTrustRegions[Index].Maximum =
			FABTSM11FinaleLaunchInput{1.0, 31.0, 1.0};
	}
}

bool FABTSM11FinaleLayoutPreset::IsValid(FString* OutFailure) const
{
	if (PresetVersion != 1
		|| CompatibleGeneratorVersion != 3
		|| CompatibleFrameLayoutVersion != 1
		|| (SearchAlgorithmVersion != 1
			&& SearchAlgorithmVersion != 2
			&& SearchAlgorithmVersion != 3)
		|| PhysicalPlaybackContractVersion != 1)
	{
		return Reject(OutFailure, TEXT("UnsupportedFinalePresetVersion"));
	}
	if (!FMath::IsFinite(ReferencePrimaryRadiusCM)
		|| !FMath::IsFinite(ReferenceLaunchRadiusCM)
		|| !FMath::IsFinite(PrimaryCompatibilityToleranceCM)
		|| ReferencePrimaryRadiusCM <= 0.0
		|| ReferenceLaunchRadiusCM <= ReferencePrimaryRadiusCM
		|| PrimaryCompatibilityToleranceCM < 0.0)
	{
		return Reject(OutFailure, TEXT("InvalidPrimaryCompatibility"));
	}
	if (!LaunchModel.IsValid(OutFailure)
		|| !CanonicalScenario.IsValid(OutFailure)
		|| !SolverConfig.IsValid(OutFailure)
		|| !ScanContract.IsValid(LaunchModel, OutFailure)
		|| !LaunchModel.Contains(NominalInput))
	{
		return false;
	}
	if (CanonicalScenario.GetPrimary().VisualRadiusCM
			!= ReferencePrimaryRadiusCM
		|| !FMath::IsFinite(TargetApproachRadiusCM)
		|| TargetApproachRadiusCM
			<= CanonicalScenario.Target.HitRadiusCM)
	{
		return Reject(OutFailure, TEXT("InvalidTargetApproachOrPrimaryReference"));
	}
	const bool bHasCertifiedManifest =
		CertificationHash != 0 || CertifiedBundleHash != 0;
	if (bHasCertifiedManifest
		&& (PresetSourceHash == 0
			|| PresetHash == 0
			|| ScanContractHash == 0
			|| CertificationHash == 0
			|| NominalTrajectoryHash == 0
			|| PhysicalPlaybackTrajectoryHash == 0
			|| CertifiedBundleHash == 0))
	{
		return Reject(OutFailure, TEXT("IncompleteCertifiedBundleIdentity"));
	}
	for (int32 Index = 0; Index < AssistCount; ++Index)
	{
		if (!FMath::IsFinite(MinimumCertifiedCorridorQuality[Index])
			|| !FMath::IsFinite(MinimumCertifiedEnergyGainCM2PerSec2[Index])
			|| MinimumCertifiedCorridorQuality[Index] <= 0.0
			|| MinimumCertifiedCorridorQuality[Index] > 1.0
			|| MinimumCertifiedEnergyGainCM2PerSec2[Index] <= 0.0)
		{
			return Reject(OutFailure, TEXT("InvalidCertifiedAssistThreshold"));
		}
		if (bHasCertifiedManifest
			&& !PrefixTrustRegions[Index].IsValid(LaunchModel))
		{
			return Reject(OutFailure, TEXT("InvalidPrefixTrustRegion"));
		}
		if (bHasCertifiedManifest
			&& (PrefixTrustRegions[Index].RegionHash == 0
				|| PrefixTrustRegions[Index].RegionHash
					!= FABTSM11FinaleLayoutHash::ComputeTrustRegionHash(
						PrefixTrustRegions[Index])))
		{
			return Reject(OutFailure, TEXT("PrefixTrustRegionHashMismatch"));
		}
	}
	if ((PresetSourceHash == 0) != (PresetHash == 0))
	{
		return Reject(OutFailure, TEXT("IncompletePresetSourceIdentity"));
	}
	if (PresetSourceHash != 0
		&& PresetSourceHash
			!= FABTSM11FinaleLayoutHash::ComputePresetSourceHash(*this))
	{
		return Reject(OutFailure, TEXT("PresetSourceHashMismatch"));
	}
	if (PresetHash != 0
		&& PresetHash != FABTSM11FinaleLayoutHash::ComputePresetHash(*this))
	{
		return Reject(OutFailure, TEXT("PresetHashMismatch"));
	}
	if (PresetHash != 0
		&& CanonicalScenario.ScenarioHash
			!= FABTSM11FinaleLayoutHash::FoldScenarioHash(PresetHash))
	{
		return Reject(OutFailure, TEXT("ScenarioHashMismatch"));
	}
	if (ScanContractHash != 0
		&& ScanContractHash
			!= FABTSM11FinaleLayoutHash::ComputeScanContractHash(*this))
	{
		return Reject(OutFailure, TEXT("ScanContractHashMismatch"));
	}
	if (bHasCertifiedManifest
		&& CertifiedBundleHash
			!= FABTSM11FinaleLayoutHash::ComputeCertifiedBundleHash(*this))
	{
		return Reject(OutFailure, TEXT("CertifiedBundleHashMismatch"));
	}
	return true;
}

bool FABTSM11FinaleLayoutPreset::BuildRequest(
	const FABTSM11FinaleLaunchInput& Input,
	const uint8 EnabledAssistMask,
	FABTSM11TrajectoryRequest& OutRequest,
	FString* OutFailure) const
{
	if (!IsValid(OutFailure))
	{
		return false;
	}
	OutRequest = FABTSM11TrajectoryRequest();
	OutRequest.Scenario = CanonicalScenario;
	OutRequest.Config = SolverConfig;
	OutRequest.Config.EnabledAssistMask = EnabledAssistMask & 0x7u;
	return LaunchModel.ApplyToRequest(Input, OutRequest, OutFailure)
		&& OutRequest.IsValid(OutFailure);
}

bool FABTSM11FinaleLayoutPreset::BuildPhysicalPlaybackRequest(
	const FABTSM11FinaleLaunchInput& Input,
	const uint8 EnabledAssistMask,
	FABTSM11TrajectoryRequest& OutRequest,
	FString* OutFailure) const
{
	if (!BuildRequest(
			Input,
			EnabledAssistMask,
			OutRequest,
			OutFailure))
	{
		return false;
	}

	FABTSM11TargetSpec& PlaybackTarget = OutRequest.Scenario.Target;
	const FVector3d PhysicalCenter =
		PlaybackTarget.GetGeometricContactCenterCM();
	const double PhysicalRadius =
		PlaybackTarget.GetGeometricContactRadiusCM();
	PlaybackTarget.CenterCM = PhysicalCenter;
	PlaybackTarget.HitRadiusCM = PhysicalRadius;
	PlaybackTarget.bUseSeparateGeometricContactCenter = false;
	PlaybackTarget.GeometricContactCenterCM = PhysicalCenter;
	PlaybackTarget.GeometricContactRadiusCM = PhysicalRadius;
	OutRequest.Scenario.ScenarioHash =
		FABTSM11FinaleLayoutHash::FoldScenarioHash(
			PresetHash ^ PhysicalPlaybackScenarioDomainV1);
	return OutRequest.IsValid(OutFailure);
}

FABTSM11FinaleLayoutPreset
FABTSM11FinaleLayoutPreset::MakeConstructiveSearchSeed()
{
	FABTSM11FinaleLayoutPreset Preset;
	Preset.CanonicalScenario.LayoutVersion = 1;
	Preset.CanonicalScenario.ScenarioHash = 0x11b00001u;
	Preset.CanonicalScenario.Bodies[0] = MakePrimary();
	Preset.CanonicalScenario.Bodies[1] =
		MakeAssist(1, FVector3d(300000.0, 180000.0, 100000.0));
	Preset.CanonicalScenario.Bodies[2] =
		MakeAssist(2, FVector3d(-260000.0, 230000.0, 140000.0));
	Preset.CanonicalScenario.Bodies[3] =
		MakeAssist(3, FVector3d(180000.0, -290000.0, 170000.0));
	Preset.CanonicalScenario.Target.TargetId = 1199;
	Preset.CanonicalScenario.Target.CenterCM =
		FVector3d(-280000.0, -210000.0, 190000.0);
	Preset.CanonicalScenario.Target.HitRadiusCM = 7000.0;
	Preset.CanonicalScenario.Target.GeometricContactRadiusCM = 800.0;
	Preset.CanonicalScenario.Target.RequiredQualifiedAssistCount =
		FABTSM11GravityScenario::AssistCount;
	Preset.CanonicalScenario.Target.MinimumQualifyingCorridorQuality = 0.05;
	Preset.CanonicalScenario.Target
		.MinimumQualifyingEnergyGainCM2PerSec2 = 20000.0;
	Preset.CanonicalScenario.Target.bRequireAllowedPassSide = true;
	Preset.CanonicalScenario.Target.PresentationForward =
		FVector3d(-1.0, 0.0, 0.0);
	Preset.TargetApproachRadiusCM = 24000.0;
	Preset.NominalInput = FABTSM11FinaleLaunchInput{0.0, 30.0, 0.975};
	Preset.SolverConfig.FixedTimeStepSeconds = 1.0 / 120.0;
	Preset.SolverConfig.MaximumSimulationTimeSeconds =
		Preset.LaunchModel.MaximumSimulationTimeSeconds;
	Preset.SolverConfig.MaximumStepCount = 2000000;
	Preset.SolverConfig.NaturalCloneMaximumTimeSeconds = 240.0;
	Preset.PresetSourceHash = 0;
	Preset.PresetHash = 0;
	Preset.ScanContractHash = 0;
	Preset.CertificationHash = 0;
	Preset.NominalTrajectoryHash = 0;
	Preset.PhysicalPlaybackTrajectoryHash = 0;
	Preset.CertifiedBundleHash = 0;
	Preset.PresetSourceHash =
		FABTSM11FinaleLayoutHash::ComputePresetSourceHash(Preset);
	Preset.PresetHash =
		FABTSM11FinaleLayoutHash::ComputePresetHash(Preset);
	Preset.CanonicalScenario.ScenarioHash =
		FABTSM11FinaleLayoutHash::FoldScenarioHash(Preset.PresetHash);
	Preset.ScanContractHash =
		FABTSM11FinaleLayoutHash::ComputeScanContractHash(Preset);
	return Preset;
}

FABTSM11FinaleLayoutPreset FABTSM11FinaleLayoutPreset::MakeCertifiedV1()
{
	FABTSM11FinaleLayoutPreset Preset = MakeConstructiveSearchSeed();
	Preset.CanonicalScenario.Bodies[1].CenterCM = FVector3d(
		97219.225601219383,
		-5700.0,
		-14094.37599498272);
	Preset.CanonicalScenario.Bodies[1].VirtualOrbitalVelocityCMPerSec =
		FVector3d(0.0, -650.0, 0.0);
	Preset.CanonicalScenario.Bodies[1].BPlaneTargetTCM =
		277.83495339392886;
	Preset.CanonicalScenario.Bodies[1].BPlaneTargetRCM =
		-6103.2950472502735;
	Preset.CanonicalScenario.Bodies[1].AllowedPassSide =
		EABTSM11AllowedPassSide::NegativeR;

	Preset.CanonicalScenario.Bodies[2].CenterCM = FVector3d(
		138324.92597291688,
		-26497.02451798931,
		-37845.625613579061);
	Preset.CanonicalScenario.Bodies[2].VirtualOrbitalVelocityCMPerSec =
		FVector3d(
			-333.29802808429707,
			-513.19881990954673,
			-219.17891257730014);
	Preset.CanonicalScenario.Bodies[2].BPlaneTargetTCM =
		3486.4912461183612;
	Preset.CanonicalScenario.Bodies[2].BPlaneTargetRCM =
		-8620.1106023168049;
	Preset.CanonicalScenario.Bodies[2].AllowedPassSide =
		EABTSM11AllowedPassSide::NegativeR;

	Preset.CanonicalScenario.Bodies[3].CenterCM = FVector3d(
		190659.21928569756,
		-61253.272725944465,
		-64968.511899881327);
	Preset.CanonicalScenario.Bodies[3].VirtualOrbitalVelocityCMPerSec =
		FVector3d(
			-1353.2802614607244,
			-2220.052371891436,
			0.0);
	Preset.CanonicalScenario.Bodies[3].BPlaneTargetTCM =
		78.081048156920389;
	Preset.CanonicalScenario.Bodies[3].BPlaneTargetRCM =
		-11664.386295301727;
	Preset.CanonicalScenario.Bodies[3].AllowedPassSide =
		EABTSM11AllowedPassSide::NegativeR;

	Preset.CanonicalScenario.Target.CenterCM = FVector3d(
		233103.20024977488,
		-78974.321891263491,
		-87227.804625011457);
	Preset.CanonicalScenario.Target.HitRadiusCM = 16000.0;
	Preset.CanonicalScenario.Target.GeometricContactRadiusCM = 800.0;
	Preset.CanonicalScenario.Target.MinimumQualifyingCorridorQuality = 0.95;
	Preset.CanonicalScenario.Target.bUseSeparateGeometricContactCenter = true;
	Preset.CanonicalScenario.Target.GeometricContactCenterCM = FVector3d(
		278058.940003354,
		-112576.146689672,
		-114647.405393587);
	Preset.CanonicalScenario.Target.PresentationForward =
		(Preset.CanonicalScenario.Bodies[3].CenterCM
			- Preset.CanonicalScenario.Target
				.GetGeometricContactCenterCM()).GetSafeNormal();
	Preset.PresetSourceHash = 0;
	Preset.PresetHash = 0;
	Preset.ScanContractHash = 0;
	Preset.CertificationHash = 0;
	Preset.NominalTrajectoryHash = 0;
	Preset.PhysicalPlaybackTrajectoryHash = 0;
	Preset.CertifiedBundleHash = 0;
	Preset.CanonicalScenario.ScenarioHash = 1;
	Preset.PresetSourceHash =
		FABTSM11FinaleLayoutHash::ComputePresetSourceHash(Preset);
	Preset.PresetHash =
		FABTSM11FinaleLayoutHash::ComputePresetHash(Preset);
	Preset.CanonicalScenario.ScenarioHash =
		FABTSM11FinaleLayoutHash::FoldScenarioHash(Preset.PresetHash);
	Preset.ScanContractHash =
		FABTSM11FinaleLayoutHash::ComputeScanContractHash(Preset);

	auto FreezeTrustRegion = [&Preset](
		const int32 Index,
		const FABTSM11FinaleLaunchInput& Minimum,
		const FABTSM11FinaleLaunchInput& Maximum)
	{
		FABTSM11PrefixTrustRegion& Region =
			Preset.PrefixTrustRegions[Index];
		Region.PrefixLevel = Index + 1;
		Region.Minimum = Minimum;
		Region.Maximum = Maximum;
		Region.CaptureMarginCells = 1.0;
		Region.ReleaseMarginCells = 2.0;
		Region.RegionHash =
			FABTSM11FinaleLayoutHash::ComputeTrustRegionHash(Region);
	};
	FreezeTrustRegion(
		0,
		// The power boundaries are frozen at the exact IEEE-754 values
		// emitted by the domain-relative final-resolution grid.
		FABTSM11FinaleLaunchInput{
			-1.125, 29.0, 0.9656250000000001},
		FABTSM11FinaleLaunchInput{0.0, 30.5, 0.984375});
	FreezeTrustRegion(
		1,
		FABTSM11FinaleLaunchInput{
			-0.1875, 29.5, 0.9750000000000001},
		FABTSM11FinaleLaunchInput{0.0, 30.0, 0.978125});
	FreezeTrustRegion(
		2,
		FABTSM11FinaleLaunchInput{
			-0.1875, 29.5, 0.9750000000000001},
		FABTSM11FinaleLaunchInput{0.0, 30.0, 0.978125});

	// Produced by ABTS.M11B.Certification.FullInputDomain under the frozen
	// base + half-cell discovery and final-resolution closure contract.
	Preset.CertificationHash = CertifiedCertificationHashV1;
	Preset.NominalTrajectoryHash = CertifiedNominalTrajectoryHashV1;
	Preset.PhysicalPlaybackTrajectoryHash =
		CertifiedPhysicalPlaybackTrajectoryHashV1;
	Preset.CertifiedBundleHash = CertifiedBundleHashV1;
	return Preset;
}

FABTSM11LayoutCertificationReport::FABTSM11LayoutCertificationReport()
{
	for (int32& Count : PrefixSampleCounts)
	{
		Count = 0;
	}
	for (int32 Index = 0; Index < Prefixes.Num(); ++Index)
	{
		Prefixes[Index].PrefixLevel = Index + 1;
	}
}
