// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/ABTSToonEnvironmentTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ABTSStylizedRenderingWorldSubsystem.generated.h"

class UPrimitiveComponent;
class USceneCaptureComponent2D;
class UMaterialInterface;
class AActor;
class FABTSStylizedMaterialOverrideRegistry;
class FABTSToonEnvironmentPresentationState;

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

protected:
	virtual bool DoesSupportWorldType(
		const EWorldType::Type WorldType) const override;

private:
	void PreloadSharedMaterials();
	void RefreshEnvironmentPresentation();
	bool RefreshLowPolyCloudPrototype(
		const struct FABTSStylizedEnvironmentParameters& Parameters,
		FString& OutFailure);
	void DestroyLowPolyCloudPrototype();

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
	bool bLastObservedStyleEnabled = false;
	bool bSharedMaterialPreloadReady = false;
	uint64 LastDiagnosticSummaryHash = 0;
	FABTSToonEnvironmentSnapshot EnvironmentSnapshot;
	bool bEnvironmentSnapshotReady = false;
	uint64 LastEnvironmentDiagnosticHash = 0;
	TWeakObjectPtr<AActor> LowPolyCloudPrototypeActor;
	uint64 LowPolyCloudLayoutHash = 0;
	uint64 LowPolyLogicalCloudLayoutHash = 0;
	int32 LowPolyLogicalCloudCount = 0;
};
