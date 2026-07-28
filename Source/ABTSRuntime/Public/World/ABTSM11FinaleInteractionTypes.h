// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "World/ABTSM11FinaleLayoutTypes.h"

enum class EABTSM11FinaleInteractionState : uint8
{
	Locked = 0,
	Ready,
	Aiming,
	ReleasePending,
	Launched,
	TargetHit,
	Failed
};

enum class EABTSM11PrefixStabilizerPhase : uint8
{
	Free = 0,
	Near,
	Stable
};

enum class EABTSM11PreviewTarget : uint8
{
	Assist1 = 0,
	Assist2,
	Assist3,
	UFO
};

enum class EABTSM11PlaybackSegmentKind : uint8
{
	/** Exact result produced by the player's immutable Release input. */
	PlayerAuthoritative = 0,
	/** Explicitly presented post-F4 C2 cinematic transfer. */
	VisibleTerminalTransfer,
	/** Frozen M11-B nominal physical-playback tail after the transfer. */
	CertifiedNominalTail
};

enum class EABTSM11FailureReason : uint8
{
	None = 0,
	MissAssist1,
	InvalidAssist1,
	MissAssist2,
	InvalidAssist2,
	MissAssist3,
	InvalidAssist3,
	BodyCollision,
	WrongOrder,
	SolarCaptured,
	OutOfBounds,
	Timeout,
	MissUFO,
	SolverFailure
};

struct ABTSRUNTIME_API FABTSM11PrefixStabilizerConfig
{
	double NearSensitivityScale = 0.45;
	double CaptureConfirmationSeconds = 0.20;
	double ReleaseConfirmationSeconds = 0.16;

	bool IsValid() const;
};

/**
 * Visible, cancellable protection for the certified F1/F2/F3 trust kernels.
 *
 * DesiredInput is never pulled toward the nominal solution. ControlledInput
 * is clamped only after a prefix has been confirmed, and only to that
 * prefix's frozen core box. Pushing DesiredInput beyond the wider release box
 * for the release dwell exits protection.
 */
class ABTSRUNTIME_API FABTSM11PrefixStabilizer final
{
public:
	bool Initialize(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11FinaleLaunchInput& InitialInput,
		const FABTSM11PrefixStabilizerConfig& InConfig = {});
	void Reset(const FABTSM11FinaleLaunchInput& Input);
	void CancelProtection();

	void ApplyInputDelta(
		double YawDeltaDegrees,
		double PitchDeltaDegrees,
		double PowerDelta);
	void Update(
		double DeltaSeconds,
		const FABTSM11PrefixClassification& Classification);

	const FABTSM11FinaleLaunchInput& GetDesiredInput() const
	{
		return DesiredInput;
	}
	const FABTSM11FinaleLaunchInput& GetControlledInput() const
	{
		return ControlledInput;
	}
	int32 GetStablePrefixLevel() const { return StablePrefixLevel; }
	int32 GetNearPrefixLevel() const { return NearPrefixLevel; }
	EABTSM11PrefixStabilizerPhase GetPhase() const;
	double GetSensitivityScale() const;

private:
	bool IsInsideExpandedRegion(
		const FABTSM11FinaleLaunchInput& Input,
		const FABTSM11PrefixTrustRegion& Region,
		double MarginCells) const;
	void ClampToLaunchDomain(FABTSM11FinaleLaunchInput& Input) const;
	void RefreshControlledInput();

	FABTSM11FinaleLaunchModel LaunchModel;
	FABTSM11LayoutScanContract ScanContract;
	TStaticArray<FABTSM11PrefixTrustRegion, 3> TrustRegions;
	FABTSM11PrefixStabilizerConfig Config;
	FABTSM11FinaleLaunchInput DesiredInput;
	FABTSM11FinaleLaunchInput ControlledInput;
	int32 StablePrefixLevel = 0;
	int32 NearPrefixLevel = 0;
	double CaptureSeconds = 0.0;
	double ReleaseSeconds = 0.0;
	bool bInitialized = false;
};

struct ABTSRUNTIME_API FABTSM11PreviewSelection
{
	EABTSM11PreviewTarget Target = EABTSM11PreviewTarget::Assist1;
	FVector3d TargetCenterCM = FVector3d::ZeroVector;
	FVector3d ClosestTrajectoryPositionCM = FVector3d::ZeroVector;
	FVector3d IncomingDirection = FVector3d::ForwardVector;
	double ClosestDistanceCM = TNumericLimits<double>::Max();
	double TargetRadiusCM = 0.0;
	bool bEnteredTargetRegion = false;
};

/** Hysteretic selector for the earliest unfinished target in the prediction. */
class ABTSRUNTIME_API FABTSM11PreviewTargetSelector final
{
public:
	void Reset();
	FABTSM11PreviewSelection Update(
		double DeltaSeconds,
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11TrajectoryResult& Result,
		const FABTSM11PrefixClassification& Classification);

	EABTSM11PreviewTarget GetLatchedTarget() const { return LatchedTarget; }

private:
	EABTSM11PreviewTarget LatchedTarget = EABTSM11PreviewTarget::Assist1;
	EABTSM11PreviewTarget PendingTarget = EABTSM11PreviewTarget::Assist1;
	double PendingSeconds = 0.0;
	bool bInitialized = false;
};

