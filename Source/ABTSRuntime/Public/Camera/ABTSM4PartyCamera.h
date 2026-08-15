// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraActor.h"
#include "Camera/ABTSM4CameraRigModel.h"
#include "ABTSM4PartyCamera.generated.h"

class AABTSBirdParty;
class AABTSM2Planet;
class AABTSM25BirdCharacter;
class UPrimitiveComponent;

/** Read-only evidence for Desired -> Safe -> Rendered camera-pose separation. */
struct ABTSRUNTIME_API FABTSM4CameraPoseSnapshot
{
	FVector Pivot = FVector::ZeroVector;
	FVector DesiredLocation = FVector::ZeroVector;
	FVector SurfaceSafeFocus = FVector::ZeroVector;
	FVector SurfaceSafeLocation = FVector::ZeroVector;
	FVector SafeLocation = FVector::ZeroVector;
	FVector RenderedLocation = FVector::ZeroVector;
	float DesiredDistanceCM = 0.0f;
	float SafeDistanceCM = 0.0f;
	float RenderedDistanceCM = 0.0f;
	float UserOrbitDistanceCM = 0.0f;
	float PitchFramingDistanceCM = 0.0f;
	float UpwardFramingAlpha = 0.0f;
	float UserElevationDegrees = 0.0f;
	float SurfaceSafetyLiftCM = 0.0f;
	float SurfaceSafetyRawPenetrationCM = 0.0f;
	float SurfaceSafetyTransitionAlpha = 0.0f;
	int32 ObstructionCandidateIndex = 0;
	EABTSM4CameraObstructionPhase ObstructionPhase = EABTSM4CameraObstructionPhase::Clear;
	bool bDirectManipulation = false;
	bool bSurfaceConstrained = false;
	TWeakObjectPtr<AActor> BlockingActor;
	TWeakObjectPtr<UPrimitiveComponent> BlockingComponent;
};

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
	void BeginDirectManipulation();
	void EndDirectManipulation();
	void AddMouseOrbitYawInput(float MouseDeltaPixels);
	void AddMouseOrbitPitchInput(float MouseDeltaPixels);
	void AddGamepadOrbitYawInput(float AxisValue, float DeltaSeconds);
	void AddGamepadOrbitPitchInput(float AxisValue, float DeltaSeconds);
	void AddZoomInput(float Value);
	void RequestRecenter();
	bool GetMovementBasisAt(const FVector& WorldLocation, FVector& OutForward, FVector& OutRight) const;
	const FABTSM4CameraPoseSnapshot& GetPoseSnapshot() const { return PoseSnapshot; }

private:
	void UpdateCamera(float DeltaSeconds, bool bForceInstant);
	void InitializeOrbit(AABTSM25BirdCharacter& TargetBird, const FVector& Up);
	void TransportOrbitForward(const FVector& NewUp);
	FVector BlendPivotOnSphere(const FVector& Start, const FVector& End, float Alpha, const FVector& PlanetCenter) const;
	FVector ResolveObstructedLocation(
		const FVector& Pivot,
		const FVector& CameraUp,
		const FVector& DesiredArmDirection,
		float DesiredDistance,
		float DeltaSeconds,
		float& OutSafeDistance);
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
	float ObstructionYawOffsetDegrees = 0.0f;
	float ObstructionVerticalOffsetDegrees = 0.0f;
	float SwitchElapsedSeconds = 0.0f;
	int32 SelectedObstructionCandidate = 0;
	int32 LastLoggedObstructionCandidate = 0;
	EABTSM4CameraObstructionPhase LastLoggedObstructionPhase = EABTSM4CameraObstructionPhase::Clear;
	FABTSM4CameraObstructionFilter ObstructionFilter;
	FABTSM4CameraPoseSnapshot PoseSnapshot;
	bool bSwitchBlendActive = false;
	bool bRecenterRequested = false;
	bool bInitializedView = false;
	bool bDirectManipulation = false;
	bool bLastSurfaceConstrained = false;
};
