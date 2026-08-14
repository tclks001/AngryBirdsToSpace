// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCG/ABTSM3MonthlyEncounter.h"
#include "PCG/ABTSM3MonthlyFinaleAnchor.h"
#include "PCG/ABTSM3MonthlyPresentation.h"
#include "PCG/ABTSM3MonthlyRoute.h"
#include "PCG/ABTSM3MonthlySatellitePreview.h"
#include "PCG/ABTSM3MonthlySchema.h"
#include "PCG/ABTSM3MonthlySlingshotField.h"
#include "PCG/ABTSM3MonthlyWitness.h"
#include "PCG/ABTSM3TaskGraphTypes.h"
#include "Planet/ABTSM2Planet.h"
#include "Terrain/ABTSM3TerrainVisualField.h"
#include "World/ABTSM110FinaleTypes.h"
#include "ABTSM3Planet.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UABTSM3TerrainMaterialBridge;
struct FABTSBuildingGenerationContract;
struct FABTSFinaleWorldContract;

USTRUCT(BlueprintType)
struct FABTSM3SurfacePhysicsProfile
{
	GENERATED_BODY()

	FABTSM3SurfacePhysicsProfile() = default;
	FABTSM3SurfacePhysicsProfile(const float InGroundDragPerSecond, const float InRestitution)
		: GroundDragPerSecond(InGroundDragPerSecond), Restitution(InRestitution) {}

	/** Linear tangential resistance consumed by the kinematic force mover. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float GroundDragPerSecond = 5.3f;

	/** Fraction of incoming normal speed returned after a sufficiently hard surface impact. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Restitution = 0.02f;
};

USTRUCT(BlueprintType)
struct FABTSM3SurfacePhysicsSample
{
	GENERATED_BODY()

	FABTSM3SurfaceSDFSample SDF;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics")
	float GroundDragPerSecond = 5.3f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics")
	float Restitution = 0.02f;
};

/** M3 presentation planet. CellTopo and TaskGraph remain the only gameplay sources. */
UCLASS(BlueprintType)
class ABTSRUNTIME_API AABTSM3Planet : public AABTSM2Planet
{
	GENERATED_BODY()

public:
	AABTSM3Planet();

	virtual bool RebuildPlanet() override;
	virtual float GetSurfaceRadiusAtDirection(const FVector& UnitDirection) const override;
	virtual FVector GetSurfaceNormalAtDirection(const FVector& UnitDirection) const override;

#if WITH_EDITOR
	/**
	 * Draws the exact R-5 target footprints, attack corridors, satellite
	 * practice layout, and R-5.2 finale-anchor clearance/frame for the
	 * explicitly selected preview candidate. Returns false when no exact
	 * preview candidate is available.
	 */
	bool DrawMonthlyLogicRegionDebugOverlay(
		float LifeTimeSeconds,
		int32& OutTargetFootprintCellCount,
		int32& OutAttackCorridorCellCount,
		bool& bOutSatelliteE5PreviewDrawn) const;
#endif

	UFUNCTION(BlueprintPure, Category = "ABTS|M3|PCG")
	const TArray<FABTSM3CellState>& GetGeneratedCellStates() const { return GeneratedCellStates; }

	UFUNCTION(BlueprintPure, Category = "ABTS|M3|PCG")
	const TArray<FABTSM3TaskNode>& GetGeneratedTasks() const { return GeneratedTasks; }

	UFUNCTION(BlueprintPure, Category = "ABTS|M3|PCG")
	const TArray<FABTSM3TaskLink>& GetGeneratedTaskLinks() const { return GeneratedTaskLinks; }

	/** Integration-owned rendering refresh calls this without rebuilding M3. */
	bool ApplyStylizedSurfaceStyle(bool bStyleEnabled);
	bool TryGetStylizedSurfaceStyleEnabled(float& OutValue) const;

	UFUNCTION(BlueprintPure, Category = "ABTS|M3|PCG")
	const TArray<FABTSM3CellEdgeState>& GetGeneratedEdgeStates() const { return GeneratedEdgeStates; }