struct ABTSRUNTIME_API FABTSM11PlaybackPoint
{
	double TimeSeconds = 0.0;
	FVector3d PositionCM = FVector3d::ZeroVector;
	FVector3d VelocityCMPerSec = FVector3d::ZeroVector;
	EABTSM11PlaybackSegmentKind SegmentKind =
		EABTSM11PlaybackSegmentKind::PlayerAuthoritative;
};

struct ABTSRUNTIME_API FABTSM11TerminalTransferContract
{
	int32 ContractVersion = 1;
	double MinimumDurationSeconds = 8.0;
	double MaximumDurationSeconds = 48.0;
	double DurationStepSeconds = 4.0;
	double SampleStepSeconds = 1.0 / 30.0;
	double BodyClearanceCM = 250.0;
	double MaximumAccelerationCMPerSec2 = 240.0;
	double MaximumJerkCMPerSec3 = 80.0;

	bool IsValid() const;
};

/**
 * Cached deterministic deep-space path.
 *
 * A non-F4 release contains only player-authoritative points. An F4 release
 * first tries the same input against the physical target. Only if that misses
 * may a visible, versioned C2 transfer join the player F4 endpoint to a future
 * state of the frozen nominal physical tail.
 */
struct ABTSRUNTIME_API FABTSM11PlaybackPlan
{
	TArray<FABTSM11PlaybackPoint> Points;
	uint64 ReleasedTrajectoryHash = 0;
	uint64 PhysicalTrajectoryHash = 0;
	uint64 PlanHash = 0;
	int32 TransferContractVersion = 0;
	double DurationSeconds = 0.0;
	double TransferStartTimeSeconds = -1.0;
	double TransferEndTimeSeconds = -1.0;
	bool bQualifiedF4 = false;
	bool bPhysicalTargetHit = false;
	bool bUsesVisibleTerminalTransfer = false;
	FString Failure;

	void Reset();
	bool Build(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11TrajectoryResult& ReleasedQualifiedResult,
		const FABTSM11PrefixClassification& Classification,
		const FABTSM11TrajectoryResult* SameInputPhysicalResult,
		const FABTSM11TrajectoryResult* NominalPhysicalResult,
		const FABTSM11TerminalTransferContract& TransferContract = {});
	bool Sample(
		double TimeSeconds,
		FVector3d& OutPositionCM,
		FVector3d& OutVelocityCMPerSec,
		EABTSM11PlaybackSegmentKind* OutSegmentKind = nullptr) const;
};

struct ABTSRUNTIME_API FABTSM11DiagramPoint
{
	FVector2d Position = FVector2d::ZeroVector;
	bool bHiddenByBody = false;
	EABTSM11PlaybackSegmentKind SegmentKind =
		EABTSM11PlaybackSegmentKind::PlayerAuthoritative;
};

struct ABTSRUNTIME_API FABTSM11DiagramBody
{
	int32 BodyId = INDEX_NONE;
	EABTSM110FinaleGravityRole Role = EABTSM110FinaleGravityRole::Primary;
	FVector2d Center = FVector2d::ZeroVector;
	double VisualRadius = 0.0;
	double CollisionRadius = 0.0;
	double InfluenceRadius = 0.0;
	FLinearColor Color = FLinearColor::White;
};

struct ABTSRUNTIME_API FABTSM11DiagramGridSegment
{
	FVector2d Start = FVector2d::ZeroVector;
	FVector2d End = FVector2d::ZeroVector;
	bool bHiddenHemisphere = false;
};

struct ABTSRUNTIME_API FABTSM11OrbitalDiagramSnapshot
{
	TArray<FABTSM11DiagramPoint> Trajectory;
	TStaticArray<FABTSM11DiagramBody, FABTSM11GravityScenario::BodyCount>
		Bodies;
	TArray<FABTSM11DiagramGridSegment> PrimaryGrid;
	FVector2d UFOCenter = FVector2d::ZeroVector;
	double UFORadius = 0.0;
	FVector3d PlaneOriginCM = FVector3d::ZeroVector;
	FVector3d PlaneAxisX = FVector3d::ForwardVector;
	FVector3d PlaneAxisY = FVector3d::RightVector;
	FVector3d PlaneNormal = FVector3d::UpVector;
	double FitRadiusCM = 1.0;
	uint64 SourceTrajectoryHash = 0;
	bool bValid = false;
};

class ABTSRUNTIME_API FABTSM11OrbitalDiagramBuilder final
{
public:
	static bool Build(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM110FinaleLocalFrame& FinaleFrame,
		TConstArrayView<FABTSM11PlaybackPoint> PlaybackPoints,
		uint64 SourceTrajectoryHash,
		FABTSM11OrbitalDiagramSnapshot& OutSnapshot,
		int32 MaximumTrajectoryPointCount = 900);
};

ABTSRUNTIME_API EABTSM11FailureReason ABTSM11ClassifyFailure(
	const FABTSM11TrajectoryResult& Result,
	const FABTSM11PrefixClassification& Classification);

ABTSRUNTIME_API const TCHAR* ABTSM11FailureReasonLabel(
	EABTSM11FailureReason Reason);
