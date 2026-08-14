// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "World/ABTSM11FinaleInteractionTypes.h"

enum class EABTSM11FinaleControlAxis : uint8
{
	Yaw = 0,
	Pitch,
	Power
};

enum class EABTSM11F4GuidanceDirection : int8
{
	Decrease = -1,
	Aligned = 0,
	Increase = 1
};

/** Bounded deterministic search used to select a safe interior F4 aim. */
struct ABTSRUNTIME_API FABTSM11F4GuidanceSearchConfig
{
	double MinimumYawStepDegrees = 0.5;
	double MinimumPitchStepDegrees = 0.75;
	double MinimumPowerStep = 0.01;
	int32 MaximumSampleCount = 4096;

	bool IsValid() const;
};

/**
 * One verified F4 sample chosen from the centre of the nearest component
 * discovered outward from nominal. The target itself is always reclassified
 * as F4 before publish.
 */
struct ABTSRUNTIME_API FABTSM11F4GuidanceTarget
{
	FABTSM11FinaleLaunchInput Input;
	double YawToleranceDegrees = 0.0;
	double PitchToleranceDegrees = 0.0;
	double PowerTolerance = 0.0;
	int32 SampleCount = 0;
	int32 F4SampleCount = 0;
	bool bSearchTruncated = false;
	bool bValid = false;

	bool IsValid(const FABTSM11FinaleLaunchModel& LaunchModel) const;
	EABTSM11F4GuidanceDirection GetDirection(
		const FABTSM11FinaleLaunchInput& Current,
		EABTSM11FinaleControlAxis Axis) const;
};

/** Pure-data F4 guidance search; safe to execute on a worker thread. */
class ABTSRUNTIME_API FABTSM11F4GuidanceBuilder final
{
public:
	static bool Build(
		const FABTSM11FinaleLayoutPreset& Preset,
		FABTSM11F4GuidanceTarget& OutTarget,
		FString* OutFailure = nullptr,
		const FABTSM11F4GuidanceSearchConfig& Config = {});
};

enum class EABTSM11ControlSpeedGear : uint8
{
	Coarse = 0,
	Fine,
	UltraFine
};

enum class EABTSM11OverviewInteractionMode : uint8
{
	Select = 0,
	Move
};

enum class EABTSM11FinaleHudCapture : uint8
{
	None = 0,
	AdjustYaw,
	AdjustPitch,
	AdjustPower,
	ScrubTrajectoryProbe,
	PanOverview,
	RotateOverview,
	AdjustOverviewZoom,
	LaunchButton
};

enum class EABTSM11TrajectorySemanticLeg : uint8
{
	Invalid = 0,
	LaunchToAssist1,
	Assist1Encounter,
	Assist1ToAssist2,
	Assist2Encounter,
	Assist2ToAssist3,
	Assist3Encounter,
	Assist3ToTarget,
	TargetApproach
};

enum class EABTSM11ProbeRemapStatus : uint8
{
	Invalid = 0,
	ExactSemanticLeg,
	ClosestMissFallback,
	TrajectoryEndedBeforeLeg
};

struct ABTSRUNTIME_API FABTSM11FinaleControlPanelConfig
{
	double FullRangeDragPixels = 360.0;
	double WheelFullRangeFraction = 0.0025;

	bool IsValid() const;
	double GetGearScale(EABTSM11ControlSpeedGear Gear) const;
};

/** Pure continuous Yaw/Pitch/Power authoring state. It never launches. */
class ABTSRUNTIME_API FABTSM11FinaleControlPanelState final
{
public:
	bool Initialize(
		const FABTSM11FinaleLaunchModel& InLaunchModel,
		const FABTSM11FinaleLaunchInput& InInitialInput,
		const FABTSM11FinaleControlPanelConfig& InConfig = {});
	void SetSpeedGear(EABTSM11ControlSpeedGear InGear);
	bool ApplyDragPixels(EABTSM11FinaleControlAxis Axis, double PixelDelta);
	bool ApplyWheelSteps(EABTSM11FinaleControlAxis Axis, double WheelSteps);
	bool ResetAxis(EABTSM11FinaleControlAxis Axis);
	void ResetAll();

