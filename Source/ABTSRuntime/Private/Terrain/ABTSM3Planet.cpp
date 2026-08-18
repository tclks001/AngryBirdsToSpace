// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/ABTSM3Planet.h"

#include "ABTSRuntime.h"
#include "Async/ParallelFor.h"
#include "CollisionQueryParams.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "PCG/ABTSM3MonthlySatellitePracticeRuntime.h"
#include "PCG/ABTSM3R5AcceptanceManifest.h"
#include "PCG/ABTSM3TaskGraphGenerator.h"
#include "ProceduralMeshComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Rendering/ABTSStylizedMaterialContract.h"
#include "Slingshot/ABTSSlingshotVisualTypes.h"
#include "Terrain/ABTSM3DecorPlacement.h"
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
TAutoConsoleVariable<int32> CVarABTSM3ContinuousSurfaceExactOracle(
	TEXT("abts.M3.ContinuousSurfaceExactOracle"),
	0,
	TEXT("Builds legacy and optimized M3 continuous-surface buffers and fails closed on any exact mismatch."),
	ECVF_Default);

struct FABTSM3JuryTerrainPadSource
{
	int32 EncounterIndex = INDEX_NONE;
	int32 PadCenterCellId = INDEX_NONE;
	FVector PlanetLocalCenterCM = FVector::ZeroVector;
	FVector Forward = FVector::ForwardVector;
	FVector Right = FVector::RightVector;
	FVector Up = FVector::UpVector;
	FVector2D PadHalfExtentCM = FVector2D::ZeroVector;
	float TargetRadiusCM = 0.0f;
};

bool BuildABTSM3JuryTerrainPads(
	const TArray<FABTSM3JuryTerrainPadSource>& Sources,
	const FVector& PlanetCenterWorld,
	const float PlanetRadiusCM,
	const float MaximumGradeSlopeDegreesSetting,
	const float MinimumGradeWidthCMSetting,
	const float SmoothStepGradeMultiplier,
	const float SurfaceNormalSmoothingDistanceCM,
	const FABTSM3TerrainVisualField& TerrainVisualField,
	TArray<FABTSM3BuildingSpawnSite>& OutPads,
	TArray<float>& OutSourceHeightDeltasCM,
	FString& OutFailure)
{
	OutPads.Reset();
	OutSourceHeightDeltasCM.Reset();
	OutFailure.Reset();
	OutPads.Reserve(Sources.Num());
	OutSourceHeightDeltasCM.Reserve(Sources.Num());
	const float MaximumGradeSlopeDegrees = FMath::Clamp(
		MaximumGradeSlopeDegreesSetting,
		5.0f,
		30.0f);
	const float MaximumGradeTangent = FMath::Tan(
		FMath::DegreesToRadians(MaximumGradeSlopeDegrees));
	const float MinimumGradeWidthCM = FMath::Max(
		300.0f,
		MinimumGradeWidthCMSetting);
	// Cubic SmoothStep peaks at 1.5 times the average slope. Callers add
	// contract-specific headroom for the CellTopo field and pad interactions.
	constexpr int32 GradeRayCount = 16;
	constexpr int32 GradeSamplesPerRay = 2;
	constexpr int32 MaximumWidthSolveIterations = 12;
	const float MaximumSafeGradeWidthCM = PlanetRadiusCM * 0.6f;
	const float MinimumLocalHeightProbeWidthCM = FMath::Max(
		200.0f,
		SurfaceNormalSmoothingDistanceCM * 2.0f);

	for (const FABTSM3JuryTerrainPadSource& Source : Sources)
	{
		if (Source.PadCenterCellId == INDEX_NONE
			|| Source.Forward.IsNearlyZero()
			|| Source.Right.IsNearlyZero()
			|| Source.Up.IsNearlyZero()
			|| FMath::Abs(FVector::DotProduct(Source.Forward, Source.Right))
				> 1.0e-3f
			|| FMath::Abs(FVector::DotProduct(Source.Forward, Source.Up))
				> 1.0e-3f
			|| FMath::Abs(FVector::DotProduct(Source.Right, Source.Up))
				> 1.0e-3f
			|| FVector::DotProduct(
				FVector::CrossProduct(Source.Up, Source.Forward),
				Source.Right) < 0.999f
			|| Source.TargetRadiusCM <= 0.0f
			|| Source.PadHalfExtentCM.X <= 0.0f
			|| Source.PadHalfExtentCM.Y <= 0.0f)
		{
			OutFailure = FString::Printf(
				TEXT("InvalidPlacementFrameOrPad:%d"),
				Source.EncounterIndex);
			return false;
		}

		FABTSM3BuildingSpawnSite& Site = OutPads.AddDefaulted_GetRef();
		Site.TaskId = INDEX_NONE;
		Site.CellId = Source.PadCenterCellId;
		Site.TaskType = EABTSM3TaskType::Unassigned;
		Site.WorldTransform = FTransform(
			FRotationMatrix::MakeFromXZ(Source.Forward, Source.Up).ToQuat(),
			PlanetCenterWorld + Source.PlanetLocalCenterCM);
		Site.MaxSlopeDegrees = 0.0f;
		Site.AnchorDirection = Source.Up;
		Site.TangentForward = Source.Forward;
		Site.TangentRight = Source.Right;
		Site.PadHalfExtentCM = Source.PadHalfExtentCM;
		Site.PadTargetRadiusCM = Source.TargetRadiusCM;

		auto SampleMaximumSourceHeightDeltaCM = [
			&TerrainVisualField,
			&Source,
			MinimumLocalHeightProbeWidthCM](const float CandidateWidthCM)
		{
			const float LocalHeightProbeWidthCM = FMath::Max(
				MinimumLocalHeightProbeWidthCM,
				CandidateWidthCM * 0.5f);
			float MaximumHeightDeltaCM = 0.0f;
			const FVector CenterDirection =
				Source.PlanetLocalCenterCM.GetSafeNormal();
			if (!CenterDirection.IsNearlyZero())
			{
				const FVector RawCenter = CenterDirection
					* TerrainVisualField.GetUnpaddedSurfaceRadius(
						CenterDirection);
				MaximumHeightDeltaCM = FMath::Abs(FVector::DotProduct(
					RawCenter - Source.PlanetLocalCenterCM,
					Source.Up));
			}
			for (int32 RayIndex = 0; RayIndex < GradeRayCount; ++RayIndex)
			{
				const float AngleRadians = UE_TWO_PI
					* static_cast<float>(RayIndex)
					/ static_cast<float>(GradeRayCount);
				const FVector2D Ray(
					FMath::Cos(AngleRadians),
					FMath::Sin(AngleRadians));
				const float BoundaryDistanceCM = 1.0f / FMath::Max(
					FMath::Abs(Ray.X) / Source.PadHalfExtentCM.X,
					FMath::Abs(Ray.Y) / Source.PadHalfExtentCM.Y);
				for (int32 SampleIndex = 0;
					SampleIndex <= GradeSamplesPerRay;
					++SampleIndex)
				{
					const float DistanceCM = BoundaryDistanceCM
						+ LocalHeightProbeWidthCM
							* static_cast<float>(SampleIndex)
							/ static_cast<float>(GradeSamplesPerRay);
					const FVector PlaneSample = Source.PlanetLocalCenterCM
						+ Source.Forward * (Ray.X * DistanceCM)
						+ Source.Right * (Ray.Y * DistanceCM);
					const FVector Direction = PlaneSample.GetSafeNormal();
					if (Direction.IsNearlyZero())
					{
						continue;
					}
					const FVector RawSurface = Direction
						* TerrainVisualField.GetUnpaddedSurfaceRadius(Direction);
					MaximumHeightDeltaCM = FMath::Max(
						MaximumHeightDeltaCM,
						FMath::Abs(FVector::DotProduct(
							RawSurface - Source.PlanetLocalCenterCM,
							Source.Up)));
				}
			}
			return MaximumHeightDeltaCM;
		};

		float ResolvedGradeWidthCM = MinimumGradeWidthCM;
		float MaximumSourceHeightDeltaCM = 0.0f;
		bool bGradeWidthConverged = false;
		for (int32 SolveIteration = 0;
			SolveIteration < MaximumWidthSolveIterations;
			++SolveIteration)
		{
			MaximumSourceHeightDeltaCM =
				SampleMaximumSourceHeightDeltaCM(ResolvedGradeWidthCM);
			const float RequiredGradeWidthCM = FMath::Max(
				MinimumGradeWidthCM,
				SmoothStepGradeMultiplier * MaximumSourceHeightDeltaCM
					/ FMath::Max(MaximumGradeTangent, UE_KINDA_SMALL_NUMBER));
			if (!FMath::IsFinite(RequiredGradeWidthCM)
				|| RequiredGradeWidthCM > MaximumSafeGradeWidthCM)
			{
				OutFailure = FString::Printf(
					TEXT("GradeWidthUnsafe:%d:Width=%.1f:Delta=%.1f"),
					Source.EncounterIndex,
					RequiredGradeWidthCM,
					MaximumSourceHeightDeltaCM);
				return false;
			}
			if (RequiredGradeWidthCM <= ResolvedGradeWidthCM + 1.0f)
			{
				bGradeWidthConverged = true;
				break;
			}
			ResolvedGradeWidthCM = RequiredGradeWidthCM;
		}
		if (!bGradeWidthConverged)
		{
			OutFailure = FString::Printf(
				TEXT("GradeWidthDidNotConverge:%d:Width=%.1f:Delta=%.1f"),
				Source.EncounterIndex,
				ResolvedGradeWidthCM,
				MaximumSourceHeightDeltaCM);
			return false;
		}
		Site.PadEdgeBlendWidthCM = ResolvedGradeWidthCM;
		Site.bTerrainPadApplied = true;
		OutSourceHeightDeltasCM.Add(MaximumSourceHeightDeltaCM);
	}
	return true;
}

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
		float SurfaceRadiusCM = 0.0f;
		return Planet.QuerySurface(
				Direction,
				OutSample.WorldLocation,
				OutSample.WorldNormal,
				SurfaceRadiusCM,
				OutSample.NearestCellId)
			&& OutSample.NearestCellId != INDEX_NONE
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

void HashABTSM3DecorPlacementValue(uint64& InOutHash, const int64 Value)
{
	constexpr uint64 Prime = 1099511628211ull;
	uint64 Bits = static_cast<uint64>(Value);
	for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
	{
		InOutHash ^= Bits & 0xffull;
		InOutHash *= Prime;
		Bits >>= 8;
	}
}

void HashABTSM3DecorPlacementTransform(
	uint64& InOutHash,
	const uint8 Type,
	const FTransform& Transform)
{
	HashABTSM3DecorPlacementValue(InOutHash, Type);
	const FVector Location = Transform.GetLocation();
	FQuat Rotation = Transform.GetRotation();
	if (Rotation.W < 0.0f)
	{
		Rotation.X *= -1.0f;
		Rotation.Y *= -1.0f;
		Rotation.Z *= -1.0f;
		Rotation.W *= -1.0f;
	}
	const FVector Scale = Transform.GetScale3D();
	HashABTSM3DecorPlacementValue(
		InOutHash, FMath::RoundToInt64(Location.X * 100.0));
	HashABTSM3DecorPlacementValue(
		InOutHash, FMath::RoundToInt64(Location.Y * 100.0));
	HashABTSM3DecorPlacementValue(
		InOutHash, FMath::RoundToInt64(Location.Z * 100.0));
	HashABTSM3DecorPlacementValue(
		InOutHash, FMath::RoundToInt64(Rotation.X * 1000000.0));
	HashABTSM3DecorPlacementValue(
		InOutHash, FMath::RoundToInt64(Rotation.Y * 1000000.0));
	HashABTSM3DecorPlacementValue(
		InOutHash, FMath::RoundToInt64(Rotation.Z * 1000000.0));
	HashABTSM3DecorPlacementValue(
		InOutHash, FMath::RoundToInt64(Rotation.W * 1000000.0));
	HashABTSM3DecorPlacementValue(
		InOutHash, FMath::RoundToInt64(Scale.X * 10000.0));
}

