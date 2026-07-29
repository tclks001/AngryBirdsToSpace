// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "World/ABTSM11FinaleInteractionTypes.h"

/**
 * The target capture and the Canvas trajectory overlay share this projection
 * contract. Keeping it pure prevents a stale SceneCapture transform from
 * disagreeing with the current authoritative prediction.
 */
inline constexpr double ABTSM11FinaleTargetPreviewFOVDegrees = 42.0;

struct ABTSRUNTIME_API FABTSM11TargetPipView
{
	FVector3d TargetCenterCM = FVector3d::ZeroVector;
	FVector3d PreviousTargetCenterCM = FVector3d::ZeroVector;
	FVector3d CameraLocationCM = FVector3d::ZeroVector;
	FVector3d Forward = FVector3d::ForwardVector;
	FVector3d Right = FVector3d::RightVector;
	FVector3d Up = FVector3d::UpVector;
	double HorizontalFOVDegrees =
		ABTSM11FinaleTargetPreviewFOVDegrees;
	double AspectRatio = 1.0;
	double FramingRadiusCM = 1.0;
	double CameraDistanceCM = 1.0;
	bool bValid = false;
};

struct ABTSRUNTIME_API FABTSM11TargetPipTrajectoryPoint
{
	FVector2D UV = FVector2D::ZeroVector;
	bool bInFront = false;
	bool bClosestApproach = false;
};

struct ABTSRUNTIME_API FABTSM11TargetPipTrajectory
{
	TArray<FABTSM11TargetPipTrajectoryPoint> Points;
	uint64 SourceTrajectoryHash = 0;
	EABTSM11PreviewTarget Target = EABTSM11PreviewTarget::Assist1;
	int32 ClosestSourcePointIndex = INDEX_NONE;
	bool bValid = false;

	void Reset()
	{
		*this = FABTSM11TargetPipTrajectory();
	}
};

/**
 * Builds a stable target-centered view. The view axis is defined only by the
 * ordered target chain, never by the player's changing incidence vector.
 */
ABTSRUNTIME_API bool ABTSM11BuildTargetPipView(
	const FABTSM11FinaleLayoutPreset& Preset,
	const FABTSM11PreviewSelection& Selection,
	int32 RenderWidth,
	int32 RenderHeight,
	FABTSM11TargetPipView& OutView);

/**
 * Extracts and projects a bounded current-result polyline around the true
 * closest approach. It never consumes nominal input or nominal trajectory.
 */
ABTSRUNTIME_API bool ABTSM11BuildTargetPipTrajectory(
	const FABTSM11TargetPipView& View,
	const FABTSM11PreviewSelection& Selection,
	const FABTSM11TrajectoryResult& CurrentPrediction,
	FABTSM11TargetPipTrajectory& OutTrajectory,
	int32 MaximumPointCount = 96);

/** Clips a normalized PIP line to an inset [0,1] rectangle. */
ABTSRUNTIME_API bool ABTSM11ClipPipLineToRect(
	FVector2D& InOutStart,
	FVector2D& InOutEnd,
	float Inset = 0.0f);

struct ABTSRUNTIME_API FABTSM11TargetWedgeConfig
{
	float AnchorMarginPixels = 38.0f;
	float ShowEdgeDistancePixels = 58.0f;
	float HideEdgeDistancePixels = 92.0f;
	double ShowHoldSeconds = 0.06;
	double HideHoldSeconds = 0.10;

	bool IsValid() const;
};

struct ABTSRUNTIME_API FABTSM11TargetWedgeProjection
{
	FVector2D RawScreenPosition = FVector2D::ZeroVector;
	bool bInFront = false;
	bool bFinite = false;
};

struct ABTSRUNTIME_API FABTSM11TargetWedgeOutput
{
	FVector2D Anchor = FVector2D::ZeroVector;
	FVector2D Direction = FVector2D(1.0f, 0.0f);
	EABTSM11PreviewTarget Target = EABTSM11PreviewTarget::Assist1;
	bool bVisible = false;
};

/** Pure perspective projection used by the single-target HUD wedge. */
ABTSRUNTIME_API FABTSM11TargetWedgeProjection
ABTSM11ProjectTargetForWedge(
	const FVector3d& TargetWorldPosition,
	const FVector3d& CameraWorldPosition,
	const FVector3d& CameraForward,
	const FVector3d& CameraRight,
	const FVector3d& CameraUp,
	double HorizontalFOVDegrees,
	const FVector2D& ViewportSize);

/**
 * Stateful but UObject-free spatial/temporal hysteresis for exactly one
 * currently selected target.
 */
class ABTSRUNTIME_API FABTSM11TargetWedgeTracker final
{
public:
	void Reset();
	FABTSM11TargetWedgeOutput Update(
		double DeltaSeconds,
		EABTSM11PreviewTarget Target,
		const FABTSM11TargetWedgeProjection& Projection,
		const FVector2D& ViewportSize,
		const FABTSM11TargetWedgeConfig& Config =
			FABTSM11TargetWedgeConfig());

private:
	EABTSM11PreviewTarget LatchedTarget =
		EABTSM11PreviewTarget::Assist1;
	double PendingSeconds = 0.0;
	bool bVisible = false;
	bool bPendingVisible = false;
	bool bInitialized = false;
};
