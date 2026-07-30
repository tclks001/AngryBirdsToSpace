// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleLayoutTypes.h"

#include "World/ABTSM11FinaleLayoutCertification.h"

namespace ABTSM11FinaleLayoutHashPrivate
{
	class FCanonicalHash
	{
	public:
		void AddUInt8(const uint8 Value) { AddBytes(&Value, sizeof(Value)); }

		void AddBool(const bool bValue)
		{
			AddUInt8(bValue ? 1u : 0u);
		}

		void AddInt32(const int32 Value)
		{
			const uint32 Bits = static_cast<uint32>(Value);
			AddUInt32(Bits);
		}

		void AddUInt32(const uint32 Value)
		{
			for (int32 ByteIndex = 0; ByteIndex < 4; ++ByteIndex)
			{
				AddUInt8(static_cast<uint8>(Value >> (ByteIndex * 8)));
			}
		}

		void AddUInt64(const uint64 Value)
		{
			for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
			{
				AddUInt8(static_cast<uint8>(Value >> (ByteIndex * 8)));
			}
		}

		void AddDouble(const double Value)
		{
			double CanonicalValue = Value == 0.0 ? 0.0 : Value;
			uint64 Bits = 0;
			static_assert(sizeof(Bits) == sizeof(CanonicalValue));
			FMemory::Memcpy(&Bits, &CanonicalValue, sizeof(Bits));
			AddUInt64(Bits);
		}

		void AddVector(const FVector3d& Value)
		{
			AddDouble(Value.X);
			AddDouble(Value.Y);
			AddDouble(Value.Z);
		}

		void AddColor(const FLinearColor& Value)
		{
			AddDouble(Value.R);
			AddDouble(Value.G);
			AddDouble(Value.B);
			AddDouble(Value.A);
		}

		void AddString(const FString& Value)
		{
			const FTCHARToUTF8 Converted(*Value);
			AddInt32(Converted.Length());
			AddBytes(Converted.Get(), Converted.Length());
		}

		uint64 Get() const { return Hash; }

	private:
		void AddBytes(const void* Data, const int32 Size)
		{
			const uint8* Bytes = static_cast<const uint8*>(Data);
			for (int32 Index = 0; Index < Size; ++Index)
			{
				Hash ^= Bytes[Index];
				Hash *= 1099511628211ull;
			}
		}

		uint64 Hash = 1469598103934665603ull;
	};

	void AddInput(
		FCanonicalHash& Hash,
		const FABTSM11FinaleLaunchInput& Input)
	{
		Hash.AddDouble(Input.YawDegrees);
		Hash.AddDouble(Input.PitchDegrees);
		Hash.AddDouble(Input.Power);
	}

	void AddLaunchModel(
		FCanonicalHash& Hash,
		const FABTSM11FinaleLaunchModel& Model)
	{
		Hash.AddInt32(Model.LaunchModelVersion);
		Hash.AddVector(Model.PouchLocalPositionCM);
		Hash.AddDouble(Model.MinimumYawDegrees);
		Hash.AddDouble(Model.MaximumYawDegrees);
		Hash.AddDouble(Model.MinimumPitchDegrees);
		Hash.AddDouble(Model.MaximumPitchDegrees);
		Hash.AddDouble(Model.MinimumPower);
		Hash.AddDouble(Model.MaximumPower);
		Hash.AddDouble(Model.MinimumLaunchSpeedCMPerSec);
		Hash.AddDouble(Model.MaximumLaunchSpeedCMPerSec);
		Hash.AddDouble(Model.MaximumSimulationTimeSeconds);
	}

