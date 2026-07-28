// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Future.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/ABTSM11FinaleInteractionTypes.h"
#include "ABTSM11FinaleInteractionSystem.generated.h"

class AABTSBirdParty;
class AABTSM11FinaleSystem;
class AABTSM25BirdCharacter;
class AABTSM51SlingshotCord;
class APlayerController;
class USceneCaptureComponent2D;
class USceneComponent;
class UTextureRenderTarget2D;
struct FABTSM11NominalSolvePayload;
struct FABTSM11PreviewSolvePayload;

/**
 * M11-C gameplay boundary.
 *
 * The M11-B system remains immutable layout authority. This Actor owns only
 * player input, coalesced predictions, presentation snapshots and cached
 * deterministic playback.
 */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM11FinaleInteractionSystem : public AActor
{
	GENERATED_BODY()

public:
	AABTSM11FinaleInteractionSystem();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	bool Initialize(
		AABTSM11FinaleSystem& InFinaleSystem,
		AABTSBirdParty& InParty);
	bool TryEnterFinale(
		AABTSM51SlingshotCord& Cord,
		APlayerController& Controller);
	void ApplyAimAxis(
		double YawAxisDelta,
		double PitchAxisDelta,
		double PowerAxisDelta);
	void RequestRelease();
	void CancelStabilizerOrResetAttempt();
	void ExitFinale();

	bool IsFinaleActive() const;
	bool IsAiming() const;
	bool IsReleasePending() const;
	EABTSM11FinaleInteractionState GetInteractionState() const
	{
		return InteractionState;
	}
	const FString& GetRuntimeFailure() const { return RuntimeFailure; }
	const FABTSM11FinaleLaunchInput& GetCurrentInput() const
	{
		return Stabilizer.GetControlledInput();
	}
	const FABTSM11PrefixClassification& GetClassification() const
	{
		return CurrentClassification;
	}
	const FABTSM11PrefixStabilizer& GetStabilizer() const
	{
		return Stabilizer;
	}
	const FABTSM11PreviewSelection& GetPreviewSelection() const
	{
		return PreviewSelection;
	}
	const FABTSM11OrbitalDiagramSnapshot& GetDiagramSnapshot() const
	{
		return DiagramSnapshot;
	}
	const FABTSM11PlaybackPlan& GetPreviewPlaybackPlan() const
	{
		return PreviewPlaybackPlan;
	}
	const FABTSM11PlaybackPlan& GetReleasedPlaybackPlan() const
	{
		return ReleasedPlaybackPlan;
	}
	const FABTSM11TrajectoryResult* GetCurrentPrediction() const;
	UTextureRenderTarget2D* GetTargetPreviewRenderTarget() const
	{
		return TargetPreviewRenderTarget;
	}
	double GetPlaybackElapsedSeconds() const { return PlaybackElapsedSeconds; }
	double GetFailureBlackoutAlpha() const
	{
		return FailureTimeline.GetBlackoutAlpha();
	}

	static bool ValidateInteractionContract(
		const AABTSM11FinaleSystem& InFinaleSystem,
		FString* OutFailure = nullptr);

private:
	void QueuePreviewSolveIfNeeded();
	void QueueNominalPhysicalSolve();
	void HandlePreviewSolveCompleted(
		TSharedPtr<FABTSM11PreviewSolvePayload> Payload);
	void HandleNominalSolveCompleted(
		TSharedPtr<FABTSM11NominalSolvePayload> Payload);
	void DrainCompletedSolves();
	void RebuildPublishedPreview();
	bool FinalizePendingRelease();
	void UpdateAiming(float DeltaSeconds);
	void UpdatePlayback(float DeltaSeconds);
	void UpdateFailurePresentation(float DeltaSeconds);
	void UpdatePouchPresentation();
	void MarkTargetCaptureDirty();
	void FlushTargetCapture();
	void RestoreAttemptToWorld(bool bKeepFinaleMode);
	void BeginAttemptFailure(const FString& Reason);
	void FailInteraction(const FString& Reason);
	bool DoesInputMatchLatestSolve() const;
	AActor* ResolvePreviewTargetActor(
		EABTSM11PreviewTarget Target) const;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11-C|Capture")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11-C|Capture")
	TObjectPtr<USceneCaptureComponent2D> TargetPreviewCapture;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> TargetPreviewRenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM11FinaleSystem> FinaleSystem;

	UPROPERTY(Transient)
	TObjectPtr<AABTSBirdParty> Party;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM25BirdCharacter> AttemptBird;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM51SlingshotCord> ActiveCord;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Input",
		meta = (ClampMin = "0.001", ClampMax = "2.0"))
	double YawDegreesPerAxisUnit = 0.18;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Input",
		meta = (ClampMin = "0.001", ClampMax = "2.0"))
	double PitchDegreesPerAxisUnit = 0.18;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Input",
		meta = (ClampMin = "0.001", ClampMax = "0.2"))
	double PowerPerWheelUnit = 0.0125;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Input",
		meta = (ClampMin = "0.0", ClampMax = "60.0"))
	double InitialPitchDegrees = 20.0;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Input",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double InitialPower = 0.90;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Input",
		meta = (ClampMin = "0.0", ClampMax = "500.0", Units = "cm"))
	double MinimumVisualPullDistanceCM = 60.0;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Input",
		meta = (ClampMin = "0.0", ClampMax = "500.0", Units = "cm"))
	double MaximumVisualPullDistanceCM = 180.0;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Input",
		meta = (ClampMin = "0.0", ClampMax = "200.0", Units = "cm"))
	double MaximumVisualPitchDropCM = 70.0;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Prediction",
		meta = (ClampMin = "0.02", ClampMax = "1.0", Units = "s"))
	double PreviewSubmitIntervalSeconds = 0.08;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Playback",
		meta = (ClampMin = "0.1", ClampMax = "100.0"))
	double PlaybackTimeScale = 18.0;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Failure",
		meta = (ClampMin = "0.0", ClampMax = "6.0", Units = "s"))
	double FailureReadableHoldSeconds = 1.25;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Failure",
		meta = (ClampMin = "0.05", ClampMax = "2.0", Units = "s"))
	double FailureFadeToBlackSeconds = 0.60;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Failure",
		meta = (ClampMin = "0.0", ClampMax = "2.0", Units = "s"))
	double FailureBlackHoldSeconds = 0.40;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Failure",
		meta = (ClampMin = "0.05", ClampMax = "2.0", Units = "s"))
	double FailureFadeFromBlackSeconds = 0.45;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Failure",
		meta = (ClampMin = "1.0", ClampMax = "12.0", Units = "s"))
	double MaximumFailureFlightDisplaySeconds = 6.0;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Capture",
		meta = (ClampMin = "64", ClampMax = "1024"))
	int32 TargetPreviewWidth = 384;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Capture",
		meta = (ClampMin = "64", ClampMax = "1024"))
	int32 TargetPreviewHeight = 240;

	EABTSM11FinaleInteractionState InteractionState =
		EABTSM11FinaleInteractionState::Locked;
	FString RuntimeFailure;
	FABTSM11PrefixStabilizer Stabilizer;
	FABTSM11PreviewTargetSelector TargetSelector;
	FABTSM11FailurePresentationTimeline FailureTimeline;
	FABTSM11PreviewSelection PreviewSelection;
	FABTSM11PrefixClassification CurrentClassification;
	FABTSM11TrajectoryResult LatestQualifiedResult;
	FABTSM11TrajectoryResult LatestSameInputPhysicalResult;
	FABTSM11TrajectoryResult NominalPhysicalResult;
	FABTSM11PlaybackPlan PreviewPlaybackPlan;
	FABTSM11PlaybackPlan ReleasedPlaybackPlan;
	FABTSM11OrbitalDiagramSnapshot DiagramSnapshot;
	FABTSM11FinaleLaunchInput LatestSolvedInput;
	FABTSM11FinaleLaunchInput FrozenReleaseInput;
	FTransform AttemptBirdOriginalTransform = FTransform::Identity;
	double PreviewSubmitAccumulatorSeconds = 0.0;
	double PlaybackElapsedSeconds = 0.0;
	double PlaybackPresentationEndTimeSeconds = 0.0;
	int64 AimRevision = 0;
	int64 LatestSolvedRevision = INDEX_NONE;
	TFuture<TSharedPtr<FABTSM11PreviewSolvePayload>> PreviewSolveFuture;
	TFuture<TSharedPtr<FABTSM11NominalSolvePayload>> NominalSolveFuture;
	bool bPreviewDirty = false;
	bool bPreviewSolveInFlight = false;
	bool bNominalSolveInFlight = false;
	bool bNominalPhysicalReady = false;
	bool bLatestPhysicalResultAvailable = false;
	bool bAttemptBirdInPouch = false;
	bool bTargetCaptureDirty = false;
};