	const FABTSM11FinaleLaunchInput& GetInput() const { return Input; }
	const FABTSM11FinaleLaunchInput& GetInitialInput() const
	{
		return InitialInput;
	}
	EABTSM11ControlSpeedGear GetSpeedGear() const { return SpeedGear; }
	bool IsInitialized() const { return bInitialized; }

private:
	bool ApplyNormalizedDelta(EABTSM11FinaleControlAxis Axis, double Delta);
	void ClampInput();

	FABTSM11FinaleLaunchModel LaunchModel;
	FABTSM11FinaleLaunchInput InitialInput;
	FABTSM11FinaleLaunchInput Input;
	FABTSM11FinaleControlPanelConfig Config;
	EABTSM11ControlSpeedGear SpeedGear = EABTSM11ControlSpeedGear::Coarse;
	bool bInitialized = false;
};

/** Exclusive mouse capture contract used later by HUD-1B. */
class ABTSRUNTIME_API FABTSM11FinaleHudCaptureState final
{
public:
	bool TryBegin(EABTSM11FinaleHudCapture RequestedCapture);
	bool End(EABTSM11FinaleHudCapture ExpectedCapture);
	void CancelForFocusLoss();
	bool CanLaunch() const;
	bool TryBeginLaunch();

	EABTSM11FinaleHudCapture GetCapture() const { return Capture; }
	bool WasFocusLossCancellation() const { return bFocusLossCancellation; }

private:
	EABTSM11FinaleHudCapture Capture = EABTSM11FinaleHudCapture::None;
	bool bFocusLossCancellation = false;
};

/** Sole HUD-1B release gate: no knob/overview/probe MouseUp may launch. */
ABTSRUNTIME_API bool ABTSM11ShouldCommitFinaleHudLaunch(
	EABTSM11FinaleHudCapture Capture,
	bool bReleasedInsideLaunchButton,
	bool bIsAiming);

/** Static capture refresh policy shared by runtime and regression tests. */
ABTSRUNTIME_API bool ABTSM11ShouldRefreshFinaleHudTargetCapture(
	bool bHasFrozenProbe,
	bool bCaptureInitialized,
	bool bAutomaticTargetChanged,
	bool bExplicitProbeMutation);

/** Maps full-viewport pixels into the translated/scaled player HUD Canvas. */
ABTSRUNTIME_API FVector2D ABTSM11MapViewportPointToHudCanvas(
	const FVector2D& ViewportPoint,
	const FVector2D& PlayerViewOrigin,
	const FVector2D& PlayerViewSize,
	const FVector2D& HudCanvasSize);

/** Positive wheel steps zoom the overview in; negative steps zoom it out. */
ABTSRUNTIME_API double ABTSM11ResolveOverviewWheelZoomMultiplier(
	double ZoomPerWheelStep,
	double WheelSteps);

/**
 * Pure-data M11-D four-zone layout. The same boxes drive Canvas drawing and
 * hit testing so DPI scaling and compact viewports cannot desynchronise them.
 */
struct ABTSRUNTIME_API FABTSM11FinaleHudVisualLayout
{
	FBox2D MissionStrip;
	FBox2D OrbitPanel;
	FBox2D ControlDeck;
	FBox2D PreviewBay;
	FVector2D DiagramCenter = FVector2D::ZeroVector;
	float DiagramRadius = 1.0f;
	TStaticArray<FVector2D, 3> KnobCenters;
	float KnobRadius = 30.0f;
	FBox2D GearCoarse;
	FBox2D GearFine;
	FBox2D GearUltraFine;
	FBox2D LaunchButton;
	FBox2D SelectButton;
	FBox2D MoveButton;
	FBox2D ResetViewButton;
	FBox2D RebasePipButton;
	FBox2D FollowAutoButton;
	bool bCompact = false;
	bool bValid = false;
};

