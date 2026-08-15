// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/ABTSM3Planet.h"

#include "ABTSRuntime.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PCG/ABTSM3MonthlySatellitePracticeRuntime.h"
#include "PCG/ABTSM3R5AcceptanceManifest.h"
#include "PCG/ABTSM3TaskGraphGenerator.h"
#include "ProceduralMeshComponent.h"
#include "Rendering/ABTSStylizedMaterialContract.h"
#include "Slingshot/ABTSSlingshotVisualTypes.h"
#include "Terrain/ABTSM3TerrainVisualField.h"
#include "Terrain/ABTSM3TerrainMaterialBridge.h"
#include "UObject/ConstructorHelpers.h"
#include "World/ABTSCollisionChannels.h"
#if WITH_EDITOR
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#endif

namespace
{
class FPlanetMonthlyFinaleAnchorSurface final
	: public IABTSM3MonthlyFinaleAnchorSurface
{
public:
	FPlanetMonthlyFinaleAnchorSurface(
		const AABTSM3Planet& InPlanet)
		: Planet(InPlanet)
	{
	}

	virtual FVector GetPrimaryCenterWorld() const override
	{
		return Planet.GetPlanetCenterWorld();
	}

	virtual float GetPrimaryRadiusCM() const override
	{
		return Planet.GetPlanetRadiusCM();
	}

	virtual bool QuerySurface(
		const FVector& UnitDirection,
		FABTSM3MonthlyFinaleSurfaceSample& OutSample) const override
	{
		const FVector Direction = UnitDirection.GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			return false;
		}
		float SurfaceRadiusCM = 0.0f;
		return Planet.QuerySurface(
				Direction,
				OutSample.WorldLocation,
				OutSample.WorldNormal,
				SurfaceRadiusCM,
				OutSample.NearestCellId)
			&& !OutSample.WorldLocation.ContainsNaN()
			&& !OutSample.WorldNormal.ContainsNaN()
			&& OutSample.WorldNormal.Normalize();
	}

private:
	const AABTSM3Planet& Planet;
};

class FPlanetMonthlySatellitePreviewSurface final
	: public IABTSM3MonthlySatellitePreviewSurface
{
public:
	FPlanetMonthlySatellitePreviewSurface(
		const AABTSM3Planet& InPlanet,
		const TArray<FABTSM2Cell>& InCells)
		: Planet(InPlanet), Cells(InCells)
	{
	}

	virtual FVector GetPrimaryCenterWorld() const override
	{
		return Planet.GetPlanetCenterWorld();
	}

	virtual float GetPrimaryRadiusCM() const override
	{
		return Planet.GetPlanetRadiusCM();
	}

	virtual bool QuerySurface(
		const FVector& UnitDirection,
		FABTSM3MonthlySatelliteSurfaceSample& OutSample) const override
	{
		const FVector Direction = UnitDirection.GetSafeNormal();
		if (Direction.IsNearlyZero() || Cells.IsEmpty())
		{
			return false;
		}
		OutSample.WorldLocation = Planet.GetPlanetCenterWorld()
			+ Direction * Planet.GetSurfaceRadiusAtDirection(Direction);
		OutSample.WorldNormal =
			Planet.GetSurfaceNormalAtDirection(Direction);
		OutSample.NearestCellId = INDEX_NONE;
		double BestDot = -2.0;
		for (int32 CellId = 0; CellId < Cells.Num(); ++CellId)
		{
			const double Dot = FVector::DotProduct(
				Direction,
				Cells[CellId].UnitCenter);
			if (Dot > BestDot)
			{
				BestDot = Dot;
				OutSample.NearestCellId = CellId;
			}
		}
		return OutSample.NearestCellId != INDEX_NONE
			&& !OutSample.WorldLocation.ContainsNaN()
			&& !OutSample.WorldNormal.ContainsNaN()
			&& OutSample.WorldNormal.Normalize();
	}

private:
	const AABTSM3Planet& Planet;
	const TArray<FABTSM2Cell>& Cells;
};

bool ValidateInstancedMeshMaterials(const TCHAR* Label, const UStaticMesh* Mesh)
{
	if (Mesh == nullptr) return false;

	bool bAllMaterialsValid = true;
	const TArray<FStaticMaterial>& StaticMaterials = Mesh->GetStaticMaterials();
	for (int32 SlotIndex = 0; SlotIndex < StaticMaterials.Num(); ++SlotIndex)
	{
		UMaterialInterface* Material = StaticMaterials[SlotIndex].MaterialInterface;
		const bool bUsageValid = Material != nullptr
			&& Material->CheckMaterialUsage_Concurrent(MATUSAGE_InstancedStaticMeshes);
		if (!bUsageValid)
		{
			bAllMaterialsValid = false;
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M3][HISM] %s mesh %s material slot %d (%s) is not compiled for Instanced Static Meshes."),
				Label, *GetNameSafe(Mesh), SlotIndex, *GetNameSafe(Material));
		}
	}
	return bAllMaterialsValid;
}
}

AABTSM3Planet::AABTSM3Planet()
{
	// Keep native preview meshes as a last-resort presentation fallback.  M3 must
	// remain visible even before the art meshes are assigned in the Blueprint.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ForestPreviewMesh(
		TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RockPreviewMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ForestAssetMesh(
		TEXT("/Game/StaticMesh/Tree/SM_PineTree_PivotFixed.SM_PineTree_PivotFixed"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RockAssetMesh(
		TEXT("/Game/StaticMesh/Stone/SM_Stone1.SM_Stone1"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ForestAssetMaterial(
		TEXT("/Game/StaticMesh/Tree/M_PineTree.M_PineTree"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RockAssetMaterial(
		TEXT("/Game/StaticMesh/Stone/M_Stone.M_Stone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ForestStylizedMaterialAsset(
		TEXT("/Game/M3/Toon/Trees/M_ABTS_M3_ToonPine.M_ABTS_M3_ToonPine"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RockStylizedMaterialAsset(
		TEXT("/Game/M3/Toon/Stones/M_ABTS_M3_ToonStone.M_ABTS_M3_ToonStone"));
	ForestStylizedMaterial = ForestStylizedMaterialAsset.Succeeded()
		? ForestStylizedMaterialAsset.Object
		: nullptr;
	RockStylizedMaterial = RockStylizedMaterialAsset.Succeeded()
		? RockStylizedMaterialAsset.Object
		: nullptr;

	ForestHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("ForestHISM"));
	ForestHISM->SetupAttachment(ContinuousSurface);
	// Instances remain static presentation/collision geometry until a future M6
	// launch hit explicitly converts one instance into a pooled destruction proxy.
	ForestHISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ForestHISM->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
	ForestHISM->SetCollisionResponseToAllChannels(ECR_Block);
	ForestHISM->SetSimulatePhysics(false);
	ForestHISM->SetMobility(EComponentMobility::Movable);
	ForestHISM->SetStaticMesh(ForestAssetMesh.Succeeded() ? ForestAssetMesh.Object : ForestPreviewMesh.Object);
	if (ForestAssetMaterial.Succeeded()) ForestHISM->SetMaterial(0, ForestAssetMaterial.Object);

	RockHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("RockHISM"));
	RockHISM->SetupAttachment(ContinuousSurface);
	RockHISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	RockHISM->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
	RockHISM->SetCollisionResponseToAllChannels(ECR_Block);
	RockHISM->SetSimulatePhysics(false);
	RockHISM->SetMobility(EComponentMobility::Movable);
	RockHISM->SetStaticMesh(RockAssetMesh.Succeeded() ? RockAssetMesh.Object : RockPreviewMesh.Object);
	if (RockAssetMaterial.Succeeded()) RockHISM->SetMaterial(0, RockAssetMaterial.Object);
}

bool AABTSM3Planet::ApplyStylizedSurfaceStyle(const bool bStyleEnabled)
{
	return TerrainMaterialBridge != nullptr
		&& TerrainMaterialBridge->ApplyStylizedSurfaceParameters(bStyleEnabled);
}

bool AABTSM3Planet::TryGetStylizedSurfaceStyleEnabled(float& OutValue) const
{
	return TerrainMaterialBridge != nullptr
		&& TerrainMaterialBridge->TryGetScalarParameterValue(
			FABTSStylizedMaterialContract::GetStyleEnabledParameterName(),
			OutValue);
}

bool AABTSM3Planet::RebuildPlanet()
{
	const double RebuildStartSeconds =
		FPlatformTime::Seconds();
	LastM3RebuildDurationMS = 0.0;
	LastMonthlyPresentationBuildDurationMS = 0.0;
	bTerrainBasePaletteApplied = false;
	TerrainBasePaletteCellCount = 0;
	MonthlyDecorAccent0InstanceCount = 0;
	MonthlyDecorAccent1InstanceCount = 0;
	bM3PresentationReady = false;
	bMonthlyPresentationPreviewActive = false;
	ActiveMonthlyPresentationPreviewCandidateId = INDEX_NONE;
	ActiveMonthlyPresentationPreviewCandidateHash = 0;
	MonthlyPresentationPreviewCellStates.Reset();
	MonthlyPresentationPreviewEdgeStates.Reset();
	MonthlyPresentationPreviewVisualField.Reset();
	ActiveMonthlyFinaleAnchorPreview =
		FABTSM3MonthlyFinaleAnchorPreview();
	MonthlyPresentationResult =
		FABTSM3MonthlyPresentationResult();
	MonthlyFinaleAnchorPlanResult =
		FABTSM3MonthlyFinaleAnchorPlanResult();
	MonthlySlingshotFieldResult =
		FABTSM3MonthlySlingshotFieldResult();
	MonthlySatellitePreviewResult =
		FABTSM3MonthlySatellitePreviewResult();
	MonthlyWitnessResult = FABTSM3MonthlyWitnessResult();
#if WITH_EDITORONLY_DATA
	MonthlySlingshotFieldDebugData =
		FABTSM3MonthlySlingshotFieldDebugData();
#endif
	if (!AABTSM2Planet::RebuildPlanet() || !GenerateLogicalTerrain()) return false;
	const float ResolvedHeightScaleCM = bDisableTerrainHeightVariationExperiment ? 0.0f : MacroHeightScaleCM;
	const float ResolvedWaterDepthCM = bDisableTerrainHeightVariationExperiment ? 0.0f : TaskWaterDepthCM;
	TerrainVisualField = MakeUnique<FABTSM3TerrainVisualField>();
	TerrainVisualField->Initialize(PlanetRadiusCM, ResolvedHeightScaleCM, ResolvedWaterDepthCM, HeightBlendWidthCM, TerrainBlendWidthCM, SurfaceNormalSmoothingDistanceCM,
		LogicalCells, GeneratedCellStates, GeneratedEdgeStates, TrailVisualHalfWidthCM, MainRoadVisualHalfWidthCM,
		StreamVisualHalfWidthCM, ShallowRiverVisualHalfWidthCM, DeepRiverVisualHalfWidthCM);
	// First resolve CellTopo anchors on the unmodified field. The field then applies
	// tangent construction pads and the sites are rebuilt so every downstream user
	// (mesh, collision query, HISM and M7 building) reads the same final surface.
	BuildBuildingSpawnSites();
	TerrainVisualField->SetBuildingPads(BuildingSpawnSites);
	BuildBuildingSpawnSites();
	const bool bFinaleFrameReady = FinaleLaunchFrame.IsUsable();
	if (bFinaleFrameReady)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M11.0][FinaleFrame] Ready Layout=%d Task=%d Cell=%d Pair=%d Separation=%.1f Forward=%s Right=%s Up=%s"),
			FinaleLaunchFrame.LayoutVersion,
			FinaleLaunchFrame.LaunchTaskId,
			FinaleLaunchFrame.AnchorCellId,
			FinaleLaunchFrame.SlotPairId,
			FVector::Distance(FinaleLaunchFrame.LeftSlotWorldLocation, FinaleLaunchFrame.RightSlotWorldLocation),
			*FinaleLaunchFrame.GetForward().ToCompactString(),
			*FinaleLaunchFrame.GetRight().ToCompactString(),
			*FinaleLaunchFrame.GetUp().ToCompactString());
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M11.0][FinaleFrame] Rejected after final terrain-pad resolution."));
	}
	FString SatellitePreviewFailure;
	const FPlanetMonthlySatellitePreviewSurface SatelliteSurface(
		*this,
		LogicalCells);
	if (!FABTSM3MonthlySatellitePreviewBuilder::Build(
			WorldSeed,
			MonthlySatellitePreviewConfig,
			LogicalCells,
			MonthlySpatialResult,
			MonthlySlingshotFieldResult,
			SatelliteSurface,
			MonthlySatellitePreviewResult,
			SatellitePreviewFailure))
	{
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M3R5.1][SatellitePreview] Pending Seed=%d Reason=%s Failure=%s CompatibilityWorldPreserved=1 MonthlyAccepted=0"),
			WorldSeed,
			FABTSM3MonthlySatellitePreviewBuilder::GetRejectReasonName(
				MonthlySatellitePreviewResult.RejectReason),
			*SatellitePreviewFailure);
	}
	BuildM3ContinuousSurface();
	const FABTSM3MonthlyCandidatePresentation*
		PresentationCandidate = nullptr;
	bool bPreviewDataReady =
		TryBuildMonthlyPresentationPreviewData(
			MonthlyPresentationPreviewCellStates,
			MonthlyPresentationPreviewEdgeStates,
			PresentationCandidate);
	if (bPreviewDataReady)
	{
		MonthlyPresentationPreviewVisualField =
			MakeUnique<FABTSM3TerrainVisualField>();
		MonthlyPresentationPreviewVisualField->Initialize(
			PlanetRadiusCM,
			ResolvedHeightScaleCM,
			ResolvedWaterDepthCM,
			HeightBlendWidthCM,
			TerrainBlendWidthCM,
			SurfaceNormalSmoothingDistanceCM,
			LogicalCells,
			MonthlyPresentationPreviewCellStates,
			MonthlyPresentationPreviewEdgeStates,
			TrailVisualHalfWidthCM,
			MainRoadVisualHalfWidthCM,
			StreamVisualHalfWidthCM,
			ShallowRiverVisualHalfWidthCM,
			DeepRiverVisualHalfWidthCM);
		if (!MonthlyPresentationPreviewVisualField->IsReady())
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M3R5][Preview] Rejected Candidate=%d Reason=PresentationVisualFieldUnavailable CompatibilityWorldPreserved=1 MonthlyAccepted=0"),
				ActiveMonthlyPresentationPreviewCandidateId);
			MonthlyPresentationPreviewCellStates.Reset();
			MonthlyPresentationPreviewEdgeStates.Reset();
			MonthlyPresentationPreviewVisualField.Reset();
			bMonthlyPresentationPreviewActive = false;
			ActiveMonthlyPresentationPreviewCandidateId = INDEX_NONE;
			ActiveMonthlyPresentationPreviewCandidateHash = 0;
			bPreviewDataReady = false;
			PresentationCandidate = nullptr;
		}
	}
	bool bMaterialReady = false;
	bool bPresentationReady = bFinaleFrameReady;
	if (TerrainMaterial)
	{
		TerrainMaterialBridge = NewObject<UABTSM3TerrainMaterialBridge>(this);
		bMaterialReady = TerrainMaterialBridge->Initialize(ContinuousSurface, TerrainMaterial, GetPlanetCenterWorld(), PlanetRadiusCM, TerrainBlendWidthCM,
			RoadColor, TrailVisualHalfWidthCM, MainRoadVisualHalfWidthCM,
			RiverColor, StreamVisualHalfWidthCM, ShallowRiverVisualHalfWidthCM, DeepRiverVisualHalfWidthCM,
			LogicalCells,
			bPreviewDataReady
				? MonthlyPresentationPreviewCellStates
				: GeneratedCellStates,
			bPreviewDataReady
				? MonthlyPresentationPreviewEdgeStates
				: GeneratedEdgeStates,
			bPreviewDataReady
				? *MonthlyPresentationPreviewVisualField
				: *TerrainVisualField,
			bPreviewDataReady
				? PresentationCandidate
				: nullptr);
		bTerrainBasePaletteApplied =
			bMaterialReady
			&& TerrainMaterialBridge->
				IsTerrainBasePaletteApplied();
		TerrainBasePaletteCellCount =
			bMaterialReady
			? TerrainMaterialBridge->
				GetTerrainBasePaletteCellCount()
			: 0;
		if (!bMaterialReady)
		{
			bPresentationReady = false;
			UE_LOG(LogABTSRuntime, Error, TEXT("[ABTS][M3] Terrain material bridge failed. Check M3 LUT parameter names and texture types."));
		}
	}
	BuildDecorInstances(
		bPreviewDataReady
			? &MonthlyPresentationPreviewCellStates
			: nullptr,
		bPreviewDataReady ? PresentationCandidate : nullptr);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M5.2][Collision] ForestHISM=%s RockHISM=%s StaticPhysics=1 DestroyableOutsideLaunch=0 PhysicsBlend=%.1f"),
		*UEnum::GetValueAsString(ForestHISM->GetCollisionEnabled()),
		*UEnum::GetValueAsString(RockHISM->GetCollisionEnabled()),
		SurfacePhysicsBlendWidthCM);

	int32 RoadCells = 0;
	int32 WaterCells = 0;
	for (const FABTSM3CellState& State : GeneratedCellStates)
	{
		RoadCells += State.bRoad ? 1 : 0;
		WaterCells += State.bWater ? 1 : 0;
	}
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M3] Ready=%d Seed=%d Version=%d Attempt=%d Tasks=%d Links=%d Cells=%d Edges=%d RoadCells=%d WaterCells=%d Buildings=%d ForestInstances=%d RockInstances=%d MaterialAssigned=%d MaterialReady=%d FlatHeightExperiment=%d EffectiveHeightScale=%.1f EffectiveWaterDepth=%.1f M3R5PreviewAuthority=%d M3R5PreviewCandidate=%d M3R5PreviewHash=%016llX"),
		bPresentationReady ? 1 : 0, WorldSeed, PCGSummary.GeneratorVersion, PCGSummary.AttemptIndex, GeneratedTasks.Num(), GeneratedTaskLinks.Num(),
		GeneratedCellStates.Num(), GeneratedEdgeStates.Num(), RoadCells, WaterCells, BuildingSpawnSites.Num(), ForestHISM->GetInstanceCount(), RockHISM->GetInstanceCount(), TerrainMaterial ? 1 : 0, bMaterialReady ? 1 : 0,
		bDisableTerrainHeightVariationExperiment ? 1 : 0, ResolvedHeightScaleCM, ResolvedWaterDepthCM,
		bMonthlyPresentationPreviewActive ? 1 : 0,
		ActiveMonthlyPresentationPreviewCandidateId,
		static_cast<unsigned long long>(
			ActiveMonthlyPresentationPreviewCandidateHash));
	bM3PresentationReady = bPresentationReady;
	LastM3RebuildDurationMS =
		(FPlatformTime::Seconds() - RebuildStartSeconds)
		* 1000.0;
