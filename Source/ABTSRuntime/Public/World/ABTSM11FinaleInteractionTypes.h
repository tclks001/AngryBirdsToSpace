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
	Failed,
	Recovering
};

/**
 * Read-only M11 narrative fact consumed by Integration environment assembly.
 *
 * This is deliberately separate from IsFinaleActive(): aiming needs the
 * finale HUD/input lease while the main world must still present its surface
 * environment. M11 publishes only the phase; it never selects a rendering
 * profile or changes remote-preview presentation through this contract.
 */
enum class EABTSM11FinaleEnvironmentStage : uint8
{
	GroundLaunch = 0,
	AtmosphereTransition = 1,
	DeepSpace = 2,
	Recovering = 3
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

ABTSRUNTIME_API bool ABTSM11IsResettableFinaleState(
	EABTSM11FinaleInteractionState State);

/** Pure gate shared by the post-hit completion, HUD and controller routes. */
ABTSRUNTIME_API bool ABTSM11ShouldShowFinaleEndScreen(
	bool bProductionBinding,
	bool bTimelineSucceeded);
ABTSRUNTIME_API bool ABTSM11ShouldExitFromFinaleEndScreen(
	bool bEndScreenActive,
	bool bActivationRequested);

/**
 * Pure resolver for the read-only finale environment stage.
 *
 * A launched or still-visible failed attempt enters DeepSpace only after the
 * immutable released trajectory reaches AssistEnter(1). Missing or invalid
 * event evidence stays in AtmosphereTransition (fail closed) rather than
 * selecting deep space. The interaction state changes to Recovering exactly
 * when the failure timeline reaches full black.
 */
ABTSRUNTIME_API EABTSM11FinaleEnvironmentStage
ABTSM11ResolveFinaleEnvironmentStage(
	EABTSM11FinaleInteractionState State,
	double PlaybackElapsedSeconds,
	const FABTSM11TrajectoryResult* ReleasedTrajectoryResult);

/** Frozen M11 copy of the existing M6 pull presentation/input constants. */
struct ABTSRUNTIME_API FABTSM11M6InputParityProfile
{
	static constexpr double MinimumPullDistanceCM = 120.0;
	static constexpr double MaximumPullDistanceCM = 430.0;
	static constexpr double PowerWheelStep = 0.08;
	static constexpr double MaximumAimPlaneOffsetCM = 260.0;
	static constexpr double LaunchTargetLiftCM = 65.0;
	static constexpr double BirdInPouchOffsetCM = 20.0;
	/** Finale-only extra clearance along the pouch launch axis. */
	static constexpr double SpaceFormationPouchForwardClearanceCM = 25.0;
	static constexpr double PouchPickRadiusPixels = 125.0;
};

/**
 * Converts a finale-local launch direction to the frozen Yaw/Pitch domain
 * while preserving the separately controlled Power value.
 */
ABTSRUNTIME_API bool ABTSM11MapLocalLaunchDirectionToInput(
	const FABTSM11FinaleLaunchModel& LaunchModel,
	const FVector3d& LocalLaunchDirection,
	double Power,
	FABTSM11FinaleLaunchInput& OutInput);

ABTSRUNTIME_API bool ABTSM11CanStartLatestOnlyPreview(
	bool bDirty,
	bool bSolveInFlight);

ABTSRUNTIME_API bool ABTSM11CanPublishLatestOnlyPreview(
	int64 SubmittedRevision,
	int64 CurrentRevision,
	bool bInputMatches);

/**
 * Small input contract used by automation and input routers: the press that
 * enters finale aim may arm that same drag gesture, while Reset/focus loss
 * always disarms it before a synthetic release can launch.
 */
class ABTSRUNTIME_API FABTSM11PrimaryReleaseGate final
{
public:
	void Enter(bool bEntryButtonDown);
	void Reset();
	void UpdateEntryButtonState(bool bButtonDown);
	void OnPrimaryPressed(bool bIsAiming);
	bool OnPrimaryReleased(bool bIsAiming);