	/** Internal read-only R-1 observation; it is never exported through the v1 M7/M11 contracts. */
	const FABTSM3MonthlyWorldSchema& GetMonthlyWorldSchema() const
	{
		return MonthlyWorldSchema;
	}

	/** R-2 route-only candidate pool. It never mutates or replaces the compatibility world. */
	const FABTSM3MonthlyRoutePool& GetMonthlyRoutePool() const
	{
		return MonthlyRoutePool;
	}

	/** Revalidates the R-2 pool against this planet's private CellTopo. */
	bool ValidateMonthlyRoutePool(FString& OutFailure) const;

	/**
	 * R-3 spatial candidate pool. It is an M3-private observation layer and
	 * never replaces the compatibility TaskGraph or v1 building exports.
	 */
	const FABTSM3MonthlySpatialResult& GetMonthlySpatialResult() const
	{
		return MonthlySpatialResult;
	}

	/** Revalidates the R-3 result against the R-2 source pool and CellTopo. */
	bool ValidateMonthlySpatialResult(FString& OutFailure) const;

	/** R-5.2 topology plans reserve the terminal road apron before ordinary slots are selected. */
	const FABTSM3MonthlyFinaleAnchorPlanResult&
		GetMonthlyFinaleAnchorPlanResult() const
	{
		return MonthlyFinaleAnchorPlanResult;
	}

	/** Rebuilds and whole-struct compares the observation-only terminal plans. */
	bool ValidateMonthlyFinaleAnchorPlanResult(
		FString& OutFailure) const;

	/** Resolves one explicit retained candidate against the final continuous surface. */
	bool TryBuildMonthlyFinaleAnchorPreview(
		int32 SourceRouteCandidateId,
		FABTSM3MonthlyFinaleAnchorPreview& OutPreview,
		FString& OutFailure) const;

	const FABTSM3MonthlyFinaleAnchorPreview&
		GetActiveMonthlyFinaleAnchorPreview() const
	{
		return ActiveMonthlyFinaleAnchorPreview;
	}

	/**
	 * R-5 candidate-bound visual plans. They preserve every R-3 candidate and
	 * never imply a selected or accepted monthly world.
	 */
	const FABTSM3MonthlyPresentationResult&
		GetMonthlyPresentationResult() const
	{
		return MonthlyPresentationResult;
	}

	/** Rebuilds and whole-struct compares the R-5 result. */
	bool ValidateMonthlyPresentationResult(
		FString& OutFailure) const;

	/** True only for an explicitly requested candidate preview. */
	bool IsMonthlyPresentationPreviewActive() const
	{
		return bMonthlyPresentationPreviewActive;
	}

	int32 GetMonthlyPresentationPreviewCandidateId() const
	{
		return ActiveMonthlyPresentationPreviewCandidateId;
	}

	int64 GetMonthlyPresentationPreviewCandidateHash() const
	{
		return ActiveMonthlyPresentationPreviewCandidateHash;
	}

	/**
	 * R-3.1 ordinary slingshot slot fields. Field identity is diagnostic only:
	 * callers must not treat it as an allowed-pair restriction.
	 */
	const FABTSM3MonthlySlingshotFieldResult&
		GetMonthlySlingshotFieldResult() const
	{
		return MonthlySlingshotFieldResult;
	}

	/** Rebuilds and whole-struct compares the R-3.1 result. */
	bool ValidateMonthlySlingshotFieldResult(
		FString& OutFailure) const;

	/** Candidate-bound frozen satellite/E5 preview. It never spawns gameplay Actors. */
	const FABTSM3MonthlySatellitePreviewResult&
		GetMonthlySatellitePreviewResult() const
	{
		return MonthlySatellitePreviewResult;
	}

	/** Rebuilds and whole-struct compares the R-5.1 preview result. */
	bool ValidateMonthlySatellitePreviewResult(
		FString& OutFailure) const;