#if WITH_EDITOR
	if (bM3PresentationReady)
	{
		if (bDrawMonthlyRouteDebugOverlay)
		{
			DrawMonthlyRouteDebugOverlay();
		}
		if (bDrawMonthlySpatialDebugOverlay)
		{
			DrawMonthlySpatialDebugOverlay();
		}
		if (bDrawMonthlyPresentationDebugOverlay)
		{
			DrawMonthlyPresentationDebugOverlay();
		}
	}
#endif
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R5][RebuildBudget] DurationMS=%.3f BudgetMS=%d Passed=%d SurfaceSubdivision=%d PreviewAuthority=%d"),
		LastM3RebuildDurationMS,
		FABTSM3R5AcceptanceManifest::
			FullRebuildBudgetMS,
		LastM3RebuildDurationMS
			<= FABTSM3R5AcceptanceManifest::
				FullRebuildBudgetMS
			? 1
			: 0,
		SurfaceSubdivision,
		bMonthlyPresentationPreviewActive ? 1 : 0);
	return bM3PresentationReady;
}

bool AABTSM3Planet::GenerateLogicalTerrain()
{
	FABTSM3TaskGraphGenerator Generator;
	FABTSM3PCGGeometryContext GeometryContext;
	GeometryContext.PlanetRadiusCM = PlanetRadiusCM;
	GeometryContext.TerrainHeightScaleCM =
		bDisableTerrainHeightVariationExperiment ? 0.0f : MacroHeightScaleCM;
	GeometryContext.BuildingPadHalfExtentCM = BuildingPadSettings.HalfExtentCM;
	GeometryContext.BuildingPadEdgeBlendWidthCM =
		BuildingPadSettings.EdgeBlendWidthCM;
	GeometryContext.TrailHalfWidthCM = TrailVisualHalfWidthCM;
	GeometryContext.MainRoadHalfWidthCM = MainRoadVisualHalfWidthCM;
	const bool bGenerated = Generator.Generate(
		WorldSeed,
		PCGConfig,
		LogicalCells,
		GeneratedTasks,
		GeneratedTaskLinks,
		GeneratedCellStates,
		GeneratedEdgeStates,
		PCGSummary,
		GeometryContext);
	if (!bGenerated)
	{
		MonthlyWorldSchema = FABTSM3MonthlyWorldSchema();
		MonthlyRoutePool = FABTSM3MonthlyRoutePool();
		MonthlySpatialResult = FABTSM3MonthlySpatialResult();
		MonthlyJuryFixedSixLayoutResult =
			FABTSM3JuryFixedSixLayoutResult();
		MonthlyPresentationResult =
			FABTSM3MonthlyPresentationResult();
		MonthlyFinaleAnchorPlanResult =
			FABTSM3MonthlyFinaleAnchorPlanResult();
		MonthlySlingshotFieldResult =
			FABTSM3MonthlySlingshotFieldResult();
		MonthlySatellitePreviewResult =
			FABTSM3MonthlySatellitePreviewResult();
		MonthlyWitnessResult = FABTSM3MonthlyWitnessResult();
#if WITH_EDITORONLY_DATA
		MonthlySchemaDebugData = FABTSM3MonthlySchemaDebugData();
		MonthlyRouteDebugData = FABTSM3MonthlyRouteDebugData();
		MonthlySpatialDebugData = FABTSM3MonthlySpatialDebugData();
		MonthlyPresentationDebugData =
			FABTSM3MonthlyPresentationDebugData();
		MonthlySlingshotFieldDebugData =
			FABTSM3MonthlySlingshotFieldDebugData();
#endif
		return false;
	}

	FString SchemaFailure;
	if (!FABTSM3MonthlySchemaBuilder::Build(
			WorldSeed,
			MonthlySchemaConfig,
			GeneratedTasks,
			GeneratedTaskLinks,
			GeneratedCellStates,
			PCGSummary,
			MonthlyWorldSchema,
			SchemaFailure))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R1][Schema] Build failed. Seed=%d Mode=%s Failure=%s"),
			WorldSeed,
			FABTSM3MonthlySchemaBuilder::GetGenerationModeName(
				MonthlySchemaConfig.Mode),
			*SchemaFailure);
		return false;
	}
#if WITH_EDITORONLY_DATA
	FABTSM3MonthlySchemaBuilder::BuildDebugData(
		MonthlyWorldSchema,
		MonthlySchemaDebugData);
#endif
	FABTSM3MonthlyRoadContext NeutralRouteContext;
	FString RouteFailure;
	if (!FABTSM3MonthlyRouteBuilder::Build(
			WorldSeed,
			MonthlyRouteConfig,
			LogicalCells,
			PlanetRadiusCM,
			NeutralRouteContext,
			MonthlyRoutePool,
			RouteFailure))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R2][Route] Build failed. Seed=%d Reason=%s Failure=%s CompatibilityWorldPreserved=1"),
			WorldSeed,
			FABTSM3MonthlyRouteBuilder::GetRejectReasonName(
				MonthlyRoutePool.RejectReason),
			*RouteFailure);
	}
#if WITH_EDITORONLY_DATA
	FABTSM3MonthlyRouteBuilder::BuildDebugData(
		MonthlyRoutePool,
		MonthlyRouteDebugData);
#endif
	FString SpatialFailure;
	const FABTSM3MonthlySpatialFaultInjection NoSpatialFaults;
	const bool bSpatialBuilt =
		FABTSM3MonthlyEncounterBuilder::Build(
			WorldSeed,
			MonthlyEncounterSpatialConfig,
			MonthlyRouteConfig,
			LogicalCells,
			PlanetRadiusCM,
			MonthlyRoutePool,
			NoSpatialFaults,
			MonthlySpatialResult,
			SpatialFailure);
	if (!bSpatialBuilt)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R3][EncounterSpatial] Build failed. Seed=%d Reason=%s Failure=%s CompatibilityWorldPreserved=1"),
			WorldSeed,
			FABTSM3MonthlyEncounterBuilder::GetRejectReasonName(
				MonthlySpatialResult.RejectReason),
			*SpatialFailure);
	}
#if WITH_EDITORONLY_DATA
	FABTSM3MonthlyEncounterBuilder::BuildDebugData(
		MonthlySpatialResult,
		MonthlySpatialDebugData);