void HashABTSM3DecorCollisionShape(
	uint64& InOutHash,
	const uint8 Type,
	const FABTSM3DecorCollisionShape& Shape)
{
	HashABTSM3DecorPlacementValue(InOutHash, Type);
	HashABTSM3DecorPlacementValue(
		InOutHash, FMath::RoundToInt64(Shape.LocalBounds.Min.X * 100.0));
	HashABTSM3DecorPlacementValue(
		InOutHash, FMath::RoundToInt64(Shape.LocalBounds.Min.Y * 100.0));
	HashABTSM3DecorPlacementValue(
		InOutHash, FMath::RoundToInt64(Shape.LocalBounds.Min.Z * 100.0));
	HashABTSM3DecorPlacementValue(
		InOutHash, FMath::RoundToInt64(Shape.LocalBounds.Max.X * 100.0));
	HashABTSM3DecorPlacementValue(
		InOutHash, FMath::RoundToInt64(Shape.LocalBounds.Max.Y * 100.0));
	HashABTSM3DecorPlacementValue(
		InOutHash, FMath::RoundToInt64(Shape.LocalBounds.Max.Z * 100.0));
	HashABTSM3DecorPlacementValue(
		InOutHash, Shape.LocalSurfaceSamples.Num());
	for (const FVector& Sample : Shape.LocalSurfaceSamples)
	{
		HashABTSM3DecorPlacementValue(
			InOutHash, FMath::RoundToInt64(Sample.X * 100.0));
		HashABTSM3DecorPlacementValue(
			InOutHash, FMath::RoundToInt64(Sample.Y * 100.0));
		HashABTSM3DecorPlacementValue(
			InOutHash, FMath::RoundToInt64(Sample.Z * 100.0));
	}
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
	JuryFixedSixTerrainPadCount = 0;
	JuryFixedSixDecorClearanceRejectedCount = 0;
	DecorPlacementSummary = FABTSM3DecorPlacementSummary();
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
	JuryMapFreezeV3Result = FABTSM3JuryMapFreezeV3Result();
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
	const TArray<FABTSM3BuildingSpawnSite> CompatibilityTerrainPads =
		BuildingSpawnSites;
	TArray<FABTSM3BuildingSpawnSite> TerrainPads =
		CompatibilityTerrainPads;
	FString JuryTerrainPadFailure;
	bool bJuryTerrainPadsReady = AppendJuryFixedSixTerrainPads(
		TerrainPads,
		JuryTerrainPadFailure);
	if (!bJuryTerrainPadsReady)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3Jury][TerrainPadBootstrap] Ready=0 Failure=%s CompatibilityBuildingSitesPreserved=1"),
			*JuryTerrainPadFailure);
	}
	TerrainVisualField->SetBuildingPads(TerrainPads);
	BuildBuildingSpawnSites();
	FString SatellitePreviewFailure;
	const FPlanetMonthlySatellitePreviewSurface SatelliteSurface(
		*this,
		LogicalCells);
	FABTSM3MonthlySatellitePreviewConfig InitialSatellitePreviewConfig =
		MonthlySatellitePreviewConfig;
	const bool bRequiresMapFreezeV3SurfaceFinalization =
		WorldSeed == FABTSM3JuryMapFreezeV3Builder::FrozenWorldSeed;
	if (bRequiresMapFreezeV3SurfaceFinalization)
	{
		// This first result is only a bootstrap input for resolving the five
		// primary V3 pads. Publish a single SatellitePreview identity after the
		// final production surface has replaced the retired V2 pad set.
		InitialSatellitePreviewConfig.bEmitPreviewLogs = false;
	}
	if (!FABTSM3MonthlySatellitePreviewBuilder::Build(
			WorldSeed,
			InitialSatellitePreviewConfig,
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
	if (bRequiresMapFreezeV3SurfaceFinalization)
	{
		auto InstallCurrentMapFreezeV3TerrainPads = [&]()
		{
			TerrainPads.Reset();
			for (const FABTSM3BuildingSpawnSite& CompatibilityPad
				: CompatibilityTerrainPads)
			{
				// V3 replaces the ordinary Workshop/Target/Furnace building
				// pads. The terminal LaunchSite remains an independent M11
				// facility and keeps its established construction plane.
				if (CompatibilityPad.TaskType == EABTSM3TaskType::LaunchSite)
				{
					TerrainPads.Add(CompatibilityPad);
				}
			}
			FString V3TerrainPadFailure;
			if (!AppendJuryMapFreezeV3TerrainPads(
					TerrainPads,
					V3TerrainPadFailure))
			{
				JuryTerrainPadFailure = MoveTemp(V3TerrainPadFailure);
				return false;
			}
			TerrainVisualField->SetBuildingPads(TerrainPads);
			BuildBuildingSpawnSites();
			return true;
		};

		FString MapFreezeV3Failure;
		if (!FABTSM3JuryMapFreezeV3Builder::Build(
			LogicalCells,
			GetActorLocation(),
			PlanetRadiusCM,
			MonthlySatellitePreviewConfig.PrimarySurfaceGravityCMPerSec2,
			MonthlySpatialResult,
			MonthlySatellitePreviewResult,
			JuryMapFreezeV3Result,
			MapFreezeV3Failure))
		{
			bJuryTerrainPadsReady = false;
			UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M3Jury][MapFreezeV3] Ready=0 Seed=%d Candidate=%d Reason=%s Failure=%s ProductionContract=V%d ActivationAllowed=1"),
			WorldSeed,
			FABTSM3JuryMapFreezeV3Builder::FrozenSourceCandidateId,
			FABTSM3JuryMapFreezeV3Builder::GetRejectReasonName(
				JuryMapFreezeV3Result.RejectReason),
			*MapFreezeV3Failure,
			FABTSJuryDemoFixedSixContract::ProductionContractVersion);
		}
		else
		{
			// The bootstrap snapshot may have sampled a retired V2 terrain-only
			// pad. Resolve the primary V3 pads first, then rebuild both the
			// satellite preview and E1 Map Freeze placement against the exact
			// surface that production runtime QuerySurface() will consume.
			bJuryTerrainPadsReady = InstallCurrentMapFreezeV3TerrainPads();
			if (!bJuryTerrainPadsReady)
			{
				UE_LOG(LogABTSRuntime, Error,
					TEXT("[ABTS][M3Jury][ProductionTerrainPads] Ready=0 Contract=V3 Phase=Bootstrap Failure=%s Authority=M3RuntimeSurface"),
					*JuryTerrainPadFailure);
				JuryMapFreezeV3Result = FABTSM3JuryMapFreezeV3Result();
			}
			else
			{
				FABTSM3MonthlySatellitePreviewResult FinalSatellitePreview;
				FString FinalSatellitePreviewFailure;
				if (!FABTSM3MonthlySatellitePreviewBuilder::Build(
						WorldSeed,
						MonthlySatellitePreviewConfig,
						LogicalCells,
						MonthlySpatialResult,
						MonthlySlingshotFieldResult,
						SatelliteSurface,
						FinalSatellitePreview,
						FinalSatellitePreviewFailure,
						EABTSM3MonthlySatelliteTargetAuthority::
							FrozenE1BuildingModules,
						FABTSM3JuryMapFreezeV3Builder::
							FrozenSourceCandidateId))
				{
					bJuryTerrainPadsReady = false;
					MonthlySatellitePreviewResult =
						MoveTemp(FinalSatellitePreview);
					JuryMapFreezeV3Result = FABTSM3JuryMapFreezeV3Result();
					UE_LOG(LogABTSRuntime, Error,
						TEXT("[ABTS][M3Jury][MapFreezeV3] Ready=0 Seed=%d Candidate=%d Phase=FinalSurfaceSatellitePreview Failure=%s ProductionContract=V%d ActivationAllowed=1"),
						WorldSeed,
						FABTSM3JuryMapFreezeV3Builder::FrozenSourceCandidateId,
						*FinalSatellitePreviewFailure,
						FABTSJuryDemoFixedSixContract::ProductionContractVersion);
				}
				else
				{
					FABTSM3JuryMapFreezeV3Result FinalMapFreezeV3;
					FString FinalMapFreezeV3Failure;
					if (!FABTSM3JuryMapFreezeV3Builder::Build(
							LogicalCells,
							GetActorLocation(),
							PlanetRadiusCM,
							MonthlySatellitePreviewConfig.PrimarySurfaceGravityCMPerSec2,
							MonthlySpatialResult,
							FinalSatellitePreview,
							FinalMapFreezeV3,
							FinalMapFreezeV3Failure,
							true))
					{
						bJuryTerrainPadsReady = false;
						MonthlySatellitePreviewResult =
							MoveTemp(FinalSatellitePreview);
						JuryMapFreezeV3Result =
							MoveTemp(FinalMapFreezeV3);
						UE_LOG(LogABTSRuntime, Error,
							TEXT("[ABTS][M3Jury][MapFreezeV3] Ready=0 Seed=%d Candidate=%d Phase=FinalSurfaceMapFreeze Reason=%s Failure=%s ProductionContract=V%d ActivationAllowed=1"),
							WorldSeed,
							FABTSM3JuryMapFreezeV3Builder::FrozenSourceCandidateId,
							FABTSM3JuryMapFreezeV3Builder::GetRejectReasonName(
								JuryMapFreezeV3Result.RejectReason),
							*FinalMapFreezeV3Failure,
							FABTSJuryDemoFixedSixContract::ProductionContractVersion);
					}
					else
					{
						MonthlySatellitePreviewResult =
							MoveTemp(FinalSatellitePreview);
						JuryMapFreezeV3Result =
							MoveTemp(FinalMapFreezeV3);
						bJuryTerrainPadsReady =
							InstallCurrentMapFreezeV3TerrainPads();
						FString SatelliteAuthorityFailure;
						FString MapFreezeAuthorityFailure;
						const bool bFinalSurfaceAuthorityReady =
							bJuryTerrainPadsReady
							&& ValidateMonthlySatellitePreviewResult(
								SatelliteAuthorityFailure)
							&& ValidateJuryMapFreezeV3Result(
								MapFreezeAuthorityFailure);
						if (!bFinalSurfaceAuthorityReady)
						{
							bJuryTerrainPadsReady = false;
							UE_LOG(LogABTSRuntime, Error,
								TEXT("[ABTS][M3Jury][MapFreezeV3] Ready=0 Seed=%d Candidate=%d Phase=FinalSurfaceAuthority Failure=%s%s%s ProductionContract=V%d ActivationAllowed=1"),
								WorldSeed,
								FABTSM3JuryMapFreezeV3Builder::FrozenSourceCandidateId,
								*JuryTerrainPadFailure,
								*SatelliteAuthorityFailure,
								*MapFreezeAuthorityFailure,
								FABTSJuryDemoFixedSixContract::ProductionContractVersion);
							JuryMapFreezeV3Result =
								FABTSM3JuryMapFreezeV3Result();
						}
						else
						{
							UE_LOG(LogABTSRuntime, Log,
								TEXT("[ABTS][M3Jury][MapFreezeV3] Ready=1 Seed=%d Candidate=%d Sites=%d Primary=5 SatelliteE1=1 Mapping=E2,E3,E4,E5,E1,E6 Catalog=%016llX LayoutHash=%016llX ProductionContract=V%d ActivationAllowed=1 SurfaceAuthority=FinalV3"),
								WorldSeed,
								JuryMapFreezeV3Result.SourceCandidateId,
								JuryMapFreezeV3Result.Placements.Num(),
								static_cast<unsigned long long>(
									JuryMapFreezeV3Result.HandoffContract.PlacementCatalogHash),
								static_cast<unsigned long long>(
									JuryMapFreezeV3Result.LayoutHash),
								FABTSJuryDemoFixedSixContract::ProductionContractVersion);
							for (const FABTSM3JuryMapFreezeV3Placement& Placement
								: JuryMapFreezeV3Result.Placements)
							{
								UE_LOG(LogABTSRuntime, Log,
									TEXT("[ABTS][M3Jury][MapFreezeV3][Site] Slot=%d Entry=%s Surface=%d PadCenterCell=%d CorridorLongAxisAbsDot=%.9f PlacementHash=%016llX"),
									Placement.Site.EncounterIndex,
									*Placement.Site.ManifestEntryId.ToString(),
									static_cast<int32>(
										Placement.Site.V3Envelope.SurfaceKind),
									Placement.PadCenterCellId,
									Placement.AttackCorridorLongAxisAbsDot,
									static_cast<unsigned long long>(
										Placement.Site.V3Envelope.PlacementHash));
							}
							UE_LOG(LogABTSRuntime, Log,
								TEXT("[ABTS][M3Jury][ProductionTerrainPads] Ready=1 Contract=V3 PrimaryPads=%d SatellitePads=0 LayoutHash=%016llX Authority=M3RuntimeSurface"),
								JuryFixedSixTerrainPadCount,
								static_cast<unsigned long long>(
									JuryMapFreezeV3Result.LayoutHash));
						}
					}
				}
			}
		}
	}
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
	if (!BuildM3ContinuousSurface())
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3][ContinuousSurface] Ready=0 Failure=ExactOracleOrBufferBuild"));
		return false;
	}
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
		MonthlyPresentationPreviewVisualField->SetBuildingPads(TerrainPads);
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
	bool bPresentationReady = bFinaleFrameReady
		&& bJuryTerrainPadsReady;
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
	if (WorldSeed == FABTSM3JuryFixedSixLayoutBuilder::FrozenWorldSeed)
	{
		int32 TerrainPadCount = 0;
		int32 PhysicalOverlapInstanceCount = 0;
		int32 DynamicOverlapInstanceCount = 0;
		float MaxPadResidualCM = 0.0f;
		FABTSM3JuryTerrainGradeDiagnostics GradeDiagnostics;
		FString ProductionClearanceFailure;
		const bool bProductionClearanceReady =
			ValidateJuryFixedSixProductionClearance(
				TerrainPadCount,
				PhysicalOverlapInstanceCount,
				DynamicOverlapInstanceCount,
				MaxPadResidualCM,
				GradeDiagnostics,
				ProductionClearanceFailure);
		bPresentationReady = bPresentationReady
			&& bProductionClearanceReady;
		if (bProductionClearanceReady)
		{
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][M3Jury][ProductionClearance] Passed=1 Contract=V3 PrimaryTerrainPads=%d SatelliteTerrainPads=0 PhysicalDecorOverlaps=%d DynamicDecorOverlaps=%d DecorRejected=%d MaxPadResidualCM=%.3f MinGradeWidthCM=%.1f MaxGradeWidthCM=%.1f MaxSourceDeltaCM=%.1f MaxGradeSlopeDegrees=%.2f MaxNormalStepDegrees=%.2f MaxEdgeResidualCM=%.3f ChaosSamples=%d MaxChaosResidualCM=%.2f GradeSlopeBudgetDegrees=%.2f Failure=None Authority=M3RuntimeSurface"),
				TerrainPadCount,
				PhysicalOverlapInstanceCount,
				DynamicOverlapInstanceCount,
				JuryFixedSixDecorClearanceRejectedCount,
				MaxPadResidualCM,
				GradeDiagnostics.MinimumBlendWidthCM,
				GradeDiagnostics.MaximumBlendWidthCM,
				GradeDiagnostics.MaximumSourceHeightDeltaCM,
				GradeDiagnostics.MaximumGradeSlopeDegrees,
				GradeDiagnostics.MaximumNormalStepDegrees,
				GradeDiagnostics.MaximumEdgeHeightResidualCM,
				GradeDiagnostics.CollisionSampleCount,
				GradeDiagnostics.MaximumCollisionResidualCM,
				JuryFixedSixMaximumGradeSlopeDegrees);
		}
		else
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M3Jury][ProductionClearance] Passed=0 Contract=V3 PrimaryTerrainPads=%d SatelliteTerrainPads=0 PhysicalDecorOverlaps=%d DynamicDecorOverlaps=%d DecorRejected=%d MaxPadResidualCM=%.3f MinGradeWidthCM=%.1f MaxGradeWidthCM=%.1f MaxSourceDeltaCM=%.1f MaxGradeSlopeDegrees=%.2f MaxNormalStepDegrees=%.2f MaxEdgeResidualCM=%.3f ChaosSamples=%d MaxChaosResidualCM=%.2f GradeSlopeBudgetDegrees=%.2f Failure=%s Authority=M3RuntimeSurface"),
				TerrainPadCount,
				PhysicalOverlapInstanceCount,
				DynamicOverlapInstanceCount,
				JuryFixedSixDecorClearanceRejectedCount,
				MaxPadResidualCM,
				GradeDiagnostics.MinimumBlendWidthCM,
				GradeDiagnostics.MaximumBlendWidthCM,
				GradeDiagnostics.MaximumSourceHeightDeltaCM,
				GradeDiagnostics.MaximumGradeSlopeDegrees,
				GradeDiagnostics.MaximumNormalStepDegrees,
				GradeDiagnostics.MaximumEdgeHeightResidualCM,
				GradeDiagnostics.CollisionSampleCount,
				GradeDiagnostics.MaximumCollisionResidualCM,
				JuryFixedSixMaximumGradeSlopeDegrees,
				*ProductionClearanceFailure);
		}
	}
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
		JuryMapFreezeV3Result = FABTSM3JuryMapFreezeV3Result();
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
			int32 ReservedPadCellCount = 0;
			int32 ReservedDynamicEnvelopeCellCount = 0;
			for (const FABTSM3JuryBuildingPlacement& Placement
				: MonthlyJuryFixedSixLayoutResult.Placements)
			{
				ReservedPadCellCount += Placement.ReservedPadCellIds.Num();
				ReservedDynamicEnvelopeCellCount +=
					Placement.ReservedDynamicEnvelopeCellIds.Num();
				FString ReservedPadCellText;
				FString ReservedDynamicEnvelopeCellText;
				for (const int32 CellId : Placement.ReservedPadCellIds)
				{
					if (!ReservedPadCellText.IsEmpty())
					{
						ReservedPadCellText += TEXT(",");
					}
					ReservedPadCellText += FString::FromInt(CellId);
				}
				for (const int32 CellId
					: Placement.ReservedDynamicEnvelopeCellIds)
				{
					if (!ReservedDynamicEnvelopeCellText.IsEmpty())
					{
						ReservedDynamicEnvelopeCellText += TEXT(",");
					}
					ReservedDynamicEnvelopeCellText += FString::FromInt(CellId);
				}
				UE_LOG(LogABTSRuntime, Log,
					TEXT("[ABTS][M3Jury][FixedSix][Placement] Contract=2 Encounter=%d Entry=%s TargetCell=%d PadCenterCell=%d ReservedPadCells=%s ReservedDynamicEnvelopeCells=%s PlacementHash=%016llX"),
					Placement.EncounterIndex,
					*Placement.ManifestEntryId.ToString(),
					Placement.TargetAnchorCellId,
					Placement.PadCenterCellId,
					*ReservedPadCellText,
					*ReservedDynamicEnvelopeCellText,
					static_cast<unsigned long long>(
						static_cast<uint64>(Placement.PlacementHash)));
			}
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][M3Jury][FixedSix] PlacementReady=1 Contract=%d Seed=%d Candidate=%d Buildings=%d ReservedPadCells=%d ReservedDynamicEnvelopeCells=%d M7Manifest=%d:%lld M7Catalog=%016llX LayoutHash=%016llX Authority=M3LocalAccepted"),
				MonthlyJuryFixedSixLayoutResult.FixedSixContractVersion,
				WorldSeed,
				MonthlyJuryFixedSixLayoutResult.SourceCandidateId,
				MonthlyJuryFixedSixLayoutResult.Placements.Num(),
				ReservedPadCellCount,
				ReservedDynamicEnvelopeCellCount,
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
			OutFailure,
			WorldSeed == FABTSM3JuryMapFreezeV3Builder::FrozenWorldSeed
				? EABTSM3MonthlySatelliteTargetAuthority::FrozenE1BuildingModules
				: EABTSM3MonthlySatelliteTargetAuthority::
					LegacyCalibrationProxy,
			WorldSeed == FABTSM3JuryMapFreezeV3Builder::FrozenWorldSeed
				? FABTSM3JuryMapFreezeV3Builder::FrozenSourceCandidateId
				: INDEX_NONE))
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

