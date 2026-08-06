// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/ABTSM11FinaleFlightCamera.h"

#include "Camera/CameraComponent.h"

namespace
{
	bool IsFiniteFlightCameraVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	}

	FVector ResolveProjectedUp(
		const FVector& CandidateUp,
		const FVector& PreferredUp,
		const FVector& Forward)
	{
		FVector Up = FVector::VectorPlaneProject(
			CandidateUp,
			Forward).GetSafeNormal();
		if (Up.IsNearlyZero())
		{
			Up = FVector::VectorPlaneProject(
				PreferredUp,
				Forward).GetSafeNormal();
		}
		if (Up.IsNearlyZero())
		{
			const FVector FallbackAxis =
				FMath::Abs(Forward.Z) < 0.9f
					? FVector::UpVector
					: FVector::RightVector;
			Up = FVector::VectorPlaneProject(
				FallbackAxis,
				Forward).GetSafeNormal();
		}
		return Up;
	}

	double SmootherStep01(const double Value)
	{
		const double Clamped = FMath::Clamp(Value, 0.0, 1.0);
		return Clamped * Clamped * Clamped
			* (Clamped * (Clamped * 6.0 - 15.0) + 10.0);
	}

	double ResolveEncounterOrientationAlpha(
		const FABTSM11FinaleCameraStageSelection& Selection,
		const double DirectorBlendAlpha)
	{
		if (Selection.Stage == EABTSM11FinaleCameraStage::CruiseToBody)
		{
			return FMath::Clamp(DirectorBlendAlpha, 0.0, 1.0);
		}
		return Selection.Stage == EABTSM11FinaleCameraStage::Approach
			|| Selection.Stage == EABTSM11FinaleCameraStage::Periapsis
				? 1.0
				: 0.0;
	}

	double ResolveTransitScreenXInTargetRadii(
		const FABTSM11FinaleCameraStageSelection& Selection,
		const FABTSM11FinaleCameraM2Settings& Settings)
	{
		if (Selection.Stage == EABTSM11FinaleCameraStage::CruiseToBody)
		{
			const double CruiseMotionAlpha = FMath::Clamp(
				(Selection.StageProgress
					- Settings.CruiseLeadInStartFraction)
					/ FMath::Max(UE_DOUBLE_SMALL_NUMBER,
						1.0 - Settings.CruiseLeadInStartFraction),
				0.0,
				1.0);
			const double AcceleratingCruiseAlpha = CruiseMotionAlpha
				* (0.8 + 0.2 * CruiseMotionAlpha);
			return FMath::Lerp(
				-Settings.TransitCruiseFarOffsetRadii,
				-Settings.TransitEntryOffsetRadii,
				AcceleratingCruiseAlpha);
		}
		if (Selection.Stage == EABTSM11FinaleCameraStage::Approach)
		{
			const double Progress = FMath::Clamp(
				Selection.StageProgress,
				0.0,
				1.0);
			const double AcceleratingApproachAlpha = Progress
				* (0.6 + 0.4 * Progress);
			return FMath::Lerp(
				-Settings.TransitEntryOffsetRadii,
				Settings.TransitClosestOffsetRadii,
				AcceleratingApproachAlpha);
		}
		const double ExitAlpha = FMath::Clamp(
			Selection.StageProgress
				/ Settings.TransitExitProgressFraction,
			0.0,
			1.0);
		const double DeceleratingExitAlpha =
			1.0 - FMath::Square(1.0 - ExitAlpha);
		return FMath::Lerp(
			Settings.TransitClosestOffsetRadii,
			Settings.TransitExitOffsetRadii,
			DeceleratingExitAlpha);
	}

	double ResolveDirectedFovDegrees(
		const FABTSM11FinaleCameraStageSelection& Selection,
		const FABTSM11FinaleCameraM2Settings& Settings)
	{
		if (Selection.Stage == EABTSM11FinaleCameraStage::Approach)
		{
			return FMath::Lerp(
				Settings.BaselineFovDegrees,
				Settings.ClosestFovDegrees,
				SmootherStep01(Selection.StageProgress));
		}
		if (Selection.Stage == EABTSM11FinaleCameraStage::Periapsis)
		{
			return FMath::Lerp(
				Settings.ClosestFovDegrees,
				Settings.BaselineFovDegrees,
				SmootherStep01(
					Selection.StageProgress
						/ Settings.PeriapsisFovRestoreFraction));
		}
		return Settings.BaselineFovDegrees;
	}

	FVector SlerpDirection(
		const FVector& From,
		const FVector& To,
		const double Alpha)
	{
		const FVector Start = From.GetSafeNormal();
		const FVector End = To.GetSafeNormal();
		const double Dot = FMath::Clamp(
			static_cast<double>(FVector::DotProduct(Start, End)),
			-1.0,
			1.0);
		const double Angle = FMath::Acos(Dot);
		FVector Axis = FVector::CrossProduct(Start, End).GetSafeNormal();
		if (Angle <= UE_DOUBLE_SMALL_NUMBER)
		{
			return FMath::Lerp(Start, End, Alpha).GetSafeNormal();
		}
		if (Axis.IsNearlyZero())
		{
			const FVector FallbackAxis =
				FMath::Abs(Start.Z) < 0.9f
					? FVector::UpVector
					: FVector::RightVector;
			Axis = FVector::CrossProduct(
				Start,
				FallbackAxis).GetSafeNormal();
		}
		return FQuat(Axis, Angle * FMath::Clamp(Alpha, 0.0, 1.0))
			.RotateVector(Start)
			.GetSafeNormal();
	}

}