#endif
	MonthlyJuryFixedSixLayoutResult = FABTSM3JuryFixedSixLayoutResult();
	if (bSpatialBuilt
		&& WorldSeed == FABTSM3JuryFixedSixLayoutBuilder::FrozenWorldSeed)
	{
		FString JuryPlacementFailure;
		if (!FABTSM3JuryFixedSixLayoutBuilder::Build(
				LogicalCells,
				PlanetRadiusCM,
				MonthlySpatialResult,
				MonthlyJuryFixedSixLayoutResult,
				JuryPlacementFailure))
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][M3Jury][FixedSix] Placement rejected. Seed=%d Candidate=%d Reason=%s Failure=%s CompatibilityWorldPreserved=1"),
				WorldSeed,
				FABTSM3JuryFixedSixLayoutBuilder::FrozenSourceCandidateId,
				FABTSM3JuryFixedSixLayoutBuilder::GetRejectReasonName(
					MonthlyJuryFixedSixLayoutResult.RejectReason),
				*JuryPlacementFailure);
		}
		else
		{
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][M3Jury][FixedSix] PlacementReady=1 Seed=%d Candidate=%d Buildings=%d M7Manifest=%d:%lld M7Catalog=%016llX LayoutHash=%016llX Authority=M3LocalAccepted"),
				WorldSeed,
				MonthlyJuryFixedSixLayoutResult.SourceCandidateId,
				MonthlyJuryFixedSixLayoutResult.Placements.Num(),
				MonthlyJuryFixedSixLayoutResult.M7SourceManifestVersion,
				MonthlyJuryFixedSixLayoutResult.M7SourceManifestHash,
				static_cast<unsigned long long>(
					static_cast<uint64>(
						MonthlyJuryFixedSixLayoutResult.M7PlacementCatalogHash)),
				static_cast<unsigned long long>(
					static_cast<uint64>(
						MonthlyJuryFixedSixLayoutResult.LayoutHash)));
		}
	}
	MonthlyFinaleAnchorPlanResult =
		FABTSM3MonthlyFinaleAnchorPlanResult();
	FString FinaleAnchorFailure;
	const FABTSM3MonthlyFinaleAnchorConfig ResolvedFinaleAnchorConfig =
		MakeResolvedMonthlyFinaleAnchorConfig();
	const bool bFinaleAnchorBuilt = bSpatialBuilt
		&& FABTSM3MonthlyFinaleAnchorBuilder::Build(
			WorldSeed,
			ResolvedFinaleAnchorConfig,
			LogicalCells,
			MonthlySpatialResult,
			MonthlyFinaleAnchorPlanResult,
			FinaleAnchorFailure);
	if (bSpatialBuilt && !bFinaleAnchorBuilt)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5.2][FinaleAnchorPlan] Build failed. Seed=%d Reason=%s Failure=%s CompatibilityWorldPreserved=1 MonthlyAccepted=0"),
			WorldSeed,
			FABTSM3MonthlyFinaleAnchorBuilder::GetRejectReasonName(
				MonthlyFinaleAnchorPlanResult.RejectReason),
			*FinaleAnchorFailure);
	}
	MonthlyPresentationResult =
		FABTSM3MonthlyPresentationResult();
	FString PresentationFailure;
	if (bSpatialBuilt)
	{
		const double PresentationBuildStartSeconds =
			FPlatformTime::Seconds();
		const bool bPresentationBuilt =
			FABTSM3MonthlyPresentationBuilder::Build(
				WorldSeed,
				MonthlyPresentationConfig,
				MonthlyEncounterSpatialConfig,
				MonthlyRouteConfig,
				LogicalCells,
				PlanetRadiusCM,
				MonthlyRoutePool,
				MonthlySpatialResult,
				FABTSM3MonthlyPresentationFaultInjection(),
				MonthlyPresentationResult,
				PresentationFailure);
		LastMonthlyPresentationBuildDurationMS =
			(FPlatformTime::Seconds()
				- PresentationBuildStartSeconds)
			* 1000.0;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M3R5][PlannerBudget] DurationMS=%.3f BudgetMS=%d Passed=%d CandidatePlans=%d PreviewAuthority=0 MonthlyAccepted=0"),
			LastMonthlyPresentationBuildDurationMS,
			FABTSM3R5AcceptanceManifest::
				PlannerMaxBudgetMS,
			LastMonthlyPresentationBuildDurationMS
				<= FABTSM3R5AcceptanceManifest::
					PlannerMaxBudgetMS
				? 1
				: 0,
			MonthlyPresentationResult
				.CandidatePresentations.Num());
		if (!bPresentationBuilt)
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M3R5][BiomePresentation] Build failed. Seed=%d Reason=%s Failure=%s CompatibilityWorldPreserved=1 MonthlyAccepted=0"),
			WorldSeed,
			FABTSM3MonthlyPresentationBuilder::
				GetRejectReasonName(
					MonthlyPresentationResult.RejectReason),
			*PresentationFailure);
		}
	}
#if WITH_EDITORONLY_DATA
	bool bPresentationPreviewRequested = false;
	const int32 PresentationDebugCandidateId =
		ResolveMonthlyPresentationPreviewCandidateId(
			bPresentationPreviewRequested);
	FABTSM3MonthlyPresentationBuilder::BuildDebugData(
		MonthlyPresentationResult,
		bPresentationPreviewRequested
			? PresentationDebugCandidateId
			: INDEX_NONE,
		MonthlyPresentationDebugData);
#endif
	MonthlySlingshotFieldResult =
		FABTSM3MonthlySlingshotFieldResult();
	FString SlingshotFieldFailure;
	if (bSpatialBuilt
		&& bFinaleAnchorBuilt
		&& !FABTSM3MonthlySlingshotFieldBuilder::Build(
			WorldSeed,
			MonthlySlingshotFieldConfig,
			LogicalCells,
			PlanetRadiusCM,
			MonthlySpatialResult,
			MonthlyFinaleAnchorPlanResult,
			MonthlySlingshotFieldResult,
			SlingshotFieldFailure))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R3.1][SlingshotFields] Build failed. Seed=%d Reason=%s Failure=%s CompatibilityWorldPreserved=1"),
			WorldSeed,
			FABTSM3MonthlySlingshotFieldBuilder::
				GetRejectReasonName(
					MonthlySlingshotFieldResult.RejectReason),
			*SlingshotFieldFailure);
	}
#if WITH_EDITORONLY_DATA
	FABTSM3MonthlySlingshotFieldBuilder::BuildDebugData(
		MonthlySlingshotFieldResult,
		MonthlySlingshotFieldDebugData);
#endif
	FString WitnessFailure;
	if (!FABTSM3MonthlyWitnessBuilder::Build(
			WorldSeed,
			MonthlyWitnessConfig,
			MonthlySpatialResult,
			MonthlySlingshotFieldResult,
			nullptr,
			MonthlyWitnessResult,
			WitnessFailure))
	{
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M3R4][Witness] Pending. Seed=%d Reason=%s Failure=%s CompatibilityWorldPreserved=1"),
			WorldSeed,
			FABTSM3MonthlyWitnessBuilder::GetRejectReasonName(
				MonthlyWitnessResult.RejectReason),
			*WitnessFailure);
	}
	return true;
}

bool AABTSM3Planet::ValidateMonthlyRoutePool(
	FString& OutFailure) const
{
	EABTSM3MonthlyRouteRejectReason RejectReason =
		EABTSM3MonthlyRouteRejectReason::None;
	if (FABTSM3MonthlyRouteBuilder::Validate(
			MonthlyRouteConfig,
			LogicalCells,
			PlanetRadiusCM,
			FABTSM3MonthlyRoadContext(),
			MonthlyRoutePool,
			RejectReason,
			OutFailure))
	{
		return true;
	}
	OutFailure = FString::Printf(
		TEXT("%s:%s"),
		FABTSM3MonthlyRouteBuilder::GetRejectReasonName(
			RejectReason),
		*OutFailure);
	return false;
}

bool AABTSM3Planet::ValidateMonthlySpatialResult(
	FString& OutFailure) const
{
	EABTSM3MonthlySpatialRejectReason RejectReason =
		EABTSM3MonthlySpatialRejectReason::None;
	if (FABTSM3MonthlyEncounterBuilder::Validate(
			MonthlyEncounterSpatialConfig,
			MonthlyRouteConfig,
			LogicalCells,
			PlanetRadiusCM,
			MonthlyRoutePool,
			FABTSM3MonthlySpatialFaultInjection(),
			MonthlySpatialResult,
			RejectReason,
			OutFailure))
	{
		return true;
	}
	OutFailure = FString::Printf(
		TEXT("%s:%s"),
		FABTSM3MonthlyEncounterBuilder::GetRejectReasonName(
			RejectReason),
		*OutFailure);
	return false;
}

FABTSM3MonthlyFinaleAnchorConfig
AABTSM3Planet::MakeResolvedMonthlyFinaleAnchorConfig() const
{
	FABTSM3MonthlyFinaleAnchorConfig Resolved =
		MonthlyFinaleAnchorConfig;
	// M11.0's already serialized pair geometry remains the single Planet-side
	// source while R-5.2 is only a Preview/Test producer.
	Resolved.SlotSeparationCM = ABTSResolveFinaleSpaceStakeSpacingCM(
		FinaleSpaceSlotSeparationCM);
	Resolved.SurfaceOffsetCM = FinaleSpaceSlotSurfaceOffsetCM;
	return Resolved;
}

bool AABTSM3Planet::ValidateMonthlyFinaleAnchorPlanResult(
	FString& OutFailure) const
{
	EABTSM3MonthlyFinaleAnchorRejectReason RejectReason =
		EABTSM3MonthlyFinaleAnchorRejectReason::None;
	if (FABTSM3MonthlyFinaleAnchorBuilder::Validate(
			MakeResolvedMonthlyFinaleAnchorConfig(),
			LogicalCells,
			MonthlySpatialResult,
			MonthlyFinaleAnchorPlanResult,
			RejectReason,
			OutFailure))
	{
		return true;
	}
	OutFailure = FString::Printf(
		TEXT("%s:%s"),
		FABTSM3MonthlyFinaleAnchorBuilder::GetRejectReasonName(
			RejectReason),
		*OutFailure);
	return false;
}

bool AABTSM3Planet::TryBuildMonthlyFinaleAnchorPreview(
	const int32 SourceRouteCandidateId,
	FABTSM3MonthlyFinaleAnchorPreview& OutPreview,
	FString& OutFailure) const
{
	if (TerrainVisualField == nullptr)
	{
		OutPreview = FABTSM3MonthlyFinaleAnchorPreview();
		OutFailure = TEXT("ContinuousSurfaceUnavailable");
		return false;
	}
	const FPlanetMonthlyFinaleAnchorSurface Surface(*this);
	return FABTSM3MonthlyFinaleAnchorBuilder::BuildPreview(
		SourceRouteCandidateId,
		MakeResolvedMonthlyFinaleAnchorConfig(),
		LogicalCells,
		MonthlySpatialResult,
		MonthlyFinaleAnchorPlanResult,
		Surface,
		OutPreview,
		OutFailure);
}

bool AABTSM3Planet::ValidateMonthlyPresentationResult(
	FString& OutFailure) const
{
	EABTSM3MonthlyPresentationRejectReason RejectReason =
		EABTSM3MonthlyPresentationRejectReason::None;
	if (FABTSM3MonthlyPresentationBuilder::Validate(
			MonthlyPresentationConfig,
			MonthlyEncounterSpatialConfig,
			MonthlyRouteConfig,
			LogicalCells,
			PlanetRadiusCM,
			MonthlyRoutePool,
			MonthlySpatialResult,
			FABTSM3MonthlyPresentationFaultInjection(),
			MonthlyPresentationResult,
			RejectReason,
			OutFailure))
	{
		return true;
	}
	OutFailure = FString::Printf(
		TEXT("%s:%s"),
		FABTSM3MonthlyPresentationBuilder::
			GetRejectReasonName(RejectReason),
		*OutFailure);
	return false;
}

int32 AABTSM3Planet::
	ResolveMonthlyPresentationPreviewCandidateId(
		bool& bOutRequested) const
{
	bOutRequested = bEnableMonthlyPresentationPreview
		|| FParse::Param(
			FCommandLine::Get(),
			TEXT("ABTSM3R5Preview"));
	int32 CandidateId =
		MonthlyPresentationPreviewCandidateId;
	int32 CommandLineCandidateId = INDEX_NONE;
	if (FParse::Value(
			FCommandLine::Get(),
			TEXT("ABTSM3R5PreviewCandidate="),
			CommandLineCandidateId))
	{
		bOutRequested = true;
		CandidateId = CommandLineCandidateId;
	}
	return CandidateId;
}

