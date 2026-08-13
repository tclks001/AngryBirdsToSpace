// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "Contracts/ABTSM11ConnectivityClosureContract.h"
#include "World/ABTSM11GravityAssistTypes.h"

struct FABTSM11CertificationSuiteReport;

/** Canonical, camera-independent M11 finale launch input. */
struct ABTSRUNTIME_API FABTSM11FinaleLaunchInput
{
	double YawDegrees = 0.0;
	double PitchDegrees = 30.0;
	double Power = 0.975;

	bool IsFinite() const;
};

/**
 * The sole Yaw/Pitch/Power -> solver initial-state mapping.
 *
 * Positive yaw rotates +X toward +Y. Positive pitch rotates the yawed
 * horizontal direction toward +Z. Power changes speed only.
 */
struct ABTSRUNTIME_API FABTSM11FinaleLaunchModel
{
	int32 LaunchModelVersion = 1;
	FVector3d PouchLocalPositionCM = FVector3d(0.0, 0.0, 180.0);

	double MinimumYawDegrees = -18.0;
	double MaximumYawDegrees = 18.0;
	double MinimumPitchDegrees = 0.0;
	double MaximumPitchDegrees = 60.0;
	double MinimumPower = 0.0;
	double MaximumPower = 1.0;

	double MinimumLaunchSpeedCMPerSec = 400.0;
	double MaximumLaunchSpeedCMPerSec = 1050.0;
	double MaximumSimulationTimeSeconds = 700.0;

	bool IsValid(FString* OutFailure = nullptr) const;
	bool Contains(const FABTSM11FinaleLaunchInput& Input) const;
	FVector3d MapDirection(const FABTSM11FinaleLaunchInput& Input) const;
	double MapSpeedCMPerSec(const FABTSM11FinaleLaunchInput& Input) const;
	bool ApplyToRequest(
		const FABTSM11FinaleLaunchInput& Input,
		FABTSM11TrajectoryRequest& InOutRequest,
		FString* OutFailure = nullptr) const;
};

/** One axis-aligned compact trust box consumed by the later M11-C stabilizer. */
struct ABTSRUNTIME_API FABTSM11PrefixTrustRegion
{
	int32 PrefixLevel = 0;
	FABTSM11FinaleLaunchInput Minimum;
	FABTSM11FinaleLaunchInput Maximum;
	double CaptureMarginCells = 1.0;
	double ReleaseMarginCells = 2.0;
	uint64 RegionHash = 0;

	bool IsValid(const FABTSM11FinaleLaunchModel& LaunchModel) const;
	bool Contains(const FABTSM11FinaleLaunchInput& Input) const;
	bool Contains(const FABTSM11PrefixTrustRegion& Other) const;
};

/** Frozen full-domain sampling and connectivity policy. */
struct ABTSRUNTIME_API FABTSM11LayoutScanContract
{
	/** Frozen v3 ceiling; covers the Rank12 49x73x113 refinement lattice. */
	static constexpr int32 BridgeClosureV3MaximumRefinementSampleCount =
		500000;

	int32 ScanContractVersion = 2;
	double YawStepDegrees = 1.5;
	double PitchStepDegrees = 2.0;
	double PowerStep = 0.025;

	int32 BoundaryRefinementDepth = 3;
	int32 DiscoveryPolicyVersion = 1;
	int32 RefinementHaloCoarseCells = 1;
	int32 MaximumRefinementIterations = 3;
	/** Legacy v2 default. MakeBridgeClosureV3 replaces it with the v3 ceiling. */
	int32 MaximumRefinementSampleCount = 250000;
	double FinalYawPrecisionDegrees = 0.1875;
	double FinalPitchPrecisionDegrees = 0.25;
	double FinalPowerPrecision = 0.003125;

	/** v2 is face-only; v3 discovery is 18-neighbor with proven bridges only. */
	int32 Connectivity = 6;
	FABTSM11BridgeClosurePolicy BridgeClosurePolicy;
	int32 MaximumCompletePrimaryOrbits = 1;

	double MinimumF4YawWidthDegrees = 0.1875;
	double MinimumF4PitchWidthDegrees = 0.5;
	int32 MinimumPlayableF4PowerSliceCount = 2;
	double MaximumLockedPowerDeficitFromFullPower = 0.05;
	double MinimumF1OnsetPower = 0.88;
	double MaximumF1OnsetPower = 0.95;
	double TrustErosionCells = 1.0;

	int32 ReferenceResolutionX = 1920;
	int32 ReferenceResolutionY = 1080;
	double ReferenceDPIScale = 1.0;
	double MinimumScreenTrustWidthPixels = 8.0;

	bool bIncludeHalfCellOffsetPass = true;

