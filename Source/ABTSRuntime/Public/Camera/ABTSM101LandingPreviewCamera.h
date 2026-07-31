// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/ABTSM10ScoutMapTypes.h"
#include "ABTSM101LandingPreviewCamera.generated.h"

class AABTSM3Planet;
class AABTSM9Satellite;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USceneCaptureComponent2D;
class USceneComponent;
class UTextureRenderTarget2D;
struct FABTSM6TrajectoryPreview;

UENUM(BlueprintType)
enum class EABTSM101PreviewSubject : uint8
{
	None,
	PrimaryLanding,
	SatelliteLanding
};

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
	/** Reuses the same capture/RT for an authoritative M9 lunar terminal. */
	void UpdateSatellitePreview(
		const FABTSM6TrajectoryPreview& Preview,
		AABTSM9Satellite& Satellite,
		AActor& E5Target,
		float DeltaSeconds);
	void DeactivatePreview();
	static bool IsSatelliteLandingTerminal(
		const FABTSM6TrajectoryPreview& Preview);
	/** Pure geometry seam used by runtime capture and automation. */
	static bool BuildSatelliteLandingViewFrame(
		const FABTSM6TrajectoryPreview& Preview,
		const FVector& SatelliteCenterWorld,
		float CameraDistanceCM,
		float PitchDegrees,
		FVector& OutLandingWorld,
		FVector& OutCameraWorld,
		FVector& OutLookDirection,
		FVector& OutScreenUp);

	bool IsPreviewActive() const { return bPreviewActive; }
	bool IsSatellitePreviewActive() const
	{
		return PreviewSubject == EABTSM101PreviewSubject::SatelliteLanding;
	}
	EABTSM101PreviewSubject GetPreviewSubject() const { return PreviewSubject; }
	UTextureRenderTarget2D* GetRenderTarget() const { return RenderTarget; }

private:
	void EnsureRenderTarget();
	void RefreshCapture(const FABTSM6TrajectoryPreview& Preview, const AABTSM3Planet& Planet);
	void RefreshSatelliteCapture(
		const FABTSM6TrajectoryPreview& Preview,
		AABTSM9Satellite& Satellite,
		AActor& E5Target,
		int32 TerminalSegmentStartIndex);
	FVector ResolveIncidenceDirection(const FABTSM6TrajectoryPreview& Preview, const FVector& LandingUp) const;
	void RebuildTrajectoryPoints(const FABTSM6TrajectoryPreview& Preview);
	void RebuildTrajectoryPointsAround(
		const FABTSM6TrajectoryPreview& Preview,
		int32 CenterSegmentStartIndex);
	void SetPreviewSubject(EABTSM101PreviewSubject NewSubject);

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
	EABTSM101PreviewSubject PreviewSubject =
		EABTSM101PreviewSubject::None;
	bool bPreviewActive = false;
};
