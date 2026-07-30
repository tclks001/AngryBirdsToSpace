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

AABTSM11FinaleFlightCamera::AABTSM11FinaleFlightCamera()
{
	PrimaryActorTick.bCanEverTick = false;
	GetCameraComponent()->SetFieldOfView(50.0f);
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
	bAuthorityFollowActive = true;
	return true;
}

bool AABTSM11FinaleFlightCamera::UpdateAuthoritySample(
	const FVector& TargetPosition,
	const FVector& TrajectoryTangent,
	const FVector& PreferredUp,
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

	const double SafeDeltaSeconds =
		FMath::Max(0.0, static_cast<double>(DeltaSeconds));
	const double Alpha = FMath::Clamp(
		1.0 - FMath::Exp(
			-FMath::Max(0.0, FollowLagSpeed)
			* SafeDeltaSeconds),
		0.0,
		1.0);
	const FVector SmoothedLocation = FMath::Lerp(
		GetActorLocation(),
		Frame.DesiredTransform.GetLocation(),
		Alpha);
	const FQuat SmoothedRotation = FQuat::Slerp(
		GetActorQuat(),
		Frame.DesiredTransform.GetRotation(),
		Alpha).GetNormalized();
	SetActorLocationAndRotation(
		SmoothedLocation,
		SmoothedRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	return true;
}

void AABTSM11FinaleFlightCamera::ResetAuthorityFollow()
{
	bAuthorityFollowActive = false;
	LastAuthorityForward = FVector::ForwardVector;
	LastTransportedUp = FVector::UpVector;
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