bool FABTSM11FinaleFlightCameraFrame::IsUsable() const
{
	return IsFiniteFlightCameraVector(TrajectoryForward)
		&& IsFiniteFlightCameraVector(TransportedUp)
		&& IsFiniteFlightCameraVector(DesiredTransform.GetLocation())
		&& !TrajectoryForward.IsNearlyZero()
		&& !TransportedUp.IsNearlyZero()
		&& FMath::Abs(FVector::DotProduct(
			TrajectoryForward,
			TransportedUp)) <= 1.0e-3f
		&& DesiredTransform.GetRotation().IsNormalized();
}

bool FABTSM11FinaleCameraM2Settings::IsUsable() const
{
	return FMath::IsFinite(MaximumRetreatCM)
		&& MaximumRetreatCM >= 0.0
		&& FMath::IsFinite(CruiseLeadInStartFraction)
		&& CruiseLeadInStartFraction >= 0.0
		&& FMath::IsFinite(CruiseLeadInBlendFraction)
		&& CruiseLeadInBlendFraction > 0.0
		&& CruiseLeadInStartFraction + CruiseLeadInBlendFraction <= 1.0
		&& FMath::IsFinite(ApproachBrakeStartFraction)
		&& ApproachBrakeStartFraction >= 0.0
		&& ApproachBrakeStartFraction < 1.0
		&& FMath::IsFinite(ClosestRetreatFraction)
		&& ClosestRetreatFraction >= 0.0
		&& ClosestRetreatFraction <= 1.0
		&& FMath::IsFinite(PeriapsisReleaseFraction)
		&& PeriapsisReleaseFraction > 0.0
		&& PeriapsisReleaseFraction <= 1.0
		&& FMath::IsFinite(MinimumForegroundBirdDistanceCM)
		&& MinimumForegroundBirdDistanceCM > 0.0
		&& FMath::IsFinite(TransitCruiseFarOffsetRadii)
		&& TransitCruiseFarOffsetRadii > TransitEntryOffsetRadii
		&& FMath::IsFinite(TransitEntryOffsetRadii)
		&& TransitEntryOffsetRadii >= 1.0
		&& FMath::IsFinite(TransitClosestOffsetRadii)
		&& TransitClosestOffsetRadii >= 0.0
		&& TransitClosestOffsetRadii < 1.0
		&& FMath::IsFinite(TransitExitOffsetRadii)
		&& TransitExitOffsetRadii > TransitClosestOffsetRadii
		&& FMath::IsFinite(TransitVerticalOffsetRadii)
		&& FMath::Abs(TransitVerticalOffsetRadii) < 1.0
		&& FMath::IsFinite(TransitExitProgressFraction)
		&& TransitExitProgressFraction > 0.0
		&& TransitExitProgressFraction <= 1.0
		&& FMath::IsFinite(BaselineFovDegrees)
		&& BaselineFovDegrees > 0.0
		&& FMath::IsFinite(ClosestFovDegrees)
		&& ClosestFovDegrees > 0.0
		&& ClosestFovDegrees < BaselineFovDegrees
		&& FMath::IsFinite(PeriapsisFovRestoreFraction)
		&& PeriapsisFovRestoreFraction > 0.0
		&& PeriapsisFovRestoreFraction <= 1.0;
}

