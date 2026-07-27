// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/ABTSM10ScoutMapTypes.h"
#include "ABTSM101LandingPreviewCamera.generated.h"

class AABTSM3Planet;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USceneCaptureComponent2D;
class USceneComponent;
class UTextureRenderTarget2D;
struct FABTSM6TrajectoryPreview;

/**
 * One runtime-only SceneCapture used by M10.1-B. Its actor root intentionally
 * stays at world identity: only the capture component moves, while trajectory
 * instances remain authored in world space.
 */
UCLASS(NotBlueprintable)
class ABTSRUNTIME_API AABTSM101LandingPreviewCamera : public AActor
{
	GENERATED_BODY()

public:
	AABTSM101LandingPreviewCamera();

	/** Called by the M10 world system before the first eligible Pulling frame. */
	void Configure(const FABTSM10ScoutMapSettings& InSettings);
	/** Refreshes the capture at the configured cadence from M6's authoritative preview. */
	void UpdatePreview(
		const FABTSM6TrajectoryPreview& Preview,
		const AABTSM3Planet& Planet,
		float DeltaSeconds);
	void DeactivatePreview();

	bool IsPreviewActive() const { return bPreviewActive; }
	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }

private:
	void EnsureRenderTarget();
	void RefreshCapture(const FABTSM6TrajectoryPreview& Preview, const AABTSM3Planet& Planet);
	FVector ResolveIncidenceDirection(const FABTSM6TrajectoryPreview& Preview, const FVector& LandingUp) const;
	void RebuildTrajectoryPoints(const FABTSM6TrajectoryPreview& Preview);

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M10.1|Landing Preview")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M10.1|Landing Preview")
	TObjectPtr<USceneCaptureComponent2D> SceneCapture;

	/** Visible only to this and any future SceneCapture; never duplicates M6's main-view debug dots. */
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M10.1|Landing Preview")
	TObjectPtr<UInstancedStaticMeshComponent> TrajectoryPointInstances;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TrajectoryMaterial;

	FABTSM10ScoutMapSettings Settings;
	float CaptureAccumulatorSeconds = 0.0f;
	bool bPreviewActive = false;
};