bool AABTSM3Planet::TryBuildMonthlyPresentationPreviewData(
	TArray<FABTSM3CellState>& OutCellStates,
	TArray<FABTSM3CellEdgeState>& OutEdgeStates,
	const FABTSM3MonthlyCandidatePresentation*&
		OutCandidate)
{
	OutCellStates.Reset();
	OutEdgeStates.Reset();
	OutCandidate = nullptr;
	bMonthlyPresentationPreviewActive = false;
	ActiveMonthlyPresentationPreviewCandidateId = INDEX_NONE;
	ActiveMonthlyPresentationPreviewCandidateHash = 0;
	ActiveMonthlyFinaleAnchorPreview =
		FABTSM3MonthlyFinaleAnchorPreview();

	bool bPreviewRequested = false;
	const int32 CandidateId =
		ResolveMonthlyPresentationPreviewCandidateId(
			bPreviewRequested);
	if (!bPreviewRequested)
	{
		return false;
	}
	if (CandidateId == INDEX_NONE)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5][Preview] Rejected Reason=ExplicitCandidateRequired MonthlyAccepted=0 CompatibilityWorldPreserved=1"));
		return false;
	}
	if (!MonthlyPresentationResult.bPresentationValid
		|| MonthlyPresentationResult.bMonthlyWorldAccepted)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5][Preview] Rejected Candidate=%d Reason=PresentationResultInvalid MonthlyAccepted=0 CompatibilityWorldPreserved=1"),
			CandidateId);
		return false;
	}
	const FABTSM3MonthlyCandidatePresentation* Presentation =
		FABTSM3MonthlyPresentationBuilder::
			FindCandidatePresentation(
				MonthlyPresentationResult,
				CandidateId);
	const FABTSM3MonthlySpatialCandidate* Spatial =
		MonthlySpatialResult.RetainedCandidates
			.FindByPredicate(
				[CandidateId](
					const FABTSM3MonthlySpatialCandidate&
						Candidate)
				{
					return Candidate.SourceRouteCandidateId
						== CandidateId;
				});
	if (Presentation == nullptr
		|| Spatial == nullptr
		|| Presentation->SourceSpatialCandidateHash
			!= Spatial->SpatialCandidateHash
		|| Presentation->SourceRecomputedRouteCandidateHash
			!= Spatial->RecomputedRoute.CandidateHash
		|| Presentation->Cells.Num()
			!= GeneratedCellStates.Num())
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5][Preview] Rejected Candidate=%d Reason=CandidateJoinMismatch MonthlyAccepted=0 CompatibilityWorldPreserved=1"),
			CandidateId);
		return false;
	}
	FABTSM3MonthlyFinaleAnchorPreview FinaleAnchorPreview;
	FString FinaleAnchorPreviewFailure;
	if (!TryBuildMonthlyFinaleAnchorPreview(
			CandidateId,
			FinaleAnchorPreview,
			FinaleAnchorPreviewFailure))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3R5.2][FinaleAnchorPreview] Rejected Candidate=%d Reason=%s MonthlyAccepted=0 CompatibilityWorldPreserved=1"),
			CandidateId,
			*FinaleAnchorPreviewFailure);
		return false;
	}

	OutCellStates = GeneratedCellStates;
	for (const FABTSM3MonthlyPresentationCell& Cell :
		Presentation->Cells)
	{
		if (!OutCellStates.IsValidIndex(Cell.CellId))
		{
			OutCellStates.Reset();
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M3R5][Preview] Rejected Candidate=%d Reason=CellIdentity Cell=%d MonthlyAccepted=0 CompatibilityWorldPreserved=1"),
				CandidateId,
				Cell.CellId);
			return false;
		}
		FABTSM3CellState& State =
			OutCellStates[Cell.CellId];
		State.TerrainType = Cell.VisualTerrainType;
		State.bWater = Cell.bWater;
		State.bRoad =
			(Cell.ActiveRoleMask
				& static_cast<int32>(
					EABTSM3ActiveRole::Route))
			!= 0;
	}

	OutEdgeStates = GeneratedEdgeStates;
	TMap<FABTSM3CellEdgeKey, int32> EdgeIndexByKey;
	EdgeIndexByKey.Reserve(OutEdgeStates.Num());
	for (int32 EdgeIndex = 0;
		EdgeIndex < OutEdgeStates.Num();
		++EdgeIndex)
	{
		OutEdgeStates[EdgeIndex].Transport =
			EABTSM3TransportType::None;
		EdgeIndexByKey.Add(
			OutEdgeStates[EdgeIndex].Key,
			EdgeIndex);
	}
	for (int32 Order = 1;
		Order < Spatial->RecomputedRoute
			.OrderedRoadCellIds.Num();
		++Order)
	{
		const FABTSM3CellEdgeKey Key(
			Spatial->RecomputedRoute
				.OrderedRoadCellIds[Order - 1],
			Spatial->RecomputedRoute
				.OrderedRoadCellIds[Order]);
		const int32* EdgeIndex =
			EdgeIndexByKey.Find(Key);
		if (EdgeIndex == nullptr)
		{
			FABTSM3CellEdgeState& Added =
				OutEdgeStates.AddDefaulted_GetRef();
			Added.Key = Key;
			Added.Transport =
				EABTSM3TransportType::MainRoad;
			EdgeIndexByKey.Add(
				Key,
				OutEdgeStates.Num() - 1);
			continue;
		}
		if (!OutEdgeStates.IsValidIndex(*EdgeIndex))
		{
			OutCellStates.Reset();
			OutEdgeStates.Reset();
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M3R5][Preview] Rejected Candidate=%d Reason=RouteEdgeIndex Edge=(%d,%d) MonthlyAccepted=0 CompatibilityWorldPreserved=1"),
				CandidateId,
				Key.CellA,
				Key.CellB);
			return false;
		}
		OutEdgeStates[*EdgeIndex].Transport =
			EABTSM3TransportType::MainRoad;
	}

	OutCandidate = Presentation;
	bMonthlyPresentationPreviewActive = true;
	ActiveMonthlyPresentationPreviewCandidateId =
		CandidateId;
	ActiveMonthlyPresentationPreviewCandidateHash =
		Presentation->CandidatePresentationHash;
	ActiveMonthlyFinaleAnchorPreview = FinaleAnchorPreview;
	const FABTSM3MonthlySatellitePreviewCandidate* SatellitePreview =
		FABTSM3MonthlySatellitePreviewBuilder::FindCandidate(
			MonthlySatellitePreviewResult,
			CandidateId);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R5][Preview] PreviewAuthority=1 MonthlyAccepted=0 Candidate=%d SourceSpatialCandidate=%016llX PresentationCandidate=%016llX SatellitePreviewCandidate=%016llX SatelliteE5=%d Cells=%d RoadCells=%d"),
		CandidateId,
		static_cast<unsigned long long>(
			Presentation->SourceSpatialCandidateHash),
		static_cast<unsigned long long>(
			Presentation->CandidatePresentationHash),
		static_cast<unsigned long long>(
			SatellitePreview != nullptr
				? static_cast<uint64>(SatellitePreview->CandidateHash)
				: 0ull),
		SatellitePreview != nullptr ? 1 : 0,
		Presentation->Cells.Num(),
		Spatial->RecomputedRoute.OrderedRoadCellIds.Num());
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3R5.2][FinaleAnchorPreview] PreviewAuthority=1 MonthlyAccepted=0 Candidate=%d TerminalCell=%d AnchorCell=%d LeftCell=%d RightCell=%d SeparationCM=%.2f SlopeDeg=%.2f Forward=%s Right=%s Up=%s Plan=%016llX Preview=%016llX"),
		CandidateId,
		FinaleAnchorPreview.RoadTerminalCellId,
		FinaleAnchorPreview.AnchorCellId,
		FinaleAnchorPreview.LeftSlotNearestCellId,
		FinaleAnchorPreview.RightSlotNearestCellId,
		FinaleAnchorPreview.ActualSlotSeparationCM,
		FinaleAnchorPreview.MaxResolvedSurfaceSlopeDegrees,
		*FinaleAnchorPreview.ForwardWorld.ToCompactString(),
		*FinaleAnchorPreview.RightWorld.ToCompactString(),
		*FinaleAnchorPreview.UpWorld.ToCompactString(),
		static_cast<unsigned long long>(
			static_cast<uint64>(
				FinaleAnchorPreview.SourcePlanResultHash)),
		static_cast<unsigned long long>(
			static_cast<uint64>(FinaleAnchorPreview.PreviewHash)));
	return true;
}

bool AABTSM3Planet::ValidateMonthlySlingshotFieldResult(
	FString& OutFailure) const
{
	EABTSM3MonthlySlingshotFieldRejectReason RejectReason =
		EABTSM3MonthlySlingshotFieldRejectReason::None;
	if (FABTSM3MonthlySlingshotFieldBuilder::Validate(
			MonthlySlingshotFieldConfig,
			LogicalCells,
			PlanetRadiusCM,
			MonthlySpatialResult,
			MonthlyFinaleAnchorPlanResult,
			MonthlySlingshotFieldResult,
			RejectReason,
			OutFailure))
	{
		return true;
	}
	OutFailure = FString::Printf(
		TEXT("%s:%s"),
		FABTSM3MonthlySlingshotFieldBuilder::
			GetRejectReasonName(RejectReason),
		*OutFailure);
	return false;
}

bool AABTSM3Planet::ValidateMonthlySatellitePreviewResult(
	FString& OutFailure) const
{
	EABTSM3MonthlySatellitePreviewRejectReason RejectReason =
		EABTSM3MonthlySatellitePreviewRejectReason::None;
	const FPlanetMonthlySatellitePreviewSurface Surface(
		*this,
		LogicalCells);
	if (FABTSM3MonthlySatellitePreviewBuilder::Validate(
			MonthlySatellitePreviewConfig,
			LogicalCells,
			MonthlySpatialResult,
			MonthlySlingshotFieldResult,
			Surface,
			MonthlySatellitePreviewResult,
			RejectReason,
			OutFailure))
	{
		return true;
	}
	OutFailure = FString::Printf(
		TEXT("%s:%s"),
		FABTSM3MonthlySatellitePreviewBuilder::
			GetRejectReasonName(RejectReason),
		*OutFailure);
	return false;
}

bool AABTSM3Planet::ValidateMonthlyWitnessResult(
	FString& OutFailure) const
{
	EABTSM3MonthlyWitnessRejectReason RejectReason =
		EABTSM3MonthlyWitnessRejectReason::None;
	if (FABTSM3MonthlyWitnessBuilder::Validate(
			MonthlyWitnessConfig,
			MonthlySpatialResult,
			MonthlySlingshotFieldResult,
			MonthlyWitnessResult,
			RejectReason,
			OutFailure))
	{
		return true;
	}
	OutFailure = FString::Printf(
		TEXT("%s:%s"),
		FABTSM3MonthlyWitnessBuilder::GetRejectReasonName(
			RejectReason),
		*OutFailure);
	return false;
}

bool AABTSM3Planet::FinalizeMonthlyGameplay(
	const IABTSM3MonthlyWitnessServices& Services,
	FString& OutFailure)
{
	return FABTSM3MonthlyWitnessBuilder::Build(
		WorldSeed,
		MonthlyWitnessConfig,
		MonthlySpatialResult,
		MonthlySlingshotFieldResult,
		&Services,
		MonthlyWitnessResult,
		OutFailure);
}

#if WITH_EDITOR
void AABTSM3Planet::DrawMonthlyRouteDebugOverlay() const
{
	if (GetWorld() == nullptr
		|| MonthlyRouteDebugData.BestRouteCellIds.Num() < 2)
	{
		return;
	}
	const FVector Center = GetPlanetCenterWorld();
	const float Radius = PlanetRadiusCM + 180.0f;
	for (int32 Index = 1;
		Index < MonthlyRouteDebugData.BestRouteCellIds.Num();
		++Index)
	{
		const int32 CellA =
			MonthlyRouteDebugData.BestRouteCellIds[Index - 1];
		const int32 CellB =
			MonthlyRouteDebugData.BestRouteCellIds[Index];
		if (!LogicalCells.IsValidIndex(CellA)
			|| !LogicalCells.IsValidIndex(CellB))
		{
			continue;
		}
		DrawDebugLine(
			GetWorld(),
			Center + LogicalCells[CellA].UnitCenter * Radius,
			Center + LogicalCells[CellB].UnitCenter * Radius,
			FColor::Cyan,
			false,
			30.0f,
			0,
			8.0f);
	}
	for (const int32 ControlCellId :
		MonthlyRouteDebugData.BestControlCellIds)
	{
		if (!LogicalCells.IsValidIndex(ControlCellId))
		{
			continue;
		}
		DrawDebugSphere(
			GetWorld(),
			Center
				+ LogicalCells[ControlCellId].UnitCenter * Radius,
			45.0f,
			8,
			FColor::Yellow,
			false,
			30.0f,
			0,
			3.0f);
	}
}

