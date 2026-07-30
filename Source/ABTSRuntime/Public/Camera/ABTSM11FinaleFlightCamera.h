// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Camera/CameraActor.h"
#include "CoreMinimal.h"
#include "ABTSM11FinaleFlightCamera.generated.h"

/** One finite, roll-stable camera frame derived from an authority trajectory sample. */
struct ABTSRUNTIME_API FABTSM11FinaleFlightCameraFrame
{
	FVector TrajectoryForward = FVector::ForwardVector;
	FVector TransportedUp = FVector::UpVector;
	FTransform DesiredTransform = FTransform::Identity;

	bool IsUsable() const;
};

namespace ABTSM11FinaleFlightCameraMath
{
	/**
	 * Builds a camera frame from the authority trajectory tangent.
	 *
	 * Up is initialized from PreferredUp, then parallel transported from the
	 * preceding trajectory frame. This keeps the view roll-stable without
	 * treating any World Actor or movement component as trajectory authority.
	 */
	ABTSRUNTIME_API bool BuildDesiredFrame(
		const FVector& TargetPosition,
		const FVector& TrajectoryTangent,
		const FVector& PreferredUp,
		const FVector& PreviousForward,
		const FVector& PreviousUp,
		bool bHasPreviousFrame,
		double FollowDistanceCM,
		double FollowHeightCM,
		double LookAheadDistanceCM,
		double LookTargetHeightCM,
		FABTSM11FinaleFlightCameraFrame& OutFrame);
}

/**
 * M11-only deterministic-flight camera.
 *
 * The interaction system feeds this Actor the sampled authority position and
 * velocity each frame. It never reads Chaos or bird movement velocity.
 */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM11FinaleFlightCamera : public ACameraActor
{
	GENERATED_BODY()

public:
	AABTSM11FinaleFlightCamera();

	bool BeginAuthorityFollow(
		const FVector& TargetPosition,
		const FVector& TrajectoryTangent,
		const FVector& PreferredUp,
		const FTransform& InitialViewTransform);
	bool UpdateAuthoritySample(
		const FVector& TargetPosition,
		const FVector& TrajectoryTangent,
		const FVector& PreferredUp,
		float DeltaSeconds);
	void ResetAuthorityFollow();

	bool IsAuthorityFollowActive() const
	{
		return bAuthorityFollowActive;
	}
	const FVector& GetLastAuthorityForward() const
	{
		return LastAuthorityForward;
	}
	const FVector& GetLastTransportedUp() const
	{
		return LastTransportedUp;
	}

private:
	bool BuildAuthorityFrame(
		const FVector& TargetPosition,
		const FVector& TrajectoryTangent,
		const FVector& PreferredUp,
		FABTSM11FinaleFlightCameraFrame& OutFrame) const;

	/** Distance behind the authority sample, measured along its trajectory tangent. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera",
		meta = (ClampMin = "100.0", UIMin = "300.0", UIMax = "4000.0", Units = "cm"))
	double FollowDistanceCM = 920.0;

	/** Offset along the transported Up vector. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2000.0", Units = "cm"))
	double FollowHeightCM = 310.0;

	/** Look-ahead along the authority tangent. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "3000.0", Units = "cm"))
	double LookAheadDistanceCM = 80.0;

	/** Target lift along transported Up. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera",
		meta = (UIMin = "-500.0", UIMax = "1000.0", Units = "cm"))
	double LookTargetHeightCM = 80.0;

	/** Exponential location and rotation response in supplied presentation time. */
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M11-C|Flight Camera",
		meta = (ClampMin = "0.0", UIMin = "1.0", UIMax = "20.0"))
	double FollowLagSpeed = 7.0;

	FVector LastAuthorityForward = FVector::ForwardVector;
	FVector LastTransportedUp = FVector::UpVector;
	bool bAuthorityFollowActive = false;
};