bool FABTSM11FinaleCameraM2Diagnostics::IsUsable() const
{
	return FMath::IsFinite(DirectorBlendAlpha)
		&& DirectorBlendAlpha >= 0.0
		&& DirectorBlendAlpha <= 1.0
		&& FMath::IsFinite(RetreatAlpha)
		&& RetreatAlpha >= 0.0
		&& RetreatAlpha <= 1.0
		&& FMath::IsFinite(TransitScreenXInTargetRadii)
		&& FMath::IsFinite(DirectedFovDegrees)
		&& DirectedFovDegrees > 0.0;
}

bool ABTSM11FinaleFlightCameraMath::BuildDesiredFrame(
	const FVector& TargetPosition,
	const FVector& TrajectoryTangent,
	const FVector& PreferredUp,
	const FVector& PreviousForward,
	const FVector& PreviousUp,
	const bool bHasPreviousFrame,
	const double FollowDistanceCM,
	const double FollowHeightCM,
	const double LookAheadDistanceCM,
	const double LookTargetHeightCM,
	FABTSM11FinaleFlightCameraFrame& OutFrame)
{
	OutFrame = FABTSM11FinaleFlightCameraFrame();
	if (!IsFiniteFlightCameraVector(TargetPosition)
		|| !IsFiniteFlightCameraVector(TrajectoryTangent)
		|| !IsFiniteFlightCameraVector(PreferredUp)
		|| !FMath::IsFinite(FollowDistanceCM)
		|| !FMath::IsFinite(FollowHeightCM)
		|| !FMath::IsFinite(LookAheadDistanceCM)
		|| !FMath::IsFinite(LookTargetHeightCM)
		|| FollowDistanceCM < 0.0)
	{
		return false;
	}

	const FVector Forward = TrajectoryTangent.GetSafeNormal();
	if (Forward.IsNearlyZero())
	{
		return false;
	}
	FVector CandidateUp = PreferredUp.GetSafeNormal();
	if (bHasPreviousFrame
		&& IsFiniteFlightCameraVector(PreviousForward)
		&& IsFiniteFlightCameraVector(PreviousUp)
		&& !PreviousForward.IsNearlyZero()
		&& !PreviousUp.IsNearlyZero())
	{
		const FQuat Transport = FQuat::FindBetweenNormals(
			PreviousForward.GetSafeNormal(),
			Forward);
		CandidateUp = Transport.RotateVector(
			PreviousUp.GetSafeNormal());
	}
	const FVector Up = ResolveProjectedUp(
		CandidateUp,
		PreferredUp,
		Forward);
	if (Up.IsNearlyZero())
	{
		return false;
	}

	const FVector DesiredLocation =
		TargetPosition
		- Forward * FollowDistanceCM
		+ Up * FollowHeightCM;
	const FVector LookTarget =
		TargetPosition
		+ Forward * LookAheadDistanceCM
		+ Up * LookTargetHeightCM;
	const FVector ViewForward =
		(LookTarget - DesiredLocation).GetSafeNormal();
	if (ViewForward.IsNearlyZero())
	{
		return false;
	}
	const FVector ViewUp = ResolveProjectedUp(
		Up,
		PreferredUp,
		ViewForward);
	if (ViewUp.IsNearlyZero())
	{
		return false;
	}

	OutFrame.TrajectoryForward = Forward;
	OutFrame.TransportedUp = Up;
	OutFrame.DesiredTransform = FTransform(
		FRotationMatrix::MakeFromXZ(
			ViewForward,
			ViewUp).ToQuat(),
		DesiredLocation);
	return OutFrame.IsUsable();
}

