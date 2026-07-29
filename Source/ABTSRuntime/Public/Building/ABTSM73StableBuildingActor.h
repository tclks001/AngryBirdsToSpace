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
class UTextRenderComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
struct FABTSM73GroundContext;
struct FABTSM73StructureData;
struct FABTSM73DAG4RuntimeState;
struct FABTSM73DAG4RuntimeStateDeleter
{
	void operator()(FABTSM73DAG4RuntimeState* State) const;
};

/** Explicit ownership state used by M6 startup warmup when waiting for M7.3. */
enum class EABTSM73IdleValidationState : uint8
{
	Pending,
	Running,
	Accepted,
	Rejected,
	NotRequired
};

/** Editor-placeable M7.3 building; TaskGraph production resolves through DAG2.3. */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "M7.3 Stable Building Generator"))
class ABTSRUNTIME_API AABTSM73StableBuildingActor : public AActor
{
	GENERATED_BODY()

public:
	AABTSM73StableBuildingActor();
	virtual ~AABTSM73StableBuildingActor() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** M7.1 GameMode calls this after creating its shared M7 material system. */
	void InitializeRuntimeBuilding(AABTSM7BuildingMaterialSystem* MaterialSystem);

	/** TaskGraph spawner keeps the authoritative terrain anchor as a CellTopo id. */
	void ConfigureSphericalAnchor(AABTSM3Planet* Planet, int32 CellId, const FTransform& DesiredFacing);

	/** Applies a TaskGraph-owned profile before FinishSpawning constructs the runtime building. */
	void ConfigureTaskGraphGeneration(
		const FABTSM73GenerationSettings& InGenerationSettings,
		const FABTSM73DAGGenerationSettings& InDAGGenerationSettings,
		const FABTSM73DAGLayoutSettings& InDAGLayoutSettings,
		const FABTSM73DAGFailureFrontierSettings& InDAGFailureFrontierSettings,
		const FABTSM73DAGFailurePatternSettings& InDAGFailurePatternSettings,
		const FABTSM73DifficultySettings& InDifficultySettings);

	void ConfigureTaskGraphGeneration(
		const FABTSM73GenerationSettings& InGenerationSettings,
		const FABTSM73DAGGenerationSettings& InDAGGenerationSettings,
		const FABTSM73DAGLayoutSettings& InDAGLayoutSettings,
		const FABTSM73DAGFailureFrontierSettings& InDAGFailureFrontierSettings,
		const FABTSM73DAGFailurePatternSettings& InDAGFailurePatternSettings,
		const FABTSM73DAGFailurePlayabilitySettings& InDAGFailurePlayabilitySettings,
		const FABTSM73DifficultySettings& InDifficultySettings);

	void ConfigureTaskGraphGeneration(
		const FABTSM73GenerationSettings& InGenerationSettings,
		const FABTSM73DAGGenerationSettings& InDAGGenerationSettings,
		const FABTSM73DAGLayoutSettings& InDAGLayoutSettings,
		const FABTSM73DAGFailureFrontierSettings& InDAGFailureFrontierSettings,
		const FABTSM73DAGFailurePatternSettings& InDAGFailurePatternSettings,
		const FABTSM73DAGFailurePlayabilitySettings& InDAGFailurePlayabilitySettings,
		const FABTSM73DAG4ValidationSettings& InDAG4ValidationSettings,
		const FABTSM73DifficultySettings& InDifficultySettings);

	UFUNCTION(BlueprintCallable, Category = "ABTS|M7.3-A")
	bool RebuildPreview();

	UFUNCTION(BlueprintPure, Category = "ABTS|M7.3-A")
	const FABTSM73GenerationSummary& GetGenerationSummary() const { return GenerationSummary; }

	/** Read-only DAG3-B result retained from the latest editor/runtime build attempt. */
	const FABTSM73DAGFailurePatternResult& GetDAGFailurePatternResultForValidation() const
	{
		return LastDAGFailurePatternResult;
	}

	const FABTSM73DAGFailurePlayabilityResult&
		GetDAGFailurePlayabilityResultForValidation() const
	{
		return LastDAGFailurePlayabilityResult;
	}

	const FABTSM73DAG4ValidationResult& GetDAG4ValidationResultForValidation() const
	{
		return LastDAG4ValidationResult;
	}

	AABTSM7BuildingModule* FindRuntimeModuleForNodeForValidation(
		int32 NodeId) const
	{
		const TWeakObjectPtr<AABTSM7BuildingModule>* Found =
			RuntimeModulesByNodeId.Find(NodeId);
		return Found != nullptr ? Found->Get() : nullptr;
	}

