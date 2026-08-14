// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Future.h"
#include "Camera/ABTSM11FinaleCameraDirector.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UI/ABTSM11FinaleHUDData.h"
#include "World/ABTSM11FinaleFormation.h"
#include "World/ABTSM11FinaleInteractionTypes.h"
#include "ABTSM11FinaleInteractionSystem.generated.h"

class AABTSBirdParty;
class AABTSM11FinaleFlightCamera;
class AABTSM11FinaleSystem;
class AABTSM25BirdCharacter;
class AABTSM51SlingshotCord;
class AABTSM6SlingshotCamera;
class APlayerController;
class UABTSM11FinaleBirdTrailComponent;
class USceneCaptureComponent2D;
class USceneComponent;
class UTextureRenderTarget2D;
enum class EABTSStylizedViewClass : uint8;
struct FABTSM11NominalSolvePayload;
struct FABTSM11PreviewSolvePayload;
struct FABTSM11F4GuidanceSolvePayload;

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

	double GetM6PouchForwardClearanceCM() const
	{
		return M6PouchForwardClearanceCM;
	}
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	bool Initialize(
		AABTSM11FinaleSystem& InFinaleSystem,
		AABTSBirdParty& InParty,
		TSubclassOf<AABTSM6SlingshotCamera> InAimCameraClass);
	bool TryEnterFinale(
		AABTSM51SlingshotCord& Cord,
		APlayerController& Controller);
	/**
	 * Explicit visual-acceptance entry. It uses the normal interaction and
	 * release pipeline, but replaces authored aim input with the current
	 * layout's exact NominalInput before requesting release.
	 */
	bool TryLaunchNominalCaptureAttempt(
		AABTSM51SlingshotCord& Cord,
		APlayerController& Controller);
	/** Acceptance-only entry that holds one explicit input in Aiming. */
	bool TryEnterCaptureAim(
		AABTSM51SlingshotCord& Cord,
		APlayerController& Controller,
		const FABTSM11FinaleLaunchInput& Input);
	/** Acceptance-only entry that submits one explicit player launch input. */
	bool TryLaunchCaptureAttempt(
		AABTSM51SlingshotCord& Cord,
		APlayerController& Controller,
		const FABTSM11FinaleLaunchInput& Input);
	bool BeginAimFromCursor(APlayerController& Controller);
	bool UpdateAimFromCursor(APlayerController& Controller);
	void AdjustAimPower(double WheelSteps);
	bool ApplyHudControlDrag(
		EABTSM11FinaleControlAxis Axis,
		double PixelDelta,
		EABTSM11ControlSpeedGear Gear);
	bool ApplyHudControlWheel(
		EABTSM11FinaleControlAxis Axis,
		double WheelSteps,
		EABTSM11ControlSpeedGear Gear);
	bool ResetHudControlAxis(EABTSM11FinaleControlAxis Axis);
	bool PanHudOverview(const FVector2d& NormalizedScreenDelta);
	bool RotateHudOverview(double YawDegrees, double PitchDegrees);
	bool ZoomHudOverview(double ZoomMultiplier);
	bool ResetHudOverview();
	bool SelectHudTrajectoryProbe(const FABTSM11TrajectoryHit& Hit);
	bool RebaseHudTrajectoryProbe();
	void FollowAutomaticPreviewTarget();
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
	/**
	 * Read-only main-world environment phase. Integration owns the actual
	 * GroundDay/altitude-transition/FinaleSpace profile mapping.
	 */
	EABTSM11FinaleEnvironmentStage GetFinaleEnvironmentStage() const;
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
	const FABTSM11F4GuidanceTarget& GetF4GuidanceTarget() const
	{
		return F4GuidanceTarget;
	}
	const FString& GetF4GuidanceFailure() const
	{
		return F4GuidanceFailure;
	}
	bool IsF4GuidanceInFlight() const
	{
		return bF4GuidanceInFlight;
	}
	bool IsCurrentInputStrictF4() const
	{
		return DoesInputMatchLatestSolve()
			&& CurrentClassification.IsF(4);
	}
	const FABTSM11PreviewSelection& GetPreviewSelection() const
	{
		return PreviewSelection;
	}
	const FABTSM11OrbitalDiagramSnapshot& GetDiagramSnapshot() const
	{
		return DiagramSnapshot;
	}
	const FABTSM11OrbitalSceneSnapshot& GetHudOrbitalScene() const
	{
		return HudOrbitalScene;
	}
	const FABTSM11OverviewProjection& GetHudOverviewProjection() const
	{
		return HudOverviewProjection;
	}
	const FABTSM11OverviewViewState& GetHudOverviewView() const
	{
		return HudOverviewView;
	}
	const FABTSM11TrajectoryProbe& GetHudTrajectoryProbe() const
	{
		return HudTrajectoryProbe;
	}
	const FABTSM11ProbeProjection& GetHudProbeProjection() const
	{
		return HudProbeProjection;
	}
	const FABTSM11OrbitalSceneSnapshot& GetHudProbeReferenceScene() const
	{
		return HudProbeReferenceScene;
	}
	bool HasHudTrajectoryProbe() const
	{
		return HudTrajectoryProbe.bValid;
	}
	uint64 GetHudOverviewRevision() const { return HudOverviewRevision; }
	uint64 GetHudProbeRevision() const { return HudProbeRevision; }
	uint64 GetTargetCaptureCount() const { return TargetCaptureCount; }
	const FABTSM11PlaybackPlan& GetPreviewPlaybackPlan() const
	{
		return PreviewPlaybackPlan;
	}
	const FABTSM11PlaybackPlan& GetReleasedPlaybackPlan() const
	{
		return ReleasedPlaybackPlan;
	}
	const FABTSM11FinaleCameraShotPlan& GetReleasedCameraShotPlan() const
	{
		return ReleasedCameraShotPlan;
	}
	const FABTSM11TrajectoryResult* GetCurrentPrediction() const;
	/** Exact current result used by the selected target's PIP. */
	const FABTSM11TrajectoryResult* GetTargetPreviewPrediction() const;
	UTextureRenderTarget2D* GetTargetPreviewRenderTarget() const
	{
		return TargetPreviewRenderTarget;
	}
	/** Stable read-only T2-B integration surface for the existing remote PIP. */
	const AActor* GetFinaleRemotePreviewCaptureOwner() const;
	USceneCaptureComponent2D* GetFinaleRemotePreviewCaptureComponent()
	{
		return TargetPreviewCapture;
	}
	const USceneCaptureComponent2D*
		GetFinaleRemotePreviewCaptureComponent() const;
	EABTSStylizedViewClass GetFinaleRemotePreviewStylizedViewClass() const;
	double GetPlaybackElapsedSeconds() const { return PlaybackElapsedSeconds; }
	/** Trajectory seconds advanced per presentation second for this playback. */
	double GetPlaybackPresentationTimeScale() const;
	double GetFailureBlackoutAlpha() const
	{
		return FailureTimeline.GetBlackoutAlpha();
	}
	AABTSM6SlingshotCamera* GetAimCamera() const
	{
		return AimCamera;
	}
	AABTSM11FinaleFlightCamera* GetFlightCamera() const
	{
		return FlightCamera;
	}
	/** Read-only subject identity for M11 camera observation/capture tools. */
	AABTSM25BirdCharacter* GetAttemptBird() const
	{
		return AttemptBird;
	}
	/** Frozen M6 order: controlled bird first, remaining BirdIds ascending. */
	const TArray<TObjectPtr<AABTSM25BirdCharacter>>&
		GetAttemptFormationBirds() const
	{
		return AttemptFormationBirds;
	}
	const TArray<double>& GetFormationAdjacentArcSpacingCM() const
	{
		return FormationAdjacentArcSpacingCM;
	}
	double GetFinaleFormationSpacingCM() const
	{
		return ResolvedFormationSpacingCM;
	}
	bool IsFinaleFormationFullyDeployed() const
	{
		return bFormationFullyDeployed;
	}
	const UABTSM11FinaleBirdTrailComponent* GetFinaleBirdTrail() const
	{
		return FinaleBirdTrail;
	}
	const AABTSM11FinaleSystem* GetFinaleSystem() const
	{
		return FinaleSystem;
	}
	double GetLastPreviewLatencyMilliseconds() const
	{
		return LastPreviewLatencyMilliseconds;
	}
	double GetLastPreviewSolveMilliseconds() const
	{
		return LastPreviewSolveMilliseconds;
	}
	uint64 GetDiscardedPreviewSolveCount() const
	{
		return DiscardedPreviewSolveCount;
	}
	bool IsPreviewSolveInFlight() const
	{
		return bPreviewSolveInFlight;
	}
	bool IsPreviewDirty() const
	{
		return bPreviewDirty;
	}

	static bool ValidateInteractionContract(
		const AABTSM11FinaleSystem& InFinaleSystem,
		FString* OutFailure = nullptr);