	/**
	 * R-4 gameplay-finalize observation. Fixture authority may prove the local
	 * algorithm, but only an Integration authority can certify external inputs.
	 */
	const FABTSM3MonthlyWitnessResult& GetMonthlyWitnessResult() const
	{
		return MonthlyWitnessResult;
	}

	/** Structural revalidation; it never replays an external provider. */
	bool ValidateMonthlyWitnessResult(FString& OutFailure) const;

	/** Explicit Integration seam. No fixture provider is created by runtime. */
	bool FinalizeMonthlyGameplay(
		const IABTSM3MonthlyWitnessServices& Services,
		FString& OutFailure);

	/** True only when the complete M3 logical, terrain and material presentation rebuild succeeded. */
	UFUNCTION(BlueprintPure, Category = "ABTS|M3|PCG")
	bool IsM3PresentationReady() const { return bM3PresentationReady; }

	double GetLastM3RebuildDurationMS() const
	{
		return LastM3RebuildDurationMS;
	}

	/** R-5 builder cost only; excludes R2/R3 source generation and surface/HISM presentation. */
	double GetLastMonthlyPresentationBuildDurationMS() const
	{
		return LastMonthlyPresentationBuildDurationMS;
	}

	bool IsTerrainBasePaletteApplied() const
	{
		return bTerrainBasePaletteApplied;
	}

	int32 GetTerrainBasePaletteCellCount() const
	{
		return TerrainBasePaletteCellCount;
	}

	int32 GetMonthlyDecorAccent0InstanceCount() const
	{
		return MonthlyDecorAccent0InstanceCount;
	}

	int32 GetMonthlyDecorAccent1InstanceCount() const
	{
		return MonthlyDecorAccent1InstanceCount;
	}

	/** Reserved interface for M4 modular building generation. M3 only returns validated spawn sites. */
	UFUNCTION(BlueprintPure, Category = "ABTS|M3|Building")
	const TArray<FABTSM3BuildingSpawnSite>& GetBuildingSpawnSites() const { return BuildingSpawnSites; }

	/** Deterministic terminal frame authored by M3 and consumed by M11 local-layout presets. */
	const FABTSM110FinaleLocalFrame& GetFinaleLaunchFrame() const { return FinaleLaunchFrame; }

	/**
	 * Preferred M7 boundary. Exports an immutable value snapshot instead of
	 * granting the consumer access to M3 TaskGraph arrays or generation config.
	 */
	bool TryExportBuildingGenerationContract(
		FABTSBuildingGenerationContract& OutContract) const;

	/**
	 * Preferred M11 boundary. Exports only accepted-world identity, primary
	 * radius and the certified finale-local frame; M9 is absent by construction.
	 */
	bool TryExportFinaleWorldContract(
		FABTSFinaleWorldContract& OutContract) const;

	UFUNCTION(BlueprintPure, Category = "ABTS|M3|Surface")
	bool QuerySurface(const FVector& UnitDirection, FVector& OutWorldPosition, FVector& OutWorldNormal, float& OutSurfaceRadius, int32& OutCellId) const;

	/** CPU reconstruction of the terrain material's SDF weights for collision response. */
	bool QuerySurfacePhysics(const FVector& UnitDirection, FABTSM3SurfacePhysicsSample& OutSample) const;

	/** CPU counterpart of the final land + road + river SDF color used by M10. */
	bool QueryScoutMapTerrainColor(
		const FVector& UnitDirection,
		FLinearColor& OutColor,
		int32 StartCellHint = 0,
		int32* OutCellId = nullptr) const;

	/** Character-center spawn transform at the Start Task's first logical road Cell. */
	UFUNCTION(BlueprintPure, Category = "ABTS|M3|Spawn")
	bool GetInitialRoadSpawnTransform(float SurfaceOffsetCM, FTransform& OutWorldTransform, int32& OutCellId) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	int32 WorldSeed = 312503;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	FABTSM3PCGConfig PCGConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	FABTSM3MonthlySchemaConfig MonthlySchemaConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	FABTSM3MonthlyRouteConfig MonthlyRouteConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	FABTSM3MonthlyEncounterSpatialConfig MonthlyEncounterSpatialConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	FABTSM3MonthlyPresentationConfig MonthlyPresentationConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	FABTSM3MonthlyFinaleAnchorConfig MonthlyFinaleAnchorConfig;

