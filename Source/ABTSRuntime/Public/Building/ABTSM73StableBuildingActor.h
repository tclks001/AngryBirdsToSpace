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
class AABTSM73StableBuildingActor;
class AABTSM7BuildingMaterialSystem;
struct FABTSM7SiteUniformGravityPolicy;
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

/** Causal origin retained for E1 production damage evidence. */
enum class EABTSM73E1DamageCause : uint8
{
	BirdImpact,
	ModuleContact,
	GameplayBlast,
	UnknownOrScripted
};

/**
 * Pure fail-closed evidence state for the product path:
 * real E1 module hit -> physical structural response -> Crystal contact break.
 */
struct ABTSRUNTIME_API FABTSM73E1DamageLifecycleState final
{
	bool bChaosActivated = false;
	bool bRealModuleImpactObserved = false;
	bool bStructuralResponseObserved = false;
	bool bCrystalDestroyedByPhysicalChain = false;
	int32 PhysicalContactDamageEventCount = 0;

	void Reset();
	void RecordChaosActivated();
	void RecordModuleDamage(
		bool bCertifiedTargetBrick,
		bool bCrystal,
		EABTSM73E1DamageCause Cause,
		bool bModuleBroken);
	bool IsAccepted() const;
};

/** One pre-promotion HISM instance in the exact public E1 Brick order. */
struct ABTSRUNTIME_API FABTSM73E1OrderedBrickInstanceBinding final
{
	int32 BrickId = INDEX_NONE;
	EABTSM7BuildingMaterial Material = EABTSM7BuildingMaterial::Wood;
	int32 MaterialInstanceIndex = INDEX_NONE;
	FTransform FrozenWorldTransform = FTransform::Identity;
	FVector HalfExtentCM = FVector::ZeroVector;
	TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> SourceHISM;
	TWeakObjectPtr<AABTSM73StableBuildingActor> OwningBuilding;

	bool IsUsable(bool bRequireLiveOwnership = true) const;
};

/**
 * Exact 54-Brick pre-promotion target union. Descriptor order is global while
 * MaterialInstanceIndex is the stable per-material HISM insertion order.
 */
struct ABTSRUNTIME_API FABTSM73E1OrderedBrickUnionBinding final
{
	static constexpr int32 FrozenBrickCount = 54;

	FName ManifestEntryId = NAME_None;
	uint64 DescriptorHash = 0;
	uint64 StaticGeometryHash = 0;
	TArray<FABTSM73E1OrderedBrickInstanceBinding> OrderedBricks;

	bool IsUsable(bool bRequireLiveOwnership = true) const;
	uint32 ComputeOrderedGeometryHash() const;
};

/** One exact descriptor OBB associated with its real promoted E1 module. */
struct ABTSRUNTIME_API FABTSM73E1DestructibleModuleTarget final
{
	int32 BrickId = INDEX_NONE;
	FTransform FrozenWorldTransform = FTransform::Identity;
	FVector HalfExtentCM = FVector::ZeroVector;
	TWeakObjectPtr<AABTSM7BuildingModule> Module;
	TWeakObjectPtr<AABTSM73StableBuildingActor> OwningBuilding;
	TWeakObjectPtr<AABTSM7BuildingMaterialSystem> OwningMaterialSystem;

	bool IsUsable(bool bRequireLiveOwnership = true) const;
};

/** Ordered union input; order is exactly the public E1 descriptor Brick order. */
struct ABTSRUNTIME_API FABTSM73E1DestructibleModuleTargetSet final
{
	FName ManifestEntryId = NAME_None;
	uint64 DescriptorHash = 0;
	uint64 StaticGeometryHash = 0;
	TArray<FABTSM73E1DestructibleModuleTarget> OrderedBrickTargets;

	bool IsUsable(
		int32 ExpectedBrickCount,
		bool bRequireLiveOwnership = true) const;
	uint32 ComputeOrderedGeometryHash() const;
};