ABTSRUNTIME_API bool ABTSM11BuildFinaleHudVisualLayout(
	const FVector2D& CanvasSize,
	float KnobRadiusViewportHeightFraction,
	float MinimumKnobRadiusPixels,
	float MaximumKnobRadiusPixels,
	FABTSM11FinaleHudVisualLayout& OutLayout);

struct ABTSRUNTIME_API FABTSM11OrbitalScenePoint
{
	double TimeSeconds = 0.0;
	double ArcLengthCM = 0.0;
	FVector3d PositionCM = FVector3d::ZeroVector;
	FVector3d VelocityCMPerSec = FVector3d::ZeroVector;
	EABTSM11PlaybackSegmentKind SegmentKind =
		EABTSM11PlaybackSegmentKind::PlayerAuthoritative;
};

struct ABTSRUNTIME_API FABTSM11OrbitalSceneBody
{
	int32 BodyIndex = INDEX_NONE;
	int32 BodyId = INDEX_NONE;
	EABTSM110FinaleGravityRole Role = EABTSM110FinaleGravityRole::Primary;
	FVector3d CenterCM = FVector3d::ZeroVector;
	FVector3d VirtualVelocityCMPerSec = FVector3d::ZeroVector;
	double VisualRadiusCM = 0.0;
	double CollisionRadiusCM = 0.0;
	double InfluenceRadiusCM = 0.0;
};

struct ABTSRUNTIME_API FABTSM11TrajectorySemanticSegment
{
	EABTSM11TrajectorySemanticLeg Leg = EABTSM11TrajectorySemanticLeg::Invalid;
	int32 StartPointIndex = INDEX_NONE;
	int32 ClosestPointIndex = INDEX_NONE;
	int32 EndPointIndex = INDEX_NONE;
	int32 ContextBodyIndex = INDEX_NONE;
	bool bContextIsTarget = false;
	bool bEncounter = false;

	bool IsValid(int32 PointCount) const;
};

struct ABTSRUNTIME_API FABTSM11TrajectorySemanticMap
{
	TArray<FABTSM11TrajectorySemanticSegment> Segments;

	const FABTSM11TrajectorySemanticSegment* Find(
		EABTSM11TrajectorySemanticLeg Leg) const;
	bool ResolvePoint(
		EABTSM11TrajectorySemanticLeg Leg,
		double PhaseWithinLeg,
		TConstArrayView<FABTSM11OrbitalScenePoint> Points,
		int32& OutPointA,
		int32& OutPointB,
		double& OutAlpha) const;
	double ComputePhase(
		const FABTSM11TrajectorySemanticSegment& Segment,
		double TimeSeconds,
		TConstArrayView<FABTSM11OrbitalScenePoint> Points) const;
};

/** Immutable, unprojected current-result snapshot consumed by Select/Move. */
struct ABTSRUNTIME_API FABTSM11OrbitalSceneSnapshot
{
	TArray<FABTSM11OrbitalScenePoint> Trajectory;
	TStaticArray<FABTSM11OrbitalSceneBody, FABTSM11GravityScenario::BodyCount>
		Bodies;
	FVector3d TargetCenterCM = FVector3d::ZeroVector;
	double TargetRadiusCM = 0.0;
	FABTSM11TrajectorySemanticMap SemanticMap;
	uint64 SourceTrajectoryHash = 0;
	uint64 SourcePlaybackPlanHash = 0;
	bool bValid = false;

	bool GetContextGeometry(
		int32 BodyIndex,
		bool bTarget,
		FVector3d& OutCenterCM,
		FVector3d& OutVelocityCMPerSec,
		double& OutVisualRadiusCM,
		double& OutInfluenceRadiusCM) const;
};

class ABTSRUNTIME_API FABTSM11OrbitalSceneBuilder final
{
public:
	static bool Build(
		const FABTSM11FinaleLayoutPreset& Preset,
		const FABTSM11TrajectoryResult& Result,
		FABTSM11OrbitalSceneSnapshot& OutSnapshot,
		int32 MaximumTrajectoryPointCount = 900);
	static bool AppendPlaybackExtension(
		const FABTSM11PlaybackPlan& PlaybackPlan,
		FABTSM11OrbitalSceneSnapshot& InOutSnapshot,
		int32 MaximumExtensionPointCount = 360,
		double MaximumChordErrorCM = 0.0);
};