void AABTSM3Planet::DrawMonthlySpatialDebugOverlay() const
{
	if (GetWorld() == nullptr
		|| MonthlySpatialResult.RetainedCandidates.IsEmpty())
	{
		return;
	}
	const FVector Center = GetPlanetCenterWorld();
	const float Radius = PlanetRadiusCM + 210.0f;
	const auto DrawCellSet = [this, &Center, Radius](
		const TArray<int32>& CellIds,
		const FColor Color,
		const float SphereRadius)
	{
		for (const int32 CellId : CellIds)
		{
			if (!LogicalCells.IsValidIndex(CellId))
			{
				continue;
			}
			DrawDebugSphere(
				GetWorld(),
				Center + LogicalCells[CellId].UnitCenter * Radius,
				SphereRadius,
				6,
				Color,
				false,
				30.0f,
				0,
				2.0f);
		}
	};
	DrawCellSet(
		MonthlySpatialDebugData.PlayableEnvelopeCellIds,
		FColor(40, 80, 200),
		12.0f);
	DrawCellSet(
		MonthlySpatialDebugData.NoRoadCellIds,
		FColor::Red,
		18.0f);
	DrawCellSet(
		MonthlySpatialDebugData.RoadArrivalCellIds,
		FColor::White,
		42.0f);
	DrawCellSet(
		MonthlySpatialDebugData.RevealCellIds,
		FColor::Cyan,
		42.0f);
	DrawCellSet(
		MonthlySpatialDebugData.SlingshotCellIds,
		FColor::Yellow,
		42.0f);
	DrawCellSet(
		MonthlySlingshotFieldDebugData.EncounterSlotCellIds,
		FColor(255, 170, 0),
		24.0f);
	DrawCellSet(
		MonthlySlingshotFieldDebugData.RoadSlotCellIds,
		FColor(160, 60, 255),
		24.0f);
	DrawCellSet(
		MonthlySlingshotFieldDebugData.FieldAnchorCellIds,
		FColor::Green,
		34.0f);
	DrawCellSet(
		MonthlySpatialDebugData.TargetAnchorCellIds,
		FColor::Red,
		58.0f);
}

void AABTSM3Planet::
	DrawMonthlyPresentationDebugOverlay() const
{
	if (GetWorld() == nullptr
		|| !MonthlyPresentationResult.bPresentationValid)
	{
		return;
	}
	const FABTSM3MonthlyCandidatePresentation* Candidate =
		FABTSM3MonthlyPresentationBuilder::
			FindCandidatePresentation(
				MonthlyPresentationResult,
				MonthlyPresentationDebugData
					.SourceRouteCandidateId);
	if (Candidate == nullptr)
	{
		return;
	}
	const FVector Center = GetPlanetCenterWorld();
	const float Radius = PlanetRadiusCM + 250.0f;
	for (const FABTSM3MonthlyPresentationCell& Cell :
		Candidate->Cells)
	{
		if (!LogicalCells.IsValidIndex(Cell.CellId))
		{
			continue;
		}
		FColor Color = FColor(60, 60, 60);
		switch (Cell.DisplayBiomeArchetype)
		{
		case EABTSM3BiomeArchetype::Forest:
			Color = FColor(30, 150, 50);
			break;
		case EABTSM3BiomeArchetype::Highland:
			Color = FColor(180, 120, 50);
			break;
		case EABTSM3BiomeArchetype::Mountain:
			Color = FColor(160, 160, 170);
			break;
		case EABTSM3BiomeArchetype::Water:
			Color = FColor(30, 100, 220);
			break;
		case EABTSM3BiomeArchetype::Plain:
			Color = FColor(90, 190, 80);
			break;
		case EABTSM3BiomeArchetype::Background:
		default:
			break;
		}
		if (bDrawMonthlyPresentationBiomeLayer)
		{
			DrawDebugPoint(
				GetWorld(),
				Center
					+ LogicalCells[Cell.CellId].UnitCenter
						* Radius,
				Cell.bPlayable ? 9.0f : 4.0f,
				Color,
				false,
				30.0f,
				0);
		}
		if (bDrawMonthlyPresentationCoverageLayer
			&& (Cell.bDeepWild
				|| Cell.ActiveRoleMask != 0))
		{
			DrawDebugPoint(
				GetWorld(),
				Center
					+ LogicalCells[Cell.CellId].UnitCenter
						* (Radius + 30.0f),
				Cell.bDeepWild ? 13.0f : 10.0f,
				Cell.bDeepWild
					? FColor::Magenta
					: FColor::Cyan,
				false,
				30.0f,
				0);
		}
		for (const int32 NeighborId :
			LogicalCells[Cell.CellId].NeighborCellIds)
		{
			if (NeighborId <= Cell.CellId
				|| !Candidate->Cells.IsValidIndex(
					NeighborId)
				|| !LogicalCells.IsValidIndex(NeighborId))
			{
				continue;
			}
			const FABTSM3MonthlyPresentationCell&
				Neighbor = Candidate->Cells[NeighborId];
			const FVector StartDirection =
				LogicalCells[Cell.CellId].UnitCenter;
			const FVector EndDirection =
				LogicalCells[NeighborId].UnitCenter;
			if (bDrawMonthlyPresentationEnvelopeLayer
				&& Cell.EnvelopeIds
					!= Neighbor.EnvelopeIds)
			{
				DrawDebugLine(
					GetWorld(),
					Center + StartDirection
						* (Radius + 55.0f),
					Center + EndDirection
						* (Radius + 55.0f),
					FColor::Yellow,
					false,
					30.0f,
					0,
					2.0f);
			}
			if (bDrawMonthlyPresentationVisualBeatLayer
				&& Cell.VisualBeatId
					!= Neighbor.VisualBeatId)
			{
				DrawDebugLine(
					GetWorld(),
					Center + StartDirection
						* (Radius + 80.0f),
					Center + EndDirection
						* (Radius + 80.0f),
					FColor(210, 90, 255),
					false,
					30.0f,
					0,
					2.0f);
			}
		}
	}
}

bool AABTSM3Planet::DrawMonthlyLogicRegionDebugOverlay(
	const float LifeTimeSeconds,
	int32& OutTargetFootprintCellCount,
	int32& OutAttackCorridorCellCount,
	bool& bOutSatelliteE5PreviewDrawn) const
{
	OutTargetFootprintCellCount = 0;
	OutAttackCorridorCellCount = 0;
	bOutSatelliteE5PreviewDrawn = false;
	if (GetWorld() == nullptr
		|| !MonthlyPresentationResult.bPresentationValid)
	{
		return false;
	}
	const FABTSM3MonthlyCandidatePresentation* Candidate =
		FABTSM3MonthlyPresentationBuilder::
			FindCandidatePresentation(
				MonthlyPresentationResult,
				MonthlyPresentationDebugData
					.SourceRouteCandidateId);
	if (Candidate == nullptr)
	{
		return false;
	}
	const int32 CandidateId =
		MonthlyPresentationDebugData.SourceRouteCandidateId;
	const FABTSM3MonthlySatellitePreviewCandidate* SatellitePreview =
		FABTSM3MonthlySatellitePreviewBuilder::FindCandidate(
			MonthlySatellitePreviewResult,
			CandidateId);
	TSet<int32> LegacyE5TargetCells;
	const FABTSM3MonthlySpatialCandidate* SpatialCandidate =
		MonthlySpatialResult.RetainedCandidates.FindByPredicate(
			[CandidateId](const FABTSM3MonthlySpatialCandidate& Value)
			{
				return Value.SourceRouteCandidateId == CandidateId;
			});
	if (SpatialCandidate != nullptr)
	{
		const FABTSM3MonthlySpatialEncounter* E5 =
			SpatialCandidate->Encounters.FindByPredicate(
				[](const FABTSM3MonthlySpatialEncounter& Encounter)
				{
					return Encounter.Contract.OrderIndex == 4;
				});
		if (E5 != nullptr)
		{
			LegacyE5TargetCells.Append(E5->TargetFootprintCellIds);
		}
	}

	const FVector Center = GetPlanetCenterWorld();
	const float Radius = PlanetRadiusCM + 360.0f;
	const float DrawLifeTime =
		FMath::Clamp(LifeTimeSeconds, 0.05f, 5.0f);
	bool bFinaleAnchorPreviewDrawn = false;
	const FABTSM3MonthlyFinaleAnchorPlanCandidate* FinalePlan =
		FABTSM3MonthlyFinaleAnchorBuilder::FindCandidate(
			MonthlyFinaleAnchorPlanResult,
			CandidateId);
	if (FinalePlan != nullptr
		&& ActiveMonthlyFinaleAnchorPreview.bPreviewValid
		&& ActiveMonthlyFinaleAnchorPreview.SourceRouteCandidateId
			== CandidateId
		&& ActiveMonthlyFinaleAnchorPreview.SourcePlanCandidateHash
			== FinalePlan->CandidateHash)
	{
		for (const int32 ClearanceCellId : FinalePlan->ClearanceCellIds)
		{
			if (!LogicalCells.IsValidIndex(ClearanceCellId))
			{
				continue;
			}
			const FVector Direction =
				LogicalCells[ClearanceCellId].UnitCenter.GetSafeNormal();
			const FVector ClearanceWorld = Center
				+ Direction
					* (GetSurfaceRadiusAtDirection(Direction) + 75.0f);
			DrawDebugPoint(
				GetWorld(),
				ClearanceWorld,
				14.0f,
				FColor(210, 90, 255),
				false,
				DrawLifeTime,
				0);
		}

		const FABTSM3MonthlyFinaleAnchorPreview& FinalePreview =
			ActiveMonthlyFinaleAnchorPreview;
		const float AxisLengthCM = 360.0f;
		DrawDebugSphere(
			GetWorld(),
			FinalePreview.AnchorSurfaceWorld,
			55.0f,
			10,
			FColor::White,
			false,
			DrawLifeTime,
			0,
			6.0f);
		DrawDebugSphere(
			GetWorld(),
			FinalePreview.LeftSlotWorldLocation,
			75.0f,
			10,
			FColor::Cyan,
			false,
			DrawLifeTime,
			0,
			7.0f);
		DrawDebugSphere(
			GetWorld(),
			FinalePreview.RightSlotWorldLocation,
			75.0f,
			10,
			FColor::Cyan,
			false,
			DrawLifeTime,
			0,
			7.0f);
		DrawDebugLine(
			GetWorld(),
			FinalePreview.LeftSlotWorldLocation,
			FinalePreview.RightSlotWorldLocation,
			FColor::Cyan,
			false,
			DrawLifeTime,
			0,
			7.0f);
		DrawDebugDirectionalArrow(
			GetWorld(),
			FinalePreview.FrameOriginWorld,
			FinalePreview.FrameOriginWorld
				+ FinalePreview.ForwardWorld * AxisLengthCM,
			45.0f,
			FColor::Red,
			false,
			DrawLifeTime,
			0,
			6.0f);
		DrawDebugDirectionalArrow(
			GetWorld(),
			FinalePreview.FrameOriginWorld,
			FinalePreview.FrameOriginWorld
				+ FinalePreview.RightWorld * AxisLengthCM,
			45.0f,
			FColor::Green,
			false,
			DrawLifeTime,
			0,
			6.0f);
		DrawDebugDirectionalArrow(
			GetWorld(),
			FinalePreview.FrameOriginWorld,
			FinalePreview.FrameOriginWorld
				+ FinalePreview.UpWorld * AxisLengthCM,
			45.0f,
			FColor::Blue,
			false,
			DrawLifeTime,
			0,
			6.0f);
		DrawDebugString(
			GetWorld(),
			FinalePreview.FrameOriginWorld
				+ FinalePreview.UpWorld * 150.0f,
			FString::Printf(
				TEXT("M11 FINALE ANCHOR C=%d TERM=%d SEP=%.1f"),
				FinalePreview.AnchorCellId,
				FinalePreview.RoadTerminalCellId,
				FinalePreview.ActualSlotSeparationCM),
			nullptr,
			FColor::Cyan,
			DrawLifeTime,
			false,
			1.1f);
		bFinaleAnchorPreviewDrawn = true;
	}
	for (const FABTSM3MonthlyPresentationCell& Cell :
		Candidate->Cells)
	{
		if (!LogicalCells.IsValidIndex(Cell.CellId))
		{
			continue;
		}
		const FVector Position =
			Center
			+ LogicalCells[Cell.CellId].UnitCenter
				* Radius;
		if (Cell.bTargetFootprint
			&& !LegacyE5TargetCells.Contains(Cell.CellId))
		{
			++OutTargetFootprintCellCount;
			DrawDebugSphere(
				GetWorld(),
				Position,
				26.0f,
				6,
				FColor(255, 45, 45),
				false,
				DrawLifeTime,
				0,
				3.0f);
		}
		if (!Cell.bAttackCorridor)
		{
			continue;
		}
		++OutAttackCorridorCellCount;
		DrawDebugPoint(
			GetWorld(),
			Position,
			18.0f,
			FColor(255, 165, 0),
			false,
			DrawLifeTime,
			0);
		for (const int32 NeighborId :
			LogicalCells[Cell.CellId].NeighborCellIds)
		{
			if (NeighborId <= Cell.CellId
				|| !Candidate->Cells.IsValidIndex(
					NeighborId)
				|| !LogicalCells.IsValidIndex(
					NeighborId)
				|| !Candidate->Cells[NeighborId]
					.bAttackCorridor)
			{
				continue;
			}
			DrawDebugLine(
				GetWorld(),
				Position,
				Center
					+ LogicalCells[NeighborId].UnitCenter
						* Radius,
				FColor(255, 165, 0),
				false,
				DrawLifeTime,
				0,
				5.0f);
		}
	}
	if (SatellitePreview != nullptr
		&& SatellitePreview->bE5OnSatelliteBackside
		&& LogicalCells.IsValidIndex(
			SatellitePreview->ReferenceSlotACellId)
		&& LogicalCells.IsValidIndex(
			SatellitePreview->ReferenceSlotBCellId))
	{
		FABTSM3MonthlySatelliteSurfaceSample SlotA;
		FABTSM3MonthlySatelliteSurfaceSample SlotB;
		const FPlanetMonthlySatellitePreviewSurface Surface(
			*this,
			LogicalCells);
		if (Surface.QuerySurface(
				LogicalCells[SatellitePreview->ReferenceSlotACellId].UnitCenter,
				SlotA)
			&& Surface.QuerySurface(
				LogicalCells[SatellitePreview->ReferenceSlotBCellId].UnitCenter,
				SlotB))
		{
			FVector DrawSatelliteCenter = SatellitePreview->SatelliteCenterWorld;
			float DrawSatelliteRadiusCM = SatellitePreview->SatelliteRadiusCM;
			FTransform DrawE5Transform = SatellitePreview->E5TargetWorldTransform;
			FVector DrawE5HalfExtentCM = SatellitePreview->E5TargetHalfExtentCM;
			FVector DrawLaunchWorld = SatellitePreview->LaunchWorldLocation;
			float DrawFacingErrorDegrees = -1.0f;
			bool bDrawTrajectoryCertified = false;
			int32 DrawGravityDependentHits = 0;
			int32 DrawSuccessIslandSamples = 0;
			for (TActorIterator<AABTSM3MonthlySatellitePracticeRuntime> It(GetWorld()); It; ++It)
			{
				const FABTSM3MonthlySatelliteRuntimeSnapshot& Runtime =
					It->GetRuntimeSnapshot();
				if (!Runtime.bValid
					|| Runtime.SourceRouteCandidateId != CandidateId)
				{
					continue;
				}
				SlotA.WorldLocation = Runtime.PracticeStakeASurfaceWorld;
				SlotB.WorldLocation = Runtime.PracticeStakeBSurfaceWorld;
				DrawLaunchWorld = Runtime.PracticeLaunchWorldTransform.GetLocation();
				DrawSatelliteCenter = Runtime.SatelliteWorldTransform.GetLocation();
				DrawSatelliteRadiusCM = Runtime.SatelliteRadiusCM;
				DrawE5Transform = Runtime.E5WorldTransform;
				DrawE5HalfExtentCM = Runtime.E5HalfExtentCM;
				DrawFacingErrorDegrees = Runtime.SatelliteFacingErrorDegrees;
				bDrawTrajectoryCertified = Runtime.bTrajectoryCertified;
				DrawGravityDependentHits = Runtime.GravityDependentHits;
				DrawSuccessIslandSamples = Runtime.LargestSuccessIslandSamples;
				break;
			}
			const FVector DrawSatelliteOutward =
				(DrawSatelliteCenter - Center).GetSafeNormal();
			DrawDebugSphere(
				GetWorld(),
				DrawSatelliteCenter,
				DrawSatelliteRadiusCM,
				24,
				FColor(125, 175, 220),
				false,
				DrawLifeTime,
				0,
				8.0f);
			DrawDebugLine(
				GetWorld(),
				SlotA.WorldLocation + SlotA.WorldNormal * 70.0f,
				SlotB.WorldLocation + SlotB.WorldNormal * 70.0f,
				FColor::Yellow,
				false,
				DrawLifeTime,
				0,
				8.0f);
			DrawDebugSphere(GetWorld(), SlotA.WorldLocation, 90.0f, 8,
				FColor::Yellow, false, DrawLifeTime, 0, 5.0f);
			DrawDebugSphere(GetWorld(), SlotB.WorldLocation, 90.0f, 8,
				FColor::Yellow, false, DrawLifeTime, 0, 5.0f);
			DrawDebugPoint(
				GetWorld(),
				DrawLaunchWorld,
				28.0f,
				FColor::Green,
				false,
				DrawLifeTime,
				0);
			DrawDebugLine(
				GetWorld(),
				DrawLaunchWorld,
				DrawSatelliteCenter,
				FColor(80, 220, 255),
				false,
				DrawLifeTime,
				0,
				6.0f);
			if (DrawFacingErrorDegrees >= 0.0f)
			{
				DrawDebugString(
					GetWorld(),
					DrawLaunchWorld,
					FString::Printf(
						TEXT("SAT FACING %.2f deg"),
						DrawFacingErrorDegrees),
					nullptr,
					DrawFacingErrorDegrees <= 5.0f
						? FColor::Green
						: FColor::Red,
					DrawLifeTime,
					false,
					1.1f);
				DrawDebugString(
					GetWorld(),
					DrawLaunchWorld
						+ (DrawLaunchWorld - Center).GetSafeNormal() * 140.0f,
					FString::Printf(
						TEXT("SAT TRAJECTORY %s DEP=%d ISLAND=%d"),
						bDrawTrajectoryCertified ? TEXT("PASS") : TEXT("FAIL"),
						DrawGravityDependentHits,
						DrawSuccessIslandSamples),
					nullptr,
					bDrawTrajectoryCertified ? FColor::Green : FColor::Red,
					DrawLifeTime,
					false,
					1.1f);
			}
			DrawDebugBox(
				GetWorld(),
				DrawE5Transform.GetLocation(),
				DrawE5HalfExtentCM,
				DrawE5Transform.GetRotation(),
				FColor::Magenta,
				false,
				DrawLifeTime,
				0,
				8.0f);
			DrawDebugLine(
				GetWorld(),
				DrawSatelliteCenter,
				DrawE5Transform.GetLocation(),
				FColor::Magenta,
				false,
				DrawLifeTime,
				0,
				5.0f);
			DrawDebugString(
				GetWorld(),
				DrawSatelliteCenter
					+ DrawSatelliteOutward
						* (DrawSatelliteRadiusCM + 300.0f),
				TEXT("M9 PRACTICE SATELLITE"),
				nullptr,
				FColor(125, 175, 220),
				DrawLifeTime,
				false,
				1.2f);
			DrawDebugString(
				GetWorld(),
				DrawE5Transform.GetLocation(),
				TEXT("E5 BACKSIDE TARGET PROXY"),
				nullptr,
				FColor::Magenta,
				DrawLifeTime,
				false,
				1.2f);
			bOutSatelliteE5PreviewDrawn = true;
		}
	}
	return (OutTargetFootprintCellCount > 0
		&& OutAttackCorridorCellCount > 0)
		|| bOutSatelliteE5PreviewDrawn
		|| bFinaleAnchorPreviewDrawn;
}
#endif