	int32 GetDAG3BWeakDebugInstanceCount() const;
	int32 GetDAG3BPivotDebugInstanceCount() const;
	int32 GetDAG3BAffectedDebugInstanceCount() const;
	int32 GetDAG3BDirectionDebugInstanceCount() const;

	EABTSM73IdleValidationState GetIdleValidationState() const { return IdleValidationState; }
	bool IsIdleValidationTerminal() const
	{
		return IdleValidationState == EABTSM73IdleValidationState::Accepted
			|| IdleValidationState == EABTSM73IdleValidationState::Rejected
			|| IdleValidationState == EABTSM73IdleValidationState::NotRequired;
	}

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
	void UpdateDAGFailurePatternDiagnostics(
		const FABTSM73GroundContext& Context,
		const FABTSM73StructureData& Data);
	void UpdateFoundationComponents(const FABTSM73GroundContext& Context, const FABTSM73StructureData& Data);
	void ClearBrickPreviews();
	void ClearDAGFailurePatternDiagnostics();
	void TryFindRuntimeMaterialSystem();
	void BeginIdleValidation(const FABTSM73GroundContext& Context);
	void FinishIdleValidation(bool bTimedOut);
	void PrepareDAG4RuntimeState(
		const FABTSM73GroundContext& Context,
		const FABTSM73StructureData& Data,
		TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles);
	bool BeginDAG4ValidationAfterIdle();
	void TickDAG4Validation(float DeltaSeconds);
	bool SpawnDAG4ShadowTrial(FString& OutError);
	void CleanupDAG4ShadowTrial();
	void CancelDAG4Validation();
	void CompleteAcceptedIdleValidation();
	UFUNCTION()
	void HandleDAG4ModuleHit(
		UPrimitiveComponent* HitComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		FVector NormalImpulse,
		const FHitResult& Hit);
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
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.3-DAG3B|Diagnostics")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> DAGFailureWeakPreview;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.3-DAG3B|Diagnostics")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> DAGFailurePivotPreview;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.3-DAG3B|Diagnostics")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> DAGFailureAffectedPreview;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.3-DAG3B|Diagnostics")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> DAGFailureDirectionPreview;
	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.3-DAG3B|Diagnostics")
	TObjectPtr<UTextRenderComponent> DAGFailurePatternLabel;
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
	/** DAG3-A pure-data frontier discovery. Production remains disabled until DAG3-C/DAG-4 gates pass. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-DAG-3|Failure Frontier")
	FABTSM73DAGFailureFrontierSettings DAGFailureFrontierSettings;
	/** DAG3-B pure-geometry rewrite. Production remains disabled until DAG3-C/DAG-4 gates pass. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-DAG-3|Failure Pattern")
	FABTSM73DAGFailurePatternSettings DAGFailurePatternSettings;
	/** DAG3-C attack/motion/material certification. Explicit opt-in only. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-DAG-3|Playability")
	FABTSM73DAGFailurePlayabilitySettings DAGFailurePlayabilitySettings;
	/** DAG-4 settled contact and reversible weak/ordinary Chaos comparison. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-DAG-4|Validation")
	FABTSM73DAG4ValidationSettings DAG4ValidationSettings;
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
	/** Non-physical overlays for inspecting an explicitly applied DAG3-B candidate. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-DAG3B|Diagnostics")
	bool bShowDAGFailurePatternDiagnostics = true;
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-DAG3B|Diagnostics")
	TObjectPtr<UMaterialInterface> DAGFailureDebugMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DAGFailureWeakDebugMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DAGFailurePivotDebugMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DAGFailureAffectedDebugMID;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DAGFailureDirectionDebugMID;

	UPROPERTY(VisibleAnywhere, Category = "ABTS|M7.3-A|Result")
	FABTSM73GenerationSummary GenerationSummary;
	FABTSM73DAGFailurePatternResult LastDAGFailurePatternResult;
	FABTSM73DAGFailurePlayabilityResult LastDAGFailurePlayabilityResult;
	FABTSM73DAG4ValidationResult LastDAG4ValidationResult;

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
	bool bDAG4ValidationRunning = false;
	EABTSM73IdleValidationState IdleValidationState = EABTSM73IdleValidationState::Pending;
	bool bRuntimePlanar = true;
	FVector RuntimeGravityReference = FVector::UpVector;
	TUniquePtr<
		FABTSM73DAG4RuntimeState,
		FABTSM73DAG4RuntimeStateDeleter> DAG4RuntimeState;
};