/** Read-only production evidence emitted after one Fixed-Six site finishes Chaos. */
struct ABTSRUNTIME_API FABTSM73JuryDemoFixedSixChaosResult
{
	FName ManifestEntryId = NAME_None;
	EABTSM73BeamDemoBuilding ComplexityId =
		EABTSM73BeamDemoBuilding::Custom;
	int32 DeterministicSeed = 0;
	int32 VisibleModuleCount = 0;
	int32 PhysicsBodyCount = 0;
	uint64 PhysicsAssemblyHash = 0;
	uint32 CandidateHash = 0;
	uint32 ResultHash = 0;
	bool bReachedQuiet = false;
	bool bEndedQuiet = false;
	float FirstQuietSeconds = 0.0f;
	float FinalPlanarDriftCM = 0.0f;
	float FinalSettlementCM = 0.0f;
	float FinalRotationDegrees = 0.0f;
	float FinalLinearSpeedCMPerSec = 0.0f;
	float FinalAngularSpeedDegreesPerSec = 0.0f;
	float PeakPlanarDriftCM = 0.0f;
	float PeakSettlementCM = 0.0f;
	float PeakRotationDegrees = 0.0f;
	int32 FinalAwakeBodyCount = 0;
	float InternalSeconds = 0.0f;
	double WallSeconds = 0.0;
	bool bAccepted = false;
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
	/** M6 startup authority: exact static identity is accepted while Chaos is deferred. */
	bool IsJuryDemoFixedSixStaticReadyDeferredForValidation() const
	{
		return IsJuryDemoFixedSixStaticRegistrationAccepted()
			&& IdleValidationState == EABTSM73IdleValidationState::Accepted
			&& !bJuryDemoFixedSixChaosRunning;
	}
	/** True only after the deferred first-hit representation has been armed. */
	bool IsJuryDemoFixedSixChaosDeferredUntilFirstHitForValidation() const
	{
		return IsJuryDemoFixedSixStaticReadyDeferredForValidation()
			&& bJuryDemoFixedSixChaosPrepared
			&& bJuryDemoFixedSixChaosDeferredUntilFirstHit
			&& !bJuryDemoFixedSixChaosDeferredActivated;
	}
	int32 GetJuryDemoFixedSixStaticModuleCount() const;
	FName GetJuryDemoFixedSixManifestEntryId() const;
	int32 GetJuryDemoFixedSixEncounterIndex() const;
	uint64 GetJuryDemoFixedSixRegistrationResultHash() const;
	EABTSM73BeamDemoBuilding GetJuryDemoFixedSixComplexityId() const;
	/** Phase one: promote exact frozen geometry, audit mass/contact, and stage bodies. */
	bool PrepareJuryDemoFixedSixChaosValidation(
		float GravityAccelerationCMPerSec2,
		FString& OutError);
	/** True only while this frozen building owns its exact blocking tangent pad. */
	bool IsJuryDemoFixedSixFrozenTangentSupportBlockingBuildingChannel() const;
	/** Exact grounded-freeze authority: only the frozen tangent support is a building support. */
	bool IsJuryDemoFixedSixGroundSupportPrimitive(const UPrimitiveComponent* Primitive) const;
	/** Phase two: start every prepared site from one GameMode batch boundary. */
	bool ActivatePreparedJuryDemoFixedSixChaosValidation(
		FString& OutError,
		const FVector* GameplayImpactWorld = nullptr,
		const TArray<AABTSM7BuildingModule*>* GameplayPhysicsModules = nullptr);
	/** Release fallback: retain static preflight and defer physical promotion until a real module hit. */
	bool MarkPreparedJuryDemoFixedSixChaosDeferred(FString& OutError);
	/** Called by the owned material system before it applies the first real module damage. */
	bool ActivateDeferredJuryDemoFixedSixChaosForFirstHit(
		const AABTSM7BuildingModule& TriggerModule, FString& OutError,
		float ImpactRadiusCM = 0.0f);
	/** Builds and atomically promotes an exact independent-brick support island. */
	bool ActivateJuryDemoFixedSixImpactSupportClosure(
		const AABTSM7BuildingModule& TriggerModule,
		float ImpactRadiusCM,
		FString& OutError);
	/**
	 * Coalesces a real same-building hit into the next game-thread damage epoch.
	 * The epoch publishes every unsupported brick once; callers must never retry
	 * a per-brick closure synchronously from a contact callback.
	 */
	bool QueueJuryDemoFixedSixDamageSeed(
		AABTSM7BuildingModule& TriggerModule,
		float ImpactRadiusCM,
		FString& OutError);
	/**
	 * Queues one radial blast as a single building-scoped epoch.  The caller
	 * supplies world-space blast authority; this actor resolves stable BrickIds
	 * once, before any individual brick is broken or promoted.
	 */
	bool QueueJuryDemoFixedSixRadialDamage(
		const FVector& OriginWorldCM,
		float ImpactRadiusCM,
		FString& OutError);
	/**
	 * Records a confirmed topology mutation before the source module is
	 * destroyed.  Unlike an ordinary dynamic contact this must re-derive the
	 * remaining frozen support graph, but it still coalesces into one epoch.
	 */
	bool QueueJuryDemoFixedSixTopologyMutation(
		AABTSM7BuildingModule& MutatedModule,
		FString& OutError);
	/**
	 * Material-system blast authority must be checked against this actor's
	 * immutable Fixed-Six runtime ledger, rather than inferring ownership from
	 * a transient module Actor owner.
	 */
	bool IsJuryDemoFixedSixRegisteredRuntimeModule(
		const AABTSM7BuildingModule& Module,
		const AABTSM7BuildingMaterialSystem& ExpectedMaterialSystem) const;
	/** Read-only damage-epoch observability for automation and aggregation logs. */
	int32 GetJuryDemoFixedSixQueuedDamageSeedCountForValidation() const
	{
		return JuryDemoFixedSixQueuedDamageSeedBrickIds.Num();
	}
	uint64 GetJuryDemoFixedSixDamageEpochForValidation() const
	{
		return JuryDemoFixedSixDamageEpoch;
	}
	/** Promotes one visible overflow brick for an explicit bird/device impact. */
	bool PromoteJuryDemoFixedSixOverflowForDirectImpact(
		AABTSM7BuildingModule& Module, FString& OutError);
	/** Walk-return fallback: retire an unresolved exact body into its own visible analytic state, never an airborne static. */
	bool AdoptJuryDemoFixedSixDynamicAsOverflow(
		AABTSM7BuildingModule& Module, FString& OutError);
	void RejectJuryDemoFixedSixChaosValidation(const FString& Reason);
	bool CopyJuryDemoFixedSixChaosResult(
		FABTSM73JuryDemoFixedSixChaosResult& OutResult) const;
	/** Copies the exact per-site gravity policy retained from the frozen V3 DTO. */
	bool CopyJuryDemoSiteUniformGravityPolicy(
		float GravityAccelerationCMPerSec2,
		FABTSM7SiteUniformGravityPolicy& OutPolicy) const;
	/** Legacy compatibility query; production target binding must not consume it. */
	bool CopyJuryDemoE1CrystalTarget(
		AActor*& OutTargetActor,
		FVector& OutHalfExtentCM) const;
	/**
	 * Audits the pre-promotion per-material HISMs against all 54 descriptor OBBs
	 * and returns the unique Crystal cap only as M3's site-recovery anchor. The
	 * cap is not a trajectory first-hit target.
	 */
	bool CopyJuryDemoE1OrderedBrickUnionBinding(
		FABTSM73E1OrderedBrickUnionBinding& OutBinding,
		FTransform& OutSiteRecoveryAnchorTransform,
		FVector& OutSiteRecoveryAnchorHalfExtentCM) const;
	/**
	 * Copies every public-descriptor E1 Brick OBB in descriptor order and binds
	 * each row to the corresponding real promoted damage module. Caps/devices
	 * are deliberately excluded from the trajectory target union.
	 */
	bool CopyJuryDemoE1DestructibleModuleTargetSet(
		FABTSM73E1DestructibleModuleTargetSet& OutTargetSet) const;
	/** Records only damage on a real module owned by this exact E1 actor. */
	void NotifyJuryDemoE1ModuleDamage(
		const AABTSM7BuildingModule& Module,
		EABTSM73E1DamageCause Cause,
		bool bModuleBroken,
		float NormalSpeedCMPerSec);
	const FABTSM73E1DamageLifecycleState&
		GetJuryDemoE1DamageLifecycleStateForValidation() const
	{
		return JuryDemoE1DamageLifecycleState;
	}

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
	/** Physical module bounds for whole-building camera framing; excludes labels and foundations. */
	bool QueryLivePresentationBounds(
		FBox& OutWorldBounds,
		int32& OutLiveModuleCount) const;
	/** Event-time ownership query; callers must not cache module pointers or infer ownership from Actor Owner. */
	bool OwnsRuntimePrimitive(const UPrimitiveComponent* Component) const;
	/** Publishes the static pre-impact HISM presentation through the same
	 * stylized material authority used by its post-impact Chaos modules. */
	void GatherPreviewComponentsForStylizedAdapter(
		TArray<TPair<UPrimitiveComponent*, EABTSM7BuildingMaterial>>& OutBindings) const;

private:
	void InitializeJuryDemoFixedSixStaticRegistration(
		AABTSM7BuildingMaterialSystem& MaterialSystem);
	void InitializeBuildingFreezeV3RuntimeRegistration(
		AABTSM7BuildingMaterialSystem& MaterialSystem);
	void ConfigureJuryDemoFixedSixStaticHISM(
		UHierarchicalInstancedStaticMeshComponent& Component);
	bool ConfigureJuryDemoFixedSixFrozenTangentSupport(
		const FABTSM73JuryDemoFixedSixStaticEntry& Entry,
		FString& OutError);
	bool ValidateJuryDemoFixedSixFrozenTangentSupport(
		const FABTSM73JuryDemoFixedSixStaticEntry& Entry,
		FString& OutError) const;
	/** Frozen-geometry support closure used only by destructive gameplay, never by static certification. */
	bool BuildJuryDemoFixedSixSupportClosure(
		TConstArrayView<int32> SeedBrickIds,
		TArray<AABTSM7BuildingModule*>& OutPhysicsModules,
		TArray<int32>* OutAffectedBrickIds,
		FString& OutError) const;
	bool ResolveJuryDemoFixedSixImpactSeedBrickIds(
		TConstArrayView<AABTSM7BuildingModule*> TriggerModules,
		float ImpactRadiusCM,
		TArray<int32>& OutSeedBrickIds,
		FString& OutError) const;
	bool QueueJuryDemoFixedSixDamageBrickIds(
		TConstArrayView<int32> SeedBrickIds,
		FString& OutError);
	bool ActivateJuryDemoFixedSixImpactSupportClosureTransaction(
		TConstArrayView<int32> TriggerBrickIds,
		uint64 DamageEpoch,
		FString& OutError);
	void ResolveQueuedJuryDemoFixedSixDamageTransaction();
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
	void TickJuryDemoFixedSixChaosValidation(float DeltaSeconds);
	void FinishJuryDemoFixedSixChaosValidation();
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
	TArray<TWeakObjectPtr<AABTSM7BuildingModule>>
		JuryDemoFixedSixChaosPhysicsModules;
	TArray<FTransform> JuryDemoFixedSixChaosInitialTransforms;
	TMap<int32, TWeakObjectPtr<AABTSM7BuildingModule>> RuntimeModulesByNodeId;
	TMap<TWeakObjectPtr<AABTSM7BuildingModule>, FTransform> IdleInitialTransforms;
	FTimerHandle MaterialSystemSearchTimer;
	int32 MaterialSystemSearchAttempts = 0;
	float IdleValidationElapsed = 0.0f;
	float IdleStableElapsed = 0.0f;
	bool bRuntimeSpawned = false;
	bool bJuryDemoFixedSixStaticRegistrationAccepted = false;
	bool bJuryDemoFixedSixFrozenTangentSupportActive = false;
	bool bJuryDemoFixedSixChaosPrepared = false;
	bool bJuryDemoFixedSixChaosRunning = false;
	bool bJuryDemoFixedSixChaosDeferredUntilFirstHit = false;
	bool bJuryDemoFixedSixChaosDeferredActivationInProgress = false;
	bool bJuryDemoFixedSixChaosDeferredActivated = false;
	FABTSM73JuryDemoFixedSixChaosResult JuryDemoFixedSixChaosResult;
	FABTSM73E1DamageLifecycleState JuryDemoE1DamageLifecycleState;
	FVector JuryDemoFixedSixChaosSiteUp = FVector::UpVector;
	float JuryDemoFixedSixChaosQuietSeconds = 0.0f;
	double JuryDemoFixedSixChaosWallStartSeconds = 0.0;
	uint64 JuryDemoFixedSixChaosActivationFrame = 0;
	int32 JuryDemoFixedSixActivePhysicsBodyCount = 0;
	TSet<int32> JuryDemoFixedSixRemovedSupportBrickIds;
	/** One stable building-scoped BrickId batch, resolved once from Tick. */
	TArray<int32> JuryDemoFixedSixQueuedDamageSeedBrickIds;
	uint64 JuryDemoFixedSixDamageEpoch = 0;
	/** Stable BrickId order; entries remain visible and independent until promoted. */
	TArray<TWeakObjectPtr<AABTSM7BuildingModule>> JuryDemoFixedSixOverflowKinematicModules;
	float JuryDemoFixedSixOverflowAccumulatorSeconds = 0.0f;
	/** Keep exact slots available for a later direct hit/ground encounter. */
	int32 JuryDemoFixedSixOverflowEmergencyReserveBodies = 16;
	void TickJuryDemoFixedSixOverflowKinematic(float DeltaSeconds);
	bool PromoteJuryDemoFixedSixOverflowModule(
		AABTSM7BuildingModule& Module, bool bDirectImpact,
		FString& OutError);
	bool bIdleValidationRunning = false;
	bool bDAG4ValidationRunning = false;
	EABTSM73IdleValidationState IdleValidationState = EABTSM73IdleValidationState::Pending;
	bool bRuntimePlanar = true;
	FVector RuntimeGravityReference = FVector::UpVector;
	TUniquePtr<
		FABTSM73DAG4RuntimeState,
		FABTSM73DAG4RuntimeStateDeleter> DAG4RuntimeState;
};