bool AABTSM3Planet::ValidateJuryMapFreezeV3Result(
	FString& OutFailure) const
{
	EABTSM3JuryMapFreezeV3RejectReason RejectReason =
		EABTSM3JuryMapFreezeV3RejectReason::None;
	if (ValidateJuryMapFreezeV3Snapshot(
			JuryMapFreezeV3Result, RejectReason, OutFailure))
	{
		return true;
	}
	OutFailure = FString::Printf(
		TEXT("%s:%s"),
		FABTSM3JuryMapFreezeV3Builder::GetRejectReasonName(RejectReason),
		*OutFailure);
	return false;
}

bool AABTSM3Planet::ValidateJuryMapFreezeV3Snapshot(
	const FABTSM3JuryMapFreezeV3Result& Result,
	EABTSM3JuryMapFreezeV3RejectReason& OutReason,
	FString& OutFailure) const
{
	return FABTSM3JuryMapFreezeV3Builder::Validate(
		LogicalCells,
		GetActorLocation(),
		PlanetRadiusCM,
		MonthlySatellitePreviewConfig.PrimarySurfaceGravityCMPerSec2,
		MonthlySpatialResult,
		MonthlySatellitePreviewResult,
		Result,
		OutReason,
		OutFailure,
		WorldSeed == FABTSM3JuryMapFreezeV3Builder::FrozenWorldSeed);
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
	int32 JuryFixedSixPlacementCount = 0;
	constexpr uint64 JuryFixedSixV2LayoutHash = 0x7029074579FDC52Eull;
	if (MonthlyJuryFixedSixLayoutResult.bPlacementReady
		&& MonthlyJuryFixedSixLayoutResult.RejectReason
			== EABTSM3JuryFixedSixRejectReason::None
		&& MonthlyJuryFixedSixLayoutResult.SourceCandidateId
			== CandidateId
		&& static_cast<uint64>(
			MonthlyJuryFixedSixLayoutResult.LayoutHash)
			== JuryFixedSixV2LayoutHash)
	{
		for (const FABTSM3JuryBuildingPlacement& Placement
			: MonthlyJuryFixedSixLayoutResult.Placements)
		{
			if (!LogicalCells.IsValidIndex(Placement.PadCenterCellId)
				|| !LogicalCells.IsValidIndex(Placement.TargetAnchorCellId))
			{
				continue;
			}
			const FVector Origin = Center + Placement.WorldLocationCM;
			const FVector Forward = Placement.WorldForwardAxis;
			const FVector Right = Placement.WorldRightAxis;
			const FVector Up = Placement.WorldUpAxis;
			const FQuat Rotation =
				FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
			const FVector PadLift = Up * 36.0f;
			const FVector PadCorners[] = {
				Origin + PadLift
					+ Forward * Placement.RequiredPadHalfExtentCM.X
					+ Right * Placement.RequiredPadHalfExtentCM.Y,
				Origin + PadLift
					- Forward * Placement.RequiredPadHalfExtentCM.X
					+ Right * Placement.RequiredPadHalfExtentCM.Y,
				Origin + PadLift
					- Forward * Placement.RequiredPadHalfExtentCM.X
					- Right * Placement.RequiredPadHalfExtentCM.Y,
				Origin + PadLift
					+ Forward * Placement.RequiredPadHalfExtentCM.X
					- Right * Placement.RequiredPadHalfExtentCM.Y
			};
			for (int32 CornerIndex = 0;
				CornerIndex < UE_ARRAY_COUNT(PadCorners);
				++CornerIndex)
			{
				DrawDebugLine(
					GetWorld(),
					PadCorners[CornerIndex],
					PadCorners[(CornerIndex + 1)
						% UE_ARRAY_COUNT(PadCorners)],
					FColor::Cyan,
					false,
					DrawLifeTime,
					0,
					7.0f);
			}

			const auto DrawLocalBounds = [this,
				&Origin,
				&Forward,
				&Right,
				&Up,
				&Rotation,
				DrawLifeTime](const FBox& Bounds, const FColor Color)
			{
				if (Bounds.IsValid == 0)
				{
					return;
				}
				const FVector LocalCenter = Bounds.GetCenter();
				const FVector WorldCenter = Origin
					+ Forward * LocalCenter.X
					+ Right * LocalCenter.Y
					+ Up * LocalCenter.Z;
				DrawDebugBox(
					GetWorld(),
					WorldCenter,
					Bounds.GetExtent(),
					Rotation,
					Color,
					false,
					DrawLifeTime,
					0,
					5.0f);
			};
			DrawLocalBounds(Placement.PhysicalBounds, FColor::Green);
			DrawLocalBounds(Placement.EffectBounds, FColor::Magenta);

			const auto DrawReservedCells = [this,
				&Center,
				DrawLifeTime](
					const TArray<int32>& CellIds,
					const float SurfaceOffsetCM,
					const FColor Color,
					const float PointSize)
			{
				for (const int32 CellId : CellIds)
				{
					if (!LogicalCells.IsValidIndex(CellId))
					{
						continue;
					}
					const FVector Direction =
						LogicalCells[CellId].UnitCenter.GetSafeNormal();
					DrawDebugPoint(
						GetWorld(),
						Center + Direction
							* (GetSurfaceRadiusAtDirection(Direction)
								+ SurfaceOffsetCM),
						PointSize,
						Color,
						false,
						DrawLifeTime,
						0);
				}
			};
			DrawReservedCells(
				Placement.ReservedPadCellIds,
				95.0f,
				FColor::Cyan,
				16.0f);
			DrawReservedCells(
				Placement.ReservedDynamicEnvelopeCellIds,
				135.0f,
				FColor::Magenta,
				20.0f);

			const FVector TargetDirection =
				LogicalCells[Placement.TargetAnchorCellId]
					.UnitCenter.GetSafeNormal();
			DrawDebugSphere(
				GetWorld(),
				Center + TargetDirection
					* (GetSurfaceRadiusAtDirection(TargetDirection) + 70.0f),
				42.0f,
				8,
				FColor::Red,
				false,
				DrawLifeTime,
				0,
				4.0f);
			DrawDebugSphere(
				GetWorld(),
				Origin + Up * 70.0f,
				48.0f,
				8,
				FColor::White,
				false,
				DrawLifeTime,
				0,
				5.0f);
			DrawDebugString(
				GetWorld(),
				Origin + Up * (Placement.PhysicalBounds.Max.Z + 180.0f),
				FString::Printf(
					TEXT("E%d %s PAD=%d TARGET=%d"),
					Placement.EncounterIndex + 1,
					*Placement.ManifestEntryId.ToString(),
					Placement.PadCenterCellId,
					Placement.TargetAnchorCellId),
				nullptr,
				FColor::White,
				DrawLifeTime,
				false,
				1.0f);
			++JuryFixedSixPlacementCount;
		}
	}
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
		&& (SatellitePreview->bE5OnSatelliteBackside
			|| SatellitePreview->bE1OperatorLandingClusterPlacement)
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
		|| JuryFixedSixPlacementCount > 0
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
	if (!TerrainVisualField->QuerySurfaceGeometry(
			Direction,
			0,
			OutCellId,
			OutSurfaceRadius,
			OutWorldNormal))
	{
		return false;
	}
	OutWorldPosition = GetPlanetCenterWorld() + Direction * OutSurfaceRadius;
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

	int32 SourceCandidateId = INDEX_NONE;
	const TCHAR* SpawnAuthority = TEXT("TaskGraphMainRoute");
	if (JuryMapFreezeV3Result.bMapFreezeReady)
	{
		SourceCandidateId = JuryMapFreezeV3Result.SourceCandidateId;
		SpawnAuthority = TEXT("MapFreezeV3MainRoute");
	}
	else if (bMonthlyPresentationPreviewActive)
	{
		SourceCandidateId = ActiveMonthlyPresentationPreviewCandidateId;
		SpawnAuthority = TEXT("PreviewMainRoute");
	}

	int32 SpawnCellId = INDEX_NONE;
	int32 NextRoadCellId = INDEX_NONE;
	if (SourceCandidateId != INDEX_NONE)
	{
		const FABTSM3MonthlySpatialCandidate* SpatialCandidate =
			MonthlySpatialResult.RetainedCandidates.FindByPredicate(
				[SourceCandidateId](const FABTSM3MonthlySpatialCandidate& Candidate)
				{
					return Candidate.SourceRouteCandidateId == SourceCandidateId;
				});
		if (SpatialCandidate == nullptr
			|| SpatialCandidate->RecomputedRoute.OrderedRoadCellIds.Num() < 2)
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M3][SpawnAuthority] Rejected Authority=%s Candidate=%d Reason=CanonicalRouteUnavailable"),
				SpawnAuthority,
				SourceCandidateId);
			return false;
		}
		SpawnCellId = SpatialCandidate->RecomputedRoute.OrderedRoadCellIds[0];
		NextRoadCellId =
			SpatialCandidate->RecomputedRoute.OrderedRoadCellIds[1];
	}
	else
	{
		const FABTSM3TaskNode* StartTask =
			GeneratedTasks.FindByPredicate([](const FABTSM3TaskNode& Task)
			{
				return Task.Type == EABTSM3TaskType::Start;
			});
		if (StartTask == nullptr) return false;

		const FABTSM3TaskLink* StartMainRoute =
			GeneratedTaskLinks.FindByPredicate(
				[StartTask](const FABTSM3TaskLink& Link)
				{
					return (Link.TaskA == StartTask->TaskId
							|| Link.TaskB == StartTask->TaskId)
						&& (Link.Role == EABTSM3TaskLinkRole::MainPath
							|| Link.Role == EABTSM3TaskLinkRole::LockedGate)
						&& Link.CorridorCells.Num() >= 2;
				});
		if (StartMainRoute == nullptr) return false;

		const bool bStartAtFirstCell =
			StartMainRoute->TaskA == StartTask->TaskId;
		const int32 RouteStartIndex = bStartAtFirstCell
			? 0
			: StartMainRoute->CorridorCells.Num() - 1;
		const int32 RouteNextIndex = bStartAtFirstCell
			? 1
			: StartMainRoute->CorridorCells.Num() - 2;
		SpawnCellId = StartMainRoute->CorridorCells[RouteStartIndex];
		NextRoadCellId = StartMainRoute->CorridorCells[RouteNextIndex];
	}
	if (!LogicalCells.IsValidIndex(SpawnCellId)
		|| !LogicalCells.IsValidIndex(NextRoadCellId))
	{
		return false;
	}

	const FVector SpawnDirection = LogicalCells[SpawnCellId].UnitCenter.GetSafeNormal();
	const FVector SurfaceNormal = TerrainVisualField->GetSurfaceNormal(SpawnDirection);
	const FVector RoadForward = FVector::VectorPlaneProject(
		LogicalCells[NextRoadCellId].UnitCenter - SpawnDirection,
		SurfaceNormal).GetSafeNormal();
	if (SpawnDirection.IsNearlyZero() || SurfaceNormal.IsNearlyZero()
		|| RoadForward.IsNearlyZero()) return false;

	const float CharacterCenterRadius = TerrainVisualField->GetSurfaceRadius(SpawnDirection) + FMath::Max(0.0f, SurfaceOffsetCM);
	const FVector CharacterCenter = GetPlanetCenterWorld() + SpawnDirection * CharacterCenterRadius;
	bool bPhysicalClearanceOverlap = false;
	bool bDynamicClearanceOverlap = false;
	GetJuryFixedSixDecorClearanceOverlaps(
		CharacterCenter - GetPlanetCenterWorld(),
		bPhysicalClearanceOverlap,
		bDynamicClearanceOverlap);
	if (bPhysicalClearanceOverlap || bDynamicClearanceOverlap)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3][SpawnAuthority] Rejected Authority=%s Candidate=%d EndpointCell=%d Reason=ProtectedBuildingClearance Physical=%d Dynamic=%d"),
			SpawnAuthority,
			SourceCandidateId,
			SpawnCellId,
			bPhysicalClearanceOverlap ? 1 : 0,
			bDynamicClearanceOverlap ? 1 : 0);
		return false;
	}
	OutWorldTransform = FTransform(FRotationMatrix::MakeFromXZ(RoadForward, SpawnDirection).ToQuat(), CharacterCenter);
	OutCellId = SpawnCellId;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3][SpawnAuthority] Authority=%s Candidate=%d EndpointOrdinal=0 EndpointCell=%d NextCell=%d Protected=0"),
		SpawnAuthority,
		SourceCandidateId,
		SpawnCellId,
		NextRoadCellId);
	return true;
}

