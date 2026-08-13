// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/ABTST4LowPolyCloudPrototype.h"
#include "Rendering/ABTSToonEnvironmentTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ABTSStylizedRenderingWorldSubsystem.generated.h"

class UPrimitiveComponent;
class USceneCaptureComponent2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class AActor;
class ACameraActor;
class FABTSStylizedMaterialOverrideRegistry;
class FABTSToonEnvironmentPresentationState;

/** Frozen A2.4 production distribution plus an optional PIE-only override. */
struct ABTSRUNTIME_API FABTST4CloudFieldTuningState
{
	FABTST4CloudClusterDistributionParameters Distribution;
	uint32 Seed = 0;
	bool bOverrideActive = false;
};

/**
 * Integration-owned T2-B1 consumer. Feature systems publish read-only semantic
 * identities; this subsystem alone owns CustomDepth and preview-view wiring.
 */
UCLASS()
class ABTSRUNTIME_API UABTSStylizedRenderingWorldSubsystem final
	: public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UABTSStylizedRenderingWorldSubsystem();
	UABTSStylizedRenderingWorldSubsystem(FVTableHelper& Helper);
	virtual ~UABTSStylizedRenderingWorldSubsystem();
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }

	/** Deterministic immediate refresh used by automation and diagnostics. */
	void RefreshNow();
	/** Re-evaluates A2.3 camera/bird/cloud visibility without rebuilding clouds. */
	void RefreshCloudTraversalNow(bool bForceImmediate = false);
	int32 GetRegisteredPrimitiveCount() const;
	int32 GetRegisteredMaterialSlotCount() const;
	int32 GetPreloadedSharedMaterialCount() const
	{
		return PreloadedSharedMaterials.Num();
	}
	bool AreSharedMaterialsPreloaded() const
	{
		return bSharedMaterialPreloadReady;
	}
	int32 GetRegisteredPreviewCount() const
	{
		return RegisteredCaptures.Num();
	}
	bool IsM7SemanticAdapterReady() const { return false; }
	/** Latest accepted T4 environment snapshot; false means fail closed. */
	bool TryGetEnvironmentSnapshot(
		FABTSToonEnvironmentSnapshot& OutSnapshot) const
	{
		OutSnapshot = EnvironmentSnapshot;
		return bEnvironmentSnapshotReady && OutSnapshot.IsValid();
	}
	/** Applies a deterministic PIE tuning override and immediately rebuilds clouds. */
	bool ApplyCloudFieldTuningOverride(
		int32 ClusterCount,
		float CloudsPerClusterMean,
		float CloudsPerClusterVariance,
		uint32 Seed,
		FString& OutFailure);
	void ClearCloudFieldTuningOverride();
	FABTST4CloudFieldTuningState GetCloudFieldTuningState() const;
	/** Runtime truth used by A2.4 capture manifests; never rebuilds the field. */
	bool IsLowPolyCloudPrototypeActive() const
	{
		return LowPolyCloudPrototypeActor.IsValid()
			&& LowPolyLogicalCloudCount > 0
			&& LowPolyLogicalCloudLayoutHash != 0;
	}
	int32 GetLowPolyLogicalCloudCount() const
	{
		return LowPolyLogicalCloudCount;
	}
	uint64 GetLowPolyLogicalCloudLayoutHash() const
	{
		return LowPolyLogicalCloudLayoutHash;
	}
	int32 GetLowPolyCloudMaterialBatchCount() const
	{
		int32 ValidCount = 0;
		for (const TWeakObjectPtr<UMaterialInstanceDynamic>& Material
			: LowPolyCloudMaterials)
		{
			ValidCount += Material.IsValid() ? 1 : 0;
		}
		return ValidCount;
	}
	/** Moves the local player to a transient whole-planet diagnostic view. */
	bool EnterCloudFieldOverview(FString& OutFailure);
	bool ExitCloudFieldOverview(FString& OutFailure);

protected:
	virtual bool DoesSupportWorldType(
		const EWorldType::Type WorldType) const override;

private:
	void PreloadSharedMaterials();
	void BindWorldLifecycleDelegates(UWorld& World);
	void UnbindWorldLifecycleDelegates();
	void HandleActorSpawned(AActor* Actor);
	void HandleActorDestroyed(AActor* Actor);
	void HandleWorldBeginTearDown(UWorld* World);
	void HandleWorldCleanup(
		UWorld* World,
		bool bSessionEnded,
		bool bCleanupResources);
	void ReleaseEnvironmentOwnership(
		const TCHAR* Reason,
		bool bUnregisterAllCaptures);
	void UnregisterCapturesOwnedBy(const AActor* Owner);
	void RefreshEnvironmentPresentation();
	bool RefreshLowPolyCloudPrototype(
		const struct FABTSStylizedEnvironmentParameters& Parameters,
		FString& OutFailure);
	void DestroyLowPolyCloudPrototype();
	void UpdateCloudTraversalVisibility(float DeltaTime, bool bForceImmediate);

	friend class FABTSToonT2B1PrimitiveRegistryTest;
	class FPrimitiveOverrideRegistry;
	TUniquePtr<FPrimitiveOverrideRegistry> PrimitiveRegistry;
	TUniquePtr<FABTSStylizedMaterialOverrideRegistry> MaterialRegistry;
	TUniquePtr<FABTSToonEnvironmentPresentationState> EnvironmentPresentation;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> PreloadedSharedMaterials;
	TSet<TWeakObjectPtr<USceneCaptureComponent2D>> RegisteredCaptures;
	float RefreshAccumulatorSeconds = 0.0f;
	bool bWorldBeganPlay = false;
	bool bWorldTearingDown = false;
	bool bLastObservedStyleEnabled = false;
	bool bSharedMaterialPreloadReady = false;
	bool bEnvironmentOwnershipActive = false;
	uint32 EnvironmentOwnershipGeneration = 0;
	uint32 EnvironmentRecoveryGeneration = 0;
	uint32 EnvironmentSourceGeneration = 0;
	FDelegateHandle ActorSpawnedHandle;
	FDelegateHandle ActorDestroyedHandle;
	FDelegateHandle WorldBeginTearDownHandle;
	FDelegateHandle WorldCleanupHandle;
	uint64 LastDiagnosticSummaryHash = 0;
	FABTSToonEnvironmentSnapshot EnvironmentSnapshot;
	bool bEnvironmentSnapshotReady = false;
	uint64 LastEnvironmentDiagnosticHash = 0;
	TWeakObjectPtr<AActor> ActiveFinaleEnvironmentSource;
	uint64 ActiveFinaleEnvironmentSourceHash = 0;
	TWeakObjectPtr<AActor> LowPolyCloudPrototypeActor;
	uint64 LowPolyCloudLayoutHash = 0;
	uint64 LowPolyLogicalCloudLayoutHash = 0;
	int32 LowPolyLogicalCloudCount = 0;
	TArray<FABTST4LowPolyCloudIslandDefinition> LowPolyCloudDefinitions;
	TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> LowPolyCloudMaterials;
	TArray<float> LowPolyCloudTraversalStrengths;
	uint64 LastCloudTraversalDiagnosticHash = 0;
	bool bHasPreviousCloudTraversalCamera = false;
	FABTST4CloudFieldTuningState CloudFieldTuningState;
	TWeakObjectPtr<ACameraActor> CloudFieldOverviewCamera;
	TWeakObjectPtr<AActor> CloudFieldOverviewPreviousViewTarget;
};