bool ABTSM11FinaleFlightCameraMath::BuildM2Assist1Frame(
	const FABTSM11FinaleFlightCameraFrame& BaselineFrame,
	const FVector& BirdPosition,
	const FABTSM11FinaleCameraDirectorSample& DirectorSample,
	const FABTSM11FinaleCameraM2Settings& Settings,
	FTransform& OutDirectedTransform,
	FABTSM11FinaleCameraM2Diagnostics& OutDiagnostics)
{
	OutDirectedTransform = BaselineFrame.DesiredTransform;
	OutDiagnostics = FABTSM11FinaleCameraM2Diagnostics();
	if (!BaselineFrame.IsUsable()
		|| !DirectorSample.IsUsable()
		|| !DirectorSample.Selection.IsM2Assist1Window()
		|| !IsFiniteFlightCameraVector(BirdPosition)
		|| !Settings.IsUsable())
	{
		return false;
	}

	const double Progress = DirectorSample.Selection.StageProgress;
	if (DirectorSample.Selection.Stage
		== EABTSM11FinaleCameraStage::CruiseToBody)
	{
		OutDiagnostics.DirectorBlendAlpha = SmootherStep01(
			(Progress - Settings.CruiseLeadInStartFraction)
			/ Settings.CruiseLeadInBlendFraction);
	}
	else if (DirectorSample.Selection.Stage
		== EABTSM11FinaleCameraStage::Approach)
	{
		OutDiagnostics.DirectorBlendAlpha = 1.0;
		OutDiagnostics.RetreatAlpha =
			Settings.ClosestRetreatFraction * SmootherStep01(
				(Progress - Settings.ApproachBrakeStartFraction)
				/ (1.0 - Settings.ApproachBrakeStartFraction));
	}
	else
	{
		OutDiagnostics.DirectorBlendAlpha = SmootherStep01(
			(1.0 - Progress) / 0.25);
		OutDiagnostics.RetreatAlpha =
			Settings.ClosestRetreatFraction
			+ (1.0 - Settings.ClosestRetreatFraction)
				* SmootherStep01(
					Progress / Settings.PeriapsisReleaseFraction);
	}
	OutDiagnostics.TransitScreenXInTargetRadii =
		ResolveTransitScreenXInTargetRadii(
			DirectorSample.Selection,
			Settings);
	OutDiagnostics.DirectedFovDegrees = ResolveDirectedFovDegrees(
		DirectorSample.Selection,
		Settings);

	const FVector BaselineLocation =
		BaselineFrame.DesiredTransform.GetLocation();
	const double BaselineBirdDistance =
		FMath::Max(1.0, FVector::Distance(
			BaselineLocation,
			BirdPosition));
	const double DirectedBirdDistance =
		FMath::Max(
			BaselineBirdDistance,
			Settings.MinimumForegroundBirdDistanceCM)
		+ Settings.MaximumRetreatCM * OutDiagnostics.RetreatAlpha;

	const double EncounterOrientationAlpha =
		ResolveEncounterOrientationAlpha(
			DirectorSample.Selection,
			OutDiagnostics.DirectorBlendAlpha);
	const FVector DirectedUp = SlerpDirection(
		BaselineFrame.TransportedUp,
		DirectorSample.EncounterScreenUp,
		EncounterOrientationAlpha);

	// Solve the foreground transit in the camera's actual planet-centred
	// projection plane. A frozen world-space chord is not sufficient: when the
	// camera turns, that chord can acquire depth and collapse a requested
	// 1.6-radius entry to a nearly central screen point. Iterating the plane
	// basis and camera ray makes the requested left/lower chord self-consistent.
	FVector DirectedLocation = BirdPosition
		+ (BirdPosition - DirectorSample.TargetCenter).GetSafeNormal()
			* DirectedBirdDistance;
	if (DirectedLocation.Equals(BirdPosition))
	{
		return false;
	}
	for (int32 Iteration = 0; Iteration < 4; ++Iteration)
	{
		FQuat PlaneRotation = FQuat::Identity;
		if (!BuildM2PlanetAnchoredRotation(
			DirectedLocation,
			DirectedUp,
			DirectorSample.TargetCenter,
			PlaneRotation))
		{
			return false;
		}
		const FVector TargetPlanePoint = DirectorSample.TargetCenter
			+ PlaneRotation.GetRightVector()
				* OutDiagnostics.TransitScreenXInTargetRadii
				* DirectorSample.TargetRadiusCM
			+ PlaneRotation.GetUpVector()
				* Settings.TransitVerticalOffsetRadii
				* DirectorSample.TargetRadiusCM;
		const FVector DirectedOffsetDirection =
			(BirdPosition - TargetPlanePoint).GetSafeNormal();
		if (DirectedOffsetDirection.IsNearlyZero())
		{
			return false;
		}
		DirectedLocation = BirdPosition
			+ DirectedOffsetDirection * DirectedBirdDistance;
	}
	FQuat DirectedRotation = FQuat::Identity;
	if (!BuildM2PlanetAnchoredRotation(
		DirectedLocation,
		DirectedUp,
		DirectorSample.TargetCenter,
		DirectedRotation))
	{
		return false;
	}
	OutDirectedTransform = FTransform(
		DirectedRotation,
		DirectedLocation);
	return IsFiniteFlightCameraVector(DirectedLocation)
		&& OutDirectedTransform.GetRotation().IsNormalized()
		&& OutDiagnostics.IsUsable();
}