bool AABTSM3Planet::BuildM3ContinuousSurface()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ABTSM3ContinuousSurface_Total);
	check(IsInGameThread());
	const double TotalStartSeconds = FPlatformTime::Seconds();
	if (TerrainVisualField == nullptr || !TerrainVisualField->IsReady()
		|| ContinuousSurface == nullptr)
	{
		return false;
	}

	const uint64 LayoutHashBefore = JuryMapFreezeV3Result.LayoutHash;
	const uint64 CatalogHashBefore =
		JuryMapFreezeV3Result.HandoffContract.PlacementCatalogHash;
	TArray<uint64> PlacementHashesBefore;
	PlacementHashesBefore.Reserve(JuryMapFreezeV3Result.Placements.Num());
	for (const FABTSM3JuryMapFreezeV3Placement& Placement
		: JuryMapFreezeV3Result.Placements)
	{
		PlacementHashesBefore.Add(
			Placement.Site.V3Envelope.PlacementHash);
	}

	const double TopologyStartSeconds = FPlatformTime::Seconds();
	FUnitSphereMesh Mesh;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ABTSM3ContinuousSurface_Icosphere);
		BuildUnitIcosphere(SurfaceSubdivision, Mesh);
	}
	const double TopologyMS =
		(FPlatformTime::Seconds() - TopologyStartSeconds) * 1000.0;

	struct FSampleBuffers
	{
		TArray<int32> CellIds;
		TArray<float> RadiiCM;
		TArray<FVector> Vertices;
		TArray<FVector> Normals;
		TArray<FLinearColor> Colors;
		TArray<float> NormalTiltDegrees;
		TArray<uint8> Valid;

		void SetNumUninitialized(const int32 Num)
		{
			CellIds.SetNumUninitialized(Num);
			RadiiCM.SetNumUninitialized(Num);
			Vertices.SetNumUninitialized(Num);
			Normals.SetNumUninitialized(Num);
			Colors.SetNumUninitialized(Num);
			NormalTiltDegrees.SetNumUninitialized(Num);
			Valid.SetNumUninitialized(Num);
		}
	};

	auto SampleOne = [this, &Mesh](
		const int32 VertexIndex,
		const bool bUseHintedQuery,
		FSampleBuffers& OutBuffers)
	{
		const FVector Unit = Mesh.Vertices[VertexIndex].GetSafeNormal();
		int32 CellId = INDEX_NONE;
		float RadiusCM = 0.0f;
		FVector SurfaceNormal = FVector::UpVector;
		FLinearColor SurfaceColor = FLinearColor::Gray;
		bool bValid = true;
		if (bUseHintedQuery)
		{
			FABTSM3ContinuousSurfaceSample Sample;
			bValid = TerrainVisualField->QueryContinuousSurfaceBaseSample(
				Unit, 0, Sample);
			if (bValid)
			{
				CellId = Sample.CellId;
				RadiusCM = Sample.SurfaceRadiusCM;
				SurfaceColor = Sample.TerrainColor;
			}
		}
		else
		{
			CellId = TerrainVisualField->FindNearestCell(Unit);
			bValid = CellId != INDEX_NONE;
			RadiusCM = TerrainVisualField->GetSurfaceRadius(Unit);
			SurfaceNormal = TerrainVisualField->GetSurfaceNormal(Unit);
			SurfaceColor = TerrainVisualField->GetDebugTerrainColor(Unit);
		}
		OutBuffers.CellIds[VertexIndex] = CellId;
		OutBuffers.RadiiCM[VertexIndex] = RadiusCM;
		OutBuffers.Vertices[VertexIndex] = Unit * RadiusCM;
		OutBuffers.Normals[VertexIndex] = SurfaceNormal;
		OutBuffers.Colors[VertexIndex] = SurfaceColor;
		OutBuffers.NormalTiltDegrees[VertexIndex] =
			FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
				FVector::DotProduct(Unit, SurfaceNormal), -1.0f, 1.0f)));
		OutBuffers.Valid[VertexIndex] = bValid ? 1 : 0;
	};

	FSampleBuffers OptimizedSamples;
	OptimizedSamples.SetNumUninitialized(Mesh.Vertices.Num());
	const double OptimizedSampleStartSeconds = FPlatformTime::Seconds();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ABTSM3ContinuousSurface_HintedSamplesParallel);
		ParallelFor(Mesh.Vertices.Num(),
			[&SampleOne, &OptimizedSamples](const int32 VertexIndex)
			{
				SampleOne(VertexIndex, true, OptimizedSamples);
			});
	}
	const double OptimizedSampleMS =
		(FPlatformTime::Seconds() - OptimizedSampleStartSeconds) * 1000.0;
	for (int32 VertexIndex = 0;
		VertexIndex < OptimizedSamples.Valid.Num();
		++VertexIndex)
	{
		if (OptimizedSamples.Valid[VertexIndex] == 0)
		{
			ContinuousSurface->ClearAllMeshSections();
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M3][ContinuousSurfaceExactOracle] Passed=0 Failure=HintedSample[%d] CommitPMC=0 FailClosed=1"),
				VertexIndex);
			return false;
		}
	}
	const double CanonicalNormalStartSeconds = FPlatformTime::Seconds();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ABTSM3ContinuousSurface_CanonicalNormalsSerial);
		for (int32 VertexIndex = 0;
			VertexIndex < Mesh.Vertices.Num();
			++VertexIndex)
		{
			const FVector Unit = Mesh.Vertices[VertexIndex].GetSafeNormal();
			const FVector SurfaceNormal =
				TerrainVisualField->GetSurfaceNormal(Unit);
			OptimizedSamples.Normals[VertexIndex] = SurfaceNormal;
			OptimizedSamples.NormalTiltDegrees[VertexIndex] =
				FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
					FVector::DotProduct(Unit, SurfaceNormal), -1.0f, 1.0f)));
		}
	}
	const double CanonicalNormalMS =
		(FPlatformTime::Seconds() - CanonicalNormalStartSeconds) * 1000.0;

	struct FExpandedBuffers
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UV0;
		TArray<FVector2D> UV1;
		TArray<FVector2D> UV2;
		TArray<FVector2D> UV3;
		TArray<FLinearColor> Colors;
		TArray<FProcMeshTangent> Tangents;

		void SetNumUninitialized(const int32 Num)
		{
			Vertices.SetNumUninitialized(Num);
			Triangles.SetNumUninitialized(Num);
			Normals.SetNumUninitialized(Num);
			UV0.SetNumUninitialized(Num);
			UV1.SetNumUninitialized(Num);
			UV2.SetNumUninitialized(Num);
			UV3.SetNumUninitialized(Num);
			Colors.SetNumUninitialized(Num);
		}
	};

	auto ExpandOne = [&Mesh](
		const int32 TriangleIndex,
		const FSampleBuffers& Samples,
		FExpandedBuffers& OutBuffers)
	{
		const FIntVector& Triangle = Mesh.Triangles[TriangleIndex];
		const auto EncodeCellId = [](const int32 CellId)
		{
			return FVector2D(
				static_cast<float>(CellId >> 8),
				static_cast<float>(CellId & 0xff));
		};
		const FVector2D EncodedA = EncodeCellId(Samples.CellIds[Triangle.X]);
		const FVector2D EncodedB = EncodeCellId(Samples.CellIds[Triangle.Y]);
		const FVector2D EncodedC = EncodeCellId(Samples.CellIds[Triangle.Z]);
		const int32 BaseIndex = TriangleIndex * 3;
		for (int32 Corner = 0; Corner < 3; ++Corner)
		{
			const int32 OutputIndex = BaseIndex + Corner;
			const int32 SourceVertexIndex = Triangle[Corner];
			OutBuffers.Vertices[OutputIndex] = Samples.Vertices[SourceVertexIndex];
			OutBuffers.Normals[OutputIndex] = Samples.Normals[SourceVertexIndex];
			OutBuffers.UV0[OutputIndex] = EncodedA;
			OutBuffers.UV1[OutputIndex] = EncodedB;
			OutBuffers.UV2[OutputIndex] = EncodedC;
			OutBuffers.UV3[OutputIndex] = FVector2D(
				Corner == 0 ? 1.0f : 0.0f,
				Corner == 1 ? 1.0f : 0.0f);
			OutBuffers.Colors[OutputIndex] = Samples.Colors[SourceVertexIndex];
			OutBuffers.Triangles[OutputIndex] = OutputIndex;
		}
	};

	FExpandedBuffers OptimizedExpanded;
	OptimizedExpanded.SetNumUninitialized(Mesh.Triangles.Num() * 3);
	const double OptimizedExpandStartSeconds = FPlatformTime::Seconds();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ABTSM3ContinuousSurface_TriangleExpandParallel);
		ParallelFor(Mesh.Triangles.Num(),
			[&ExpandOne, &OptimizedSamples, &OptimizedExpanded](
				const int32 TriangleIndex)
			{
				ExpandOne(TriangleIndex, OptimizedSamples, OptimizedExpanded);
			});
	}
	const double OptimizedExpandMS =
		(FPlatformTime::Seconds() - OptimizedExpandStartSeconds) * 1000.0;

	const bool bRunExactOracle =
		CVarABTSM3ContinuousSurfaceExactOracle.GetValueOnGameThread() != 0
		|| FParse::Param(FCommandLine::Get(),
			TEXT("ABTSM3ContinuousSurfaceExactOracle"));
	double LegacySampleMS = 0.0;
	double LegacyExpandMS = 0.0;
	double ExactCompareMS = 0.0;
	FString ExactFailure;
	uint64 QuerySurfaceHashLegacy = 0;
	uint64 QuerySurfaceHashOptimized = 0;
	if (bRunExactOracle)
	{
		FSampleBuffers LegacySamples;
		LegacySamples.SetNumUninitialized(Mesh.Vertices.Num());
		const double LegacySampleStartSeconds = FPlatformTime::Seconds();
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ABTSM3ContinuousSurface_LegacySamplesSerial);
			for (int32 VertexIndex = 0; VertexIndex < Mesh.Vertices.Num(); ++VertexIndex)
			{
				SampleOne(VertexIndex, false, LegacySamples);
			}
		}
		LegacySampleMS =
			(FPlatformTime::Seconds() - LegacySampleStartSeconds) * 1000.0;

		FExpandedBuffers LegacyExpanded;
		LegacyExpanded.SetNumUninitialized(Mesh.Triangles.Num() * 3);
		const double LegacyExpandStartSeconds = FPlatformTime::Seconds();
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ABTSM3ContinuousSurface_LegacyTriangleExpandSerial);
			for (int32 TriangleIndex = 0;
				TriangleIndex < Mesh.Triangles.Num();
				++TriangleIndex)
			{
				ExpandOne(TriangleIndex, LegacySamples, LegacyExpanded);
			}
		}
		LegacyExpandMS =
			(FPlatformTime::Seconds() - LegacyExpandStartSeconds) * 1000.0;

		const double ExactCompareStartSeconds = FPlatformTime::Seconds();
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ABTSM3ContinuousSurface_ExactCompareSerial);
			for (int32 VertexIndex = 0;
				VertexIndex < Mesh.Vertices.Num() && ExactFailure.IsEmpty();
				++VertexIndex)
			{
				const TCHAR* Field = nullptr;
				if (LegacySamples.CellIds[VertexIndex]
						!= OptimizedSamples.CellIds[VertexIndex]
					|| LegacySamples.Valid[VertexIndex]
						!= OptimizedSamples.Valid[VertexIndex])
				{
					Field = TEXT("CellOrValidity");
				}
				else if (LegacySamples.RadiiCM[VertexIndex]
					!= OptimizedSamples.RadiiCM[VertexIndex])
				{
					Field = TEXT("Radius");
				}
				else if (LegacySamples.Normals[VertexIndex]
					!= OptimizedSamples.Normals[VertexIndex])
				{
					Field = TEXT("Normal");
				}
				else if (LegacySamples.Colors[VertexIndex]
					!= OptimizedSamples.Colors[VertexIndex])
				{
					Field = TEXT("Color");
				}
				if (Field != nullptr)
				{
					ExactFailure = FString::Printf(
						TEXT("Sample[%d].%s:Cell(L=%d O=%d) Radius(L=%.17g O=%.17g) Normal(L=(%.17g,%.17g,%.17g) O=(%.17g,%.17g,%.17g)) Color(L=(%.9g,%.9g,%.9g,%.9g) O=(%.9g,%.9g,%.9g,%.9g))"),
						VertexIndex,
						Field,
						LegacySamples.CellIds[VertexIndex],
						OptimizedSamples.CellIds[VertexIndex],
						LegacySamples.RadiiCM[VertexIndex],
						OptimizedSamples.RadiiCM[VertexIndex],
						LegacySamples.Normals[VertexIndex].X,
						LegacySamples.Normals[VertexIndex].Y,
						LegacySamples.Normals[VertexIndex].Z,
						OptimizedSamples.Normals[VertexIndex].X,
						OptimizedSamples.Normals[VertexIndex].Y,
						OptimizedSamples.Normals[VertexIndex].Z,
						LegacySamples.Colors[VertexIndex].R,
						LegacySamples.Colors[VertexIndex].G,
						LegacySamples.Colors[VertexIndex].B,
						LegacySamples.Colors[VertexIndex].A,
						OptimizedSamples.Colors[VertexIndex].R,
						OptimizedSamples.Colors[VertexIndex].G,
						OptimizedSamples.Colors[VertexIndex].B,
						OptimizedSamples.Colors[VertexIndex].A);
				}
			}

			auto CompareExactArray = [&ExactFailure](
				const TCHAR* Name,
				const auto& Legacy,
				const auto& Optimized)
			{
				if (!ExactFailure.IsEmpty()) return;
				if (Legacy.Num() != Optimized.Num())
				{
					ExactFailure = FString::Printf(
						TEXT("%s:Count"), Name);
					return;
				}
				for (int32 Index = 0; Index < Legacy.Num(); ++Index)
				{
					if (Legacy[Index] != Optimized[Index])
					{
						ExactFailure = FString::Printf(
							TEXT("%s[%d]"), Name, Index);
						return;
					}
				}
			};
			CompareExactArray(TEXT("Vertices"), LegacyExpanded.Vertices, OptimizedExpanded.Vertices);
			CompareExactArray(TEXT("Triangles"), LegacyExpanded.Triangles, OptimizedExpanded.Triangles);
			CompareExactArray(TEXT("Normals"), LegacyExpanded.Normals, OptimizedExpanded.Normals);
			CompareExactArray(TEXT("UV0"), LegacyExpanded.UV0, OptimizedExpanded.UV0);
			CompareExactArray(TEXT("UV1"), LegacyExpanded.UV1, OptimizedExpanded.UV1);
			CompareExactArray(TEXT("UV2"), LegacyExpanded.UV2, OptimizedExpanded.UV2);
			CompareExactArray(TEXT("UV3"), LegacyExpanded.UV3, OptimizedExpanded.UV3);
			CompareExactArray(TEXT("Colors"), LegacyExpanded.Colors, OptimizedExpanded.Colors);
		}
		ExactCompareMS =
			(FPlatformTime::Seconds() - ExactCompareStartSeconds) * 1000.0;

		auto ComputeQuerySurfaceHash = [this](const bool bUseHintedQuery)
		{
			static const FVector Directions[] = {
				FVector::ForwardVector, -FVector::ForwardVector,
				FVector::RightVector, -FVector::RightVector,
				FVector::UpVector, -FVector::UpVector,
				FVector(1.0, 1.0, 1.0).GetSafeNormal(),
				FVector(-1.0, 1.0, -1.0).GetSafeNormal()};
			uint64 Hash = 14695981039346656037ull;
			const auto Add = [&Hash](const uint64 Input)
			{
				for (int32 ByteIndex = 0; ByteIndex < 8; ++ByteIndex)
				{
					Hash ^= static_cast<uint8>(
						(Input >> (ByteIndex * 8)) & 0xffull);
					Hash *= 1099511628211ull;
				}
			};
			for (const FVector& Direction : Directions)
			{
				int32 CellId = INDEX_NONE;
				float RadiusCM = 0.0f;
				FVector Normal = FVector::ZeroVector;
				bool bHit = false;
				if (bUseHintedQuery)
				{
					FABTSM3ContinuousSurfaceSample Sample;
					bHit = TerrainVisualField->QueryContinuousSurfaceSample(
						Direction, 0, Sample);
					CellId = Sample.CellId;
					RadiusCM = Sample.SurfaceRadiusCM;
					Normal = Sample.SurfaceNormal;
				}
				else
				{
					CellId = TerrainVisualField->FindNearestCell(Direction);
					RadiusCM = TerrainVisualField->GetSurfaceRadius(Direction);
					Normal = TerrainVisualField->GetSurfaceNormal(Direction);
					bHit = CellId != INDEX_NONE;
				}
				Add(bHit ? 1ull : 0ull);
				Add(static_cast<uint32>(CellId));
				Add(static_cast<uint32>(FMath::RoundToInt(RadiusCM * 100.0f)));
				Add(static_cast<uint32>(FMath::RoundToInt(Normal.X * 1000000.0)));
				Add(static_cast<uint32>(FMath::RoundToInt(Normal.Y * 1000000.0)));
				Add(static_cast<uint32>(FMath::RoundToInt(Normal.Z * 1000000.0)));
			}
			return Hash;
		};
		QuerySurfaceHashLegacy = ComputeQuerySurfaceHash(false);
		QuerySurfaceHashOptimized = ComputeQuerySurfaceHash(true);
		if (ExactFailure.IsEmpty()
			&& QuerySurfaceHashLegacy != QuerySurfaceHashOptimized)
		{
			ExactFailure = TEXT("QuerySurfaceHash");
		}
	}

	if (!ExactFailure.IsEmpty())
	{
		check(IsInGameThread());
		ContinuousSurface->ClearAllMeshSections();
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3][ContinuousSurfaceExactOracle] Passed=0 Failure=%s QuerySurfaceHashLegacy=%016llX QuerySurfaceHashOptimized=%016llX CommitPMC=0 FailClosed=1"),
			*ExactFailure,
			static_cast<unsigned long long>(QuerySurfaceHashLegacy),
			static_cast<unsigned long long>(QuerySurfaceHashOptimized));
		return false;
	}

	float MaxSurfaceNormalTiltDegrees = 0.0f;
	int32 ExtremeSurfaceNormalCount = 0;
	for (const float NormalTiltDegrees : OptimizedSamples.NormalTiltDegrees)
	{
		MaxSurfaceNormalTiltDegrees = FMath::Max(
			MaxSurfaceNormalTiltDegrees, NormalTiltDegrees);
		ExtremeSurfaceNormalCount += NormalTiltDegrees > 80.0f ? 1 : 0;
	}

	check(IsInGameThread());
	const double PMCStartSeconds = FPlatformTime::Seconds();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ABTSM3ContinuousSurface_PMCGameThread);
		ContinuousSurface->ClearAllMeshSections();
		ContinuousSurface->CreateMeshSection_LinearColor(
			0,
			OptimizedExpanded.Vertices,
			OptimizedExpanded.Triangles,
			OptimizedExpanded.Normals,
			OptimizedExpanded.UV0,
			OptimizedExpanded.UV1,
			OptimizedExpanded.UV2,
			OptimizedExpanded.UV3,
			OptimizedExpanded.Colors,
			OptimizedExpanded.Tangents,
			true,
			false);
	}
	const double PMCMS =
		(FPlatformTime::Seconds() - PMCStartSeconds) * 1000.0;
	const double PhysicsStartSeconds = FPlatformTime::Seconds();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ABTSM3ContinuousSurface_PhysicsGameThread);
		// Rebuild Chaos state after installing the runtime-generated M3 section.
		ContinuousSurface->RecreatePhysicsState();
	}
	const double PhysicsMS =
		(FPlatformTime::Seconds() - PhysicsStartSeconds) * 1000.0;
	if (TerrainMaterial) ContinuousSurface->SetMaterial(0, TerrainMaterial);

	bool bV3IdentityUnchanged =
		LayoutHashBefore == JuryMapFreezeV3Result.LayoutHash
		&& CatalogHashBefore
			== JuryMapFreezeV3Result.HandoffContract.PlacementCatalogHash
		&& PlacementHashesBefore.Num()
			== JuryMapFreezeV3Result.Placements.Num();
	for (int32 Index = 0;
		bV3IdentityUnchanged && Index < PlacementHashesBefore.Num();
		++Index)
	{
		bV3IdentityUnchanged = PlacementHashesBefore[Index]
			== JuryMapFreezeV3Result.Placements[Index]
				.Site.V3Envelope.PlacementHash;
	}
	if (!bV3IdentityUnchanged)
	{
		ContinuousSurface->ClearAllMeshSections();
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3][ContinuousSurfaceExactOracle] Passed=0 Failure=V3IdentityMutation CommitPMC=0 FailClosed=1"));
		return false;
	}

	const double TotalMS =
		(FPlatformTime::Seconds() - TotalStartSeconds) * 1000.0;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3][ContinuousSurfaceTiming] Ready=1 Subdivision=%d UniqueSamples=%d Triangles=%d OutputVertices=%d ParallelBaseSamples=1 CanonicalNormalsSerial=1 ParallelExpand=1 StableIndexMerge=1 UObjectWritesGameThread=1 WallMS.Total=%.3f WallMS.Topology=%.3f WallMS.HintedBaseSamples=%.3f WallMS.CanonicalNormals=%.3f WallMS.TriangleExpand=%.3f WallMS.LegacySamples=%.3f WallMS.LegacyExpand=%.3f WallMS.ExactCompare=%.3f WallMS.PMC=%.3f WallMS.Physics=%.3f CpuTrace=ABTSM3ContinuousSurface_*"),
		SurfaceSubdivision,
		OptimizedSamples.Normals.Num(),
		Mesh.Triangles.Num(),
		OptimizedExpanded.Normals.Num(),
		TotalMS,
		TopologyMS,
		OptimizedSampleMS,
		CanonicalNormalMS,
		OptimizedExpandMS,
		LegacySampleMS,
		LegacyExpandMS,
		ExactCompareMS,
		PMCMS,
		PhysicsMS);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3][ContinuousSurfaceExactOracle] Enabled=%d Passed=1 QuerySurfaceHashLegacy=%016llX QuerySurfaceHashOptimized=%016llX LayoutHash=%016llX CatalogHash=%016llX PlacementCount=%d V3IdentityUnchanged=1 FailClosed=1"),
		bRunExactOracle ? 1 : 0,
		static_cast<unsigned long long>(QuerySurfaceHashLegacy),
		static_cast<unsigned long long>(QuerySurfaceHashOptimized),
		static_cast<unsigned long long>(JuryMapFreezeV3Result.LayoutHash),
		static_cast<unsigned long long>(
			JuryMapFreezeV3Result.HandoffContract.PlacementCatalogHash),
		JuryMapFreezeV3Result.Placements.Num());
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3][SurfaceNormals] SmoothingDistance=%.1f MaxTilt=%.2f ExtremeOver80=%d UniqueSamples=%d Vertices=%d"),
		SurfaceNormalSmoothingDistanceCM,
		MaxSurfaceNormalTiltDegrees,
		ExtremeSurfaceNormalCount,
		OptimizedSamples.Normals.Num(),
		OptimizedExpanded.Normals.Num());
	return true;
}