	void AddSolverConfig(
		FCanonicalHash& Hash,
		const FABTSM11SolverConfig& Config)
	{
		Hash.AddInt32(Config.SolverVersion);
		Hash.AddInt32(Config.HashSchemaVersion);
		Hash.AddDouble(Config.FixedTimeStepSeconds);
		Hash.AddDouble(Config.MaximumSimulationTimeSeconds);
		Hash.AddInt32(Config.MaximumStepCount);
		Hash.AddInt32(Config.MaximumSubdivisionDepth);
		if (Config.HashSchemaVersion >= 2)
		{
			Hash.AddInt32(Config.MaximumCoastStepExpansionDepth);
		}
		Hash.AddDouble(Config.AssistStepRadiusFraction);
		Hash.AddDouble(Config.CollisionStepRadiusFraction);
		Hash.AddDouble(Config.GravityTimescaleFraction);
		Hash.AddDouble(Config.PositionErrorLimitCM);
		Hash.AddInt32(Config.RootBisectionIterations);
		Hash.AddDouble(Config.RootAlphaTolerance);
		Hash.AddDouble(Config.BPlaneBasisMinimumLength);
		Hash.AddDouble(Config.MinimumVInfinityCMPerSec);
		Hash.AddDouble(Config.MaximumNaturalDeflectionErrorRadians);
		Hash.AddDouble(Config.EnergyQualityPower);
		Hash.AddDouble(Config.EnergyRootEpsilonCM2PerSec2);
		Hash.AddDouble(Config.ExitEnergyResidualToleranceCM2PerSec2);
		Hash.AddInt32(Config.EnergyShootingIterationCount);
		Hash.AddDouble(Config.NaturalCloneMaximumTimeSeconds);
		Hash.AddInt32(Config.NaturalCloneMaximumStepCount);
		Hash.AddUInt8(Config.EnabledAssistMask);
	}

	void AddBody(
		FCanonicalHash& Hash,
		const FABTSM11GravityBodySpec& Body)
	{
		Hash.AddInt32(Body.BodyId);
		Hash.AddUInt8(static_cast<uint8>(Body.Role));
		Hash.AddVector(Body.CenterCM);
		Hash.AddDouble(Body.GravitationalParameterCM3PerSec2);
		Hash.AddDouble(Body.MinimumEvaluationRadiusCM);
		Hash.AddDouble(Body.VisualRadiusCM);
		Hash.AddDouble(Body.CollisionRadiusCM);
		Hash.AddDouble(Body.MaximumSimulationRadiusCM);
		Hash.AddDouble(Body.InfluenceRadiusCM);
		Hash.AddDouble(Body.AssistReferenceRadiusCM);
		Hash.AddDouble(Body.InfluenceBlendWidthCM);
		Hash.AddVector(Body.VirtualOrbitalVelocityCMPerSec);
		Hash.AddVector(Body.BPlaneReferenceNormal);
		Hash.AddVector(Body.BPlaneFallbackAxis);
		Hash.AddDouble(Body.BPlaneTargetTCM);
		Hash.AddDouble(Body.BPlaneTargetRCM);
		Hash.AddDouble(Body.BPlaneSigmaTCM);
		Hash.AddDouble(Body.BPlaneSigmaRCM);
		Hash.AddDouble(Body.BPlaneOuterChiSquared);
		Hash.AddUInt8(static_cast<uint8>(Body.AllowedPassSide));
		Hash.AddDouble(Body.MinimumEnergyChangeCM2PerSec2);
		Hash.AddDouble(Body.MaximumEnergyChangeCM2PerSec2);
		Hash.AddColor(Body.DebugColor);
	}

	void AddScanContract(
		FCanonicalHash& Hash,
		const FABTSM11LayoutScanContract& Scan)
	{
		Hash.AddInt32(Scan.ScanContractVersion);
		Hash.AddDouble(Scan.YawStepDegrees);
		Hash.AddDouble(Scan.PitchStepDegrees);
		Hash.AddDouble(Scan.PowerStep);
		Hash.AddInt32(Scan.BoundaryRefinementDepth);
		Hash.AddInt32(Scan.DiscoveryPolicyVersion);
		Hash.AddInt32(Scan.RefinementHaloCoarseCells);
		Hash.AddInt32(Scan.MaximumRefinementIterations);
		Hash.AddInt32(Scan.MaximumRefinementSampleCount);
		Hash.AddDouble(Scan.FinalYawPrecisionDegrees);
		Hash.AddDouble(Scan.FinalPitchPrecisionDegrees);
		Hash.AddDouble(Scan.FinalPowerPrecision);
		Hash.AddInt32(Scan.Connectivity);
		Hash.AddInt32(Scan.MaximumCompletePrimaryOrbits);
		Hash.AddDouble(Scan.MinimumF4YawWidthDegrees);
		Hash.AddDouble(Scan.MinimumF4PitchWidthDegrees);
		Hash.AddInt32(Scan.MinimumPlayableF4PowerSliceCount);
		Hash.AddDouble(Scan.MaximumLockedPowerDeficitFromFullPower);
		Hash.AddDouble(Scan.MinimumF1OnsetPower);
		Hash.AddDouble(Scan.MaximumF1OnsetPower);
		Hash.AddDouble(Scan.TrustErosionCells);
		Hash.AddInt32(Scan.ReferenceResolutionX);
		Hash.AddInt32(Scan.ReferenceResolutionY);
		Hash.AddDouble(Scan.ReferenceDPIScale);
		Hash.AddDouble(Scan.MinimumScreenTrustWidthPixels);
		Hash.AddBool(Scan.bIncludeHalfCellOffsetPass);
	}