/** Attempt-frozen overview projection. Aim changes do not alter this state. */
struct ABTSRUNTIME_API FABTSM11OverviewViewState
{
	FVector3d ProjectionCenterCM = FVector3d::ZeroVector;
	FVector3d AxisX = FVector3d::ForwardVector;
	FVector3d AxisY = FVector3d::RightVector;
	FVector3d ViewForward = FVector3d::UpVector;
	double ProjectionScaleCM = 1.0;
	double Zoom = 1.0;
	bool bValid = false;

	bool Initialize(
		const FVector3d& InCenterCM,
		const FVector3d& InAxisX,
		const FVector3d& InAxisY,
		double InProjectionScaleCM);
	bool InitializeFromScene(
		const FABTSM11OrbitalSceneSnapshot& Scene,
		const FVector3d& InAxisX,
		const FVector3d& InAxisY);
	bool ApplyPanNormalized(const FVector2d& ScreenDelta);
	bool ApplyOrbitRotation(double YawDegrees, double PitchDegrees);
	bool ApplyZoom(double ZoomMultiplier, double MinimumZoom = 0.25, double MaximumZoom = 4.0);
	FVector2d Project(const FVector3d& PositionCM) const;
	double ProjectDepth(const FVector3d& PositionCM) const;
};

struct ABTSRUNTIME_API FABTSM11OverviewProjectedBody
{
	int32 BodyIndex = INDEX_NONE;
	FVector2d Center = FVector2d::ZeroVector;
	double VisualRadius = 0.0;
};

struct ABTSRUNTIME_API FABTSM11OverviewProjectedPoint
{
	FVector2d Position = FVector2d::ZeroVector;
	double Depth = 0.0;
	double TimeSeconds = 0.0;
	double ArcLengthCM = 0.0;
	bool bHiddenByBody = false;
};

struct ABTSRUNTIME_API FABTSM11OverviewHitProxy
{
	FVector2d Start = FVector2d::ZeroVector;
	FVector2d End = FVector2d::ZeroVector;
	double StartTimeSeconds = 0.0;
	double EndTimeSeconds = 0.0;
	double StartPhase = 0.0;
	double EndPhase = 0.0;
	EABTSM11TrajectorySemanticLeg Leg = EABTSM11TrajectorySemanticLeg::Invalid;
	EABTSM11PlaybackSegmentKind SegmentKind =
		EABTSM11PlaybackSegmentKind::PlayerAuthoritative;
	bool bHiddenByBody = false;
};

ABTSRUNTIME_API bool ABTSM11ShouldDashOverviewTrajectorySegment(
	EABTSM11PlaybackSegmentKind SegmentKind,
	bool bHiddenByBody);

struct ABTSRUNTIME_API FABTSM11OverviewProjection
{
	TArray<FABTSM11OverviewProjectedPoint> Trajectory;
	TStaticArray<FABTSM11OverviewProjectedBody, FABTSM11GravityScenario::BodyCount>
		Bodies;
	FVector2d TargetCenter = FVector2d::ZeroVector;
	double TargetRadius = 0.0;
	TArray<FABTSM11OverviewHitProxy> HitProxies;
	uint64 SourceTrajectoryHash = 0;
	uint64 SourcePlaybackPlanHash = 0;
	bool bValid = false;
};

class ABTSRUNTIME_API FABTSM11OverviewProjector final
{
public:
	static bool Build(
		const FABTSM11OrbitalSceneSnapshot& Scene,
		const FABTSM11OverviewViewState& View,
		FABTSM11OverviewProjection& OutProjection);
};

struct ABTSRUNTIME_API FABTSM11TrajectoryHit
{
	EABTSM11TrajectorySemanticLeg Leg = EABTSM11TrajectorySemanticLeg::Invalid;
	double PhaseWithinLeg = 0.0;
	double TimeSeconds = 0.0;
	double PixelDistance = TNumericLimits<double>::Max();
	bool bHiddenByBody = false;
	bool bValid = false;
};