bool ABTSM11FinaleFlightCameraMath::BuildM2PlanetAnchoredRotation(
	const FVector& CameraLocation,
	const FVector& PreferredUp,
	const FVector& TargetCenter,
	FQuat& OutRotation)
{
	OutRotation = FQuat::Identity;
	if (!IsFiniteFlightCameraVector(CameraLocation)
		|| !IsFiniteFlightCameraVector(PreferredUp)
		|| !IsFiniteFlightCameraVector(TargetCenter))
	{
		return false;
	}
	const FVector ViewDirection =
		(TargetCenter - CameraLocation).GetSafeNormal();
	if (ViewDirection.IsNearlyZero())
	{
		return false;
	}
	const FVector ViewUp = ResolveProjectedUp(
		PreferredUp,
		FVector::UpVector,
		ViewDirection);
	if (ViewDirection.IsNearlyZero() || ViewUp.IsNearlyZero())
	{
		return false;
	}
	OutRotation = FRotationMatrix::MakeFromXZ(
		ViewDirection,
		ViewUp).ToQuat();
	return OutRotation.IsNormalized();
}

AABTSM11FinaleFlightCamera::AABTSM11FinaleFlightCamera()
{
	PrimaryActorTick.bCanEverTick = false;
	GetCameraComponent()->SetFieldOfView(
		static_cast<float>(BaselineFovDegrees));
}