float AABTSM3Planet::GetSurfaceRadiusAtDirection(const FVector& UnitDirection) const
{
	return TerrainVisualField && TerrainVisualField->IsReady()
		? TerrainVisualField->GetSurfaceRadius(UnitDirection)
		: Super::GetSurfaceRadiusAtDirection(UnitDirection);
}

FVector AABTSM3Planet::GetSurfaceNormalAtDirection(const FVector& UnitDirection) const
{
	return TerrainVisualField && TerrainVisualField->IsReady()
		? TerrainVisualField->GetSurfaceNormal(UnitDirection)
		: Super::GetSurfaceNormalAtDirection(UnitDirection);
}

int32 AABTSM3Planet::FindNearestCell(const FVector& UnitDirection) const
{
	return TerrainVisualField && TerrainVisualField->IsReady() ? TerrainVisualField->FindNearestCell(UnitDirection) : INDEX_NONE;
}

bool AABTSM3Planet::QuerySurface(
	const FVector& UnitDirection,
	FVector& OutWorldPosition,
	FVector& OutWorldNormal,
	float& OutSurfaceRadius,
	int32& OutCellId) const
{
	if (!TerrainVisualField || !TerrainVisualField->IsReady() || UnitDirection.IsNearlyZero()) return false;
	const FVector Direction = UnitDirection.GetSafeNormal();
	OutSurfaceRadius = TerrainVisualField->GetSurfaceRadius(Direction);
	OutWorldPosition = GetPlanetCenterWorld() + Direction * OutSurfaceRadius;
	OutWorldNormal = TerrainVisualField->GetSurfaceNormal(Direction);
	OutCellId = TerrainVisualField->FindNearestCell(Direction);
	return OutCellId != INDEX_NONE;
}

bool AABTSM3Planet::QuerySurfacePhysics(const FVector& UnitDirection, FABTSM3SurfacePhysicsSample& OutSample) const
{
	OutSample = FABTSM3SurfacePhysicsSample();
	if (!TerrainVisualField || !TerrainVisualField->QuerySurfaceSDF(UnitDirection, SurfacePhysicsBlendWidthCM, OutSample.SDF)) return false;

	const auto ResolveProfile = [this](const EABTSM3TerrainType Type) -> const FABTSM3SurfacePhysicsProfile&
	{
		switch (Type)
		{
		case EABTSM3TerrainType::Forest: return ForestPhysics;
		case EABTSM3TerrainType::Highland: return HighlandPhysics;
		case EABTSM3TerrainType::Mountain: return MountainPhysics;
		default: return PlainPhysics;
		}
	};
	const FABTSM3SurfacePhysicsProfile& Primary = ResolveProfile(OutSample.SDF.PrimaryTerrain);
	const FABTSM3SurfacePhysicsProfile& Secondary = ResolveProfile(OutSample.SDF.SecondaryTerrain);
	const float PrimaryWeight = FMath::Clamp(OutSample.SDF.PrimaryTerrainWeight, 0.0f, 1.0f);
	OutSample.GroundDragPerSecond = FMath::Lerp(Secondary.GroundDragPerSecond, Primary.GroundDragPerSecond, PrimaryWeight);
	OutSample.Restitution = FMath::Lerp(Secondary.Restitution, Primary.Restitution, PrimaryWeight);
	OutSample.GroundDragPerSecond = FMath::Lerp(OutSample.GroundDragPerSecond, RoadPhysics.GroundDragPerSecond, OutSample.SDF.RoadWeight);
	OutSample.Restitution = FMath::Lerp(OutSample.Restitution, RoadPhysics.Restitution, OutSample.SDF.RoadWeight);
	OutSample.GroundDragPerSecond = FMath::Lerp(OutSample.GroundDragPerSecond, RiverPhysics.GroundDragPerSecond, OutSample.SDF.RiverWeight);
	OutSample.Restitution = FMath::Lerp(OutSample.Restitution, RiverPhysics.Restitution, OutSample.SDF.RiverWeight);
	return true;
}

bool AABTSM3Planet::QueryScoutMapTerrainColor(
	const FVector& UnitDirection,
	FLinearColor& OutColor,
	const int32 StartCellHint,
	int32* OutCellId) const
{
	OutColor = FLinearColor::Black;
	const FABTSM3TerrainVisualField* PresentedVisualField =
		bMonthlyPresentationPreviewActive
			? MonthlyPresentationPreviewVisualField.Get()
			: TerrainVisualField.Get();
	if (PresentedVisualField == nullptr
		|| !PresentedVisualField->IsReady()
		|| UnitDirection.IsNearlyZero())
	{
		return false;
	}
	int32 ResolvedCellId = INDEX_NONE;
	const bool bResult = PresentedVisualField->QueryScoutMapColor(
		UnitDirection, RoadColor, RiverColor, StartCellHint, ResolvedCellId, OutColor);
	if (OutCellId) *OutCellId = ResolvedCellId;
	return bResult;
}

