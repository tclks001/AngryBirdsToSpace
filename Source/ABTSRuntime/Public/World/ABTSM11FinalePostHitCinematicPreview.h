// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/ABTSM11FinalePostHitCinematicTypes.h"
#include "ABTSM11FinalePostHitCinematicPreview.generated.h"

class APlayerController;
class UABTSBirdAnimationPresentationComponent;
class UCameraComponent;
class UPointLightComponent;
class UPrimitiveComponent;
class USceneCaptureComponent2D;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextureRenderTarget2D;

/**
 * Collision-free M11-D post-hit cinematic previsualization. The Actor owns
 * only disposable presentation proxies and never mutates Party, solver,
 * trajectory, authoritative UFO or gameplay state.
 */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM11FinalePostHitCinematicPreview final
	: public AActor
{
	GENERATED_BODY()

public:
	AABTSM11FinalePostHitCinematicPreview();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void SetPreviewTimeScale(float InTimeScale);
	bool ConfigureOffscreenCapture(
		const FString& InOutputDirectory,
		const FString& InMovieName,
		int32 InFrameRate,
		int32 InWidth,
		int32 InHeight,
		int32 InJpegQuality);
	void StopPreview();

	float GetElapsedPreviewSeconds() const { return ElapsedSeconds; }
	EABTSM11FinalePostHitPhase GetPreviewPhase() const
	{
		return FABTSM11FinalePostHitCinematicEvaluator::ResolvePhase(
			ElapsedSeconds);
	}
	bool IsOffscreenCaptureEnabled() const { return bCaptureEnabled; }
	FString GetCaptureVideoPath() const;
	const FString& GetCaptureFailureReason() const
	{
		return CaptureFailureReason;
	}

private:
	void InitializeAnimationDrivers();
	void InitializeUFOPresentation();
	bool UpdatePresentation(float DeltaSeconds);
	void UpdateBirds(float DeltaSeconds);
	bool UpdateUFOAndDebris();
	bool ActivateRealUFODebris();
	bool AdvanceRealUFODebrisPlayback();
	void StopRealUFODebrisSimulation();
	void UpdateCamera();
	void UpdateLighting();
	void TriggerCrossedAudioCues(float PreviousTimeSeconds, float CurrentTimeSeconds);
	FQuat ResolveBirdVisualRotation(const FVector& LocalFacing) const;
	void FinishPreview(bool bBlendBack, bool bSuccess, const FString& Reason);

	bool StartOffscreenCapture();
	bool CaptureCurrentFrame();
	bool MuxCapturedFramesToAvi();
	bool WriteCaptureManifest(bool bSuccess, const FString& Reason) const;
	void RestoreCaptureGlobals();
	void RestoreGeometryCollectionRenderer();
	FString GetCaptureFrameWildcard() const;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11-D|Post Hit Preview")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11-D|Post Hit Preview")
	TObjectPtr<UCameraComponent> CinematicCamera;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11-D|Post Hit Preview")
	TObjectPtr<USceneCaptureComponent2D> RecordingCapture;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11-D|Post Hit Preview")
	TObjectPtr<UStaticMeshComponent> FallbackUFOVisual;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11-D|Post Hit Preview")
	TObjectPtr<UPointLightComponent> ImpactFlash;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11-D|Post Hit Preview")
	TObjectPtr<UPointLightComponent> CinematicKeyLight;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11-D|Post Hit Preview")
	TObjectPtr<UPointLightComponent> CinematicFillLight;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11-D|Post Hit Preview")
	TObjectPtr<UPointLightComponent> CinematicRimLight;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M11-D|Post Hit Preview")
	TArray<TObjectPtr<USkeletalMeshComponent>> BirdVisuals;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UABTSBirdAnimationPresentationComponent>>
		AnimationDrivers;

	UPROPERTY(Transient)
	TObjectPtr<AActor> UFOPresentationActor;

	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> UFOIntactVisual;

	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> UFOBrokenVisual;
	TArray<FTransform> InitialRealDebrisTransforms;

	UPROPERTY(Transient)
	TObjectPtr<APlayerController> PreviewController;

	UPROPERTY(Transient)
	TObjectPtr<AActor> SavedViewTarget;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RecordingRenderTarget;

	UPROPERTY(EditAnywhere, Category = "ABTS|M11-D|Post Hit Preview",
		meta = (ClampMin = "0.05", ClampMax = "8.0"))
	float PreviewTimeScale = 1.0f;

	FString CaptureOutputDirectory;
	FString CaptureMovieName = TEXT("M11PostHitFinale");
	FString CaptureFailureReason;
	float ElapsedSeconds = 0.0f;
	double PreviousFixedDeltaTime = 0.0;
	int32 CaptureFrameRate = 30;
	int32 CaptureWidth = 1280;
	int32 CaptureHeight = 720;
	int32 CaptureJpegQuality = 90;
	int32 CapturedFrameCount = 0;
	int32 RemainingWarmupFrames = 24;
	int32 PreviousStylizedProfile = 0;
	int32 PreviousGeometryCollectionCustomRenderer = 1;
	bool bCaptureEnabled = false;
	bool bCaptureStarted = false;
	bool bPreviewFinished = false;
	bool bPlaybackActionsStarted = false;
	bool bPreviousUseFixedTimeStep = false;
	bool bPreviousStylizedEnabled = false;
	bool bStylizedCaptureRegistered = false;
	bool bCaptureGlobalsRestored = false;
	bool bGeometryCollectionRendererOverridden = false;
	bool bRealUFODebrisReady = false;
	bool bRealUFODebrisActivated = false;
	bool bRealUFODebrisImpulseApplied = false;
	bool bRealUFODebrisStopped = false;
};
