// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/ABTSInventoryTypes.h"
#include "Slingshot/ABTSM6PhysicsSettleMonitor.h"
#include "Slingshot/ABTSM6Types.h"
#include "Slingshot/ABTSSlingshotVisualTypes.h"
#include "ABTSM6SlingshotSystem.generated.h"

class AABTSBirdParty;
class AABTSM3Planet;
class AABTSM25BirdCharacter;
class AABTSM51SlingshotCord;
class AABTSM6DestructibleProxy;
class AABTSM6SlingshotCamera;
class AABTSM7BuildingMaterialSystem;
class AABTSM71PlaceableSlingshotActor;
class UHierarchicalInstancedStaticMeshComponent;
class USceneComponent;
class UStaticMeshComponent;
class UPhysicalMaterial;

/** One HISM component's descending, index-stable startup overlap queue. */
struct FABTSM6StartupHISMWarmupQueue
{
	TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> Component;
	TArray<int32> CandidateIndicesDescending;
	int32 NextCandidateOffset = 0;
};

/** Fired once after a launched bird has returned and M6 is inactive again. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FABTSM6LaunchCompletedNative, EABTSBirdId, const FVector&);

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
	void ConfigurePlanarTestMode(const FVector& InPlaneOrigin, const FVector& InPlaneUp);

	EABTSM6LaunchState GetLaunchState() const { return LaunchState; }
	bool IsLaunchModeActive() const { return LaunchState != EABTSM6LaunchState::Inactive; }
	/** True only after startup Chaos settling has frozen every promoted world body. */
	bool IsStartupPhysicsWarmupComplete() const { return !bEnableStartupPhysicsWarmup || bStartupPhysicsWarmupComplete; }
	/** The location is the final settled landing point captured before return flight begins. */
	FABTSM6LaunchCompletedNative& OnLaunchCompleted() { return LaunchCompletedNative; }
	/** Stable source for M10; callers must not cache HISM indices or proxy pointers across refreshes. */
	void GatherLiveDestructibleProxies(TArray<AABTSM6DestructibleProxy*>& OutProxies) const;

