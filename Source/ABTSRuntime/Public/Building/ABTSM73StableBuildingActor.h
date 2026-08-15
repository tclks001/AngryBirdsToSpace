// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/ABTSM73BuildingTypes.h"
#include "Building/ABTSM73BuildingFreezeV3RuntimeRegistration.h"
#include "Building/ABTSM73DAG5Types.h"
#include "Building/ABTSM73JuryDemoFixedSixRegistration.h"
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

	/** Consumes one already verified J4 payload before deferred spawning finishes. */
	bool ConfigureJuryDemoFixedSixStaticRegistration(
		FABTSM73JuryDemoFixedSixStaticEntry&& InEntry,
		FString& OutError);
	/** Removes every staged instance/module before destroying an atomic batch. */
	void RollbackJuryDemoFixedSixStaticRegistration(const FString& Reason);
	bool IsJuryDemoFixedSixStaticRegistrationAccepted() const;
	int32 GetJuryDemoFixedSixStaticModuleCount() const;
	FName GetJuryDemoFixedSixManifestEntryId() const;
	int32 GetJuryDemoFixedSixEncounterIndex() const;
	uint64 GetJuryDemoFixedSixRegistrationResultHash() const;

	/** Consumes one M7-owned, placement-bound V3 fixture payload. */
	bool ConfigureBuildingFreezeV3RuntimeRegistration(
		FABTSM73BuildingFreezeV3RuntimeEntry&& InEntry,
		FString& OutError);
	void RollbackBuildingFreezeV3RuntimeRegistration(const FString& Reason);
	bool IsBuildingFreezeV3RuntimeRegistrationAccepted() const;
	int32 GetBuildingFreezeV3RuntimeModuleCount() const;
	EABTSM73BeamDemoBuilding GetBuildingFreezeV3ComplexityId() const;
	int32 GetBuildingFreezeV3ComplexityIndex() const;
	int32 GetBuildingFreezeV3EncounterSlot() const;
	uint64 GetBuildingFreezeV3RegistrationResultHash() const;
	int32 GetBuildingFreezeV3BodyInstanceCount(
		EABTSM7BuildingMaterial Material) const;

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

	const FABTSM73DAG5AResult& GetDAG5AResultForValidation() const
	{
		return LastDAG5AResult;
	}

	const FABTSM73DAG5BResult& GetDAG5BResultForValidation() const
	{
		return LastDAG5BResult;
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

	EABTSM73IdleValidationState GetIdleValidationState() const
	{
		return bParticipateInSlingshotValidationGate
			? IdleValidationState
			: EABTSM73IdleValidationState::NotRequired;
	}
	EABTSM73IdleValidationState GetRawIdleValidationStateForValidation() const
	{
		return IdleValidationState;
	}
	bool ShouldParticipateInPIERuntime() const { return bParticipateInPIERuntime; }
	bool ShouldParticipateInSlingshotValidationGate() const
	{
		return bParticipateInSlingshotValidationGate;
	}
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
	/** Generic read-only runtime centroid for camera/UI consumers; false for rejected or destroyed buildings. */
	bool QueryLivePresentationAnchor(
		FVector& OutWorldLocation,
		int32& OutLiveModuleCount) const;
	/** Event-time ownership query; callers must not cache module pointers or infer ownership from Actor Owner. */
	bool OwnsRuntimePrimitive(const UPrimitiveComponent* Component) const;

private:
	void InitializeJuryDemoFixedSixStaticRegistration(
		AABTSM7BuildingMaterialSystem& MaterialSystem);
	void InitializeBuildingFreezeV3RuntimeRegistration(
		AABTSM7BuildingMaterialSystem& MaterialSystem);
	void ConfigureJuryDemoFixedSixStaticHISM(
		UHierarchicalInstancedStaticMeshComponent& Component);
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

	/** Off keeps this editor fixture preview-only: PIE spawns no real Modules and runs no Chaos validation. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-A|PIE",
		meta = (DisplayName = "Participate in PIE Runtime"))
	bool bParticipateInPIERuntime = true;

	/** Off still permits PIE physics, but this Actor reports NotRequired to the slingshot startup gate. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-A|PIE",
		meta = (DisplayName = "Participate in Slingshot Validation Gate"))
	bool bParticipateInSlingshotValidationGate = true;

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
	/** DAG5-A bounded feasibility search. Default off preserves the one-shot DAG2.3 path. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-DAG-5A|Search")
	FABTSM73DAG5ASettings DAG5ASettings;
	/** DAG5-B Shape Grammar + local semantic WFC. Explicit opt-in. */
	UPROPERTY(EditAnywhere, Category = "ABTS|M7.3-DAG-5B|Envelope")
	FABTSM73DAG5BSettings DAG5BSettings;
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
	UPROPERTY(VisibleAnywhere, Transient, Category = "ABTS|M7.3-DAG-5A|Result")
	FABTSM73DAG5AResult LastDAG5AResult;
	FABTSM73DAG5BResult LastDAG5BResult;

	TWeakObjectPtr<AABTSM7BuildingMaterialSystem> RuntimeMaterialSystem;
	TOptional<FABTSM73JuryDemoFixedSixStaticEntry>
		JuryDemoFixedSixStaticEntry;
	TOptional<FABTSM73BuildingFreezeV3RuntimeEntry>
		BuildingFreezeV3RuntimeEntry;
	int32 JuryDemoFixedSixStaticBrickInstanceCount = 0;
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