void AABTSM3Planet::BuildDecorInstances(
	const TArray<FABTSM3CellState>*
		PresentationCellStates,
	const FABTSM3MonthlyCandidatePresentation*
		PresentationCandidate)
{
	struct FPendingDecorPlacement
	{
		UHierarchicalInstancedStaticMeshComponent* TargetHISM = nullptr;
		const FABTSM3DecorCollisionShape* CollisionShape = nullptr;
		FTransform Transform = FTransform::Identity;
		FABTSM3DecorOrientedBounds Bounds;
		uint8 Type = 0;
	};

	DecorPlacementSummary = FABTSM3DecorPlacementSummary();
	ForestHISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	RockHISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
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
	JuryFixedSixDecorClearanceRejectedCount = 0;

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

	FABTSM3DecorCollisionShape ForestCollisionShape;
	FABTSM3DecorCollisionShape RockCollisionShape;
	FString ForestCollisionFailure;
	FString RockCollisionFailure;
	if (!FABTSM3DecorPlacementGeometry::BuildCollisionShape(
			ResolvedForestMesh,
			ForestCollisionShape,
			ForestCollisionFailure)
		|| !FABTSM3DecorPlacementGeometry::BuildCollisionShape(
			ResolvedRockMesh,
			RockCollisionShape,
			RockCollisionFailure))
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3][HISMPlacement] Accepted=0 Failure=CollisionDescriptionUnavailable Forest=%s Rock=%s"),
			*ForestCollisionFailure,
			*RockCollisionFailure);
		return;
	}
	if (InstancesPerCell <= 0)
	{
		DecorPlacementSummary.bAccepted = true;
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M3][HISM] InstancesPerCell=%d; decoration generation is disabled."),
			InstancesPerCell);
		return;
	}

	constexpr float MaximumDecorScale = 1.25f * 1.12f * 1.05f;
	const float SpatialCellSizeCM = FMath::Max(
		ForestCollisionShape.LocalBounds.GetSize().GetMax(),
		RockCollisionShape.LocalBounds.GetSize().GetMax())
		* MaximumDecorScale
		+ FMath::Max(DecorInstanceSeparationMarginCM, 0.0f);
	FABTSM3DecorSpatialHash SpatialHash(SpatialCellSizeCM);
	TArray<FPendingDecorPlacement> PendingPlacements;
	PendingPlacements.Reserve(LogicalCells.Num() * InstancesPerCell);
	int32 EligibleForestCells = 0;
	int32 EligibleRockCells = 0;
	float MaxForestSurfaceTiltDegrees = 0.0f;
	float MaxForestAppliedTiltDegrees = 0.0f;
	int32 ProtectedCellCount = 0;
	int32 PlannedInstanceBudget = 0;
	float MinimumGroundClearanceCM = TNumericLimits<float>::Max();
	float MinimumPairAxisGapCM = TNumericLimits<float>::Max();
	const int32 ResolvedAttemptsPerSlot = FMath::Clamp(
		DecorPlacementAttemptsPerSlot, 1, 32);
	const float ResolvedGroundClearanceCM = FMath::Max(
		DecorGroundClearanceCM, 0.0f);
	const float ResolvedSeparationMarginCM = FMath::Max(
		DecorInstanceSeparationMarginCM, 0.0f);
	const float ResolvedMaximumAdditionalSeatLiftCM = FMath::Max(
		DecorMaximumAdditionalSeatLiftCM, 0.0f);
	const TArray<FABTSM3CellState>& EffectiveCellStates =
		PresentationCellStates != nullptr
		? *PresentationCellStates
		: GeneratedCellStates;

	for (int32 CellId = 0; CellId < LogicalCells.Num(); ++CellId)
	{
		if (!EffectiveCellStates.IsValidIndex(CellId)) continue;
		const FABTSM3CellState& State = EffectiveCellStates[CellId];
		const FABTSM3MonthlyPresentationCell* PresentationCell =
			PresentationCandidate != nullptr
				&& PresentationCandidate->Cells.IsValidIndex(CellId)
			? &PresentationCandidate->Cells[CellId]
			: nullptr;
		if (PresentationCell != nullptr
			&& (PresentationCell->CellId != CellId
				|| PresentationCell->bDecorationProtected))
		{
			ProtectedCellCount += PresentationCell->bDecorationProtected ? 1 : 0;
			continue;
		}
		if (State.bRoad || State.bBuildingAnchor || State.bWater) continue;

		UHierarchicalInstancedStaticMeshComponent* TargetHISM = nullptr;
		const FABTSM3DecorCollisionShape* CollisionShape = nullptr;
		uint8 DecorType = 0;
		if (State.TerrainType == EABTSM3TerrainType::Forest && ResolvedForestMesh)
		{
			TargetHISM = ForestHISM;
			CollisionShape = &ForestCollisionShape;
			DecorType = 1;
			++EligibleForestCells;
		}
		else if (State.TerrainType == EABTSM3TerrainType::Mountain && ResolvedRockMesh)
		{
			TargetHISM = RockHISM;
			CollisionShape = &RockCollisionShape;
			DecorType = 2;
			++EligibleRockCells;
		}
		if (TargetHISM == nullptr || CollisionShape == nullptr) continue;

		int32 CellInstanceCount = InstancesPerCell;
		if (PresentationCell != nullptr)
		{
			const int32 RequiredDecorationMask = TargetHISM == ForestHISM
				? static_cast<int32>(EABTSM3MonthlyDecorationKind::Forest)
				: static_cast<int32>(EABTSM3MonthlyDecorationKind::Rock);
			if ((PresentationCell->DecorationKindMask & RequiredDecorationMask) == 0)
			{
				continue;
			}
			CellInstanceCount = FMath::Min(
				CellInstanceCount,
				PresentationCell->MaxDecorationInstances);
			PlannedInstanceBudget += CellInstanceCount;
		}
		if (CellInstanceCount <= 0) continue;

		int32 AccentVariantId = 0;
		if (PresentationCell != nullptr)
		{
			const FABTSM3MonthlyVisualBeat* Beat =
				PresentationCandidate->VisualBeats.FindByPredicate(
					[PresentationCell](const FABTSM3MonthlyVisualBeat& Item)
					{
						return Item.VisualBeatId == PresentationCell->VisualBeatId;
					});
			if (Beat == nullptr) continue;
			AccentVariantId = Beat->AccentVariantId;
			if ((AccentVariantId & 1) == 0 && CellInstanceCount > 1)
			{
				--CellInstanceCount;
			}
		}
		const uint32 VisualVariantSeed = PresentationCell != nullptr
			? HashCombineFast(
				GetTypeHash(PresentationCell->ThemeVariantId),
				GetTypeHash(AccentVariantId))
			: 0u;
		const FVector Center = LogicalCells[CellId].UnitCenter;
		if (LogicalCells[CellId].NeighborCellIds.IsEmpty()) continue;

		for (int32 Slot = 0; Slot < CellInstanceCount; ++Slot)
		{
			++DecorPlacementSummary.RequestedSlots;
			for (int32 Attempt = 0; Attempt < ResolvedAttemptsPerSlot; ++Attempt)
			{
				FRandomStream AttemptStream(
					FABTSM3DecorPlacementGeometry::MakeAttemptSeed(
						WorldSeed,
						CellId,
						Slot,
						Attempt,
						VisualVariantSeed));
				const int32 NeighborId = LogicalCells[CellId].NeighborCellIds[
					AttemptStream.RandRange(
						0,
						LogicalCells[CellId].NeighborCellIds.Num() - 1)];
				if (!LogicalCells.IsValidIndex(NeighborId))
				{
					++DecorPlacementSummary.RejectedProtectedOrReserved;
					continue;
				}
				const FVector Direction = FMath::Lerp(
					Center,
					LogicalCells[NeighborId].UnitCenter,
					AttemptStream.FRandRange(0.0f, 0.42f)).GetSafeNormal();
				if (Direction.IsNearlyZero())
				{
					++DecorPlacementSummary.RejectedGround;
					continue;
				}
				if (PresentationCandidate != nullptr)
				{
					const int32 ResolvedCellId = FindNearestCell(Direction);
					if (ResolvedCellId != CellId
						|| !PresentationCandidate->Cells.IsValidIndex(ResolvedCellId)
						|| PresentationCandidate->Cells[ResolvedCellId].bDecorationProtected)
					{
						++DecorPlacementSummary.RejectedProtectedOrReserved;
						continue;
					}
				}
				if (TerrainVisualField->IsInsideBuildingPad(Direction))
				{
					++DecorPlacementSummary.RejectedProtectedOrReserved;
					continue;
				}
				const float SurfaceRadiusCM = TerrainVisualField->GetSurfaceRadius(Direction);
				const FVector PlanetLocalSurfaceLocation = Direction * SurfaceRadiusCM;

				const FVector RadialUp = Direction;
				FVector SurfaceUp = TerrainVisualField->GetSurfaceNormal(Direction).GetSafeNormal();
				if (FVector::DotProduct(SurfaceUp, RadialUp) < 0.0f) SurfaceUp *= -1.0f;
				FVector Up = SurfaceUp;
				float SurfaceTiltDegrees = 0.0f;
				float AppliedTiltDegrees = 0.0f;
				if (TargetHISM == ForestHISM)
				{
					const float SurfaceBlend = FMath::Clamp(
						ForestSurfaceNormalBlend, 0.0f, 1.0f);
					Up = FMath::Lerp(RadialUp, SurfaceUp, SurfaceBlend).GetSafeNormal();
					if (Up.IsNearlyZero()) Up = RadialUp;
					SurfaceTiltDegrees = FMath::RadiansToDegrees(FMath::Acos(
						FMath::Clamp(FVector::DotProduct(RadialUp, SurfaceUp), -1.0f, 1.0f)));
					AppliedTiltDegrees = FMath::RadiansToDegrees(FMath::Acos(
						FMath::Clamp(FVector::DotProduct(RadialUp, Up), -1.0f, 1.0f)));
				}
				FVector Forward = FVector::VectorPlaneProject(
					LogicalCells[NeighborId].UnitCenter - Center,
					Up).GetSafeNormal();
				if (Forward.IsNearlyZero())
				{
					Forward = FVector::VectorPlaneProject(FVector::ForwardVector, Up).GetSafeNormal();
				}
				if (Forward.IsNearlyZero())
				{
					Forward = FVector::VectorPlaneProject(FVector::RightVector, Up).GetSafeNormal();
				}
				const FQuat Rotation = FRotationMatrix::MakeFromXZ(Forward, Up).ToQuat();
				const float BeatScale = PresentationCell != nullptr
					&& (AccentVariantId & 1) != 0
					? 1.12f
					: (PresentationCell != nullptr ? 0.88f : 1.0f);
				const float ThemeScale = PresentationCell != nullptr
					&& (PresentationCell->ThemeVariantId & 1) != 0
					? 1.05f
					: (PresentationCell != nullptr ? 0.95f : 1.0f);
				const float Scale = AttemptStream.FRandRange(0.75f, 1.25f)
					* BeatScale * ThemeScale;

				FTransform CandidateTransform;
				float SeatCorrectionCM = 0.0f;
				float CandidateMinimumGroundClearanceCM = 0.0f;
				if (!FABTSM3DecorPlacementGeometry::TrySeatOnSurface(
						*CollisionShape,
						RadialUp,
						Rotation,
						Scale,
						PlanetLocalSurfaceLocation,
						ResolvedGroundClearanceCM,
						[this](const FVector& Point)
						{
							const FVector PointDirection = Point.GetSafeNormal();
							if (PointDirection.IsNearlyZero())
							{
								return -TNumericLimits<float>::Max();
							}
							return static_cast<float>(Point.Size())
								- TerrainVisualField->GetSurfaceRadius(PointDirection);
						},
						CandidateTransform,
						SeatCorrectionCM,
						CandidateMinimumGroundClearanceCM))
				{
					++DecorPlacementSummary.RejectedGround;
					continue;
				}
				const float PivotConventionCorrectionCM = FMath::Max(
					0.0f,
					- static_cast<float>(CollisionShape->LocalBounds.Min.Z)
						* Scale
						+ ResolvedGroundClearanceCM);
				if (SeatCorrectionCM
					> PivotConventionCorrectionCM
						+ ResolvedMaximumAdditionalSeatLiftCM)
				{
					++DecorPlacementSummary.RejectedGround;
					continue;
				}
				bool bPhysicalClearanceOverlap = false;
				bool bDynamicClearanceOverlap = false;
				GetJuryFixedSixDecorClearanceOverlaps(
					CandidateTransform.GetLocation(),
					bPhysicalClearanceOverlap,
					bDynamicClearanceOverlap);
				if (bPhysicalClearanceOverlap || bDynamicClearanceOverlap)
				{
					++JuryFixedSixDecorClearanceRejectedCount;
					++DecorPlacementSummary.RejectedProtectedOrReserved;
					continue;
				}

				const FABTSM3DecorOrientedBounds CandidateBounds =
					FABTSM3DecorPlacementGeometry::BuildOrientedBounds(
						*CollisionShape,
						CandidateTransform);
				float CandidatePairAxisGapCM = TNumericLimits<float>::Max();
				if (SpatialHash.WouldOverlap(
						CandidateBounds,
						ResolvedSeparationMarginCM,
						CandidatePairAxisGapCM))
				{
					++DecorPlacementSummary.RejectedPairOverlap;
					continue;
				}

				FPendingDecorPlacement& Accepted = PendingPlacements.AddDefaulted_GetRef();
				Accepted.TargetHISM = TargetHISM;
				Accepted.CollisionShape = CollisionShape;
				Accepted.Transform = CandidateTransform;
				Accepted.Bounds = CandidateBounds;
				Accepted.Type = DecorType;
				SpatialHash.Add(CandidateBounds);
				DecorPlacementSummary.MaxSeatCorrectionCM = FMath::Max(
					DecorPlacementSummary.MaxSeatCorrectionCM,
					SeatCorrectionCM);
				MinimumGroundClearanceCM = FMath::Min(
					MinimumGroundClearanceCM,
					CandidateMinimumGroundClearanceCM);
				if (FMath::IsFinite(CandidatePairAxisGapCM))
				{
					MinimumPairAxisGapCM = FMath::Min(
						MinimumPairAxisGapCM,
						CandidatePairAxisGapCM);
				}
				if (TargetHISM == ForestHISM)
				{
					MaxForestSurfaceTiltDegrees = FMath::Max(
						MaxForestSurfaceTiltDegrees,
						SurfaceTiltDegrees);
					MaxForestAppliedTiltDegrees = FMath::Max(
						MaxForestAppliedTiltDegrees,
						AppliedTiltDegrees);
				}
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
				break;
			}
		}
	}

	// Re-run the pure geometry gate over the complete candidate set before any
	// transform enters a HISM. A failure publishes no partial decoration world.
	FABTSM3DecorSpatialHash ValidationHash(SpatialCellSizeCM);
	bool bFinalValidationPassed = true;
	for (const FPendingDecorPlacement& Placement : PendingPlacements)
	{
		float IgnoredAxisGapCM = 0.0f;
		if (ValidationHash.WouldOverlap(
				Placement.Bounds,
				ResolvedSeparationMarginCM,
				IgnoredAxisGapCM))
		{
			bFinalValidationPassed = false;
			break;
		}
		for (const FVector& LocalSample : Placement.CollisionShape->LocalSurfaceSamples)
		{
			const FVector Point = Placement.Transform.TransformPosition(LocalSample);
			const FVector PointDirection = Point.GetSafeNormal();
			const float SignedDistanceCM = PointDirection.IsNearlyZero()
				? -TNumericLimits<float>::Max()
				: static_cast<float>(Point.Size())
					- TerrainVisualField->GetSurfaceRadius(PointDirection);
			if (!FMath::IsFinite(SignedDistanceCM)
				|| SignedDistanceCM + 0.02f < ResolvedGroundClearanceCM)
			{
				bFinalValidationPassed = false;
				break;
			}
		}
		if (!bFinalValidationPassed) break;
		ValidationHash.Add(Placement.Bounds);
	}
	if (!bFinalValidationPassed)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M3][HISMPlacement] Accepted=0 Failure=FinalCollisionValidation Requested=%d CandidateAccepted=%d"),
			DecorPlacementSummary.RequestedSlots,
			PendingPlacements.Num());
		return;
	}

	TArray<FTransform> ForestTransforms;
	TArray<FTransform> RockTransforms;
	ForestTransforms.Reserve(PendingPlacements.Num());
	RockTransforms.Reserve(PendingPlacements.Num());
	uint64 PlacementResultHash = 1469598103934665603ull;
	HashABTSM3DecorPlacementValue(
		PlacementResultHash,
		DecorPlacementSummary.PlacementAlgorithmVersion);
	HashABTSM3DecorPlacementValue(
		PlacementResultHash,
		ResolvedAttemptsPerSlot);
	HashABTSM3DecorPlacementValue(
		PlacementResultHash,
		FMath::RoundToInt64(ResolvedGroundClearanceCM * 100.0));
	HashABTSM3DecorPlacementValue(
		PlacementResultHash,
		FMath::RoundToInt64(ResolvedMaximumAdditionalSeatLiftCM * 100.0));
	HashABTSM3DecorPlacementValue(
		PlacementResultHash,
		FMath::RoundToInt64(ResolvedSeparationMarginCM * 100.0));
	HashABTSM3DecorCollisionShape(
		PlacementResultHash,
		1,
		ForestCollisionShape);
	HashABTSM3DecorCollisionShape(
		PlacementResultHash,
		2,
		RockCollisionShape);
	for (const FPendingDecorPlacement& Placement : PendingPlacements)
	{
		if (Placement.TargetHISM == ForestHISM)
		{
			ForestTransforms.Add(Placement.Transform);
		}
		else
		{
			RockTransforms.Add(Placement.Transform);
		}
		HashABTSM3DecorPlacementTransform(
			PlacementResultHash,
			Placement.Type,
			Placement.Transform);
	}
	ForestHISM->AddInstances(ForestTransforms, false, false, true);
	RockHISM->AddInstances(RockTransforms, false, false, true);
	DecorPlacementSummary.bAccepted = true;
	DecorPlacementSummary.AcceptedInstances = PendingPlacements.Num();
	DecorPlacementSummary.MinimumGroundClearanceCM =
		FMath::IsFinite(MinimumGroundClearanceCM)
		? MinimumGroundClearanceCM
		: 0.0f;
	DecorPlacementSummary.MinimumPairAxisGapCM =
		FMath::IsFinite(MinimumPairAxisGapCM)
		? MinimumPairAxisGapCM
		: (PendingPlacements.Num() > 1
			? ResolvedSeparationMarginCM
			: 0.0f);
	DecorPlacementSummary.PlacementResultHash =
		static_cast<int64>(PlacementResultHash);

	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3][HISMPlacement] Accepted=1 Version=%d AttemptsPerSlot=%d Requested=%d AcceptedInstances=%d RejectedProtected=%d RejectedGround=%d RejectedPairOverlap=%d GroundClearanceCM=%.2f MaximumAdditionalSeatLiftCM=%.2f SeparationMarginCM=%.2f MaxSeatCorrectionCM=%.2f MinimumGroundClearanceCM=%.2f MinimumPairAxisGapCM=%.2f ResultHash=%lld"),
		DecorPlacementSummary.PlacementAlgorithmVersion,
		ResolvedAttemptsPerSlot,
		DecorPlacementSummary.RequestedSlots,
		DecorPlacementSummary.AcceptedInstances,
		DecorPlacementSummary.RejectedProtectedOrReserved,
		DecorPlacementSummary.RejectedGround,
		DecorPlacementSummary.RejectedPairOverlap,
		ResolvedGroundClearanceCM,
		ResolvedMaximumAdditionalSeatLiftCM,
		ResolvedSeparationMarginCM,
		DecorPlacementSummary.MaxSeatCorrectionCM,
		DecorPlacementSummary.MinimumGroundClearanceCM,
		DecorPlacementSummary.MinimumPairAxisGapCM,
		DecorPlacementSummary.PlacementResultHash);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M3][HISM] ForestMesh=%s RockMesh=%s ForestMaterialsValid=%d RockMaterialsValid=%d EligibleForestCells=%d EligibleRockCells=%d ForestInstances=%d RockInstances=%d ForestNormalBlend=%.2f MaxSurfaceTilt=%.2f MaxAppliedTilt=%.2f M3R5PreviewAuthority=%d ProtectedCells=%d PlannedInstanceBudget=%d Accent0Instances=%d Accent1Instances=%d JuryDynamicClearanceRejected=%d Collision=QueryAndPhysics ObstacleChannel=%d SimulatePhysics=0"),
		*GetNameSafe(ResolvedForestMesh), *GetNameSafe(ResolvedRockMesh),
		bForestMaterialsValid ? 1 : 0, bRockMaterialsValid ? 1 : 0,
		EligibleForestCells, EligibleRockCells,
		ForestHISM->GetInstanceCount(), RockHISM->GetInstanceCount(),
		FMath::Clamp(ForestSurfaceNormalBlend, 0.0f, 1.0f),
		MaxForestSurfaceTiltDegrees, MaxForestAppliedTiltDegrees,
		PresentationCandidate != nullptr ? 1 : 0,
		ProtectedCellCount, PlannedInstanceBudget,
		MonthlyDecorAccent0InstanceCount, MonthlyDecorAccent1InstanceCount,
		JuryFixedSixDecorClearanceRejectedCount,
		static_cast<int32>(ABTSDeveloperObstacleChannel));
}