	bool IsValid(
		const FABTSM11FinaleLaunchModel& LaunchModel,
		FString* OutFailure = nullptr) const;
	bool UsesBridgeClosureV3() const;
	static FABTSM11LayoutScanContract MakeBridgeClosureV3(
		const FABTSM11LayoutScanContract& LegacyV2);
	int32 GetYawCount(const FABTSM11FinaleLaunchModel& LaunchModel) const;
	int32 GetPitchCount(const FABTSM11FinaleLaunchModel& LaunchModel) const;
	int32 GetPowerCount(const FABTSM11FinaleLaunchModel& LaunchModel) const;
	int32 GetSampleCount(const FABTSM11FinaleLaunchModel& LaunchModel) const;
	FABTSM11FinaleLaunchInput GetInput(
		const FABTSM11FinaleLaunchModel& LaunchModel,
		int32 YawIndex,
		int32 PitchIndex,
		int32 PowerIndex) const;
};

/**
 * A sampling window over the immutable launch model.
 *
 * Changing this grid never changes Yaw/Pitch/Power -> velocity mapping. This
 * separation is mandatory for half-cell and boundary-refinement passes.
 */
struct ABTSRUNTIME_API FABTSM11InputGrid
{
	FABTSM11FinaleLaunchInput Minimum;
	FABTSM11FinaleLaunchInput Maximum;
	double YawStepDegrees = 1.5;
	double PitchStepDegrees = 2.0;
	double PowerStep = 0.025;

	bool IsValid(
		const FABTSM11FinaleLaunchModel& LaunchModel,
		FString* OutFailure = nullptr) const;
	int32 GetYawCount() const;
	int32 GetPitchCount() const;
	int32 GetPowerCount() const;
	int32 GetSampleCount() const;
	FABTSM11FinaleLaunchInput GetInput(
		int32 YawIndex,
		int32 PitchIndex,
		int32 PowerIndex) const;

	static FABTSM11InputGrid MakeFullDomain(
		const FABTSM11FinaleLaunchModel& LaunchModel,
		const FABTSM11LayoutScanContract& ScanContract);
};

/**
 * Immutable reference-scale layout. All centers and vectors are expressed in
 * FABTSM110FinaleLocalFrame coordinates; no world transform is serialized.
 */
struct ABTSRUNTIME_API FABTSM11FinaleLayoutPreset
{
	static constexpr int32 AssistCount = FABTSM11GravityScenario::AssistCount;

	int32 PresetVersion = 1;
	int32 CompatibleGeneratorVersion = 3;
	int32 CompatibleFrameLayoutVersion = 1;
	int32 SearchAlgorithmVersion = 1;

	double ReferencePrimaryRadiusCM = 10000.0;
	double ReferenceLaunchRadiusCM = 10180.0;
	double PrimaryCompatibilityToleranceCM = 25.0;

	FABTSM11FinaleLaunchModel LaunchModel;
	FABTSM11GravityScenario CanonicalScenario;
	FABTSM11SolverConfig SolverConfig;
	double TargetApproachRadiusCM = 2400.0;

	TStaticArray<double, AssistCount> MinimumCertifiedCorridorQuality;
	TStaticArray<double, AssistCount> MinimumCertifiedEnergyGainCM2PerSec2;

	FABTSM11LayoutScanContract ScanContract;
	FABTSM11FinaleLaunchInput NominalInput;
	TStaticArray<FABTSM11PrefixTrustRegion, AssistCount> PrefixTrustRegions;

	/** Hash of the immutable source layout and solver/certification inputs. */
	uint64 PresetSourceHash = 0;

	/**
	 * Compatibility identity for downstream M11 consumers.
	 *
	 * Schema v1 deliberately equals PresetSourceHash. Both fields are retained
	 * in the certified bundle so a future PresetHash schema migration cannot
	 * silently reinterpret an old source hash.
	 */
	uint64 PresetHash = 0;
	uint64 ScanContractHash = 0;
	uint64 CertificationHash = 0;
	uint64 NominalTrajectoryHash = 0;
	int32 PhysicalPlaybackContractVersion = 1;
	/**
	 * Frozen result identity for the full launch-to-physical-UFO playback
	 * request. This is deliberately distinct from NominalTrajectoryHash,
	 * whose certified solve terminates at the earlier qualification envelope.
	 */
	uint64 PhysicalPlaybackTrajectoryHash = 0;

	/**
	 * Final manifest identity binding source, scenario, scan, certification,
	 * nominal trajectory and all three frozen prefix trust regions.
	 */
	uint64 CertifiedBundleHash = 0;

	FABTSM11FinaleLayoutPreset();

	bool IsValid(FString* OutFailure = nullptr) const;
	bool BuildRequest(
		const FABTSM11FinaleLaunchInput& Input,
		uint8 EnabledAssistMask,
		FABTSM11TrajectoryRequest& OutRequest,
		FString* OutFailure = nullptr) const;

	/**
	 * Builds the deterministic full-flight request consumed by M11-C/D.
	 *
	 * It starts from the same pouch state and uses the same bodies, solver and
	 * qualification rules as BuildRequest, but derives its terminal sphere
	 * from the independent physical UFO center/radius. It never resumes from
	 * inside the earlier qualification envelope.
	 */
	bool BuildPhysicalPlaybackRequest(
		const FABTSM11FinaleLaunchInput& Input,
		uint8 EnabledAssistMask,
		FABTSM11TrajectoryRequest& OutRequest,
		FString* OutFailure = nullptr) const;