	/**
	 * Explicit preview authority only. R-5 does not select a candidate, so this
	 * remains off unless an editor user or -ABTSM3R5Preview requests it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Preview")
	bool bEnableMonthlyPresentationPreview = false;

	/** Exact R-3 SourceRouteCandidateId required by the preview. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Preview")
	int32 MonthlyPresentationPreviewCandidateId = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	FABTSM3MonthlySlingshotFieldConfig MonthlySlingshotFieldConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	FABTSM3MonthlySatellitePreviewConfig MonthlySatellitePreviewConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	FABTSM3MonthlyWitnessConfig MonthlyWitnessConfig;

	/** CellTopo anchor driven construction pads consumed by the M7 TaskGraph building spawner. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M7|Spherical Buildings")
	FABTSM3BuildingPadSettings BuildingPadSettings;

	/** World-space spacing of the one terminal Space-slingshot slot pair. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M11.0|Finale Closure",
		meta = (ClampMin = "100.0", ClampMax = "600.0", UIMin = "160.0", UIMax = "360.0", Units = "cm"))
	float FinaleSpaceSlotSeparationCM = 320.0f;

	/** Small lift that keeps the slot interaction mesh above the certified pad. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M11.0|Finale Closure",
		meta = (ClampMin = "0.0", ClampMax = "40.0", UIMax = "12.0", Units = "cm"))
	float FinaleSpaceSlotSurfaceOffsetCM = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Terrain", meta = (ClampMin = "0.0"))
	float MacroHeightScaleCM = 900.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Terrain", meta = (ClampMin = "0.0"))
	float TaskWaterDepthCM = 80.0f;

	/**
	 * Diagnostic flat-surface experiment. Keeps TaskGraph terrain types, roads,
	 * rivers, materials and HISM placement, but removes all radial height
	 * variation (including the river depression) from the rendered/query surface.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Experiment")
	bool bDisableTerrainHeightVariationExperiment = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Terrain", meta = (ClampMin = "1.0"))
	float TerrainBlendWidthCM = 240.0f;

	/** Geometric transition width; independent from TerrainBlendWidthCM, which controls material color only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Terrain", meta = (ClampMin = "1.0"))
	float HeightBlendWidthCM = 160.0f;

	/** World-space radius of the central-difference stencil used to smooth terrain vertex normals. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Terrain", meta = (ClampMin = "10.0", ClampMax = "800.0", UIMin = "40.0", UIMax = "400.0"))
	float SurfaceNormalSmoothingDistanceCM = 160.0f;

	/** Independent from visual color blending, so art changes do not silently change locomotion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics", meta = (ClampMin = "1.0", ClampMax = "1200.0"))
	float SurfacePhysicsBlendWidthCM = 240.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics")
	FABTSM3SurfacePhysicsProfile PlainPhysics = {5.3f, 0.02f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics")
	FABTSM3SurfacePhysicsProfile ForestPhysics = {6.2f, 0.01f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics")
	FABTSM3SurfacePhysicsProfile HighlandPhysics = {5.8f, 0.04f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics")
	FABTSM3SurfacePhysicsProfile MountainPhysics = {4.1f, 0.16f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics")
	FABTSM3SurfacePhysicsProfile RoadPhysics = {3.0f, 0.04f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M5.2|Physics")
	FABTSM3SurfacePhysicsProfile RiverPhysics = {11.0f, 0.0f};

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Material")
	FLinearColor RoadColor = FLinearColor(0.22f, 0.12f, 0.045f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Material", meta = (ClampMin = "10.0", ClampMax = "800.0"))
	float TrailVisualHalfWidthCM = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Material", meta = (ClampMin = "10.0", ClampMax = "1200.0"))
	float MainRoadVisualHalfWidthCM = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Material")
	FLinearColor RiverColor = FLinearColor(0.03f, 0.20f, 0.36f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Material", meta = (ClampMin = "10.0", ClampMax = "600.0"))
	float StreamVisualHalfWidthCM = 70.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Material", meta = (ClampMin = "10.0", ClampMax = "800.0"))
	float ShallowRiverVisualHalfWidthCM = 125.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Material", meta = (ClampMin = "10.0", ClampMax = "1200.0"))
	float DeepRiverVisualHalfWidthCM = 190.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Material")
	TObjectPtr<UMaterialInterface> TerrainMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM")
	TObjectPtr<UStaticMesh> ForestInstanceMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM")
	TObjectPtr<UStaticMesh> RockInstanceMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM", meta = (ClampMin = "0", ClampMax = "8"))
	int32 InstancesPerCell = 2;

	/** 0 keeps trees purely radial; 1 fully follows the rendered terrain normal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "0.5"))
	float ForestSurfaceNormalBlend = 0.2f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> ForestHISM;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RockHISM;

	/** M3-owned T3-A1 material; published read-only to Integration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM|Stylized")
	TObjectPtr<UMaterialInterface> ForestStylizedMaterial;

	/** M3-owned T3-A1 material; published read-only to Integration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|HISM|Stylized")
	TObjectPtr<UMaterialInterface> RockStylizedMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	TArray<FABTSM3TaskNode> GeneratedTasks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	TArray<FABTSM3TaskLink> GeneratedTaskLinks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	TArray<FABTSM3CellState> GeneratedCellStates;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	TArray<FABTSM3CellEdgeState> GeneratedEdgeStates;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|PCG")
	FABTSM3PCGSummary PCGSummary;

	/** R-1 observation only. Compatibility TaskGraph identity remains in PCGSummary. */
	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema")
	FABTSM3MonthlyWorldSchema MonthlyWorldSchema;

