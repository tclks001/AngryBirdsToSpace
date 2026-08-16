// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Presentation/ABTSOpeningCinematicTypes.h"
#include "ABTSOpeningCinematicPreview.generated.h"

class APlayerController;
class AABTSM25BirdCharacter;
class UABTSBirdAnimationPresentationComponent;
class UCameraComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UWorld;

enum class EABTSOpeningStartResult : uint8
{
	Started,
	DebugSkipped,
	Rejected
};

/**
 * C++ opening presentation. Console preview stays isolated; the release entry
 * temporarily takes visual control of the ready real Party and restores it at
 * an exact proxy-to-real handoff.
 */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSOpeningCinematicPreview final : public AActor
{
	GENERATED_BODY()

public:
	AABTSOpeningCinematicPreview();

	/** Starts the release sequence at the ready four-bird party's real spawn frame. */
	static EABTSOpeningStartResult TryStartProductionOpening(UWorld* World);

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void SetPreviewTimeScale(float InTimeScale);
	void StopPreview();
	float GetElapsedPreviewSeconds() const { return ElapsedSeconds; }
	EABTSOpeningPhase GetOpeningPhase() const { return FABTSOpeningCinematicEvaluator::ResolvePhase(ElapsedSeconds); }

private:
	void InitializeAnimationDrivers();
	void InitializeCaptureBeamVisual();
	bool InitializeProductionBinding();
	void ReleaseProductionBinding();
	void UpdateBirds(float DeltaSeconds);
	void UpdateUFOAndCaptureBeam();
	void UpdateCamera();
	void FinishPreview(bool bBlendBack);
	FQuat ResolveBirdVisualRotation(const FVector& LocalFacing) const;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|Opening Preview")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|Opening Preview")
	TObjectPtr<UCameraComponent> CinematicCamera;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|Opening Preview")
	TObjectPtr<UStaticMeshComponent> PreviewStage;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|Opening Preview")
	TObjectPtr<UStaticMeshComponent> UFOVisual;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|Opening Preview")
	TObjectPtr<UProceduralMeshComponent> CaptureBeam;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|Opening Preview")
	TObjectPtr<UProceduralMeshComponent> CaptureBeamHalo;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|Opening Preview")
	TObjectPtr<UPointLightComponent> CaptureLight;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|Opening Preview")
	TArray<TObjectPtr<USkeletalMeshComponent>> BirdVisuals;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UABTSBirdAnimationPresentationComponent>> AnimationDrivers;

	UPROPERTY(Transient)
	TObjectPtr<APlayerController> PreviewController;

	UPROPERTY(Transient)
	TObjectPtr<AActor> SavedViewTarget;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AABTSM25BirdCharacter>> ProductionPartyBirds;
	TArray<bool> ProductionBirdWasHidden;
	TArray<FVector> ProductionHandoffLocalLocations;
	TArray<FQuat> ProductionHandoffLocalRotations;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CaptureBeamCoreMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CaptureBeamHaloMID;

	UPROPERTY()
	TSoftObjectPtr<UMaterialInterface> CaptureBeamCoreMaterial;

	UPROPERTY()
	TSoftObjectPtr<UMaterialInterface> CaptureBeamHaloMaterial;

	UPROPERTY(EditAnywhere, Category = "ABTS|Opening Preview", meta = (ClampMin = "0.05", ClampMax = "8.0"))
	float PreviewTimeScale = 1.0f;

	float ElapsedSeconds = 0.0f;
	double LastProductionWallSeconds = 0.0;
	bool bProductionBinding = false;
	bool bProductionBindingReleased = false;
	bool bProductionWorldWasPaused = false;
	bool bProductionInputWasBlocked = false;
	bool bProductionHUDWasVisible = true;
	bool bPreviewFinished = false;
};