bool AABTSM11FinaleFlightCamera::BeginAuthorityFollow(
	const FVector& TargetPosition,
	const FVector& TrajectoryTangent,
	const FVector& PreferredUp,
	const FTransform& InitialViewTransform)
{
	ResetAuthorityFollow();
	FABTSM11FinaleFlightCameraFrame Frame;
	if (!BuildAuthorityFrame(
		TargetPosition,
		TrajectoryTangent,
		PreferredUp,
		Frame))
	{
		return false;
	}
	SetActorTransform(
		InitialViewTransform,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	LastAuthorityForward = Frame.TrajectoryForward;
	LastTransportedUp = Frame.TransportedUp;
	bM2DirectorFrozenEnabled =
		ABTSM11FinaleCameraDirector::IsM2Enabled();
	LastM2BlendAlpha = 0.0;
	LastM2RetreatAlpha = 0.0;
	LastM2TransitScreenXInTargetRadii = -M2TransitCruiseFarOffsetRadii;
	GetCameraComponent()->SetFieldOfView(
		static_cast<float>(BaselineFovDegrees));
	LastDirectorStage = EABTSM11FinaleCameraStage::PreLaunch;
	bAuthorityFollowActive = true;
	return true;
}

bool AABTSM11FinaleFlightCamera::UpdateAuthoritySample(
	const FVector& TargetPosition,
	const FVector& TrajectoryTangent,
	const FVector& PreferredUp,
	const FABTSM11FinaleCameraDirectorSample* DirectorSample,
	const float DeltaSeconds)
{
	if (!bAuthorityFollowActive)
	{
		return false;
	}
	FABTSM11FinaleFlightCameraFrame Frame;
	if (!BuildAuthorityFrame(
		TargetPosition,
		TrajectoryTangent,
		PreferredUp,
		Frame))
	{
		return false;
	}
	LastAuthorityForward = Frame.TrajectoryForward;
	LastTransportedUp = Frame.TransportedUp;
	LastM2BlendAlpha = 0.0;
	LastM2RetreatAlpha = 0.0;
	LastM2TransitScreenXInTargetRadii = -M2TransitCruiseFarOffsetRadii;
	LastDirectorStage = DirectorSample != nullptr
		? DirectorSample->Selection.Stage
		: EABTSM11FinaleCameraStage::Unavailable;
	FTransform DesiredTransform = Frame.DesiredTransform;
	double DesiredFovDegrees = BaselineFovDegrees;
	if (bM2DirectorFrozenEnabled
		&& DirectorSample != nullptr
		&& DirectorSample->Selection.IsM2Assist1Window())
	{
		FTransform DirectedTransform;
		FABTSM11FinaleCameraM2Settings Settings;
		Settings.MaximumRetreatCM = M2MaximumRetreatCM;
		Settings.CruiseLeadInStartFraction =
			M2CruiseLeadInStartFraction;
		Settings.CruiseLeadInBlendFraction =
			M2CruiseLeadInBlendFraction;
		Settings.ApproachBrakeStartFraction =
			M2ApproachBrakeStartFraction;
		Settings.ClosestRetreatFraction =
			M2ClosestRetreatFraction;
		Settings.PeriapsisReleaseFraction =
			M2PeriapsisReleaseFraction;
		Settings.MinimumForegroundBirdDistanceCM =
			M2MinimumForegroundBirdDistanceCM;
		Settings.TransitCruiseFarOffsetRadii =
			M2TransitCruiseFarOffsetRadii;
		Settings.TransitEntryOffsetRadii =
			M2TransitEntryOffsetRadii;
		Settings.TransitClosestOffsetRadii =
			M2TransitClosestOffsetRadii;
		Settings.TransitExitOffsetRadii =
			M2TransitExitOffsetRadii;
		Settings.TransitVerticalOffsetRadii =
			M2TransitVerticalOffsetRadii;
		Settings.TransitExitProgressFraction =
			M2TransitExitProgressFraction;
		Settings.BaselineFovDegrees = BaselineFovDegrees;
		Settings.ClosestFovDegrees = M2ClosestFovDegrees;
		Settings.PeriapsisFovRestoreFraction =
			M2PeriapsisFovRestoreFraction;
		FABTSM11FinaleCameraM2Diagnostics Diagnostics;
		if (!ABTSM11FinaleFlightCameraMath::BuildM2Assist1Frame(
			Frame,
			TargetPosition,
			*DirectorSample,
			Settings,
			DirectedTransform,
			Diagnostics))
		{
			return false;
		}
		LastM2BlendAlpha = Diagnostics.DirectorBlendAlpha;
		LastM2RetreatAlpha = Diagnostics.RetreatAlpha;
		LastM2TransitScreenXInTargetRadii =
			Diagnostics.TransitScreenXInTargetRadii;
		DesiredFovDegrees = Diagnostics.DirectedFovDegrees;
		const FVector BaselineOffset =
			Frame.DesiredTransform.GetLocation() - TargetPosition;
		const FVector DirectedOffset =
			DirectedTransform.GetLocation() - TargetPosition;
		const FVector BlendedOffsetDirection = SlerpDirection(
			BaselineOffset,
			DirectedOffset,
			LastM2BlendAlpha);
		const double BlendedOffsetDistance = FMath::Lerp(
			BaselineOffset.Size(),
			DirectedOffset.Size(),
			LastM2BlendAlpha);
		if (BlendedOffsetDirection.IsNearlyZero()
			|| !FMath::IsFinite(BlendedOffsetDistance))
		{
			return false;
		}
		DesiredTransform.SetLocation(
			TargetPosition
			+ BlendedOffsetDirection * BlendedOffsetDistance);
		DesiredTransform.SetRotation(FQuat::Slerp(
			Frame.DesiredTransform.GetRotation(),
			DirectedTransform.GetRotation(),
			LastM2BlendAlpha).GetNormalized());
	}

	const double SafeDeltaSeconds =
		FMath::Max(0.0, static_cast<double>(DeltaSeconds));
	const double ResponseSpeed = LastM2BlendAlpha > UE_DOUBLE_SMALL_NUMBER
		? M2FollowLagSpeed
		: FollowLagSpeed;
	const double Alpha = FMath::Clamp(
		1.0 - FMath::Exp(
			-FMath::Max(0.0, ResponseSpeed)
			* SafeDeltaSeconds),
		0.0,
		1.0);
	const FVector SmoothedLocation = FMath::Lerp(
		GetActorLocation(),
		DesiredTransform.GetLocation(),
		Alpha);
	FQuat LocationAwareDesiredRotation =
		DesiredTransform.GetRotation();
	if (LastM2BlendAlpha > UE_DOUBLE_SMALL_NUMBER
		&& DirectorSample != nullptr)
	{
		const double EncounterOrientationAlpha =
			ResolveEncounterOrientationAlpha(
				DirectorSample->Selection,
				LastM2BlendAlpha);
		const FVector LocationAwareUp = SlerpDirection(
			Frame.TransportedUp,
			DirectorSample->EncounterScreenUp,
			EncounterOrientationAlpha);
		FQuat DirectedRotationAtSmoothedLocation;
		if (!ABTSM11FinaleFlightCameraMath::BuildM2PlanetAnchoredRotation(
			SmoothedLocation,
			LocationAwareUp,
			DirectorSample->TargetCenter,
			DirectedRotationAtSmoothedLocation))
		{
			return false;
		}
		LocationAwareDesiredRotation = FQuat::Slerp(
			Frame.DesiredTransform.GetRotation(),
			DirectedRotationAtSmoothedLocation,
			LastM2BlendAlpha).GetNormalized();
	}
	const FQuat SmoothedRotation = FQuat::Slerp(
		GetActorQuat(),
		LocationAwareDesiredRotation,
		Alpha).GetNormalized();
	SetActorLocationAndRotation(
		SmoothedLocation,
		SmoothedRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	GetCameraComponent()->SetFieldOfView(
		static_cast<float>(DesiredFovDegrees));
	return true;
}

void AABTSM11FinaleFlightCamera::ResetAuthorityFollow()
{
	bAuthorityFollowActive = false;
	LastAuthorityForward = FVector::ForwardVector;
	LastTransportedUp = FVector::UpVector;
	LastM2BlendAlpha = 0.0;
	LastM2RetreatAlpha = 0.0;
	LastM2TransitScreenXInTargetRadii = -M2TransitCruiseFarOffsetRadii;
	LastDirectorStage = EABTSM11FinaleCameraStage::PreLaunch;
	bM2DirectorFrozenEnabled = false;
	GetCameraComponent()->SetFieldOfView(
		static_cast<float>(BaselineFovDegrees));
}

bool AABTSM11FinaleFlightCamera::BuildAuthorityFrame(
	const FVector& TargetPosition,
	const FVector& TrajectoryTangent,
	const FVector& PreferredUp,
	FABTSM11FinaleFlightCameraFrame& OutFrame) const
{
	return ABTSM11FinaleFlightCameraMath::BuildDesiredFrame(
		TargetPosition,
		TrajectoryTangent,
		PreferredUp,
		LastAuthorityForward,
		LastTransportedUp,
		bAuthorityFollowActive,
		FollowDistanceCM,
		FollowHeightCM,
		LookAheadDistanceCM,
		LookTargetHeightCM,
		OutFrame);
}