bool AABTSM3Planet::AppendJuryFixedSixTerrainPads(
	TArray<FABTSM3BuildingSpawnSite>& InOutTerrainPads,
	FString& OutFailure)
{
	JuryFixedSixTerrainPadCount = 0;
	JuryFixedSixTerrainPads.Reset();
	JuryFixedSixTerrainSourceHeightDeltasCM.Reset();
	OutFailure.Reset();
	if (WorldSeed != FABTSM3JuryFixedSixLayoutBuilder::FrozenWorldSeed)
	{
		return true;
	}

	const FABTSM3JuryFixedSixLayoutResult& Result =
		MonthlyJuryFixedSixLayoutResult;
	if (!Result.bPlacementReady
		|| TerrainVisualField == nullptr
		|| !TerrainVisualField->IsReady()
		|| Result.RejectReason != EABTSM3JuryFixedSixRejectReason::None
		|| Result.Placements.Num()
			!= FABTSM3JuryFixedSixLayoutBuilder::ExpectedEncounterCount
		|| static_cast<uint64>(Result.LayoutHash)
			!= FABTSM3JuryFixedSixLayoutBuilder::ComputeLayoutHash(Result))
	{
		OutFailure = TEXT("FixedSixLayoutUnavailableOrInvalid");
		return false;
	}

	TArray<FABTSM3JuryTerrainPadSource> Sources;
	Sources.Reserve(
		FABTSM3JuryFixedSixLayoutBuilder::ExpectedEncounterCount);
	for (const FABTSM3JuryBuildingPlacement& Placement : Result.Placements)
	{
		if (!LogicalCells.IsValidIndex(Placement.PadCenterCellId))
		{
			OutFailure = FString::Printf(
				TEXT("InvalidPadCenterCell:%d"),
				Placement.EncounterIndex);
			return false;
		}
		FABTSM3JuryTerrainPadSource& Source =
			Sources.AddDefaulted_GetRef();
		Source.EncounterIndex = Placement.EncounterIndex;
		Source.PadCenterCellId = Placement.PadCenterCellId;
		Source.PlanetLocalCenterCM = Placement.WorldLocationCM;
		Source.Forward = Placement.WorldForwardAxis.GetSafeNormal();
		Source.Right = Placement.WorldRightAxis.GetSafeNormal();
		Source.Up = Placement.WorldUpAxis.GetSafeNormal();
		Source.PadHalfExtentCM = Placement.RequiredPadHalfExtentCM;
		Source.TargetRadiusCM = FVector::DotProduct(
			Placement.WorldLocationCM,
			Source.Up);
	}
	TArray<FABTSM3BuildingSpawnSite> JuryTerrainPads;
	TArray<float> JuryTerrainSourceHeightDeltasCM;
	if (!BuildABTSM3JuryTerrainPads(
			Sources,
			GetPlanetCenterWorld(),
			PlanetRadiusCM,
			JuryFixedSixMaximumGradeSlopeDegrees,
			JuryFixedSixMinimumGradeWidthCM,
			1.725f,
			SurfaceNormalSmoothingDistanceCM,
			*TerrainVisualField,
			JuryTerrainPads,
			JuryTerrainSourceHeightDeltasCM,
			OutFailure))
	{
		return false;
	}
	if (JuryTerrainPads.Num()
		!= FABTSM3JuryFixedSixLayoutBuilder::ExpectedEncounterCount)
	{
		OutFailure = TEXT("TerrainPadCountMismatch");
		return false;
	}
	InOutTerrainPads.Append(JuryTerrainPads);
	JuryFixedSixTerrainPads = JuryTerrainPads;
	JuryFixedSixTerrainSourceHeightDeltasCM =
		MoveTemp(JuryTerrainSourceHeightDeltasCM);
	JuryFixedSixTerrainPadCount = JuryTerrainPads.Num();
	return true;
}

