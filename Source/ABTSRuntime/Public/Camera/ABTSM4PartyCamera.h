// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "ABTSM4PartyCamera.generated.h"

class AABTSBirdParty;
class AABTSM2Planet;
class AABTSM25BirdCharacter;

/** Full-pitch radial orbit camera that smoothly tracks the currently controlled party bird. */
UCLASS()
class ABTSRUNTIME_API AABTSM4PartyCamera : public ACameraActor
{
	GENERATED_BODY()

public:
	AABTSM4PartyCamera();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	void AddOrbitYawInput(float Value);
	void AddOrbitPitchInput(float Value);
	void AddZoomInput(float Value);
	void RequestRecenter();
	bool GetMovementBasisAt(const FVector& WorldLocation, FVector& OutForward, FVector& OutRight) const;

private:
	void UpdateCamera(float DeltaSeconds, bool bForceInstant);
	void InitializeOrbit(AABTSM25BirdCharacter& TargetBird, const FVector& Up);
	void TransportOrbitForward(const FVector& NewUp);
	FVector BlendPivotOnSphere(const FVector& Start, const FVector& End, float Alpha, const FVector& PlanetCenter) const;
	float ResolveObstructedDistance(const FVector& Pivot, const FVector& DesiredLocation, float DesiredDistance, float DeltaSeconds);
	AABTSBirdParty* FindParty();
	AABTSM2Planet* FindPlanet();

	TWeakObjectPtr<AABTSBirdParty> Party;
	TWeakObjectPtr<AABTSM2Planet> Planet;
	TWeakObjectPtr<AABTSM25BirdCharacter> LastTargetBird;
	FVector OrbitForwardTangent = FVector::ForwardVector;
	FVector PreviousUp = FVector::UpVector;
	FVector SmoothedPivot = FVector::ZeroVector;
	FVector SwitchStartPivot = FVector::ZeroVector;
	float ElevationDegrees = 60.0f;
	float OrbitDistanceCM = 850.0f;
	float EffectiveDistanceCM = 850.0f;
	float SwitchElapsedSeconds = 0.0f;
	bool bSwitchBlendActive = false;
	bool bRecenterRequested = false;
	bool bInitializedView = false;
};
