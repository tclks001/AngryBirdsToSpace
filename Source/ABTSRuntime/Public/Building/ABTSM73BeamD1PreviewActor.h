// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BeamD1Types.h"
#include "GameFramework/Actor.h"
#include "ABTSM73BeamD1PreviewActor.generated.h"

class AABTSM7BuildingMaterialSystem;
class AABTSM7BuildingModule;
class FABTSM73BeamD1DelayedMaterialSystemTest;
class UHierarchicalInstancedStaticMeshComponent;
class USceneComponent;

/** Independent Beam-D1 editor preview and opt-in real Brick runtime harness. */
UCLASS(BlueprintType, Blueprintable,
	meta = (DisplayName = "M7.3 Beam-D1 Real Brick Preview"))
class ABTSRUNTIME_API AABTSM73BeamD1PreviewActor : public AActor
{
	GENERATED_BODY()

public:
	AABTSM73BeamD1PreviewActor();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(CallInEditor, Category = "ABTS|M7.3-Beam-D1")
	void RegeneratePreview();

	/** Explicit D1 test entry point; does not change the production DAG2.3 route. */
	bool InitializeRuntimeBuilding(AABTSM7BuildingMaterialSystem* MaterialSystem);

	const FABTSM73BeamD1Summary& GetSummaryForValidation() const
	{
		return LastSummary;
	}
	const TArray<FABTSM73BeamD1BrickBinding>& GetCompiledBricksForValidation() const
	{
		return CompiledBricks;
	}
	int32 GetRuntimeModuleCountForValidation() const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-Beam-D1")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-Beam-D1|Preview")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> WoodPreview;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-Beam-D1|Preview")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> StonePreview;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-Beam-D1|Preview")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> IronPreview;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-Beam-D1|Preview")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> GlassPreview;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-D1")
	FABTSM73BeamD1Settings Settings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-D1|Preview")
	bool bShowEditorPreview = true;

	/** Opt-in only: real Modules can affect PIE physics and startup gates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite,
		Category = "ABTS|M7.3-Beam-D1|Runtime")
	bool bSpawnRuntimeModulesInPIE = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly,
		Category = "ABTS|M7.3-Beam-D1|Last Result")
	FABTSM73BeamD1Summary LastSummary;

private:
	friend class FABTSM73BeamD1DelayedMaterialSystemTest;

	UHierarchicalInstancedStaticMeshComponent* GetPreview(
		EABTSM7BuildingMaterial Material) const;
	void ClearPreview();
	void TryInitializeRuntimeBuilding();

	TArray<FABTSM73BeamD1BrickBinding> CompiledBricks;
	TArray<TWeakObjectPtr<AABTSM7BuildingModule>> RuntimeModules;
	int32 RuntimeSystemSearchAttempts = 0;
	FTimerHandle RuntimeSystemSearchTimer;
};
