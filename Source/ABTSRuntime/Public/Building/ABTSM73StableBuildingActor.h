// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BuildingTypes.h"
#include "GameFramework/Actor.h"
#include "ABTSM73StableBuildingActor.generated.h"

class AABTSM3Planet;
class AABTSM7BuildingMaterialSystem;
class AABTSM7BuildingModule;
class UArrowComponent;
class UHierarchicalInstancedStaticMeshComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Editor-placeable M7.3 stable building with deterministic weak-point planning. */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "M7.3 Stable Building Generator"))
class ABTSRUNTIME_API AABTSM73StableBuildingActor : public AActor
{
	GENERATED_BODY()

public:
	AABTSM73StableBuildingActor();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** M7.1 GameMode calls this after creating its shared M7 material system. */
	void InitializeRuntimeBuilding(AABTSM7BuildingMaterialSystem* MaterialSystem);

	/** Used by a future TaskGraph spawner; the anchor remains a CellTopo id. */
	void ConfigureSphericalAnchor(AABTSM3Planet* Planet, int32 CellId, const FTransform& DesiredFacing);

	/** Applies a TaskGraph-owned profile before FinishSpawning constructs the runtime building. */
	void ConfigureTaskGraphGeneration(
		const FABTSM73GenerationSettings& InGenerationSettings,
		const FABTSM73DAGGenerationSettings& InDAGGenerationSettings,
		const FABTSM73DAGLayoutSettings& InDAGLayoutSettings,
		const FABTSM73DifficultySettings& InDifficultySettings);

	UFUNCTION(BlueprintCallable, Category = "ABTS|M7.3-A")
	bool RebuildPreview();

	UFUNCTION(BlueprintPure, Category = "ABTS|M7.3-A")
	const FABTSM73GenerationSummary& GetGenerationSummary() const { return GenerationSummary; }

	/** Live module centroid for one building-level M10 marker; false hides rejected or fully destroyed buildings. */
	bool QueryScoutMapMarkerLocation(
		const AABTSM3Planet* ExpectedPlanet,
		FVector& OutWorldLocation,
		int32& OutLiveModuleCount) const;

private:
	bool BuildResolvedStructure(bool bAllowFlatEditorFallback, struct FABTSM73GroundContext& OutContext,
		struct FABTSM73StructureData& OutData, FString& OutError,
		const AABTSM7BuildingMaterialSystem* MaterialProfileSource = nullptr);
	void FillGenerationSummary(const FABTSM73GroundContext& Context, const FABTSM73StructureData& Data,
		bool bAccepted, const FString& Error);
	void UpdatePreviewComponents(const FABTSM73GroundContext& Context, const FABTSM73StructureData& Data);
	void UpdateFoundationComponents(const FABTSM73GroundContext& Context, const FABTSM73StructureData& Data);
	void ClearBrickPreviews();
	void TryFindRuntimeMaterialSystem();
	void BeginIdleValidation(const FABTSM73GroundContext& Context);
	void FinishIdleValidation(bool bTimedOut);
	void RejectRuntimeStructure(const FString& Reason);
	UHierarchicalInstancedStaticMeshComponent* GetPreviewForMaterial(EABTSM7BuildingMaterial Material) const;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.3-A|Components")
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.3-A|Components")
	TObjectPtr<UArrowComponent> AttackDirection;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.3-A|Preview")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> WoodPreview;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.3-A|Preview")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> StonePreview;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.3-A|Preview")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> IronPreview;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.3-A|Preview")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> GlassPreview;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.3-B|Preview")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> WeakPointPreview;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.3-A|Foundation")
	TObjectPtr<UStaticMeshComponent> FoundationCap;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.3-A|Foundation")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FoundationFeet;

	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-A|Generation")
	EABTSM73GroundMode GroundMode = EABTSM73GroundMode::Auto;
	/** Off keeps the complete generated building at the authored XYZ transform in M7.1. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-A|Placement", meta = (EditCondition = "GroundMode != EABTSM73GroundMode::SphericalCellTopo"))
	bool bSnapPlanarAnchorToTestStage = false;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-A|Generation", meta = (EditCondition = "GroundMode != EABTSM73GroundMode::PlanarTestStage"))
	int32 AnchorCellId = INDEX_NONE;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-A|Generation")
	FABTSM73GenerationSettings GenerationSettings;
	/** DAG-1 topology settings. Used only when GenerationAlgorithm is RecursiveSupportDAG. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-DAG-1|Generation")
	FABTSM73DAGGenerationSettings DAGGenerationSettings;
	/** DAG-2 Scope split, sparse support and plate/column lowering settings. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-DAG-2|Layout")
	FABTSM73DAGLayoutSettings DAGLayoutSettings;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-B|Difficulty")
	FABTSM73DifficultySettings DifficultySettings;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-A|Validation")
	bool bRunIdleChaosValidation = true;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-A|Validation", meta = (ClampMin = "0.0", UIMax = "3000.0"))
	float ValidationGravityCMPerSec2 = 980.0f;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-A|Preview")
	bool bShowEditorPreview = true;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-A|Assets")
	TObjectPtr<UStaticMesh> BrickMesh;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-A|Assets")
	TObjectPtr<UMaterialInterface> FoundationMaterial;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-B|Debug")
	TObjectPtr<UMaterialInterface> WeakPointDebugMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WeakPointDebugMID;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.3-A|Result")
	FABTSM73GenerationSummary GenerationSummary;

	TWeakObjectPtr<AABTSM7BuildingMaterialSystem> RuntimeMaterialSystem;
	TWeakObjectPtr<AABTSM3Planet> ConfiguredPlanet;
	TArray<TWeakObjectPtr<AABTSM7BuildingModule>> RuntimeModules;
	TMap<int32, TWeakObjectPtr<AABTSM7BuildingModule>> RuntimeModulesByNodeId;
	TMap<TWeakObjectPtr<AABTSM7BuildingModule>, FTransform> IdleInitialTransforms;
	FTimerHandle MaterialSystemSearchTimer;
	int32 MaterialSystemSearchAttempts = 0;
	float IdleValidationElapsed = 0.0f;
	float IdleStableElapsed = 0.0f;
	bool bRuntimeSpawned = false;
	bool bIdleValidationRunning = false;
	bool bRuntimePlanar = true;
	FVector RuntimeGravityReference = FVector::UpVector;
};