	bool IsLaunchArmed() const { return bLaunchArmed; }
	bool IsWaitingForEntryRelease() const
	{
		return bWaitingForEntryRelease;
	}

private:
	bool bWaitingForEntryRelease = false;
	bool bLaunchArmed = false;
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
	/**
	 * Applies the delta between successive absolute cursor-authored
	 * directions. Near/stable sensitivity therefore remains frame-rate
	 * independent without pulling the input toward any nominal answer.
	 */
	void SetAbsoluteDirectionInput(
		const FABTSM11FinaleLaunchInput& Input);
	/** Power remains the same independent 0.08-per-wheel channel as M6. */
	void SetDesiredPower(double Power);
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
	FABTSM11FinaleLaunchInput LastAbsoluteDirectionInput;
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
	int32 GetGeometryBuildCount() const { return GeometryBuildCount; }

private:
	EABTSM11PreviewTarget LatchedTarget = EABTSM11PreviewTarget::Assist1;
	EABTSM11PreviewTarget PendingTarget = EABTSM11PreviewTarget::Assist1;
	EABTSM11PreviewTarget CachedSelectionTarget =
		EABTSM11PreviewTarget::Assist1;
	FABTSM11PreviewSelection CachedSelection;
	uint64 CachedResultHash = 0;
	double PendingSeconds = 0.0;
	int32 GeometryBuildCount = 0;
	bool bInitialized = false;
	bool bCachedSelectionValid = false;
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
 * state of the frozen nominal physical tail. Editor Candidate presentation
 * instead hands off at the latest shape-valid released state, blends C2 into
 * the tangent 3D contact circle, and follows that circle to first contact.
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
	/**
	 * Editor-only v2.1 experience result. This is a qualified intercept at
	 * the candidate target radius, never a certified 800 cm UFO contact.
	 */
	bool bCandidateQualifiedIntercept = false;
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
	bool BuildCandidateQualified(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11TrajectoryResult& ReleasedQualifiedResult,
		const FABTSM11PrefixClassification& Classification);
	bool BuildCandidatePresentationContact(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11TrajectoryResult& ReleasedQualifiedResult,
		const FABTSM11PrefixClassification& Classification,
		const FABTSM11TerminalTransferContract& TransferContract = {});
	bool Sample(
		double TimeSeconds,
		FVector3d& OutPositionCM,
		FVector3d& OutVelocityCMPerSec,
		EABTSM11PlaybackSegmentKind* OutSegmentKind = nullptr) const;
};

enum class EABTSM11FailurePresentationPhase : uint8
{
	Inactive = 0,
	Hold,
	FadeToBlack,
	BlackHold,
	FadeFromBlack,
	Complete
};

struct ABTSRUNTIME_API FABTSM11FailurePresentationConfig
{
	double ReadableHoldSeconds = 1.25;
	double FadeToBlackSeconds = 0.60;
	double BlackHoldSeconds = 0.40;
	double FadeFromBlackSeconds = 0.45;

	bool IsValid() const;
};

/** Deterministic failure fade clock; World restoration is emitted exactly once. */
class ABTSRUNTIME_API FABTSM11FailurePresentationTimeline final
{
public:
	bool Begin(const FABTSM11FailurePresentationConfig& InConfig);
	void Reset();
	void Advance(double DeltaSeconds, bool& bOutShouldRestoreWorld);

	bool IsActive() const;
	bool IsComplete() const;
	double GetBlackoutAlpha() const;
	double GetSecondsUntilRestore() const;
	EABTSM11FailurePresentationPhase GetPhase() const;

private:
	FABTSM11FailurePresentationConfig Config;
	double ElapsedSeconds = 0.0;
	bool bStarted = false;
	bool bRestoreWorldIssued = false;
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

/** Presentation-only pull pose. The solver still launches at PouchLocalPositionCM. */
ABTSRUNTIME_API FVector3d ABTSM11ComputeAimPouchLocalPosition(
	const FABTSM11FinaleLaunchModel& LaunchModel,
	const FABTSM11FinaleLaunchInput& Input,
	double MinimumPullDistanceCM,
	double MaximumPullDistanceCM,
	double MaximumPitchDropCM);

/**
 * Finds a visual stop before an analytic body-collision endpoint. The
 * authoritative trajectory and playback-plan hash remain unchanged.
 */
ABTSRUNTIME_API double ABTSM11ResolveFailurePresentationEndTime(
	const FABTSM11FinaleLayoutPreset& Preset,
	const FABTSM11TrajectoryResult& Result,
	const FABTSM11PlaybackPlan& Plan,
	double BirdClearanceCM);

/**
 * Schedules the failure fade so immutable route playback and its camera
 * director reach the presentation endpoint on the exact full-black recovery
 * boundary.
 */
ABTSRUNTIME_API bool ABTSM11ResolveFailurePresentationSchedule(
	double PlaybackStartTimeSeconds,
	double PlaybackEndTimeSeconds,
	double PlaybackTimeScale,
	const FABTSM11FailurePresentationConfig& DesiredConfig,
	double& OutFailureStartTimeSeconds,
	FABTSM11FailurePresentationConfig& OutScheduledConfig);

/** Shared circular-panel clipping used by every M11 diagram primitive. */
ABTSRUNTIME_API bool ABTSM11ClipDiagramSegmentToUnitCircle(
	FVector2d& InOutStart,
	FVector2d& InOutEnd);

ABTSRUNTIME_API EABTSM11FailureReason ABTSM11ClassifyFailure(
	const FABTSM11TrajectoryResult& Result,
	const FABTSM11PrefixClassification& Classification);

ABTSRUNTIME_API const TCHAR* ABTSM11FailureReasonLabel(
	EABTSM11FailureReason Reason);
