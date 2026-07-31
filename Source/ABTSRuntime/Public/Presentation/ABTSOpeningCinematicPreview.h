// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Presentation/ABTSOpeningCinematicTypes.h"
#include "ABTSOpeningCinematicPreview.generated.h"

class APlayerController;
class UABTSBirdAnimationPresentationComponent;
class UCameraComponent;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UPointLightComponent;

/**
 * Self-contained C++ opening previsualization. It owns only collision-free
 * presentation components and never queries or mutates the real bird party.
 */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSOpeningCinematicPreview final : public AActor
{
	GENERATED_BODY()

public:
	AABTSOpeningCinematicPreview();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	void SetPreviewTimeScale(float InTimeScale);
	void StopPreview();
	float GetElapsedPreviewSeconds() const { return ElapsedSeconds; }
	EABTSOpeningPhase GetOpeningPhase() const { return FABTSOpeningCinematicEvaluator::ResolvePhase(ElapsedSeconds); }

private:
	void InitializeAnimationDrivers();
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
	TObjectPtr<UStaticMeshComponent> CaptureBeam;

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

	UPROPERTY(EditAnywhere, Category = "ABTS|Opening Preview", meta = (ClampMin = "0.05", ClampMax = "8.0"))
	float PreviewTimeScale = 1.0f;

	float ElapsedSeconds = 0.0f;
	bool bPreviewFinished = false;
};