bool AABTSM3Planet::GetInitialRoadSpawnTransform(
	const float SurfaceOffsetCM,
	FTransform& OutWorldTransform,
	int32& OutCellId) const
{
	OutWorldTransform = FTransform::Identity;
	OutCellId = INDEX_NONE;
	if (!TerrainVisualField || !TerrainVisualField->IsReady()) return false;

	const FABTSM3TaskNode* StartTask = GeneratedTasks.FindByPredicate([](const FABTSM3TaskNode& Task)
	{
		return Task.Type == EABTSM3TaskType::Start;
	});
	if (StartTask == nullptr || !LogicalCells.IsValidIndex(StartTask->SeedCellId)) return false;

	// The first Task seed is always included in the first main-road path. Keep a
	// defensive search inside the Start region for future graph templates.
	int32 SpawnCellId = StartTask->SeedCellId;
	if (!GeneratedCellStates.IsValidIndex(SpawnCellId) || !GeneratedCellStates[SpawnCellId].bRoad)
	{
		SpawnCellId = INDEX_NONE;
		for (const int32 CellId : StartTask->CellIds)
		{
			if (GeneratedCellStates.IsValidIndex(CellId) && GeneratedCellStates[CellId].bRoad)
			{
				SpawnCellId = CellId;
				break;
			}
		}
	}
	if (!LogicalCells.IsValidIndex(SpawnCellId)) return false;

	const FVector SpawnDirection = LogicalCells[SpawnCellId].UnitCenter.GetSafeNormal();
	const FVector SurfaceNormal = TerrainVisualField->GetSurfaceNormal(SpawnDirection);
	FVector RoadForward = FVector::ZeroVector;
	float BestNextTaskDot = -2.0f;
	const FABTSM3TaskNode* NextMainTask = nullptr;
	for (const int32 LinkedTaskId : StartTask->LinkedTaskIds)
	{
		const FABTSM3TaskNode* LinkedTask = GeneratedTasks.FindByPredicate([LinkedTaskId](const FABTSM3TaskNode& Task)
		{
			return Task.TaskId == LinkedTaskId;
		});
		if (LinkedTask != nullptr && LogicalCells.IsValidIndex(LinkedTask->SeedCellId))
		{
			const float Dot = FVector::DotProduct(SpawnDirection, LogicalCells[LinkedTask->SeedCellId].UnitCenter);
			if (Dot > BestNextTaskDot)
			{
				BestNextTaskDot = Dot;
				NextMainTask = LinkedTask;
			}
		}
	}
	if (NextMainTask != nullptr)
	{
		const FVector TargetDirection = LogicalCells[NextMainTask->SeedCellId].UnitCenter;
		int32 FirstRoadNeighborId = INDEX_NONE;
		float BestRoadProgress = -2.0f;
		for (const int32 NeighborId : LogicalCells[SpawnCellId].NeighborCellIds)
		{
			if (!GeneratedCellStates.IsValidIndex(NeighborId) || !GeneratedCellStates[NeighborId].bRoad) continue;
			const float Progress = FVector::DotProduct(LogicalCells[NeighborId].UnitCenter, TargetDirection);
			if (Progress > BestRoadProgress)
			{
				BestRoadProgress = Progress;
				FirstRoadNeighborId = NeighborId;
			}
		}
		if (FirstRoadNeighborId != INDEX_NONE)
		{
			RoadForward = FVector::VectorPlaneProject(LogicalCells[FirstRoadNeighborId].UnitCenter - SpawnDirection, SurfaceNormal).GetSafeNormal();
		}
	}
	if (RoadForward.IsNearlyZero())
	{
		for (const int32 NeighborId : LogicalCells[SpawnCellId].NeighborCellIds)
		{
			if (GeneratedCellStates.IsValidIndex(NeighborId) && GeneratedCellStates[NeighborId].bRoad)
			{
				RoadForward = FVector::VectorPlaneProject(LogicalCells[NeighborId].UnitCenter - SpawnDirection, SurfaceNormal).GetSafeNormal();
				if (!RoadForward.IsNearlyZero()) break;
			}
		}
	}
	if (RoadForward.IsNearlyZero())
	{
		RoadForward = FVector::VectorPlaneProject(FVector::ForwardVector, SurfaceNormal).GetSafeNormal();
		if (RoadForward.IsNearlyZero()) RoadForward = FVector::VectorPlaneProject(FVector::RightVector, SurfaceNormal).GetSafeNormal();
	}

	const float CharacterCenterRadius = TerrainVisualField->GetSurfaceRadius(SpawnDirection) + FMath::Max(0.0f, SurfaceOffsetCM);
	const FVector CharacterCenter = GetPlanetCenterWorld() + SpawnDirection * CharacterCenterRadius;
	OutWorldTransform = FTransform(FRotationMatrix::MakeFromXZ(RoadForward, SpawnDirection).ToQuat(), CharacterCenter);
	OutCellId = SpawnCellId;
	return true;
}

void AABTSM3Planet::BuildM3ContinuousSurface()
{
	FUnitSphereMesh Mesh;
	BuildUnitIcosphere(SurfaceSubdivision, Mesh);
	TArray<int32> CachedCellIds;
	TArray<FVector> CachedSurfaceVertices;
	TArray<FVector> CachedSurfaceNormals;
	TArray<FLinearColor> CachedSurfaceColors;
	CachedCellIds.SetNumUninitialized(Mesh.Vertices.Num());
	CachedSurfaceVertices.SetNumUninitialized(
		Mesh.Vertices.Num());
	CachedSurfaceNormals.SetNumUninitialized(
		Mesh.Vertices.Num());
	CachedSurfaceColors.SetNumUninitialized(
		Mesh.Vertices.Num());
	float MaxSurfaceNormalTiltDegrees = 0.0f;
	int32 ExtremeSurfaceNormalCount = 0;

	// Surface subdivision 7 shares each icosphere vertex across several
	// triangles, while the material requires triangle-local UV candidates.
	// Cache the expensive surface-field samples once per unique vertex, then
	// duplicate only the already-resolved values into the PMC vertex stream.
	for (int32 VertexIndex = 0;
		VertexIndex < Mesh.Vertices.Num();
		++VertexIndex)
	{
		const FVector Unit =
			Mesh.Vertices[VertexIndex].GetSafeNormal();
		CachedCellIds[VertexIndex] = FindNearestCell(Unit);
		CachedSurfaceVertices[VertexIndex] =
			Unit * TerrainVisualField->GetSurfaceRadius(Unit);
		const FVector SurfaceNormal =
			TerrainVisualField->GetSurfaceNormal(Unit);
		CachedSurfaceNormals[VertexIndex] = SurfaceNormal;
		CachedSurfaceColors[VertexIndex] =
			TerrainVisualField->GetDebugTerrainColor(Unit);
		const float NormalTiltDegrees =
			FMath::RadiansToDegrees(FMath::Acos(
				FMath::Clamp(
					FVector::DotProduct(Unit, SurfaceNormal),
					-1.0f,
					1.0f)));
		MaxSurfaceNormalTiltDegrees = FMath::Max(
			MaxSurfaceNormalTiltDegrees,
			NormalTiltDegrees);
		ExtremeSurfaceNormalCount +=
			NormalTiltDegrees > 80.0f ? 1 : 0;
	}

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0, UV1, UV2, UV3;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	Vertices.Reserve(Mesh.Triangles.Num() * 3);
	Triangles.Reserve(Mesh.Triangles.Num() * 3);
	Normals.Reserve(Mesh.Triangles.Num() * 3);
	Colors.Reserve(Mesh.Triangles.Num() * 3);

	for (const FIntVector& Triangle : Mesh.Triangles)
	{
		const int32 CandidateCellIds[3] = {
			CachedCellIds[Triangle.X],
			CachedCellIds[Triangle.Y],
			CachedCellIds[Triangle.Z]};
		const auto EncodeCellId = [](const int32 CellId)
		{
			return FVector2D(static_cast<float>(CellId >> 8), static_cast<float>(CellId & 0xff));
		};
		const FVector2D EncodedA = EncodeCellId(CandidateCellIds[0]);
		const FVector2D EncodedB = EncodeCellId(CandidateCellIds[1]);
		const FVector2D EncodedC = EncodeCellId(CandidateCellIds[2]);
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const int32 SourceVertexIndex = Triangle[Corner];
			const int32 BaseIndex = Vertices.Num();
			Vertices.Add(
				CachedSurfaceVertices[SourceVertexIndex]);
			Normals.Add(
				CachedSurfaceNormals[SourceVertexIndex]);
			// UV0/1/2 are constant per triangle: three material candidate CellIds.
			// UV3 is one-hot so the material can reconstruct barycentric role if needed.
			UV0.Add(EncodedA);
			UV1.Add(EncodedB);
			UV2.Add(EncodedC);
			UV3.Emplace(Corner == 0 ? 1.0f : 0.0f, Corner == 1 ? 1.0f : 0.0f);
			Colors.Add(
				CachedSurfaceColors[SourceVertexIndex]);
			Triangles.Add(BaseIndex);
		}
	}

	ContinuousSurface->ClearAllMeshSections();
	ContinuousSurface->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV0, UV1, UV2, UV3, Colors, Tangents, true, false);
	// Rebuild Chaos state after installing the runtime-generated M3 section.
	ContinuousSurface->RecreatePhysicsState();
	if (TerrainMaterial) ContinuousSurface->SetMaterial(0, TerrainMaterial);
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][M3][SurfaceNormals] SmoothingDistance=%.1f MaxTilt=%.2f ExtremeOver80=%d UniqueSamples=%d Vertices=%d"),
		SurfaceNormalSmoothingDistanceCM, MaxSurfaceNormalTiltDegrees, ExtremeSurfaceNormalCount, CachedSurfaceNormals.Num(), Normals.Num());
}

