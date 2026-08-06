// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ABTSM11FinaleCameraCaptureRunner.generated.h"

class AABTSM11FinaleInteractionSystem;
class AABTSM11FinaleSystem;
class AABTSM51SlingshotCord;
class AABTSM51SlingshotStake;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
struct FMinimalViewInfo;

/** Explicit, process-start contract for one M11 camera acceptance recording. */
struct ABTSRUNTIME_API FABTSM11FinaleCameraCaptureConfig
{
	static constexpr int32 ContractVersion = 5;

	bool bEnabled = false;
	int32 CandidateRank = 0;
	bool bStylized = false;
	bool bDirectorM2 = false;
	bool bAutoExit = true;
	int32 WarmupFrames = 30;
	int32 TerminalHoldFrames = 24;
	int32 FrameRate = 30;
	int32 JpegQuality = 90;
	int32 CaptureWidth = 1280;
	int32 CaptureHeight = 720;
	double TimeoutSeconds = 180.0;
	FString OutputDirectory;
	FString MovieName;
	FString MovieFormat;

	static bool Parse(
		const TCHAR* CommandLine,
		FABTSM11FinaleCameraCaptureConfig& OutConfig,
		FString* OutFailure = nullptr);
	bool IsValid(FString* OutFailure = nullptr) const;
	FString GetExpectedVideoPath() const;
	FString GetFrameWildcard() const;
	int32 GetObservedFrameCount() const;
	FString GetManifestPath() const;
	FString GetObservationCsvPath() const;
};

/** One frame of renderer-independent M1 camera/subject telemetry. */
struct ABTSRUNTIME_API FABTSM11FinaleCameraObservationSample
{
	int32 FrameIndex = INDEX_NONE;
	double CaptureSeconds = 0.0;
	double PlaybackSeconds = 0.0;
	FString InteractionState;
	FString Stage;
	FString CurrentTarget;
	FString StageReason;
	FString DirectorMode;
	double DirectorBlendAlpha = 0.0;
	bool bDirectorM2FrozenEnabled = false;
	FVector BirdWorld = FVector::ZeroVector;
	FVector2D BirdScreen = FVector2D::ZeroVector;
	double BirdDepthCM = 0.0;
	double BirdPixelRadius = 0.0;
	double BirdVisibleRatio = 0.0;
	FVector TargetWorld = FVector::ZeroVector;
	FVector2D TargetScreen = FVector2D::ZeroVector;
	double TargetDepthCM = 0.0;
	double TargetPixelRadius = 0.0;
	double TargetVisibleRatio = 0.0;
	FVector CameraWorld = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	double CameraToBirdCM = 0.0;
	double CameraToTargetCM = 0.0;
	double FovDegrees = 0.0;
	double CameraPositionDeltaCM = 0.0;
	double CameraRotationDeltaDegrees = 0.0;
	double FovDeltaDegrees = 0.0;
};

enum class EABTSM11FinaleCameraCapturePhase : uint8
{
	Inactive = 0,
	WarmingRenderMode,
	WaitingForDependencies,
	WaitingForLaunch,
	Recording,
	HoldingTerminalFrame,
	Finalizing,
	Terminal
};

/**
 * M11-owned visual acceptance runner. In a command-line -game process it reads
 * the game viewport and writes deterministic JPEG frames while driving the
 * existing nominal launch and old flight camera, then muxes those frames to
 * MJPEG AVI before process exit. It never authors trajectory or camera data
 * and does not support PIE.
 */
UCLASS()
class ABTSRUNTIME_API AABTSM11FinaleCameraCaptureRunner final : public AActor
{
	GENERATED_BODY()

public:
	AABTSM11FinaleCameraCaptureRunner();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	bool Initialize(
		const FABTSM11FinaleCameraCaptureConfig& InConfig,
		AABTSM11FinaleSystem& InFinaleSystem,
		AABTSM11FinaleInteractionSystem& InInteractionSystem);

	EABTSM11FinaleCameraCapturePhase GetCapturePhase() const
	{
		return Phase;
	}
	const FString& GetFailureReason() const { return FailureReason; }

private:
	bool TryBeginNominalAttempt();
	bool TryResolveOrCreateCaptureCord(
		AABTSM51SlingshotCord*& OutCord);
	bool TryStartRecording();
	bool CaptureCurrentFrame();
	bool RecordCameraObservation(const FMinimalViewInfo& View);
	bool WriteObservationCsv();
	bool MuxCapturedFramesToAvi();
	void StopRecording();
	void Finish(bool bSuccess, const FString& Reason);
	bool WriteManifest(bool bSuccess, const FString& Reason) const;

	FABTSM11FinaleCameraCaptureConfig Config;
	EABTSM11FinaleCameraCapturePhase Phase =
		EABTSM11FinaleCameraCapturePhase::Inactive;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM11FinaleSystem> FinaleSystem;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM11FinaleInteractionSystem> InteractionSystem;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM51SlingshotStake> CaptureFixtureLeftStake;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM51SlingshotStake> CaptureFixtureRightStake;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM51SlingshotCord> CaptureFixtureCord;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneCaptureComponent2D> RecordingCapture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RecordingRenderTarget;

	FString FailureReason;
	FString PendingFinalizeReason;
	FDateTime StartUtc;
	FDateTime EndUtc;
	double StartPlatformSeconds = 0.0;
	int32 RemainingWarmupFrames = 0;
	int32 RemainingTerminalHoldFrames = 0;
	int32 CapturedFrameCount = 0;
	FIntPoint CapturedFrameSize = FIntPoint::ZeroValue;
	TArray<FABTSM11FinaleCameraObservationSample> ObservationSamples;
	FVector PreviousObservedCameraLocation = FVector::ZeroVector;
	FRotator PreviousObservedCameraRotation = FRotator::ZeroRotator;
	double PreviousObservedFovDegrees = 0.0;
	bool bMovieCaptureStarted = false;
	bool bMovieCaptureStopped = false;
	bool bPendingFinalizeSuccess = false;
	bool bCaptureFixtureCreated = false;
	bool bStylizedViewRegistered = false;
	bool bHasPreviousCameraObservation = false;
	bool bObservationCsvWritten = false;
};
