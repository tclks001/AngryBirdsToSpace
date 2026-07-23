// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "Slingshot/ABTSM6Types.h"
#include "ABTSM6SlingshotSystem.generated.h"

class AABTSBirdParty;
class AABTSM3Planet;
class AABTSM25BirdCharacter;
class AABTSM51SlingshotCord;
class AABTSM6DestructibleProxy;
class AABTSM6SlingshotCamera;
class AABTSM7BuildingMaterialSystem;
class UHierarchicalInstancedStaticMeshComponent;

/** M6 launch, trajectory, impact promotion, explosion and return coordinator. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM6SlingshotSystem : public AActor
{
	GENERATED_BODY()

public:
	AABTSM6SlingshotSystem();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	bool TryEnterLaunchMode(AABTSM51SlingshotCord& Cord);
	bool BeginPull(APlayerController& Controller);
	void UpdateAimFromCursor(APlayerController& Controller);
	void AdjustPullPower(float MouseWheelValue);
	void ReleaseLaunch();
	bool TryManualBlackDetonation(AActor* ClickedActor);
	void HandleProxyImpact(AABTSM6DestructibleProxy& Proxy, const FHitResult& Hit, float NormalSpeedCMPerSec);
	void ConfigureDebugSlingshots(bool bEnable, int32 InStartCellId);

	EABTSM6LaunchState GetLaunchState() const { return LaunchState; }
	bool IsLaunchModeActive() const { return LaunchState != EABTSM6LaunchState::Inactive; }

private:
	bool ResolveDependencies();
	bool IsBirdAllowed(const AABTSM25BirdCharacter& Bird, const AABTSM51SlingshotCord& Cord) const;
	void BuildLaunchFrame(AABTSM51SlingshotCord& Cord, AABTSM25BirdCharacter& Bird);
	void ArrangeWaitingBirds();
	void UpdatePouchAndPreview();
	void DrawPredictedTrajectory() const;
	FVector ComputeLaunchVelocity() const;
	void HandleBirdImpact(const FHitResult& Hit, float NormalSpeedCMPerSec, const FVector& IncomingVelocity);
	EABTSM6ImpactMaterial ResolveMaterial(const UPrimitiveComponent* Component) const;
	const FABTSM6BirdImpactProfile& GetBirdProfile(EABTSBirdId BirdId) const;
	const FABTSM6MaterialImpactProfile& GetMaterialProfile(EABTSM6ImpactMaterial Material) const;
	bool PromoteOrBreakHISM(UHierarchicalInstancedStaticMeshComponent& HISM, int32 InstanceIndex, EABTSM6ImpactMaterial Material, float NormalSpeedCMPerSec, const FVector& ImpulseDirection, float KnockThreshold, float BreakThreshold);
	void DetonateBlackBird(bool bManual);
	void BeginReturn();
	void UpdateReturn(float DeltaSeconds);
	void FinishReturn();
	void FreezeDynamicProxies();
	void SpawnDebugSlingshots();
	bool SpawnDebugSlingshotPair(const FVector& CenterDirection, const FVector& LaunchDirection, EABTSItemId StakeItem);
	bool QueryDebugSurfaceTransform(const FVector& UnitDirection, const FVector& Forward, float HeightOffsetCM, FTransform& OutTransform) const;

	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Classes")
	TSubclassOf<AABTSM6DestructibleProxy> ProxyClass;
	UPROPERTY(EditDefaultsOnly, Category = "ABTS|M6|Classes")
	TSubclassOf<AABTSM6SlingshotCamera> CameraClass;

	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Launch", meta = (ClampMin = "100.0"))
	float MinLaunchSpeedCMPerSec = 900.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Launch", meta = (ClampMin = "100.0"))
	float MaxLaunchSpeedCMPerSec = 2300.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Launch", meta = (ClampMin = "10.0"))
	float MinPullDistanceCM = 120.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Launch", meta = (ClampMin = "10.0"))
	float MaxPullDistanceCM = 430.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Launch", meta = (ClampMin = "0.01", ClampMax = "2.0"))
	float PullPowerWheelStep = 0.08f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Launch", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float FlightAirDragPerSecond = 0.08f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Launch", meta = (ClampMin = "20.0"))
	float MaxAimPlaneOffsetCM = 260.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Trajectory", meta = (ClampMin = "8", ClampMax = "128"))
	int32 TrajectorySampleCount = 54;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Trajectory", meta = (ClampMin = "0.01", ClampMax = "0.25"))
	float TrajectoryStepSeconds = 0.075f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Trajectory", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float TrajectoryPointSize = 8.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Impact")
	TArray<FABTSM6BirdImpactProfile> BirdImpactProfiles;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Impact")
	TArray<FABTSM6MaterialImpactProfile> MaterialImpactProfiles;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Impact", meta = (ClampMin = "0.0"))
	float SignificantImpactSpeedCMPerSec = 80.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Impact", meta = (ClampMin = "0.0"))
	float ProxyChainBreakSpeedCMPerSec = 760.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Black Bird", meta = (ClampMin = "50.0"))
	float BlackExplosionRadiusCM = 360.0f;
	/** Outer ring: objects survive but are promoted/reactivated and pushed away. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Black Bird", meta = (ClampMin = "50.0"))
	float BlackExplosionImpulseRadiusCM = 760.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Black Bird", meta = (ClampMin = "0.0"))
	float BlackExplosionImpulseSpeedCMPerSec = 1500.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Black Bird", meta = (ClampMin = "0.0"))
	float BlackAutoFuseSeconds = 2.2f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Return", meta = (ClampMin = "0.1"))
	float PostLandingQuietSeconds = 2.5f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Return", meta = (ClampMin = "0.1"))
	float ReturnDurationSeconds = 1.15f;

	TWeakObjectPtr<AABTSBirdParty> Party;
	TWeakObjectPtr<AABTSM3Planet> Planet;
	TWeakObjectPtr<AABTSM25BirdCharacter> LaunchedBird;
	TWeakObjectPtr<AABTSM51SlingshotCord> ActiveCord;
	TWeakObjectPtr<AABTSM7BuildingMaterialSystem> BuildingMaterialSystem;
	TObjectPtr<AABTSM6SlingshotCamera> SlingshotCamera;
	TArray<TWeakObjectPtr<AABTSM6DestructibleProxy>> DynamicProxies;
	EABTSM6LaunchState LaunchState = EABTSM6LaunchState::Inactive;
	FVector SlingCenter = FVector::ZeroVector;
	FVector SlingUp = FVector::UpVector;
	FVector SlingForward = FVector::ForwardVector;
	FVector SlingRight = FVector::RightVector;
	FVector RestPouchLocation = FVector::ZeroVector;
	FVector PouchLocation = FVector::ZeroVector;
	FVector AimPlaneOffset = FVector::ZeroVector;
	FVector ReturnStartLocation = FVector::ZeroVector;
	FVector ReturnTargetLocation = FVector::ZeroVector;
	float PullAlpha = 0.55f;
	float FlightElapsedSeconds = 0.0f;
	float QuietElapsedSeconds = 0.0f;
	float ReturnElapsedSeconds = 0.0f;
	float BlackFuseRemainingSeconds = -1.0f;
	bool bBlackDetonated = false;
	bool bSpawnDebugSlingshotsAtStart = false;
	bool bDebugSlingshotsSpawned = false;
	int32 DebugStartCellId = INDEX_NONE;
};