	void AddPrefixRegion(
		FCanonicalHash& Hash,
		const FABTSM11PrefixTrustRegion& Region)
	{
		Hash.AddInt32(Region.PrefixLevel);
		AddInput(Hash, Region.Minimum);
		AddInput(Hash, Region.Maximum);
		Hash.AddDouble(Region.CaptureMarginCells);
		Hash.AddDouble(Region.ReleaseMarginCells);
	}

	void AddInputGrid(
		FCanonicalHash& Hash,
		const FABTSM11InputGrid& Grid)
	{
		AddInput(Hash, Grid.Minimum);
		AddInput(Hash, Grid.Maximum);
		Hash.AddDouble(Grid.YawStepDegrees);
		Hash.AddDouble(Grid.PitchStepDegrees);
		Hash.AddDouble(Grid.PowerStep);
	}
}

uint64 FABTSM11FinaleLayoutHash::ComputePresetSourceHash(
	const FABTSM11FinaleLayoutPreset& Preset)
{
	using namespace ABTSM11FinaleLayoutHashPrivate;
	FCanonicalHash Hash;
	Hash.AddInt32(Preset.PresetVersion);
	Hash.AddInt32(Preset.CompatibleGeneratorVersion);
	Hash.AddInt32(Preset.CompatibleFrameLayoutVersion);
	Hash.AddInt32(Preset.SearchAlgorithmVersion);
	Hash.AddDouble(Preset.ReferencePrimaryRadiusCM);
	Hash.AddDouble(Preset.ReferenceLaunchRadiusCM);
	Hash.AddDouble(Preset.PrimaryCompatibilityToleranceCM);
	AddLaunchModel(Hash, Preset.LaunchModel);
	Hash.AddInt32(Preset.CanonicalScenario.LayoutVersion);
	for (const FABTSM11GravityBodySpec& Body : Preset.CanonicalScenario.Bodies)
	{
		AddBody(Hash, Body);
	}
	Hash.AddInt32(Preset.CanonicalScenario.Target.TargetId);
	Hash.AddVector(Preset.CanonicalScenario.Target.CenterCM);
	Hash.AddDouble(Preset.CanonicalScenario.Target.HitRadiusCM);
	Hash.AddDouble(
		Preset.CanonicalScenario.Target.GeometricContactRadiusCM);
	Hash.AddBool(
		Preset.CanonicalScenario.Target
			.bUseSeparateGeometricContactCenter);
	Hash.AddVector(
		Preset.CanonicalScenario.Target.GeometricContactCenterCM);
	Hash.AddInt32(
		Preset.CanonicalScenario.Target.RequiredQualifiedAssistCount);
	Hash.AddDouble(
		Preset.CanonicalScenario.Target.MinimumQualifyingCorridorQuality);
	Hash.AddDouble(
		Preset.CanonicalScenario.Target
			.MinimumQualifyingEnergyGainCM2PerSec2);
	Hash.AddBool(
		Preset.CanonicalScenario.Target.bRequireAllowedPassSide);
	Hash.AddVector(Preset.CanonicalScenario.Target.PresentationForward);
	AddSolverConfig(Hash, Preset.SolverConfig);
	Hash.AddDouble(Preset.TargetApproachRadiusCM);
	for (int32 Index = 0; Index < FABTSM11FinaleLayoutPreset::AssistCount; ++Index)
	{
		Hash.AddDouble(Preset.MinimumCertifiedCorridorQuality[Index]);
		Hash.AddDouble(Preset.MinimumCertifiedEnergyGainCM2PerSec2[Index]);
	}
	AddScanContract(Hash, Preset.ScanContract);
	AddInput(Hash, Preset.NominalInput);
	return Hash.Get();
}