	/** Search seed only. It is not a certified production layout. */
	static FABTSM11FinaleLayoutPreset MakeConstructiveSearchSeed();

	/** Frozen output of the M11-B offline search. */
	static FABTSM11FinaleLayoutPreset MakeCertifiedV1();
};

/** Compressed classification of one authoritative M11-A result. */
struct ABTSRUNTIME_API FABTSM11PrefixClassification
{
	uint8 HighestPrefixLevel = 0;
	uint8 ValidAssistMask = 0;
	bool bEnteredTargetApproach = false;
	bool bGeometricTargetContact = false;
	bool bBypassTargetHit = false;
	bool bExceededOrbitLimit = false;
	bool bWrongOrder = false;
	double MinimumTargetDistanceCM = TNumericLimits<double>::Max();
	TStaticArray<double, FABTSM11GravityScenario::AssistCount> CorridorQuality;
	TStaticArray<double, FABTSM11GravityScenario::AssistCount> AppliedEnergyGain;

	bool IsF(int32 PrefixLevel) const
	{
		return PrefixLevel >= 1
			&& PrefixLevel <= 4
			&& HighestPrefixLevel >= PrefixLevel;
	}
};

/** One regular-grid sample retained by the certification report. */
struct ABTSRUNTIME_API FABTSM11CertificationSample
{
	uint8 HighestPrefixLevel = 0;
	uint8 ValidAssistMask = 0;
	EABTSM11TrajectoryTermination Termination =
		EABTSM11TrajectoryTermination::InvalidInput;
	bool bTargetContact = false;
	bool bBypassTargetHit = false;
	bool bWrongOrder = false;
	bool bExceededOrbitLimit = false;
	uint64 TrajectoryHash = 0;
};

struct ABTSRUNTIME_API FABTSM11PrefixComponentSummary
{
	int32 PrefixLevel = 0;
	int32 SampleCount = 0;
	int32 ComponentCount = 0;
	int32 NominalComponentSampleCount = 0;
	FABTSM11FinaleLaunchInput Minimum;
	FABTSM11FinaleLaunchInput Maximum;
	FABTSM11PrefixTrustRegion TrustRegion;
	/**
	 * Largest balanced solid yaw/pitch rectangle on the nominal locked-power
	 * slice. Unlike TrustRegion this is a 2-D aimability proof, not a 3-D
	 * stabilizer kernel.
	 */
	FABTSM11PrefixTrustRegion PlayableAimRegion;
	double PlayableAimYawWidthPixels = 0.0;
	double PlayableAimPitchWidthPixels = 0.0;
	int32 PlayablePowerSliceCount = 0;
	double PlayablePowerMinimum = 0.0;
	double PlayablePowerMaximum = 0.0;
};

/** Deterministically ordered output of one complete regular-grid scan. */
struct ABTSRUNTIME_API FABTSM11LayoutCertificationReport
{
	int32 ReportVersion = 3;
	uint8 EnabledAssistMask = 0x7u;
	uint64 PresetHash = 0;
	uint32 ScenarioHash = 0;
	uint64 ScanContractHash = 0;
	FABTSM11InputGrid Grid;
	int32 YawCount = 0;
	int32 PitchCount = 0;
	int32 PowerCount = 0;
	int32 TotalSampleCount = 0;
	int32 SolverInvocationCount = 0;
	int32 InvalidRequestCount = 0;
	int32 TargetContactCount = 0;
	int32 TargetHitCount = 0;
	int32 BypassTargetHitCount = 0;
	TStaticArray<int32, 5> PrefixSampleCounts;
	TStaticArray<FABTSM11PrefixComponentSummary, 4> Prefixes;
	TArray<FABTSM11CertificationSample> Samples;
	uint64 ReportHash = 0;
	bool bPassed = false;
	FString Failure;

	FABTSM11LayoutCertificationReport();
};

/** Stable hashing shared by the preset compiler and certification report. */
class ABTSRUNTIME_API FABTSM11FinaleLayoutHash final
{
public:
	static uint64 ComputePresetSourceHash(
		const FABTSM11FinaleLayoutPreset& Preset);
	static uint64 ComputePresetHash(const FABTSM11FinaleLayoutPreset& Preset);
	static uint64 ComputeScanContractHash(const FABTSM11FinaleLayoutPreset& Preset);
	static uint64 ComputeCertifiedBundleHash(
		const FABTSM11FinaleLayoutPreset& Preset);
	static uint64 ComputeReportHash(const FABTSM11LayoutCertificationReport& Report);
	static uint64 ComputeCertificationSuiteHash(
		const FABTSM11CertificationSuiteReport& Suite);
	static uint64 ComputeTrustRegionHash(
		const FABTSM11PrefixTrustRegion& Region);
	static uint32 FoldScenarioHash(uint64 PresetHash);
};
