// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ABTSStylizedRenderingWorldSubsystem.generated.h"

class UPrimitiveComponent;
class USceneCaptureComponent2D;
class FABTSStylizedMaterialOverrideRegistry;

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
	int32 GetRegisteredPreviewCount() const
	{
		return RegisteredCaptures.Num();
	}
	bool IsM7SemanticAdapterReady() const { return false; }

protected:
	virtual bool DoesSupportWorldType(
		const EWorldType::Type WorldType) const override;

private:
	friend class FABTSToonT2B1PrimitiveRegistryTest;
	class FPrimitiveOverrideRegistry;
	TUniquePtr<FPrimitiveOverrideRegistry> PrimitiveRegistry;
	TUniquePtr<FABTSStylizedMaterialOverrideRegistry> MaterialRegistry;
	TSet<TWeakObjectPtr<USceneCaptureComponent2D>> RegisteredCaptures;
	float RefreshAccumulatorSeconds = 0.0f;
	bool bWorldBeganPlay = false;
	bool bLastObservedStyleEnabled = false;
	uint64 LastDiagnosticSummaryHash = 0;
};