private:
	bool ResolveDependencies();
	bool IsBirdAllowed(const AABTSM25BirdCharacter& Bird, const AABTSM51SlingshotCord& Cord) const;
	void BuildLaunchFrame(AABTSM51SlingshotCord& Cord, AABTSM25BirdCharacter& Bird);
	void ArrangeWaitingBirds();
	void UpdatePouchAndPreview();
	void ConfigurePouchVisual(const AABTSM51SlingshotCord& Cord);
	void UpdatePouchVisual(const FQuat& PouchRotation);
	void SetPouchVisualActive(bool bActive);
	FVector GetBirdInPouchLocation(const FQuat& PouchRotation) const;
	void DrawPredictedTrajectory() const;
	FVector ComputeLaunchVelocity() const;
	void HandleBirdImpact(const FHitResult& Hit, float NormalSpeedCMPerSec, const FVector& IncomingVelocity);
	EABTSM6ImpactMaterial ResolveMaterial(const UPrimitiveComponent* Component) const;
	const FABTSM6BirdImpactProfile& GetBirdProfile(EABTSBirdId BirdId) const;
	const FABTSM6MaterialImpactProfile& GetMaterialProfile(EABTSM6ImpactMaterial Material) const;
	bool PromoteOrBreakHISM(UHierarchicalInstancedStaticMeshComponent& HISM, int32 InstanceIndex, EABTSM6ImpactMaterial Material, const FABTSM6MaterialImpactProfile& MaterialProfile, float NormalSpeedCMPerSec, const FVector& ImpulseDirection, float KnockThreshold, float BreakThreshold, float AccumulatedDamage = 0.0f);
	float ComputeDamageGain(const FABTSM6MaterialImpactProfile& MaterialProfile, float NormalSpeedCMPerSec, float BreakThreshold) const;
	uint64 GetHISMDamageKey(const UHierarchicalInstancedStaticMeshComponent& HISM, int32 InstanceIndex) const;
	void BeginLaunchGravityPhase();
	int32 PromoteHISMForLaunchGravity(UHierarchicalInstancedStaticMeshComponent& HISM);
	void UpdateStartupPhysicsWarmup(float DeltaSeconds);
	void BeginStartupPhysicsWarmup();
	int32 BuildStartupHISMOverlapQueues(
		const TArray<UHierarchicalInstancedStaticMeshComponent*>& HISMs,
		int32& OutOverlapPairCount,
		int32& OutFallbackPairCount);
	int32 StartNextStartupHISMWarmupBatch();
	bool HasPendingStartupHISMCandidates() const;
	int32 RestoreStartupHISMProxies();
	void FinishStartupPhysicsWarmup(const FABTSM6PhysicsActivitySummary& Summary);
	void DetonateBlackBird(bool bManual);
	void BeginSettlement();
	void UpdatePhysicsSettlement(float DeltaSeconds);
	void CollectDynamicPhysicsBodies(TArray<UPrimitiveComponent*>& OutBodies);
	void MarkPhysicsActivity();
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
	/** Shared local +Z offset from the pouch centre to the bird actor while aiming. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Visual", meta = (UIMin = "-100.0", UIMax = "100.0"))
	float BirdInPouchOffsetCM = 20.0f;
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
	/** HISM objects inside this radius become Chaos bodies while launch mode is active. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Impact", meta = (ClampMin = "100.0", UIMin = "1000.0", UIMax = "12000.0"))
	float LaunchGravityActivationRadiusCM = 6000.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Impact", meta = (ClampMin = "0.0"))
	float LaunchObjectGravityAccelerationCMPerSec2 = 980.0f;
	/** Shared no-damage window after launch-time static bodies enter Chaos. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Impact", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float LaunchContactDamageGraceSeconds = 0.20f;

	/** Runs once after placement. Buildings and only overlapping HISM candidates settle before the first launch. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Startup Physics")
	bool bEnableStartupPhysicsWarmup = true;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Startup Physics", meta = (ClampMin = "0.0", UIMax = "5.0"))
	float StartupPhysicsWarmupInitialDelaySeconds = 0.25f;
	/** HISM origins farther apart than this are never expensive-tested for startup overlap. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Startup Physics", meta = (ClampMin = "10.0", UIMax = "1000.0", Units = "cm"))
	float StartupHISMOverlapSearchRadiusCM = 260.0f;
	/** Conservative fallback for meshes without usable convex simple collision. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Startup Physics", meta = (ClampMin = "0.0", UIMax = "300.0", Units = "cm"))
	float StartupHISMFallbackCenterDistanceCM = 45.0f;
	/** Hard upper bound on simultaneous startup HISM Chaos proxies. Remaining candidates run in later batches. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Startup Physics", meta = (ClampMin = "8", ClampMax = "512"))
	int32 StartupHISMMaxSimultaneousBodies = 384;
	/** Fixed penetration-relaxation window for each HISM batch; long downhill motion is deliberately frozen afterward. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Startup Physics", meta = (ClampMin = "0.1", UIMax = "3.0", Units = "s"))
	float StartupHISMBatchRelaxationSeconds = 0.75f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Startup Physics", meta = (ClampMin = "0.0", UIMax = "100.0"))
	float StartupSettleLinearSpeedThresholdCMPerSec = 8.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Startup Physics", meta = (ClampMin = "0.0", UIMax = "100.0"))
	float StartupSettleAngularSpeedThresholdDegPerSec = 4.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Startup Physics", meta = (ClampMin = "0.0", UIMax = "10.0"))
	float StartupSettleStableHoldSeconds = 1.25f;
	/** Per-batch hard deadline. A pathological batch is frozen with an error instead of blocking PIE forever. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Startup Physics", meta = (ClampMin = "1.0", UIMax = "30.0", Units = "s"))
	float StartupSettleDiagnosticPeriodSeconds = 6.0f;

	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Black Bird", meta = (ClampMin = "50.0"))
	float BlackExplosionRadiusCM = 360.0f;
	/** Outer ring: objects survive but are promoted/reactivated and pushed away. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Black Bird", meta = (ClampMin = "50.0"))
	float BlackExplosionImpulseRadiusCM = 760.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Black Bird", meta = (ClampMin = "0.0"))
	float BlackExplosionImpulseSpeedCMPerSec = 1500.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Black Bird", meta = (ClampMin = "0.0"))
	float BlackAutoFuseSeconds = 2.2f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Return|Settlement", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "200.0"))
	float SettleLinearSpeedThresholdCMPerSec = 20.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Return|Settlement", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "90.0"))
	float SettleAngularSpeedThresholdDegPerSec = 10.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Return|Settlement", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0"))
	float SettleStableHoldSeconds = 2.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Return|Settlement", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0"))
	float SettleMinimumPostActivitySeconds = 2.5f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Return|Settlement", meta = (ClampMin = "0.1", UIMin = "1.0", UIMax = "30.0"))
	float SettleMaximumWaitSeconds = 15.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Return|Settlement", meta = (ClampMin = "0.01", ClampMax = "1.0", UIMin = "0.05", UIMax = "0.5"))
	float SettleSampleIntervalSeconds = 0.1f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Return", meta = (ClampMin = "0.1"))
	float ReturnDurationSeconds = 1.15f;

	TWeakObjectPtr<AABTSBirdParty> Party;
	TWeakObjectPtr<AABTSM3Planet> Planet;
	FVector PlanarOrigin = FVector::ZeroVector;
	FVector PlanarUp = FVector::UpVector;
	bool bPlanarTestMode = false;
	TWeakObjectPtr<AABTSM25BirdCharacter> LaunchedBird;
	TWeakObjectPtr<AABTSM51SlingshotCord> ActiveCord;
	TWeakObjectPtr<AABTSM7BuildingMaterialSystem> BuildingMaterialSystem;
	TObjectPtr<AABTSM6SlingshotCamera> SlingshotCamera;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M6|Visual")
	TObjectPtr<USceneComponent> VisualRoot;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M6|Visual")
	TObjectPtr<UStaticMeshComponent> PouchVisualMesh;
	FABTSSlingshotVisualSlot ActivePouchVisualSlot;
	TArray<TWeakObjectPtr<AABTSM6DestructibleProxy>> DynamicProxies;
	TMap<uint64, float> HISMDamageByStableKey;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UPhysicalMaterial>> RuntimeImpactPhysicalMaterials;
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
	float ReturnElapsedSeconds = 0.0f;
	float BlackFuseRemainingSeconds = -1.0f;
	bool bBlackDetonated = false;
	FABTSM6LaunchCompletedNative LaunchCompletedNative;
	FVector PendingCompletedLandingLocation = FVector::ZeroVector;
	EABTSBirdId PendingCompletedBirdId = EABTSBirdId::Red;
	bool bHasPendingLaunchCompletion = false;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Debug")
	bool bSpawnDebugSlingshotsAtStart = false;
	/** Reuses the M7.1 complete-slingshot classes, including their VisualSlot scale, rotation and pivot rules. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Debug|Slingshot Visual")
	TSubclassOf<AABTSM71PlaceableSlingshotActor> DebugSimpleSlingshotClass;
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Debug|Slingshot Visual")
	TSubclassOf<AABTSM71PlaceableSlingshotActor> DebugReinforcedSlingshotClass;
	/** Mirrors the M7.1 placeable Actor scale; only local Y changes stake spacing by design. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M6|Debug|Slingshot Visual")
	FVector DebugSlingshotActorScale = FVector::OneVector;
	bool bDebugSlingshotsSpawned = false;
	int32 DebugStartCellId = INDEX_NONE;
	FABTSM6PhysicsSettleMonitor PhysicsSettleMonitor;
	float NextSettleDiagnosticTimeSeconds = 0.0f;
	FABTSM6PhysicsSettleMonitor StartupPhysicsSettleMonitor;
	TArray<FABTSM6StartupHISMWarmupQueue> StartupHISMWarmupQueues;
	TMap<TWeakObjectPtr<AABTSM6DestructibleProxy>, TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent>> StartupProxySourceHISMs;
	float StartupPhysicsWarmupEligibleTimeSeconds = 0.0f;
	float NextStartupWarmupDiagnosticTimeSeconds = 0.0f;
	int32 StartupHISMWarmupTotalCandidates = 0;
	int32 StartupHISMWarmupPromotedTotal = 0;
	int32 StartupHISMWarmupBatchIndex = 0;
	int32 StartupHISMWarmupTimedOutBatches = 0;
	bool bStartupBuildingSettlementActive = false;
	bool bStartupPhysicsWarmupStarted = false;
	bool bStartupPhysicsWarmupComplete = false;
	bool bStartupPhysicsWarmupWaitingLogged = false;
};