ABTSRUNTIME_API bool ABTSM11HitTestOverviewTrajectory(
	const FABTSM11OverviewProjection& Projection,
	const FVector2d& MousePixels,
	const FVector2d& PanelCenterPixels,
	double PanelRadiusPixels,
	double HitRadiusPixels,
	FABTSM11TrajectoryHit& OutHit,
	EABTSM11TrajectorySemanticLeg PreferredLeg =
		EABTSM11TrajectorySemanticLeg::Invalid);

struct ABTSRUNTIME_API FABTSM11FrozenPipView
{
	FVector3d ContextCenterCM = FVector3d::ZeroVector;
	FVector3d ViewCenterCM = FVector3d::ZeroVector;
	FVector3d ViewForward = FVector3d::ForwardVector;
	FVector3d ViewUp = FVector3d::UpVector;
	FVector3d ViewRight = FVector3d::RightVector;
	double HalfExtentCM = 1.0;
	bool bValid = false;

	FVector2d Project(const FVector3d& PositionCM) const;
};

struct ABTSRUNTIME_API FABTSM11TrajectoryProbe
{
	uint64 ReferenceResultHash = 0;
	EABTSM11TrajectorySemanticLeg Leg = EABTSM11TrajectorySemanticLeg::Invalid;
	double PhaseWithinLeg = 0.0;
	int32 ContextBodyIndex = INDEX_NONE;
	bool bContextIsTarget = false;
	double ReferenceSolverTime = 0.0;
	FVector3d ReferenceLocalPosition = FVector3d::ZeroVector;
	FVector3d ReferenceTangent = FVector3d::ForwardVector;
	FABTSM11FrozenPipView FrozenPipView;
	bool bValid = false;
};

struct ABTSRUNTIME_API FABTSM11ProbeProjection
{
	EABTSM11ProbeRemapStatus Status = EABTSM11ProbeRemapStatus::Invalid;
	double TimeSeconds = 0.0;
	FVector3d PositionCM = FVector3d::ZeroVector;
	FVector3d VelocityCMPerSec = FVector3d::ZeroVector;
	FVector2d PipPosition = FVector2d::ZeroVector;
	double ContextDistanceCM = TNumericLimits<double>::Max();
	bool bValid = false;
};

/**
 * Stable edge cue for a point that has left a normalized PIP viewport.
 *
 * AnchorUV is clamped to the inset viewport rectangle. DirectionUV points
 * from the viewport centre towards the off-screen point and is suitable for
 * orienting an arrow head. OvershootUV is diagnostic only.
 */
struct ABTSRUNTIME_API FABTSM11PipEdgeIndicator
{
	FVector2d AnchorUV = FVector2d(0.5, 0.5);
	FVector2d DirectionUV = FVector2d::ZeroVector;
	double OvershootUV = 0.0;
	bool bVisible = false;
};

ABTSRUNTIME_API bool ABTSM11BuildPipEdgeIndicator(
	const FVector2d& PointUV,
	double MarginUV,
	FABTSM11PipEdgeIndicator& OutIndicator);

class ABTSRUNTIME_API FABTSM11TrajectoryProbeBuilder final
{
public:
	static bool Create(
		const FABTSM11OrbitalSceneSnapshot& ReferenceScene,
		const FABTSM11TrajectoryHit& Hit,
		const FVector3d& FinaleLocalUp,
		const FVector3d& PreferredViewForward,
		FABTSM11TrajectoryProbe& OutProbe);
	static bool Rebase(
		const FABTSM11OrbitalSceneSnapshot& Scene,
		const FABTSM11TrajectoryProbe& ExistingProbe,
		const FVector3d& FinaleLocalUp,
		FABTSM11TrajectoryProbe& OutProbe);
};

class ABTSRUNTIME_API FABTSM11TrajectoryProbeResolver final
{
public:
	static bool Resolve(
		const FABTSM11OrbitalSceneSnapshot& Scene,
		const FABTSM11TrajectoryProbe& Probe,
		FABTSM11ProbeProjection& OutProjection);
};