	/** R-2 route-only result. The Gen3/Policy1 PCGSummary remains authoritative for presentation. */
	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route")
	FABTSM3MonthlyRoutePool MonthlyRoutePool;

	/** R-3 six-Encounter spatial result; it is not exported through compatibility contracts. */
	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter")
	FABTSM3MonthlySpatialResult MonthlySpatialResult;

	/** R-5 read-only presentation plans for every retained R-3 candidate. */
	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation")
	FABTSM3MonthlyPresentationResult MonthlyPresentationResult;

	/** R-5.2 terminal-apron plans; no compatibility frame or stable contract is mutated. */
	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category = "ABTS|M3|Monthly Finale Anchor")
	FABTSM3MonthlyFinaleAnchorPlanResult MonthlyFinaleAnchorPlanResult;

	/** R-3.1 ordinary slot-field alternatives; finale Space slots remain a separate exact pair. */
	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field")
	FABTSM3MonthlySlingshotFieldResult MonthlySlingshotFieldResult;

	/** R-5.1 exact alternatives; no M9/M7 Actor and no monthly-world authority. */
	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category = "ABTS|M3|Monthly Satellite Preview")
	FABTSM3MonthlySatellitePreviewResult MonthlySatellitePreviewResult;

	/** R-4 additive finalize result; never overwrites PCGSummary.LayoutHash. */
	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category = "ABTS|M3|Monthly Witness")
	FABTSM3MonthlyWitnessResult MonthlyWitnessResult;

