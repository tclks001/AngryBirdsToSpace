// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Party/ABTSBirdTypes.h"
#include "World/ABTSM10ScoutMapTypes.h"
#include "ABTSM10ScoutMapSystem.generated.h"

class AABTSM3Planet;
class AABTSM6SlingshotSystem;
class AABTSM6DestructibleProxy;
class UHierarchicalInstancedStaticMeshComponent;
class UTexture2D;
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

	bool IsScoutMapRevealed() const { return bScoutMapRevealed; }
	UTexture2D* GetTerrainTexture() const { return TerrainTexture; }
	const FABTSM10ScoutMapSettings& GetSettings() const { return Settings; }
	const TArray<FABTSM10ScoutMapMarker>& GetEnvironmentMarkers() const { return EnvironmentMarkers; }
	/** Maps a world point into the immutable reveal frame. +X is east, +Y is screen-down. */
	bool ProjectWorldLocation(const FVector& WorldLocation, FVector2D& OutNormalizedMapPosition) const;
	/** Copies M6's authoritative aim prediction; this system never integrates a second HUD-only trajectory. */
	bool CopyCurrentTrajectoryPreview(FABTSM6TrajectoryPreview& OutPreview) const;

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

	UPROPERTY(VisibleInstanceOnly, Category = "ABTS|M10")
	FABTSM10ScoutMapSettings Settings;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TerrainTexture;

	TWeakObjectPtr<AABTSM3Planet> Planet;
	TWeakObjectPtr<AABTSM6SlingshotSystem> SlingshotSystem;
	TArray<FABTSM10ScoutMapMarker> EnvironmentMarkers;
	TArray<AABTSM6DestructibleProxy*> ProxyRefreshScratch;
	FVector RevealCenterUnit = FVector::UpVector;
	FVector MapEastUnit = FVector::RightVector;
	FVector MapNorthUnit = FVector::ForwardVector;
	float ResolvedScoutRadiusCM = 0.0f;
	float DependencyResolveAccumulatorSeconds = 0.5f;
	float EnvironmentRefreshAccumulatorSeconds = 0.0f;
	bool bBoundToSlingshot = false;
	bool bScoutMapRevealed = false;
};