uint64 FABTSM11FinaleLayoutHash::ComputePresetHash(
	const FABTSM11FinaleLayoutPreset& Preset)
{
	// PresetHash schema v1 is the source-layout identity. Keep the entry point
	// separate so a future compatibility schema can migrate without changing
	// the meaning of PresetSourceHash.
	return ComputePresetSourceHash(Preset);
}

uint64 FABTSM11FinaleLayoutHash::ComputeScanContractHash(
	const FABTSM11FinaleLayoutPreset& Preset)
{
	using namespace ABTSM11FinaleLayoutHashPrivate;
	FCanonicalHash Hash;
	AddLaunchModel(Hash, Preset.LaunchModel);
	AddScanContract(Hash, Preset.ScanContract);
	AddSolverConfig(Hash, Preset.SolverConfig);
	Hash.AddDouble(Preset.TargetApproachRadiusCM);
	for (int32 Index = 0; Index < FABTSM11FinaleLayoutPreset::AssistCount; ++Index)
	{
		Hash.AddDouble(Preset.MinimumCertifiedCorridorQuality[Index]);
		Hash.AddDouble(Preset.MinimumCertifiedEnergyGainCM2PerSec2[Index]);
	}
	return Hash.Get();
}

uint64 FABTSM11FinaleLayoutHash::ComputeCertifiedBundleHash(
	const FABTSM11FinaleLayoutPreset& Preset)
{
	using namespace ABTSM11FinaleLayoutHashPrivate;
	FCanonicalHash Hash;

	// Domain separator and schema version. The bundle intentionally includes
	// both source identities even though schema v1 makes them equal.
	Hash.AddUInt32(0x11b10002u);
	Hash.AddUInt64(Preset.PresetSourceHash);
	Hash.AddUInt64(Preset.PresetHash);
	Hash.AddUInt32(Preset.CanonicalScenario.ScenarioHash);
	Hash.AddUInt64(Preset.ScanContractHash);
	Hash.AddUInt64(Preset.CertificationHash);
	Hash.AddUInt64(Preset.NominalTrajectoryHash);
	Hash.AddInt32(Preset.PhysicalPlaybackContractVersion);
	Hash.AddUInt64(Preset.PhysicalPlaybackTrajectoryHash);
	for (const FABTSM11PrefixTrustRegion& Region
		: Preset.PrefixTrustRegions)
	{
		AddPrefixRegion(Hash, Region);
		Hash.AddUInt64(Region.RegionHash);
	}
	return Hash.Get();
}

