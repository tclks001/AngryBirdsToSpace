// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Camera/ABTSM6SlingshotCamera.h"
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ABTSM9SatelliteCameraCaptureSubsystem.generated.h"

class AABTSM25BirdCharacter;
class AABTSM51SlingshotCord;
class AABTSM6SlingshotSystem;
class AABTSM9Satellite;
class AABTSSlingshotSatelliteCalibrationRig;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

UENUM()
enum class EABTSM9SatelliteCameraCapturePhase : uint8
{
	WaitingForRig,
	Warmup,
	Recording,
	Terminal
};

/**
 * Explicit -game recorder for M9 camera iteration. It drives the certified
 * calibration witness through the real M6/M9 launch path and mirrors the
 * settled PlayerCameraManager view into an offscreen SceneCapture.
 */
UCLASS()
class ABTSRUNTIME_API UABTSM9SatelliteCameraCaptureSubsystem final
	: public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
	bool ResolveAndLaunch(FString& OutFailure);
	bool CaptureFrame(FString& OutFailure);
	bool WriteManifest(bool bSuccess, const FString& Reason) const;
	void Finish(bool bSuccess, const FString& Reason);

	EABTSM9SatelliteCameraCapturePhase Phase =
		EABTSM9SatelliteCameraCapturePhase::WaitingForRig;
	double StartRealSeconds = 0.0;
	double RecordingStartSeconds = 0.0;
	double TimeoutSeconds = 45.0;
	double RecordingDurationSeconds = 18.0;
	int32 WarmupFrames = 30;
	int32 RemainingWarmupFrames = 30;
	int32 FrameRate = 30;
	int32 JpegQuality = 90;
	int32 CaptureWidth = 1280;
	int32 CaptureHeight = 720;
	int32 CapturedFrameCount = 0;
	FString OutputDirectory;
	FString MovieName = TEXT("M9SatelliteCamera");
	FString ConfigFailure;
	EABTSM9SatelliteFlightCameraIntent LockedIntent =
		EABTSM9SatelliteFlightCameraIntent::None;
	int32 IntentVisibleFrames = 0;
	int32 BirdVisibleFrames = 0;
	int32 SatelliteVisibleIntentFrames = 0;
	int32 SatelliteMissingIntentFrames = 0;
	int32 SurfaceFrameBlendFrames = 0;
	int32 SurfaceFrameCommittedFrames = 0;
	int32 FirstSurfaceFrameCommittedFrame = INDEX_NONE;
	float MaximumSurfaceFrameAlpha = 0.0f;
	float MaximumBirdVisualFrameDeltaDegrees = 0.0f;
	int32 MaximumBirdVisualFrameDeltaFrame = INDEX_NONE;
	float MaximumSurfaceFrameBirdVisualDeltaDegrees = 0.0f;
	int32 MaximumSurfaceFrameBirdVisualDeltaFrame = INDEX_NONE;
	float MaximumSurfaceFrameCameraRelativeBirdDeltaDegrees = 0.0f;
	int32 MaximumSurfaceFrameCameraRelativeBirdDeltaFrame = INDEX_NONE;
	float MaximumHandoffCameraRelativeBirdDeltaDegrees = 0.0f;
	int32 MaximumHandoffCameraRelativeBirdDeltaFrame = INDEX_NONE;
	float MaximumHandoffBirdScreenMotionPixelsPerFrame = 0.0f;
	int32 MaximumHandoffBirdScreenMotionFrame = INDEX_NONE;
	float MaximumHandoffBirdScreenAccelerationPixelsPerFrameSquared = 0.0f;
	int32 MaximumHandoffBirdScreenAccelerationFrame = INDEX_NONE;
	int32 SuddenBirdHalfTurnFrames = 0;
	float MaximumCameraRotationFrameDeltaDegrees = 0.0f;
	float MaximumCameraPhaseTransitionDeltaDegrees = 0.0f;
	int32 SuddenCameraPhaseCutFrames = 0;
	float MinimumIntentCameraBirdDistanceCM = TNumericLimits<float>::Max();
	float MaximumIntentCameraBirdDistanceCM = 0.0f;
	float MinimumIntentFieldOfViewDegrees = TNumericLimits<float>::Max();
	float MaximumIntentFieldOfViewDegrees = 0.0f;
	bool bHasPreviousCameraRotation = false;
	FQuat PreviousCameraRotation = FQuat::Identity;
	bool bHasPreviousCameraPhase = false;
	EABTSM9SatelliteFlightCameraPhase PreviousCameraPhase =
		EABTSM9SatelliteFlightCameraPhase::PrimaryFollow;
	bool bHasPreviousBirdVisualRotation = false;
	FQuat PreviousBirdVisualRotation = FQuat::Identity;
	bool bHasPreviousCameraRelativeBirdRotation = false;
	FQuat PreviousCameraRelativeBirdRotation = FQuat::Identity;
	bool bHasPreviousHandoffBirdScreen = false;
	FVector2D PreviousHandoffBirdScreen = FVector2D::ZeroVector;
	bool bHasPreviousHandoffBirdScreenVelocity = false;
	FVector2D PreviousHandoffBirdScreenVelocity = FVector2D::ZeroVector;
	double MinimumBirdVisibleRatio = 1.0;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM6SlingshotSystem> SlingshotSystem;
	UPROPERTY(Transient)
	TObjectPtr<AABTSM25BirdCharacter> Bird;
	UPROPERTY(Transient)
	TObjectPtr<AABTSM9Satellite> Satellite;
	UPROPERTY(Transient)
	TObjectPtr<AActor> E5Target;
	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> RecordingCapture;
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RecordingRenderTarget;
	bool bStylizedViewRegistered = false;
};