#if WITH_EDITORONLY_DATA
	/** Index-only debug snapshot. R-1 does not draw or alter the production map. */
	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category = "ABTS|M3|Monthly Schema|Debug")
	FABTSM3MonthlySchemaDebugData MonthlySchemaDebugData;

	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Debug")
	FABTSM3MonthlyRouteDebugData MonthlyRouteDebugData;

	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Debug")
	FABTSM3MonthlySpatialDebugData MonthlySpatialDebugData;

	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Debug")
	FABTSM3MonthlyPresentationDebugData MonthlyPresentationDebugData;

	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category = "ABTS|M3|Monthly Slingshot Field|Debug")
	FABTSM3MonthlySlingshotFieldDebugData MonthlySlingshotFieldDebugData;

	/** Draws the best R-2 route as a temporary Editor/PIE line overlay without changing terrain. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Route|Debug")
	bool bDrawMonthlyRouteDebugOverlay = false;

	/** Draws R-3 reservations/envelope only; it never changes production terrain. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Encounter|Debug")
	bool bDrawMonthlySpatialDebugOverlay = false;

	/** Draws the explicitly bound R-5 candidate's biome/role audit points. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Debug")
	bool bDrawMonthlyPresentationDebugOverlay = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Debug")
	bool bDrawMonthlyPresentationBiomeLayer = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Debug")
	bool bDrawMonthlyPresentationEnvelopeLayer = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Debug")
	bool bDrawMonthlyPresentationVisualBeatLayer = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Monthly Presentation|Debug")
	bool bDrawMonthlyPresentationCoverageLayer = true;
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M3|Building")
	TArray<FABTSM3BuildingSpawnSite> BuildingSpawnSites;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ABTS|M11.0|Finale Frame")
	FABTSM110FinaleLocalFrame FinaleLaunchFrame;

private:
	bool GenerateLogicalTerrain();
	void BuildM3ContinuousSurface();
	void BuildDecorInstances(
		const TArray<FABTSM3CellState>*
			PresentationCellStates = nullptr,
		const FABTSM3MonthlyCandidatePresentation*
			PresentationCandidate = nullptr);
	void BuildBuildingSpawnSites();
	bool TryBuildMonthlyPresentationPreviewData(
		TArray<FABTSM3CellState>& OutCellStates,
		TArray<FABTSM3CellEdgeState>& OutEdgeStates,
		const FABTSM3MonthlyCandidatePresentation*&
			OutCandidate);
	int32 ResolveMonthlyPresentationPreviewCandidateId(
		bool& bOutRequested) const;
	FABTSM3MonthlyFinaleAnchorConfig
		MakeResolvedMonthlyFinaleAnchorConfig() const;
	int32 FindNearestCell(const FVector& UnitDirection) const;
#if WITH_EDITOR
	void DrawMonthlyRouteDebugOverlay() const;
	void DrawMonthlySpatialDebugOverlay() const;
	void DrawMonthlyPresentationDebugOverlay() const;
#endif

	TUniquePtr<FABTSM3TerrainVisualField> TerrainVisualField;
	/**
	 * Persistent preview-owned inputs and field used by every color consumer.
	 * The field stores pointers to these arrays, so all three must share the
	 * Planet lifetime rather than a RebuildPlanet() stack frame.
	 */
	TArray<FABTSM3CellState> MonthlyPresentationPreviewCellStates;
	TArray<FABTSM3CellEdgeState> MonthlyPresentationPreviewEdgeStates;
	TUniquePtr<FABTSM3TerrainVisualField>
		MonthlyPresentationPreviewVisualField;
	bool bM3PresentationReady = false;
	double LastM3RebuildDurationMS = 0.0;
	double LastMonthlyPresentationBuildDurationMS = 0.0;
	bool bTerrainBasePaletteApplied = false;
	int32 TerrainBasePaletteCellCount = 0;
	int32 MonthlyDecorAccent0InstanceCount = 0;
	int32 MonthlyDecorAccent1InstanceCount = 0;
	bool bMonthlyPresentationPreviewActive = false;
	int32 ActiveMonthlyPresentationPreviewCandidateId =
		INDEX_NONE;
	int64 ActiveMonthlyPresentationPreviewCandidateHash = 0;
	FABTSM3MonthlyFinaleAnchorPreview
		ActiveMonthlyFinaleAnchorPreview;

	UPROPERTY(Transient)
	TObjectPtr<UABTSM3TerrainMaterialBridge> TerrainMaterialBridge;
};