uint64 FABTSM11FinaleLayoutHash::ComputeReportHash(
	const FABTSM11LayoutCertificationReport& Report)
{
	using namespace ABTSM11FinaleLayoutHashPrivate;
	FCanonicalHash Hash;
	Hash.AddInt32(Report.ReportVersion);
	Hash.AddUInt8(Report.EnabledAssistMask);
	Hash.AddUInt64(Report.PresetHash);
	Hash.AddUInt32(Report.ScenarioHash);
	Hash.AddUInt64(Report.ScanContractHash);
	AddInputGrid(Hash, Report.Grid);
	Hash.AddInt32(Report.YawCount);
	Hash.AddInt32(Report.PitchCount);
	Hash.AddInt32(Report.PowerCount);
	Hash.AddInt32(Report.TotalSampleCount);
	Hash.AddInt32(Report.SolverInvocationCount);
	Hash.AddInt32(Report.InvalidRequestCount);
	Hash.AddInt32(Report.TargetContactCount);
	Hash.AddInt32(Report.TargetHitCount);
	Hash.AddInt32(Report.BypassTargetHitCount);
	for (const int32 Count : Report.PrefixSampleCounts)
	{
		Hash.AddInt32(Count);
	}
	for (const FABTSM11PrefixComponentSummary& Prefix : Report.Prefixes)
	{
		Hash.AddInt32(Prefix.PrefixLevel);
		Hash.AddInt32(Prefix.SampleCount);
		Hash.AddInt32(Prefix.ComponentCount);
		Hash.AddInt32(Prefix.NominalComponentSampleCount);
		AddInput(Hash, Prefix.Minimum);
		AddInput(Hash, Prefix.Maximum);
		AddPrefixRegion(Hash, Prefix.TrustRegion);
		AddPrefixRegion(Hash, Prefix.PlayableAimRegion);
		Hash.AddDouble(Prefix.PlayableAimYawWidthPixels);
		Hash.AddDouble(Prefix.PlayableAimPitchWidthPixels);
		Hash.AddInt32(Prefix.PlayablePowerSliceCount);
		Hash.AddDouble(Prefix.PlayablePowerMinimum);
		Hash.AddDouble(Prefix.PlayablePowerMaximum);
	}
	for (const FABTSM11CertificationSample& Sample : Report.Samples)
	{
		Hash.AddUInt8(Sample.HighestPrefixLevel);
		Hash.AddUInt8(Sample.ValidAssistMask);
		Hash.AddUInt8(static_cast<uint8>(Sample.Termination));
		Hash.AddBool(Sample.bTargetContact);
		Hash.AddBool(Sample.bBypassTargetHit);
		Hash.AddBool(Sample.bWrongOrder);
		Hash.AddBool(Sample.bExceededOrbitLimit);
		Hash.AddUInt64(Sample.TrajectoryHash);
	}
	Hash.AddBool(Report.bPassed);
	Hash.AddString(Report.Failure);
	return Hash.Get();
}

uint64 FABTSM11FinaleLayoutHash::ComputeCertificationSuiteHash(
	const FABTSM11CertificationSuiteReport& Suite)
{
	using namespace ABTSM11FinaleLayoutHashPrivate;
	FCanonicalHash Hash;
	Hash.AddUInt32(0x11b20002u);
	Hash.AddInt32(Suite.SuiteVersion);
	const auto AddReport = [&Hash](
		const FABTSM11LayoutCertificationReport& Report)
	{
		// Bind both the stored child identity and its canonical replay. This
		// makes a stale ReportHash or a mutated report body independently
		// visible at the suite boundary.
		Hash.AddUInt64(Report.ReportHash);
		Hash.AddUInt64(
			FABTSM11FinaleLayoutHash::ComputeReportHash(Report));
	};
	AddReport(Suite.Baseline);
	AddReport(Suite.HalfCellBaseline);
	AddReport(Suite.RefinedBaseline);
	for (int32 Index = 0; Index < Suite.Ablations.Num(); ++Index)
	{
		Hash.AddUInt8(Suite.AblationMasks[Index]);
		AddReport(Suite.Ablations[Index]);
		AddReport(Suite.HalfCellAblations[Index]);
		AddReport(Suite.RefinedAblations[Index]);
	}
	Hash.AddInt32(Suite.DiscoverySampleCount);
	Hash.AddInt32(Suite.RefinementSampleCount);
	Hash.AddInt32(Suite.TotalSolverInvocationCount);
	Hash.AddInt32(Suite.RefinementIterationCount);
	Hash.AddBool(Suite.bDiscoveryCoverageComplete);
	Hash.AddBool(Suite.bClosureConverged);
	Hash.AddBool(Suite.bPassed);
	Hash.AddString(Suite.Failure);
	return Hash.Get();
}

uint64 FABTSM11FinaleLayoutHash::ComputeTrustRegionHash(
	const FABTSM11PrefixTrustRegion& Region)
{
	using namespace ABTSM11FinaleLayoutHashPrivate;
	FCanonicalHash Hash;
	AddPrefixRegion(Hash, Region);
	return Hash.Get();
}

uint32 FABTSM11FinaleLayoutHash::FoldScenarioHash(const uint64 PresetHash)
{
	uint32 Result = static_cast<uint32>(PresetHash)
		^ static_cast<uint32>(PresetHash >> 32);
	if (Result == 0)
	{
		Result = 0x11b00001u;
	}
	return Result;
}