bool AABTSM3Planet::AppendJuryMapFreezeV3TerrainPads(
	TArray<FABTSM3BuildingSpawnSite>& InOutTerrainPads,
	FString& OutFailure)
{
	JuryFixedSixTerrainPadCount = 0;
	JuryFixedSixTerrainPads.Reset();
	JuryFixedSixTerrainSourceHeightDeltasCM.Reset();
	OutFailure.Reset();
	if (WorldSeed != FABTSM3JuryMapFreezeV3Builder::FrozenWorldSeed)
	{
		return true;
	}
	if (FABTSJuryDemoFixedSixContract::ProductionContractVersion
		!= FABTSJuryDemoFixedSixContract::SupportedV3ContractVersion)
	{
		OutFailure = TEXT("MapFreezeV3IsNotProductionContract");
		return false;
	}
	if (TerrainVisualField == nullptr
		|| !TerrainVisualField->IsReady()
		|| !JuryMapFreezeV3Result.bMapFreezeReady
		|| JuryMapFreezeV3Result.RejectReason
			!= EABTSM3JuryMapFreezeV3RejectReason::None
		|| JuryMapFreezeV3Result.Placements.Num()
			!= FABTSM3JuryMapFreezeV3Builder::ExpectedSiteCount
		|| JuryMapFreezeV3Result.LayoutHash
			!= FABTSM3JuryMapFreezeV3Builder::ComputeLayoutHash(
				JuryMapFreezeV3Result))
	{
		OutFailure = TEXT("MapFreezeV3UnavailableOrInvalid");
		return false;
	}

	TArray<FABTSM3JuryTerrainPadSource> Sources;
	Sources.Reserve(FABTSM3JuryMapFreezeV3Builder::ExpectedPrimarySiteCount);
	for (const FABTSM3JuryMapFreezeV3Placement& Placement
		: JuryMapFreezeV3Result.Placements)
	{
		const FABTSJuryDemoFixedSixBuildingSite& Site = Placement.Site;
		if (Site.V3Envelope.SurfaceKind
			== EABTSJuryDemoFixedSixSurfaceKind::Satellite)
		{
			continue;
		}
		const FVector Forward =
			Site.WorldTransform.GetUnitAxis(EAxis::X).GetSafeNormal();
		const FVector Right =
			Site.WorldTransform.GetUnitAxis(EAxis::Y).GetSafeNormal();
		const FVector Up =
			Site.WorldTransform.GetUnitAxis(EAxis::Z).GetSafeNormal();
		const FVector PlanetLocalCenterCM =
			Site.WorldTransform.GetLocation() - GetPlanetCenterWorld();
		const float TargetRadiusCM =
			static_cast<float>(Site.V3Envelope.SupportRadiusCM);
		const FVector ExpectedWorldLocation = GetPlanetCenterWorld()
			+ Up * TargetRadiusCM;
		if (Site.V3Envelope.SurfaceKind
				!= EABTSJuryDemoFixedSixSurfaceKind::PrimaryPlanet
			|| !LogicalCells.IsValidIndex(Placement.PadCenterCellId)
			|| Site.V3Envelope.PadBounds.IsValid == 0
			|| !Site.V3Envelope.SupportCenterWorldCM.Equals(
				GetPlanetCenterWorld(), 0.1)
			|| !Site.WorldTransform.GetLocation().Equals(
				ExpectedWorldLocation, 0.1)
			|| !FMath::IsNearlyEqual(TargetRadiusCM, PlanetRadiusCM, 0.1f))
		{
			OutFailure = FString::Printf(
				TEXT("InvalidV3PrimarySupport:%d"),
				Site.EncounterIndex);
			return false;
		}

		FABTSM3JuryTerrainPadSource& Source =
			Sources.AddDefaulted_GetRef();
		Source.EncounterIndex = Site.EncounterIndex;
		Source.PadCenterCellId = Placement.PadCenterCellId;
		Source.PlanetLocalCenterCM = PlanetLocalCenterCM;
		Source.Forward = Forward;
		Source.Right = Right;
		Source.Up = Up;
		// The frozen symmetric half extent is the conservative horizontal
		// envelope of the exact (potentially asymmetric) V3 PadBounds.
		Source.PadHalfExtentCM = Site.PadHalfExtentCM;
		Source.TargetRadiusCM = TargetRadiusCM;
	}
	if (Sources.Num()
		!= FABTSM3JuryMapFreezeV3Builder::ExpectedPrimarySiteCount)
	{
		OutFailure = FString::Printf(
			TEXT("V3PrimaryTerrainPadCount:%d"),
			Sources.Num());
		return false;
	}

	TArray<FABTSM3BuildingSpawnSite> JuryTerrainPads;
	TArray<float> JuryTerrainSourceHeightDeltasCM;
	if (!BuildABTSM3JuryTerrainPads(
			Sources,
			GetPlanetCenterWorld(),
			PlanetRadiusCM,
			JuryFixedSixMaximumGradeSlopeDegrees,
			JuryFixedSixMinimumGradeWidthCM,
			1.725f,
			SurfaceNormalSmoothingDistanceCM,
			*TerrainVisualField,
			JuryTerrainPads,
			JuryTerrainSourceHeightDeltasCM,
			OutFailure))
	{
		return false;
	}
	InOutTerrainPads.Append(JuryTerrainPads);
	JuryFixedSixTerrainPads = JuryTerrainPads;
	JuryFixedSixTerrainSourceHeightDeltasCM =
		MoveTemp(JuryTerrainSourceHeightDeltasCM);
	JuryFixedSixTerrainPadCount = JuryTerrainPads.Num();
	return true;
}

void AABTSM3Planet::GetJuryFixedSixDecorClearanceOverlaps(
	const FVector& PlanetLocalLocation,
	bool& bOutPhysicalOverlap,
	bool& bOutDynamicOverlap) const
{
	bOutPhysicalOverlap = false;
	bOutDynamicOverlap = false;
	if (FABTSJuryDemoFixedSixContract::ProductionContractVersion
			== FABTSJuryDemoFixedSixContract::SupportedV3ContractVersion
		&& JuryMapFreezeV3Result.bMapFreezeReady)
	{
		const FVector WorldLocation =
			GetPlanetCenterWorld() + PlanetLocalLocation;
		for (const FABTSM3JuryMapFreezeV3Placement& Placement
			: JuryMapFreezeV3Result.Placements)
		{
			const FABTSJuryDemoFixedSixBuildingSite& Site = Placement.Site;
			if (Site.V3Envelope.SurfaceKind
				!= EABTSJuryDemoFixedSixSurfaceKind::PrimaryPlanet)
			{
				continue;
			}
			const FVector SiteLocalLocation =
				Site.WorldTransform.InverseTransformPosition(WorldLocation);
			const FBox& PhysicalBounds = Site.V3Envelope.SiteLocalBounds;
			const FBox& EffectBounds = Site.V3Envelope.EffectBounds;
			if (PhysicalBounds.IsValid != 0
				&& SiteLocalLocation.X >= PhysicalBounds.Min.X
				&& SiteLocalLocation.X <= PhysicalBounds.Max.X
				&& SiteLocalLocation.Y >= PhysicalBounds.Min.Y
				&& SiteLocalLocation.Y <= PhysicalBounds.Max.Y)
			{
				bOutPhysicalOverlap = true;
			}
			if (EffectBounds.IsValid != 0
				&& SiteLocalLocation.X >= EffectBounds.Min.X
				&& SiteLocalLocation.X <= EffectBounds.Max.X
				&& SiteLocalLocation.Y >= EffectBounds.Min.Y
				&& SiteLocalLocation.Y <= EffectBounds.Max.Y)
			{
				bOutDynamicOverlap = true;
			}
			if (bOutPhysicalOverlap && bOutDynamicOverlap)
			{
				return;
			}
		}
		return;
	}
	if (!MonthlyJuryFixedSixLayoutResult.bPlacementReady)
	{
		return;
	}
	for (const FABTSM3JuryBuildingPlacement& Placement
		: MonthlyJuryFixedSixLayoutResult.Placements)
	{
		const FVector Offset = PlanetLocalLocation
			- Placement.WorldLocationCM;
		const float LocalX = FVector::DotProduct(
			Offset,
			Placement.WorldForwardAxis);
		const float LocalY = FVector::DotProduct(
			Offset,
			Placement.WorldRightAxis);
		if (Placement.PhysicalBounds.IsValid != 0
			&& LocalX >= Placement.PhysicalBounds.Min.X
			&& LocalX <= Placement.PhysicalBounds.Max.X
			&& LocalY >= Placement.PhysicalBounds.Min.Y
			&& LocalY <= Placement.PhysicalBounds.Max.Y)
		{
			bOutPhysicalOverlap = true;
		}
		if (Placement.bDynamicEnvelopeRequired
			&& Placement.EffectBounds.IsValid != 0
			&& LocalX >= Placement.EffectBounds.Min.X
			&& LocalX <= Placement.EffectBounds.Max.X
			&& LocalY >= Placement.EffectBounds.Min.Y
			&& LocalY <= Placement.EffectBounds.Max.Y)
		{
			bOutDynamicOverlap = true;
		}
		if (bOutPhysicalOverlap && bOutDynamicOverlap)
		{
			return;
		}
	}
}