private:
	void QueuePreviewSolveIfNeeded();
	void QueueNominalPhysicalSolve();
	void QueueF4GuidanceSolve();
	void HandlePreviewSolveCompleted(
		TSharedPtr<FABTSM11PreviewSolvePayload> Payload);
	void HandleNominalSolveCompleted(
		TSharedPtr<FABTSM11NominalSolvePayload> Payload);
	void HandleF4GuidanceSolveCompleted(
		TSharedPtr<FABTSM11F4GuidanceSolvePayload> Payload);
	void DrainCompletedSolves();
	void RebuildPublishedPreview();
	void RebuildHudPublishedData();
	bool ApplyHudTargetInput(
		const FABTSM11FinaleLaunchInput& TargetDesiredInput);
	bool FinalizePendingRelease();
	void UpdateAiming(float DeltaSeconds);
	void UpdatePlayback(float DeltaSeconds);
	void UpdateFailurePresentation(float DeltaSeconds);
	void UpdatePouchPresentation();
	bool BuildAttemptFormation(
		AABTSM25BirdCharacter& ControlledBird,
		FString& OutFailure);
	bool EnterAttemptFormationPouch(
		const FVector& PouchLocation,
		const FQuat& PouchRotation);
	bool UpdateFormationPlayback(
		double PlaybackTimeSeconds,
		const FABTSM110FinaleLocalFrame& Frame,
		const FVector& PrimaryWorldPosition);
	bool UpdateFormationFlightRotations(
		double PlaybackTimeSeconds,
		const FABTSM110FinaleLocalFrame& Frame,
		const FVector& PrimaryWorldVelocity,
		const FQuat& CurrentViewRotation);
	FTransform BuildFormationPouchTransform(
		int32 FormationIndex,
		const FVector& PouchLocation,
		const FQuat& PouchRotation) const;
	void ApplyFormationMountedVisualFrame(int32 FormationIndex);
	void ResetFormationRuntime();
	bool EnsureAimCamera();
	bool EnsureFlightCamera();
	void RestoreAimCameraView();
	bool BuildAimFrame(
		const AABTSM51SlingshotCord& Cord,
		const AABTSM25BirdCharacter& Bird);
	bool ApplyAbsoluteCursorAim(APlayerController& Controller);
	void MarkTargetCaptureDirty();
	void FlushTargetCapture();
	void RestoreAttemptToWorld(bool bKeepFinaleMode);
	void BeginAttemptFailure(
		const FString& Reason,
		bool bContinueReleasedFlight = false);
	void FailInteraction(const FString& Reason);
	bool DoesInputMatchLatestSolve() const;
	AActor* ResolvePreviewTargetActor(
		EABTSM11PreviewTarget Target) const;
	AActor* ResolveHudProbeContextActor() const;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11-C|Capture")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11-C|Capture")
	TObjectPtr<USceneCaptureComponent2D> TargetPreviewCapture;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11-C|Presentation")
	TObjectPtr<UABTSM11FinaleBirdTrailComponent> FinaleBirdTrail;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> TargetPreviewRenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM11FinaleSystem> FinaleSystem;

	UPROPERTY(Transient)
	TObjectPtr<AABTSBirdParty> Party;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM25BirdCharacter> AttemptBird;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AABTSM25BirdCharacter>> AttemptFormationBirds;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM51SlingshotCord> ActiveCord;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM6SlingshotCamera> AimCamera;

	UPROPERTY(Transient)
	TObjectPtr<AABTSM11FinaleFlightCamera> FlightCamera;

	UPROPERTY(Transient)
	TSubclassOf<AABTSM6SlingshotCamera> AimCameraClass;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Input",
		meta = (ClampMin = "0.0", ClampMax = "60.0"))
	double InitialPitchDegrees = 20.0;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Input",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double InitialPower = 0.90;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|Playback",
		meta = (ClampMin = "0.1", ClampMax = "100.0"))
	double PlaybackTimeScale = 18.0;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|M6 Formation",
		meta = (ClampMin = "20.0", ClampMax = "500.0", Units = "cm"))
	double M6PouchHorizontalSpacingCM = 110.0;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|M6 Formation",
		meta = (ClampMin = "20.0", ClampMax = "500.0", Units = "cm"))
	double M6PouchVerticalSpacingCM = 100.0;

	/** Extra Space-only separation between the large pouch and all four birds. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|M6 Formation",
		meta = (ClampMin = "0.0", ClampMax = "200.0", Units = "cm"))
	double M6PouchForwardClearanceCM =
		FABTSM11M6InputParityProfile::SpaceFormationPouchForwardClearanceCM;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|M6 Formation",
		meta = (ClampMin = "50.0", ClampMax = "2000.0", Units = "cm"))
	double M6MinimumFlightSpacingCM = 260.0;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|M6 Formation",
		meta = (ClampMin = "0.0", ClampMax = "500.0", Units = "cm"))
	double M6FormationClearanceCM = 80.0;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-C|M6 Formation",
		meta = (ClampMin = "0.05", ClampMax = "1.0"))
	double M6FollowerDeploymentWindowSpacingFraction = 0.50;

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
	FABTSM11FailurePresentationConfig ReleasedFailurePresentationConfig;
	FABTSM11PreviewSelection PreviewSelection;
	FABTSM11PrefixClassification CurrentClassification;
	FABTSM11F4GuidanceTarget F4GuidanceTarget;
	FString F4GuidanceFailure;
	FABTSM11TrajectoryResult LatestQualifiedResult;
	FABTSM11TrajectoryResult LatestSameInputPhysicalResult;
	FABTSM11TrajectoryResult NominalPhysicalResult;
	FABTSM11PlaybackPlan PreviewPlaybackPlan;
	FABTSM11PlaybackPlan ReleasedPlaybackPlan;
	FABTSM11TrajectoryResult ReleasedCameraTrajectoryResult;
	FABTSM11FinaleCameraShotPlan ReleasedCameraShotPlan;
	FABTSM11OrbitalDiagramSnapshot DiagramSnapshot;
	FABTSM11FinaleControlPanelState HudControlPanel;
	FABTSM11OverviewViewState HudOverviewView;
	FABTSM11OverviewViewState InitialHudOverviewView;
	FABTSM11OrbitalSceneSnapshot HudOrbitalScene;
	FABTSM11OverviewProjection HudOverviewProjection;
	FABTSM11TrajectoryProbe HudTrajectoryProbe;
	FABTSM11ProbeProjection HudProbeProjection;
	FABTSM11OrbitalSceneSnapshot HudProbeReferenceScene;
	FABTSM11FinaleLaunchInput InitialAimInput;
	FABTSM11FinaleLaunchInput LatestSolvedInput;
	FABTSM11FinaleLaunchInput FrozenReleaseInput;
	FTransform AttemptBirdOriginalTransform = FTransform::Identity;
	FVector AttemptBirdOriginalVisualScale = FVector::OneVector;
	TArray<FTransform> AttemptFormationOriginalTransforms;
	TArray<FVector> AttemptFormationOriginalVisualScales;
	TArray<FVector> AttemptFormationVisualRelativeLocations;
	TArray<FQuat> AttemptFormationVisualAxisCorrections;
	TArray<FTransform> AttemptFormationPouchTransforms;
	TArray<uint8> AttemptFormationInPouch;
	FABTSM11FinaleFormationPath FormationPath;
	TArray<double> FormationAdjacentArcSpacingCM;
	double ResolvedFormationSpacingCM = 0.0;
	bool bFormationFullyDeployed = false;
	TWeakObjectPtr<APlayerController> ActiveFinaleController;
	FVector AimSlingCenter = FVector::ZeroVector;
	FVector AimSlingForward = FVector::ForwardVector;
	FVector AimSlingRight = FVector::RightVector;
	FVector AimSlingUp = FVector::UpVector;
	FVector AimRestPouchLocation = FVector::ZeroVector;
	FVector AimPouchLocation = FVector::ZeroVector;
	double LastPreviewLatencyMilliseconds = 0.0;
	double LastPreviewSolveMilliseconds = 0.0;
	double PlaybackElapsedSeconds = 0.0;
	double PlaybackPresentationEndTimeSeconds = 0.0;
	double FailurePresentationStartTimeSeconds = 0.0;
	bool bFailureFlightContinuationActive = false;
	uint64 DiscardedPreviewSolveCount = 0;
	uint64 HudOverviewRevision = 0;
	uint64 HudProbeRevision = 0;
	uint64 TargetCaptureCount = 0;
	int64 AimRevision = 0;
	int64 LatestSolvedRevision = INDEX_NONE;
	TFuture<TSharedPtr<FABTSM11PreviewSolvePayload>> PreviewSolveFuture;
	TFuture<TSharedPtr<FABTSM11NominalSolvePayload>> NominalSolveFuture;
	TFuture<TSharedPtr<FABTSM11F4GuidanceSolvePayload>> F4GuidanceFuture;
	bool bPreviewDirty = false;
	bool bPreviewSolveInFlight = false;
	bool bNominalSolveInFlight = false;
	bool bF4GuidanceInFlight = false;
	bool bNominalPhysicalReady = false;
	bool bLatestPhysicalResultAvailable = false;
	bool bAttemptBirdInPouch = false;
	bool bCameraDirectorFallbackLogged = false;
	bool bTargetCaptureDirty = false;
	bool bTargetCaptureInitialized = false;
	bool bAimFrameValid = false;
};