void AABTSM3Planet::BuildDecorInstances(
	const TArray<FABTSM3CellState>*
		PresentationCellStates,
	const FABTSM3MonthlyCandidatePresentation*
		PresentationCandidate)
{
	// Blueprint children created before the debug obstacle channel existed can
	// serialize their old WorldStatic component type. Reapply the runtime
	// contract whenever the instances are rebuilt.
	ForestHISM->SetCollisionEnabled(
		ECollisionEnabled::QueryAndPhysics);
	RockHISM->SetCollisionEnabled(
		ECollisionEnabled::QueryAndPhysics);
	ForestHISM->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
	RockHISM->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
	ForestHISM->SetCollisionResponseToAllChannels(ECR_Block);
	RockHISM->SetCollisionResponseToAllChannels(ECR_Block);
	ForestHISM->SetSimulatePhysics(false);
	RockHISM->SetSimulatePhysics(false);
	ForestHISM->ClearInstances();
	RockHISM->ClearInstances();
	MonthlyDecorAccent0InstanceCount = 0;
	MonthlyDecorAccent1InstanceCount = 0;

	// An artist may configure either the Actor properties or the HISM component
	// templates in BP_ABTSM3Planet.  A null Actor property must not erase a mesh
	// already assigned on the component.
	if (ForestInstanceMesh)
	{
		ForestHISM->SetStaticMesh(ForestInstanceMesh);
	}
	if (RockInstanceMesh)
	{
		RockHISM->SetStaticMesh(RockInstanceMesh);
	}

	UStaticMesh* ResolvedForestMesh = ForestHISM->GetStaticMesh();
	UStaticMesh* ResolvedRockMesh = RockHISM->GetStaticMesh();
	// Existing Blueprints created before native preview meshes were introduced
	// can serialize an explicit null component mesh.  Resolve that migration case
	// at runtime so the HISM path is immediately testable without an asset edit.
	if (ResolvedForestMesh == nullptr)
	{
		ResolvedForestMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
		ForestHISM->SetStaticMesh(ResolvedForestMesh);
	}
	if (ResolvedRockMesh == nullptr)
	{
		ResolvedRockMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		RockHISM->SetStaticMesh(ResolvedRockMesh);
	}
	const bool bForestMaterialsValid = ValidateInstancedMeshMaterials(TEXT("Forest"), ResolvedForestMesh);
	const bool bRockMaterialsValid = ValidateInstancedMeshMaterials(TEXT("Rock"), ResolvedRockMesh);

	if (InstancesPerCell <= 0)
	{
		UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M3][HISM] InstancesPerCell=%d; decoration generation is disabled."), InstancesPerCell);
		return;
	}

	int32 EligibleForestCells = 0;
	int32 EligibleRockCells = 0;
	float MaxForestSurfaceTiltDegrees = 0.0f;
	float MaxForestAppliedTiltDegrees = 0.0f;
	int32 ProtectedCellCount = 0;
	int32 PlannedInstanceBudget = 0;
	const TArray<FABTSM3CellState>& EffectiveCellStates =
		PresentationCellStates != nullptr
		? *PresentationCellStates
		: GeneratedCellStates;

	for (int32 CellId = 0; CellId < LogicalCells.Num(); ++CellId)
	{
		if (!EffectiveCellStates.IsValidIndex(CellId)) continue;
		const FABTSM3CellState& State =
			EffectiveCellStates[CellId];
		const FABTSM3MonthlyPresentationCell*
			PresentationCell =
				PresentationCandidate != nullptr
					&& PresentationCandidate->Cells
						.IsValidIndex(CellId)
				? &PresentationCandidate->Cells[CellId]
				: nullptr;
		if (PresentationCell != nullptr
			&& (PresentationCell->CellId != CellId
				|| PresentationCell
					->bDecorationProtected))
		{
			ProtectedCellCount +=
				PresentationCell->bDecorationProtected
				? 1 : 0;
			continue;
		}
		UHierarchicalInstancedStaticMeshComponent* TargetHISM = nullptr;
		if (State.bRoad || State.bBuildingAnchor || State.bWater) continue;
		if (State.TerrainType == EABTSM3TerrainType::Forest && ResolvedForestMesh)
		{
			TargetHISM = ForestHISM;
			++EligibleForestCells;
		}
		if (State.TerrainType == EABTSM3TerrainType::Mountain && ResolvedRockMesh)
		{
			TargetHISM = RockHISM;
			++EligibleRockCells;
		}
		if (TargetHISM == nullptr) continue;
		int32 CellInstanceCount = InstancesPerCell;
		if (PresentationCell != nullptr)
		{
			const int32 RequiredDecorationMask =
				TargetHISM == ForestHISM
				? static_cast<int32>(
					EABTSM3MonthlyDecorationKind::Forest)
				: static_cast<int32>(
					EABTSM3MonthlyDecorationKind::Rock);
			if ((PresentationCell->DecorationKindMask
					& RequiredDecorationMask)
				== 0)
			{
				continue;
			}
			CellInstanceCount = FMath::Min(
				CellInstanceCount,
				PresentationCell
					->MaxDecorationInstances);
			PlannedInstanceBudget += CellInstanceCount;
		}
		if (CellInstanceCount <= 0) continue;

		int32 AccentVariantId = 0;
		if (PresentationCell != nullptr)
		{
			const FABTSM3MonthlyVisualBeat* Beat =
				PresentationCandidate->VisualBeats
					.FindByPredicate(
						[PresentationCell](
							const FABTSM3MonthlyVisualBeat&
								Item)
						{
							return Item.VisualBeatId
								== PresentationCell->
									VisualBeatId;
						});
			if (Beat == nullptr)
			{
				continue;
			}
			AccentVariantId = Beat->AccentVariantId;
			if ((AccentVariantId & 1) == 0
				&& CellInstanceCount > 1)
			{
				--CellInstanceCount;
			}
		}
		const uint32 VisualVariantSeed =
			PresentationCell != nullptr
			? HashCombineFast(
				GetTypeHash(
					PresentationCell->ThemeVariantId),
				GetTypeHash(AccentVariantId))
			: 0u;
		FRandomStream Stream(HashCombineFast(
			HashCombineFast(
				GetTypeHash(WorldSeed),
				GetTypeHash(CellId)),
			VisualVariantSeed));
		const FVector Center = LogicalCells[CellId].UnitCenter;
		for (int32 Slot = 0;
			Slot < CellInstanceCount;
			++Slot)
		{
			const int32 NeighborId = LogicalCells[CellId].NeighborCellIds[Stream.RandRange(0, LogicalCells[CellId].NeighborCellIds.Num() - 1)];
			const FVector Direction = FMath::Lerp(Center, LogicalCells[NeighborId].UnitCenter, Stream.FRandRange(0.0f, 0.42f)).GetSafeNormal();
			if (PresentationCandidate != nullptr)
			{
				const int32 ResolvedCellId =
					FindNearestCell(Direction);
				if (ResolvedCellId != CellId
					|| !PresentationCandidate->Cells
						.IsValidIndex(
							ResolvedCellId)
					|| PresentationCandidate->Cells[
						ResolvedCellId]
						.bDecorationProtected)
				{
					continue;
				}
			}
			if (TerrainVisualField->IsInsideBuildingPad(Direction)) continue;
			const float Radius = TerrainVisualField->GetSurfaceRadius(Direction) - 8.0f;
			const FVector RadialUp = Direction;
			FVector SurfaceUp = TerrainVisualField->GetSurfaceNormal(Direction).GetSafeNormal();
			if (FVector::DotProduct(SurfaceUp, RadialUp) < 0.0f) SurfaceUp *= -1.0f;
			FVector Up = SurfaceUp;
			if (TargetHISM == ForestHISM)
			{
				// A tree should visually grow away from the planet, not lie along every
				// local terrain ripple. Keep radial Up dominant and use the surface
				// normal only as a controlled slope response.
				const float SurfaceBlend = FMath::Clamp(ForestSurfaceNormalBlend, 0.0f, 1.0f);
				Up = FMath::Lerp(RadialUp, SurfaceUp, SurfaceBlend).GetSafeNormal();
				if (Up.IsNearlyZero()) Up = RadialUp;
				const float SurfaceTiltDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(RadialUp, SurfaceUp), -1.0f, 1.0f)));
				const float AppliedTiltDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(RadialUp, Up), -1.0f, 1.0f)));
				MaxForestSurfaceTiltDegrees = FMath::Max(MaxForestSurfaceTiltDegrees, SurfaceTiltDegrees);
				MaxForestAppliedTiltDegrees = FMath::Max(MaxForestAppliedTiltDegrees, AppliedTiltDegrees);
			}
			FVector Forward = FVector::VectorPlaneProject(LogicalCells[NeighborId].UnitCenter - Center, Up).GetSafeNormal();
			if (Forward.IsNearlyZero()) Forward = FVector::VectorPlaneProject(FVector::ForwardVector, Up).GetSafeNormal();
			if (Forward.IsNearlyZero()) Forward = FVector::VectorPlaneProject(FVector::RightVector, Up).GetSafeNormal();
			const FQuat Rotation = FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
			const float BeatScale =
				PresentationCell != nullptr
				&& (AccentVariantId & 1) != 0
				? 1.12f
				: (PresentationCell != nullptr
					? 0.88f
					: 1.0f);
			const float ThemeScale =
				PresentationCell != nullptr
				&& (PresentationCell->ThemeVariantId & 1)
					!= 0
				? 1.05f
				: (PresentationCell != nullptr
					? 0.95f
					: 1.0f);
			const float Scale =
				Stream.FRandRange(0.75f, 1.25f)
				* BeatScale
				* ThemeScale;
			TargetHISM->AddInstance(FTransform(Rotation, Direction * Radius, FVector(Scale)), false);
			if (PresentationCell != nullptr)
			{
				if ((AccentVariantId & 1) != 0)
				{
					++MonthlyDecorAccent1InstanceCount;
				}
				else
				{
					++MonthlyDecorAccent0InstanceCount;
				}
			}
		}
	}

	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3][HISM] ForestMesh=%s RockMesh=%s ForestMaterialsValid=%d RockMaterialsValid=%d EligibleForestCells=%d EligibleRockCells=%d ForestInstances=%d RockInstances=%d ForestNormalBlend=%.2f MaxSurfaceTilt=%.2f MaxAppliedTilt=%.2f M3R5PreviewAuthority=%d ProtectedCells=%d PlannedInstanceBudget=%d Accent0Instances=%d Accent1Instances=%d Collision=QueryAndPhysics ObstacleChannel=%d SimulatePhysics=0"),
		*GetNameSafe(ResolvedForestMesh), *GetNameSafe(ResolvedRockMesh), bForestMaterialsValid ? 1 : 0, bRockMaterialsValid ? 1 : 0, EligibleForestCells, EligibleRockCells,
		ForestHISM->GetInstanceCount(), RockHISM->GetInstanceCount(), FMath::Clamp(ForestSurfaceNormalBlend, 0.0f, 1.0f),
		MaxForestSurfaceTiltDegrees, MaxForestAppliedTiltDegrees,
		PresentationCandidate != nullptr ? 1 : 0,
		ProtectedCellCount,
		PlannedInstanceBudget,
		MonthlyDecorAccent0InstanceCount,
		MonthlyDecorAccent1InstanceCount,
		static_cast<int32>(ABTSDeveloperObstacleChannel));
}

void AABTSM3Planet::BuildBuildingSpawnSites()
{
	BuildingSpawnSites.Reset();
	FinaleLaunchFrame = FABTSM110FinaleLocalFrame();
	const FABTSM3TaskNode* SatelliteWindowTask = GeneratedTasks.FindByPredicate([](const FABTSM3TaskNode& Task)
	{
		return Task.Type == EABTSM3TaskType::SatelliteWindow;
	});
	for (int32 CellId = 0; CellId < GeneratedCellStates.Num(); ++CellId)
	{
		if (!GeneratedCellStates[CellId].bBuildingAnchor) continue;
		const FVector Direction = LogicalCells[CellId].UnitCenter;
		const FVector SurfaceNormal = TerrainVisualField->GetSurfaceNormal(Direction);
		// Buildings use CellTopo's radial normal as their construction vertical. This
		// keeps the pad, ground adapter and runtime gravity in one shared frame.
		const FVector PadUp = Direction.GetSafeNormal();
		FVector Forward = FVector::VectorPlaneProject(FVector::ForwardVector, PadUp).GetSafeNormal();
		if (Forward.IsNearlyZero()) Forward = FVector::VectorPlaneProject(FVector::RightVector, PadUp).GetSafeNormal();
		FABTSM3BuildingSpawnSite& Site = BuildingSpawnSites.AddDefaulted_GetRef();
		Site.CellId = CellId;
		Site.TaskId = GeneratedCellStates[CellId].TaskId;
		const FABTSM3TaskNode* SiteTask = GeneratedTasks.FindByPredicate([TaskId = Site.TaskId](const FABTSM3TaskNode& Task)
		{
			return Task.TaskId == TaskId;
		});
		Site.TaskType = SiteTask != nullptr ? SiteTask->Type : EABTSM3TaskType::Unassigned;
		Site.AnchorDirection = PadUp;
		if (Site.TaskType != EABTSM3TaskType::LaunchSite
			&& SiteTask != nullptr
			&& LogicalCells.IsValidIndex(SiteTask->RoadPortalCellId))
		{
			// The ordinary building's +X attack direction points from its road
			// arrival portal into the off-road target. M7 can therefore retain
			// its local weak-point semantics after M3 separates road and pad.
			const FVector RoadToBuilding = FVector::VectorPlaneProject(
				Direction - LogicalCells[SiteTask->RoadPortalCellId].UnitCenter,
				PadUp).GetSafeNormal();
			if (!RoadToBuilding.IsNearlyZero()) Forward = RoadToBuilding;
		}
		else if (Site.TaskType == EABTSM3TaskType::LaunchSite
			&& SatelliteWindowTask != nullptr
			&& LogicalCells.IsValidIndex(SatelliteWindowTask->SeedCellId))
		{
			// The terminal pair's positive Y axis always points toward the
			// SatelliteWindow in the LaunchSite tangent plane. This is derived
			// solely from TaskGraph data and can never flip with player position.
			FVector CanonicalRight = FVector::VectorPlaneProject(
				LogicalCells[SatelliteWindowTask->SeedCellId].UnitCenter,
				PadUp).GetSafeNormal();
			if (!CanonicalRight.IsNearlyZero())
			{
				Forward = FVector::CrossProduct(CanonicalRight, PadUp).GetSafeNormal();
			}
		}
		Site.TangentForward = Forward;
		Site.TangentRight = FVector::CrossProduct(PadUp, Forward).GetSafeNormal();
		Site.PadHalfExtentCM = BuildingPadSettings.HalfExtentCM;
		Site.PadEdgeBlendWidthCM = BuildingPadSettings.EdgeBlendWidthCM;
		Site.PadTargetRadiusCM = TerrainVisualField->GetSurfaceRadius(Direction);
		Site.bTerrainPadApplied = BuildingPadSettings.bEnableTerrainFlattening;
		Site.WorldTransform = FTransform(FRotationMatrix::MakeFromXZ(Forward, PadUp).ToQuat(),
			GetPlanetCenterWorld() + Direction * Site.PadTargetRadiusCM);
		Site.MaxSlopeDegrees = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(PadUp, SurfaceNormal), -1.0f, 1.0f)));

		if (Site.TaskType == EABTSM3TaskType::LaunchSite)
		{
			const float SafeSeparationCM = ABTSResolveFinaleSpaceStakeSpacingCM(
				FinaleSpaceSlotSeparationCM);
			const FVector SlotOrigin = Site.WorldTransform.GetLocation()
				+ PadUp * FMath::Max(0.0f, FinaleSpaceSlotSurfaceOffsetCM);
			FinaleLaunchFrame.LayoutVersion = 1;
			FinaleLaunchFrame.LaunchTaskId = Site.TaskId;
			FinaleLaunchFrame.AnchorCellId = Site.CellId;
			FinaleLaunchFrame.SlotPairId = static_cast<int32>(HashCombineFast(
				GetTypeHash(WorldSeed),
				HashCombineFast(
					GetTypeHash(Site.TaskId),
					HashCombineFast(GetTypeHash(Site.CellId), GetTypeHash(FinaleLaunchFrame.LayoutVersion))))
				& MAX_int32);
			FinaleLaunchFrame.WorldTransform = FTransform(
				FRotationMatrix::MakeFromXZ(Site.TangentForward, PadUp).ToQuat(),
				SlotOrigin);
			FinaleLaunchFrame.LeftSlotWorldLocation =
				SlotOrigin - Site.TangentRight * (SafeSeparationCM * 0.5f);
			FinaleLaunchFrame.RightSlotWorldLocation =
				SlotOrigin + Site.TangentRight * (SafeSeparationCM * 0.5f);
			FinaleLaunchFrame.bValid = true;
			if (!FinaleLaunchFrame.IsUsable())
			{
				FinaleLaunchFrame.bValid = false;
			}
		}
	}
}