bool AABTSM3Planet::ValidateJuryFixedSixProductionClearance(
	int32& OutTerrainPadCount,
	int32& OutPhysicalOverlapInstanceCount,
	int32& OutDynamicOverlapInstanceCount,
	float& OutMaxPadResidualCM,
	FABTSM3JuryTerrainGradeDiagnostics& OutGradeDiagnostics,
	FString& OutFailure) const
{
	constexpr float MaxAllowedPadResidualCM = 0.5f;
	constexpr float MaxAllowedEdgeHeightResidualCM = 0.05f;
	constexpr float MaxAllowedCollisionResidualCM = 35.0f;
	constexpr float GradeSlopeToleranceDegrees = 2.0f;
	constexpr int32 GradeRayCount = 16;
	constexpr int32 MinimumGradeSamplesPerRay = 32;
	constexpr int32 MaximumGradeSamplesPerRay = 128;
	constexpr float MaximumNormalSampleSpacingCM = 60.0f;
	const float MaxAllowedNormalStepDegrees = FMath::Clamp(
		JuryFixedSixMaximumGradeSlopeDegrees,
		5.0f,
		30.0f);
	static constexpr float SampleFractions[] = {-0.75f, 0.0f, 0.75f};
	OutTerrainPadCount = JuryFixedSixTerrainPadCount;
	OutPhysicalOverlapInstanceCount = 0;
	OutDynamicOverlapInstanceCount = 0;
	OutMaxPadResidualCM = 0.0f;
	OutGradeDiagnostics = FABTSM3JuryTerrainGradeDiagnostics();
	OutFailure.Reset();
	const bool bWorldHasPhysicsScene = GetWorld() != nullptr
		&& GetWorld()->GetPhysicsScene() != nullptr;
	const bool bChaosCollisionValidationRequired =
		bWorldHasPhysicsScene
		&& ContinuousSurface != nullptr
		&& ContinuousSurface->IsPhysicsStateCreated();

	FString MapFreezeV3Failure;
	TArray<FABTSM3JuryTerrainPadSource> Sources;
	Sources.Reserve(FABTSM3JuryMapFreezeV3Builder::ExpectedPrimarySiteCount);
	if (WorldSeed != FABTSM3JuryFixedSixLayoutBuilder::FrozenWorldSeed
		|| TerrainVisualField == nullptr
		|| !TerrainVisualField->IsReady()
		|| ContinuousSurface == nullptr
		|| (bWorldHasPhysicsScene
			&& !ContinuousSurface->IsPhysicsStateCreated())
		|| ForestHISM == nullptr
		|| RockHISM == nullptr
		|| FABTSJuryDemoFixedSixContract::ProductionContractVersion
			!= FABTSJuryDemoFixedSixContract::SupportedV3ContractVersion
		|| !ValidateJuryMapFreezeV3Result(MapFreezeV3Failure)
		|| JuryFixedSixTerrainPadCount
			!= FABTSM3JuryMapFreezeV3Builder::ExpectedPrimarySiteCount
		|| JuryFixedSixTerrainPads.Num()
			!= FABTSM3JuryMapFreezeV3Builder::ExpectedPrimarySiteCount
		|| JuryFixedSixTerrainSourceHeightDeltasCM.Num()
			!= FABTSM3JuryMapFreezeV3Builder::ExpectedPrimarySiteCount)
	{
		OutFailure = MapFreezeV3Failure.IsEmpty()
			? TEXT("ProductionPadStateUnavailable")
			: FString::Printf(
				TEXT("ProductionMapFreezeV3:%s"),
				*MapFreezeV3Failure);
		return false;
	}
	for (const FABTSM3JuryMapFreezeV3Placement& Placement
		: JuryMapFreezeV3Result.Placements)
	{
		const FABTSJuryDemoFixedSixBuildingSite& Site = Placement.Site;
		if (Site.V3Envelope.SurfaceKind
			== EABTSJuryDemoFixedSixSurfaceKind::Satellite)
		{
			continue;
		}
		FABTSM3JuryTerrainPadSource& Source =
			Sources.AddDefaulted_GetRef();
		Source.EncounterIndex = Site.EncounterIndex;
		Source.PadCenterCellId = Placement.PadCenterCellId;
		Source.PlanetLocalCenterCM =
			Site.WorldTransform.GetLocation() - GetPlanetCenterWorld();
		Source.Forward =
			Site.WorldTransform.GetUnitAxis(EAxis::X).GetSafeNormal();
		Source.Right =
			Site.WorldTransform.GetUnitAxis(EAxis::Y).GetSafeNormal();
		Source.Up =
			Site.WorldTransform.GetUnitAxis(EAxis::Z).GetSafeNormal();
		Source.PadHalfExtentCM = Site.PadHalfExtentCM;
		Source.TargetRadiusCM =
			static_cast<float>(Site.V3Envelope.SupportRadiusCM);
	}
	if (Sources.Num()
		!= FABTSM3JuryMapFreezeV3Builder::ExpectedPrimarySiteCount)
	{
		OutFailure = FString::Printf(
			TEXT("ProductionPrimarySourceCount:%d"),
			Sources.Num());
		return false;
	}

	for (const FABTSM3JuryTerrainPadSource& Placement : Sources)
	{
		for (const float XFraction : SampleFractions)
		{
			for (const float YFraction : SampleFractions)
			{
				const FVector PlaneSample = Placement.PlanetLocalCenterCM
					+ Placement.Forward
						* (Placement.PadHalfExtentCM.X * XFraction)
					+ Placement.Right
						* (Placement.PadHalfExtentCM.Y * YFraction);
				const FVector Direction = PlaneSample.GetSafeNormal();
				if (Direction.IsNearlyZero()
					|| !TerrainVisualField->IsInsideBuildingPad(Direction))
				{
					OutFailure = FString::Printf(
						TEXT("PadCoverage:%d"),
						Placement.EncounterIndex);
					return false;
				}
				const FVector SurfaceSample = Direction
					* TerrainVisualField->GetSurfaceRadius(Direction);
				const float ResidualCM = FMath::Abs(FVector::DotProduct(
					SurfaceSample - Placement.PlanetLocalCenterCM,
					Placement.Up));
				OutMaxPadResidualCM = FMath::Max(
					OutMaxPadResidualCM,
					ResidualCM);
			}
		}
	}
	if (OutMaxPadResidualCM > MaxAllowedPadResidualCM)
	{
		OutFailure = FString::Printf(
			TEXT("PadResidual:%.3f"),
			OutMaxPadResidualCM);
		return false;
	}

	OutGradeDiagnostics.MinimumBlendWidthCM = TNumericLimits<float>::Max();
	FCollisionQueryParams CollisionQueryParams(
		FName(TEXT("ABTSM3JuryTerrainGrade")),
		true);
	for (int32 PlacementIndex = 0;
		PlacementIndex < Sources.Num();
		++PlacementIndex)
	{
		const FABTSM3JuryTerrainPadSource& Placement =
			Sources[PlacementIndex];
		const FABTSM3BuildingSpawnSite& Pad =
			JuryFixedSixTerrainPads[PlacementIndex];
		OutGradeDiagnostics.MinimumBlendWidthCM = FMath::Min(
			OutGradeDiagnostics.MinimumBlendWidthCM,
			Pad.PadEdgeBlendWidthCM);
		OutGradeDiagnostics.MaximumBlendWidthCM = FMath::Max(
			OutGradeDiagnostics.MaximumBlendWidthCM,
			Pad.PadEdgeBlendWidthCM);
		OutGradeDiagnostics.MaximumSourceHeightDeltaCM = FMath::Max(
			OutGradeDiagnostics.MaximumSourceHeightDeltaCM,
			JuryFixedSixTerrainSourceHeightDeltasCM[PlacementIndex]);
		float PlacementMaximumGradeSlopeDegrees = 0.0f;
		float PlacementMaximumNormalStepDegrees = 0.0f;
		PlacementMaximumGradeSlopeDegrees = FMath::RadiansToDegrees(FMath::Atan(
			1.5f * JuryFixedSixTerrainSourceHeightDeltasCM[PlacementIndex]
				/ FMath::Max(Pad.PadEdgeBlendWidthCM, 1.0f)));
		OutGradeDiagnostics.MaximumGradeSlopeDegrees = FMath::Max(
			OutGradeDiagnostics.MaximumGradeSlopeDegrees,
			PlacementMaximumGradeSlopeDegrees);

		auto SampleDirectionAtDistance = [&Placement](
			const FVector2D& Ray,
			const float DistanceCM)
		{
			return (Placement.PlanetLocalCenterCM
				+ Placement.Forward * (Ray.X * DistanceCM)
				+ Placement.Right * (Ray.Y * DistanceCM)).GetSafeNormal();
		};
		auto TraceCollisionSample = [this, bChaosCollisionValidationRequired, &CollisionQueryParams, &OutGradeDiagnostics](
			const FVector& Direction)
		{
			if (!bChaosCollisionValidationRequired)
			{
				return true;
			}
			const FVector ExpectedWorld = GetPlanetCenterWorld()
				+ Direction * TerrainVisualField->GetSurfaceRadius(Direction);
			FHitResult Hit;
			if (!ContinuousSurface->LineTraceComponent(
					Hit,
					ExpectedWorld + Direction * 500.0f,
					ExpectedWorld - Direction * 500.0f,
					CollisionQueryParams))
			{
				return false;
			}
			OutGradeDiagnostics.MaximumCollisionResidualCM = FMath::Max(
				OutGradeDiagnostics.MaximumCollisionResidualCM,
				FVector::Distance(ExpectedWorld, Hit.ImpactPoint));
			++OutGradeDiagnostics.CollisionSampleCount;
			return true;
		};

		int32 CenterCollisionHitCount = 0;
		static constexpr float CenterCollisionProbeOffsetCM = 5.0f;
		const FVector CenterCollisionProbePoints[] =
		{
			Placement.PlanetLocalCenterCM,
			Placement.PlanetLocalCenterCM
				+ Placement.Forward * CenterCollisionProbeOffsetCM,
			Placement.PlanetLocalCenterCM
				- Placement.Forward * CenterCollisionProbeOffsetCM,
			Placement.PlanetLocalCenterCM
				+ Placement.Right * CenterCollisionProbeOffsetCM,
			Placement.PlanetLocalCenterCM
				- Placement.Right * CenterCollisionProbeOffsetCM
		};
		for (const FVector& CenterCollisionProbePoint : CenterCollisionProbePoints)
		{
			CenterCollisionHitCount += TraceCollisionSample(
				CenterCollisionProbePoint.GetSafeNormal()) ? 1 : 0;
		}
		if (CenterCollisionHitCount < 4)
		{
			OutFailure = FString::Printf(
				TEXT("ChaosSurfaceMiss:%d:CenterHits=%d/5"),
				Placement.EncounterIndex,
				CenterCollisionHitCount);
			return false;
		}
		for (int32 RayIndex = 0; RayIndex < GradeRayCount; ++RayIndex)
		{
			const float AngleRadians = UE_TWO_PI
				* static_cast<float>(RayIndex)
				/ static_cast<float>(GradeRayCount);
			const FVector2D Ray(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians));
			const float BoundaryDistanceCM = 1.0f / FMath::Max(
				FMath::Abs(Ray.X) / Pad.PadHalfExtentCM.X,
				FMath::Abs(Ray.Y) / Pad.PadHalfExtentCM.Y);
			float LowDistanceCM = BoundaryDistanceCM;
			float HighDistanceCM = BoundaryDistanceCM
				+ Pad.PadEdgeBlendWidthCM * 1.5f + 100.0f;
			bool bOuterEdgeBracketed = false;
			for (int32 ExpandIteration = 0; ExpandIteration < 8; ++ExpandIteration)
			{
				const FVector HighDirection = SampleDirectionAtDistance(
					Ray,
					HighDistanceCM);
				const float HighRawRadiusCM =
					TerrainVisualField->GetCompatibilityPaddedSurfaceRadius(HighDirection);
				if (TerrainVisualField->GetBuildingPadSignedDistanceCM(
						HighDirection,
						HighRawRadiusCM,
						Pad) >= Pad.PadEdgeBlendWidthCM)
				{
					bOuterEdgeBracketed = true;
					break;
				}
				HighDistanceCM += Pad.PadEdgeBlendWidthCM;
			}
			if (!bOuterEdgeBracketed)
			{
				OutFailure = FString::Printf(
					TEXT("GradeEdgeUnbounded:%d:Ray=%d"),
					Placement.EncounterIndex,
					RayIndex);
				return false;
			}
			for (int32 SearchIteration = 0; SearchIteration < 24; ++SearchIteration)
			{
				const float MidDistanceCM = 0.5f
					* (LowDistanceCM + HighDistanceCM);
				const FVector MidDirection = SampleDirectionAtDistance(
					Ray,
					MidDistanceCM);
				const float RawRadiusCM =
					TerrainVisualField->GetCompatibilityPaddedSurfaceRadius(MidDirection);
				const float SignedDistanceCM =
					TerrainVisualField->GetBuildingPadSignedDistanceCM(
						MidDirection,
						RawRadiusCM,
						Pad);
				if (SignedDistanceCM < Pad.PadEdgeBlendWidthCM)
				{
					LowDistanceCM = MidDistanceCM;
				}
				else
				{
					HighDistanceCM = MidDistanceCM;
				}
			}
			const float EdgeDistanceCM = HighDistanceCM;
			const int32 GradeSamplesPerRay = FMath::Clamp(
				FMath::CeilToInt(
					(EdgeDistanceCM - BoundaryDistanceCM)
						/ MaximumNormalSampleSpacingCM),
				MinimumGradeSamplesPerRay,
				MaximumGradeSamplesPerRay);
			const FVector EdgeDirection = SampleDirectionAtDistance(
				Ray,
				EdgeDistanceCM);
			const float EdgeRawRadiusCM =
				TerrainVisualField->GetCompatibilityPaddedSurfaceRadius(EdgeDirection);
			bool bEdgeOccupiedByAnotherGrade = false;
			for (int32 OtherPadIndex = 0;
				OtherPadIndex < JuryFixedSixTerrainPads.Num();
				++OtherPadIndex)
			{
				if (OtherPadIndex == PlacementIndex)
				{
					continue;
				}
				const FABTSM3BuildingSpawnSite& OtherPad =
					JuryFixedSixTerrainPads[OtherPadIndex];
				if (TerrainVisualField->GetBuildingPadSignedDistanceCM(
						EdgeDirection,
						EdgeRawRadiusCM,
						OtherPad) < OtherPad.PadEdgeBlendWidthCM - 0.1f)
				{
					bEdgeOccupiedByAnotherGrade = true;
					break;
				}
			}
			if (!bEdgeOccupiedByAnotherGrade)
			{
				OutGradeDiagnostics.MaximumEdgeHeightResidualCM = FMath::Max(
					OutGradeDiagnostics.MaximumEdgeHeightResidualCM,
					FMath::Abs(
						TerrainVisualField->GetSurfaceRadius(EdgeDirection)
						- EdgeRawRadiusCM));
			}

			FVector PreviousNormal = FVector::ZeroVector;
			for (int32 SampleIndex = 0;
				SampleIndex <= GradeSamplesPerRay;
				++SampleIndex)
			{
				const float DistanceCM = FMath::Lerp(
					BoundaryDistanceCM,
					EdgeDistanceCM,
					static_cast<float>(SampleIndex)
						/ static_cast<float>(GradeSamplesPerRay));
				const FVector Direction = SampleDirectionAtDistance(Ray, DistanceCM);
				const FVector Normal = TerrainVisualField->GetSurfaceNormal(Direction);
				if (!PreviousNormal.IsNearlyZero())
				{
					const float NormalStepDegrees =
						FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
							FVector::DotProduct(PreviousNormal, Normal),
							-1.0f,
							1.0f)));
					OutGradeDiagnostics.MaximumNormalStepDegrees = FMath::Max(
						OutGradeDiagnostics.MaximumNormalStepDegrees,
						NormalStepDegrees);
					PlacementMaximumNormalStepDegrees = FMath::Max(
						PlacementMaximumNormalStepDegrees,
						NormalStepDegrees);
				}
				PreviousNormal = Normal;
				if ((RayIndex % 2) == 0
					&& (SampleIndex == 0
						|| SampleIndex == GradeSamplesPerRay / 2
						|| SampleIndex == GradeSamplesPerRay)
					&& !TraceCollisionSample(Direction))
				{
					OutFailure = FString::Printf(
						TEXT("ChaosSurfaceMiss:%d:Ray=%d:Sample=%d"),
						Placement.EncounterIndex,
						RayIndex,
						SampleIndex);
					return false;
				}
			}
		}
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M3Jury][ProductionGradeSite] Encounter=%d WidthCM=%.1f SourceDeltaCM=%.1f ResolvedPeakGradeSlopeDegrees=%.2f MaxNormalStepDegrees=%.2f CenterChaosHits=%d/5"),
			Placement.EncounterIndex,
			Pad.PadEdgeBlendWidthCM,
			JuryFixedSixTerrainSourceHeightDeltasCM[PlacementIndex],
			PlacementMaximumGradeSlopeDegrees,
			PlacementMaximumNormalStepDegrees,
			CenterCollisionHitCount);
	}
	if (!FMath::IsFinite(OutGradeDiagnostics.MinimumBlendWidthCM))
	{
		OutGradeDiagnostics.MinimumBlendWidthCM = 0.0f;
	}
	if (OutGradeDiagnostics.MaximumGradeSlopeDegrees
		> FMath::Clamp(JuryFixedSixMaximumGradeSlopeDegrees, 5.0f, 30.0f)
			+ GradeSlopeToleranceDegrees)
	{
		OutFailure = FString::Printf(
			TEXT("GradeSlope:%.2f:Budget=%.2f"),
			OutGradeDiagnostics.MaximumGradeSlopeDegrees,
			JuryFixedSixMaximumGradeSlopeDegrees);
		return false;
	}
	if (OutGradeDiagnostics.MaximumNormalStepDegrees
		> MaxAllowedNormalStepDegrees)
	{
		OutFailure = FString::Printf(
			TEXT("GradeNormalStep:%.2f"),
			OutGradeDiagnostics.MaximumNormalStepDegrees);
		return false;
	}
	if (OutGradeDiagnostics.MaximumEdgeHeightResidualCM
		> MaxAllowedEdgeHeightResidualCM)
	{
		OutFailure = FString::Printf(
			TEXT("GradeEdgeResidual:%.3f"),
			OutGradeDiagnostics.MaximumEdgeHeightResidualCM);
		return false;
	}
	if (bChaosCollisionValidationRequired
		&& (OutGradeDiagnostics.CollisionSampleCount <= 0
			|| OutGradeDiagnostics.MaximumCollisionResidualCM
				> MaxAllowedCollisionResidualCM))
	{
		OutFailure = FString::Printf(
			TEXT("ChaosSurfaceResidual:%.2f:Samples=%d"),
			OutGradeDiagnostics.MaximumCollisionResidualCM,
			OutGradeDiagnostics.CollisionSampleCount);
		return false;
	}

	auto CountHorizontalOverlaps = [this](
		const UHierarchicalInstancedStaticMeshComponent* HISM,
		int32& InOutPhysicalOverlapCount,
		int32& InOutDynamicOverlapCount)
	{
		if (HISM == nullptr)
		{
			return false;
		}
		for (int32 InstanceIndex = 0;
			InstanceIndex < HISM->GetInstanceCount();
			++InstanceIndex)
		{
			FTransform InstanceTransform;
			if (!HISM->GetInstanceTransform(
					InstanceIndex,
					InstanceTransform,
					true))
			{
				return false;
			}
			bool bPhysicalOverlap = false;
			bool bDynamicOverlap = false;
			GetJuryFixedSixDecorClearanceOverlaps(
				InstanceTransform.GetLocation() - GetPlanetCenterWorld(),
				bPhysicalOverlap,
				bDynamicOverlap);
			InOutPhysicalOverlapCount += bPhysicalOverlap ? 1 : 0;
			InOutDynamicOverlapCount += bDynamicOverlap ? 1 : 0;
		}
		return true;
	};
	if (!CountHorizontalOverlaps(
			ForestHISM,
			OutPhysicalOverlapInstanceCount,
			OutDynamicOverlapInstanceCount)
		|| !CountHorizontalOverlaps(
			RockHISM,
			OutPhysicalOverlapInstanceCount,
			OutDynamicOverlapInstanceCount))
	{
		OutFailure = TEXT("DecorInstanceTransformUnavailable");
		return false;
	}
	if (OutPhysicalOverlapInstanceCount != 0
		|| OutDynamicOverlapInstanceCount != 0)
	{
		OutFailure = FString::Printf(
			TEXT("DecorOverlap:Physical=%d:Dynamic=%d"),
			OutPhysicalOverlapInstanceCount,
			OutDynamicOverlapInstanceCount);
		return false;
	}
	return true;
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
