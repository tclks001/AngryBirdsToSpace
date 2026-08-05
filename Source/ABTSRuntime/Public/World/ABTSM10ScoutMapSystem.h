// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Party/ABTSBirdTypes.h"
#include "World/ABTSM101OrbitalOverviewTypes.h"
#include "World/ABTSM10ScoutMapTypes.h"
#include "ABTSM10ScoutMapSystem.generated.h"

class AABTSM3Planet;
class AABTSM6SlingshotSystem;
class AABTSM6DestructibleProxy;
class AABTSM101LandingPreviewCamera;
class AABTSM9Satellite;
class UHierarchicalInstancedStaticMeshComponent;
class UTexture2D;
class UTextureRenderTarget2D;
struct FABTSM6TrajectoryPreview;

/** Owns one fixed spherical scout snapshot plus live projected environment markers. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM10ScoutMapSystem : public AActor
{
	GENERATED_BODY()

public:
	AABTSM10ScoutMapSystem();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Must be called before FinishSpawningActor so BeginPlay binds with final settings. */
	void Configure(const FABTSM10ScoutMapSettings& InSettings);
	/** Isolated calibration entry; normal gameplay still reveals only after a completed Twig launch. */
	bool RevealForSlingshotCalibration(const FVector& CalibrationOriginWorld);

	bool IsScoutMapRevealed() const { return bScoutMapRevealed; }
	UTexture2D* GetTerrainTexture() const { return TerrainTexture; }
	const FABTSM10ScoutMapSettings& GetSettings() const { return Settings; }
	const TArray<FABTSM10ScoutMapMarker>& GetEnvironmentMarkers() const { return EnvironmentMarkers; }
	/** Maps a world point into the immutable reveal frame. +X is east, +Y is screen-down. */
	bool ProjectWorldLocation(const FVector& WorldLocation, FVector2D& OutNormalizedMapPosition) const;
	/** Copies M6's authoritative aim prediction; this system never integrates a second HUD-only trajectory. */
	bool CopyCurrentTrajectoryPreview(FABTSM6TrajectoryPreview& OutPreview) const;
	/** Narrow M10.1-B eligibility gate shared by the SceneCapture path and any future landing UI. */
	bool TryGetQualifiedReinforcedLandingPreview(FABTSM6TrajectoryPreview& OutPreview) const;
	/** True only while the scoped M10.1-B SceneCapture is currently eligible and visible. */
	bool IsLandingPreviewActive() const;
	/** Distinguishes the calibration-only lunar landing label from the normal landing label. */
	bool IsSatelliteLandingPreviewActive() const;
	/** Runtime-only render target consumed by the M10 HUD; nullptr while no capture was initialized. */
	UTextureRenderTarget2D* GetLandingPreviewRenderTarget() const;
	/** Integration-only read seam; ownership and capture cadence remain with M10. */
	AABTSM101LandingPreviewCamera* GetLandingPreviewCamera() const
	{
		return LandingPreviewCamera;
	}
	/** True while M10.1-C has a long reinforced prediction and a valid fitted-plane snapshot. */
	bool IsOrbitalOverviewActive() const { return OrbitalOverviewSnapshot.bValid; }
	/** Screen-independent projected geometry consumed read-only by the HUD. */
	const FABTSM101OrbitalOverviewSnapshot& GetOrbitalOverviewSnapshot() const { return OrbitalOverviewSnapshot; }

private:
	bool ResolveDependencies();
	void HandleLaunchCompleted(EABTSBirdId BirdId, const FVector& LandingLocation);
	bool RevealAtLanding(const FVector& LandingLocation);
	void BuildFixedMapFrame(const FVector& CenterUnitDirection, FVector& OutEastUnit, FVector& OutNorthUnit) const;
	UTexture2D* BuildTerrainTexture(
		const FVector& CenterUnitDirection,
		const FVector& EastUnit,
		const FVector& NorthUnit,
		float ScoutRadiusCM) const;
	void RefreshEnvironmentMarkers();
	void AppendHISMMarkers(UHierarchicalInstancedStaticMeshComponent* HISM, EABTSM10ScoutMarkerType Type);
	void AppendMarker(const FVector& WorldLocation, EABTSM10ScoutMarkerType Type);
	bool IsInsideEnvironmentBroadphase(const FVector& WorldLocation) const;
	void UpdateLandingPreview(float DeltaSeconds);
	void EnsureLandingPreviewCamera();
	bool TryGetQualifiedSatelliteLandingPreview(
		const FABTSM6TrajectoryPreview& Preview,
		AABTSM9Satellite*& OutSatellite,
		AActor*& OutTarget) const;
	void UpdateOrbitalOverview();
	bool BuildOrbitalOverviewSnapshot(const FABTSM6TrajectoryPreview& Preview);
	void ClearOrbitalOverview(bool bLogTransition);

	UPROPERTY(VisibleInstanceOnly, Category = "ABTS|M10")
	FABTSM10ScoutMapSettings Settings;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TerrainTexture;

	TWeakObjectPtr<AABTSM3Planet> Planet;
	TWeakObjectPtr<AABTSM6SlingshotSystem> SlingshotSystem;
	UPROPERTY(Transient)
	TObjectPtr<AABTSM101LandingPreviewCamera> LandingPreviewCamera;
	TArray<FABTSM10ScoutMapMarker> EnvironmentMarkers;
	TArray<AABTSM6DestructibleProxy*> ProxyRefreshScratch;
	FVector RevealCenterUnit = FVector::UpVector;
	FVector MapEastUnit = FVector::RightVector;
	FVector MapNorthUnit = FVector::ForwardVector;
	float ResolvedScoutRadiusCM = 0.0f;
	float DependencyResolveAccumulatorSeconds = 0.5f;
	float EnvironmentRefreshAccumulatorSeconds = 0.0f;
	FABTSM101OrbitalOverviewSnapshot OrbitalOverviewSnapshot;
	FVector CachedOrbitalPreviewStart = FVector::ZeroVector;
	FVector CachedOrbitalPreviewVelocity = FVector::ZeroVector;
	float CachedOrbitalPreviewPathLengthCM = -1.0f;
	int32 CachedOrbitalPreviewPointCount = 0;
	FVector LastOrbitalPlaneNormal = FVector::ZeroVector;
	FVector LastOrbitalHorizontalAxis = FVector::ZeroVector;
	bool bBoundToSlingshot = false;
	bool bScoutMapRevealed = false;
};
