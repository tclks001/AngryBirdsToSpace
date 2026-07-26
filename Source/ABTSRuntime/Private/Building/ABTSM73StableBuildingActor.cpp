// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73StableBuildingActor.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73DAGBuildingPipeline.h"
#include "Building/ABTSM73GroundAdapter.h"
#include "Building/ABTSM73StabilityValidator.h"
#include "Building/ABTSM73StructureBuilder.h"
#include "Building/ABTSM73StructureData.h"
#include "Building/ABTSM73WeakPointPlanner.h"
#include "Building/ABTSM7MaterialProfileLibrary.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Building/ABTSM7PenetrationValidator.h"
#include "Components/ArrowComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Terrain/ABTSM3Planet.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "World/ABTSCollisionChannels.h"

namespace
{
	constexpr float BasicCubeSizeCM = 100.0f;

	FTransform WorldBoxTransform(const FABTSM73GroundContext& Context, const FVector& LocalCenter, const FVector& Dimensions)
	{
		return FTransform(Context.AnchorTransform.GetRotation(),
			Context.AnchorTransform.TransformPositionNoScale(LocalCenter), Dimensions / BasicCubeSizeCM);
	}
}

AABTSM73StableBuildingActor::AABTSM73StableBuildingActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	AttackDirection = CreateDefaultSubobject<UArrowComponent>(TEXT("AttackDirection"));
	AttackDirection->SetupAttachment(Root);
	AttackDirection->ArrowSize = 1.4f;
	WoodPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("WoodPreview"));
	StonePreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("StonePreview"));
	IronPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("IronPreview"));
	GlassPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("GlassPreview"));
	WeakPointPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("WeakPointPreview"));
	FoundationCap = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FoundationCap"));
	FoundationFeet = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("FoundationFeet"));
	for (UHierarchicalInstancedStaticMeshComponent* Preview : {WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get()})
	{
		Preview->SetupAttachment(Root);
		Preview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Preview->SetGenerateOverlapEvents(false);
	}
	WeakPointPreview->SetupAttachment(Root);
	WeakPointPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WeakPointPreview->SetGenerateOverlapEvents(false);
	FoundationCap->SetupAttachment(Root);
	FoundationCap->SetCollisionProfileName(TEXT("BlockAll"));
	FoundationCap->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
	FoundationCap->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	// These components are non-simulating supports, but the generator is freely
	// transformable in the editor and rebuilds their world transforms.
	FoundationCap->SetMobility(EComponentMobility::Movable);
	FoundationFeet->SetupAttachment(Root);
	FoundationFeet->SetCollisionProfileName(TEXT("BlockAll"));
	FoundationFeet->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
	FoundationFeet->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	FoundationFeet->SetMobility(EComponentMobility::Movable);
	FoundationFeet->SetGenerateOverlapEvents(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WoodMaterial(TEXT("/Game/StaticMesh/BrickMaterials/MI_Bricks_Wood.MI_Bricks_Wood"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> StoneMaterial(TEXT("/Game/StaticMesh/BrickMaterials/MI_Bricks_Stone.MI_Bricks_Stone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SteelMaterial(TEXT("/Game/StaticMesh/BrickMaterials/MI_Bricks_Steel.MI_Bricks_Steel"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlassMaterial(TEXT("/Game/StaticMesh/BrickMaterials/MI_Bricks_Glass.MI_Bricks_Glass"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (Cube.Succeeded())
	{
		BrickMesh = Cube.Object;
		FoundationCap->SetStaticMesh(Cube.Object);
		FoundationFeet->SetStaticMesh(Cube.Object);
		for (UHierarchicalInstancedStaticMeshComponent* Preview : {WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get()}) Preview->SetStaticMesh(Cube.Object);
		WeakPointPreview->SetStaticMesh(Cube.Object);
	}
	if (BasicShapeMaterial.Succeeded()) WeakPointDebugMaterial = BasicShapeMaterial.Object;
	if (WoodMaterial.Succeeded()) WoodPreview->SetMaterial(0, WoodMaterial.Object);
	if (StoneMaterial.Succeeded()) StonePreview->SetMaterial(0, StoneMaterial.Object);
	if (SteelMaterial.Succeeded()) IronPreview->SetMaterial(0, SteelMaterial.Object);
	if (GlassMaterial.Succeeded()) GlassPreview->SetMaterial(0, GlassMaterial.Object);
}

void AABTSM73StableBuildingActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildPreview();
}

void AABTSM73StableBuildingActor::BeginPlay()
{
	Super::BeginPlay();
	TryFindRuntimeMaterialSystem();
}

void AABTSM73StableBuildingActor::ConfigureSphericalAnchor(
	AABTSM3Planet* Planet,
	const int32 CellId,
	const FTransform& DesiredFacing)
{
	ConfiguredPlanet = Planet;
	GroundMode = EABTSM73GroundMode::SphericalCellTopo;
	AnchorCellId = CellId;
	SetActorTransform(DesiredFacing, false, nullptr, ETeleportType::TeleportPhysics);
}

void AABTSM73StableBuildingActor::ConfigureTaskGraphGeneration(
	const FABTSM73GenerationSettings& InGenerationSettings,
	const FABTSM73DAGGenerationSettings& InDAGGenerationSettings,
	const FABTSM73DAGLayoutSettings& InDAGLayoutSettings,
	const FABTSM73DifficultySettings& InDifficultySettings)
{
	if (bRuntimeSpawned)
	{
		UE_LOG(LogABTSRuntime, Warning, TEXT("[ABTS][M7][TaskGraphBuilding] Ignored late profile Actor=%s"), *GetName());
		return;
	}
	GenerationSettings = InGenerationSettings;
	DAGGenerationSettings = InDAGGenerationSettings;
	DAGGenerationSettings.BuildingSeed = GenerationSettings.BuildingSeed;
	DAGLayoutSettings = InDAGLayoutSettings;
	DifficultySettings = InDifficultySettings;
}

bool AABTSM73StableBuildingActor::BuildResolvedStructure(
	const bool bAllowFlatEditorFallback,
	FABTSM73GroundContext& OutContext,
	FABTSM73StructureData& OutData,
	FString& OutError,
	const AABTSM7BuildingMaterialSystem* MaterialProfileSource)
{
	if (GenerationSettings.GenerationAlgorithm == EABTSM73GenerationAlgorithm::RecursiveSupportDAG)
	{
		FABTSM73DAGGenerationSettings ResolvedDAGSettings = DAGGenerationSettings;
		ResolvedDAGSettings.BuildingSeed = GenerationSettings.BuildingSeed;
		ResolvedDAGSettings.MaxEstimatedBrickCount = FMath::Min(
			ResolvedDAGSettings.MaxEstimatedBrickCount, GenerationSettings.MaxBrickCount);
		FABTSM73DAGBuildingPipeline Pipeline;
		if (!Pipeline.Build(ResolvedDAGSettings, DAGLayoutSettings, GenerationSettings, OutData, OutError)) return false;
	}
	else
	{
		FABTSM73StructureBuilder Builder;
		if (!Builder.Build(GenerationSettings, OutData, OutError)) return false;
	}
	FABTSM73GroundAdapter Ground;
	if (!Ground.Resolve(*this, GroundMode, AnchorCellId, bSnapPlanarAnchorToTestStage, OutContext, OutError))
	{
		if (!bAllowFlatEditorFallback) return false;
		OutContext = FABTSM73GroundContext();
		OutContext.bValid = true;
		OutContext.bPlanar = true;
		OutContext.GravityUp = GetActorUpVector().GetSafeNormal();
		if (OutContext.GravityUp.IsNearlyZero()) OutContext.GravityUp = FVector::UpVector;
		FVector Forward = FVector::VectorPlaneProject(GetActorForwardVector(), OutContext.GravityUp).GetSafeNormal();
		if (Forward.IsNearlyZero()) Forward = FVector::ForwardVector;
		OutContext.AnchorTransform = FTransform(FRotationMatrix::MakeFromXZ(Forward, OutContext.GravityUp).ToQuat(), GetActorLocation());
		OutError.Reset();
	}
	if (!Ground.AnalyzeFootprint(GenerationSettings, OutContext, OutData, OutError)) return false;
	TArray<FABTSM7MaterialProfile> MaterialProfiles;
	if (MaterialProfileSource != nullptr) MaterialProfileSource->CopyMaterialProfiles(MaterialProfiles);
	else MaterialProfiles = FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
	FVector LocalAttackDirection = OutContext.AnchorTransform.InverseTransformVectorNoScale(AttackDirection->GetForwardVector()).GetSafeNormal();
	if (LocalAttackDirection.IsNearlyZero()) LocalAttackDirection = FVector::ForwardVector;
	if (GenerationSettings.GenerationAlgorithm == EABTSM73GenerationAlgorithm::LegacyLayeredAB2)
	{
		FABTSM73WeakPointPlanner WeakPointPlanner;
		if (!WeakPointPlanner.Plan(DifficultySettings, MaterialProfiles, LocalAttackDirection,
			GenerationSettings.BuildingSeed, OutData, OutError)) return false;
	}
	FABTSM73StabilityValidator Validator;
	if (!Validator.Validate(GenerationSettings, OutData, OutError)) return false;
	return true;
}

void AABTSM73StableBuildingActor::FillGenerationSummary(
	const FABTSM73GroundContext& Context,
	const FABTSM73StructureData& Data,
	const bool bAccepted,
	const FString& Error)
{
	GenerationSummary = FABTSM73GenerationSummary();
	GenerationSummary.bAccepted = bAccepted;
	GenerationSummary.bPlanar = Context.bPlanar;
	GenerationSummary.BrickCount = Data.Bricks.Num();
	GenerationSummary.SupportEdgeCount = Data.SupportEdges.Num();
	GenerationSummary.GroundNodeCount = Data.GroundNodeIds.Num();
	GenerationSummary.GenerationAlgorithm = GenerationSettings.GenerationAlgorithm;
	GenerationSummary.DAGMacroNodeCount = Data.DAGMacroNodeCount;
	GenerationSummary.DAGSelectedSupportCount = Data.DAGSelectedSupportCount;
	GenerationSummary.DAGMissingRequiredContactCount = Data.DAGMissingRequiredContactCount;
	GenerationSummary.DAGUnexpectedBypassCount = Data.DAGUnexpectedBypassCount;
	GenerationSummary.DAGTopologyHash = static_cast<int64>(Data.DAGTopologyHash);
	GenerationSummary.FoundationFootCount = Data.FoundationFeet.Num();
	GenerationSummary.FootprintTerrainDeltaCM = Data.TerrainDeltaCM;
	GenerationSummary.CurvatureDropCM = Data.CurvatureDropCM;
	GenerationSummary.MaxSlopeDegrees = Data.MaxSlopeDegrees;
	GenerationSummary.MaxFoundationDepthCM = Data.MaxFoundationDepthCM;
	GenerationSummary.WeakPointCount = Data.WeakPoints.Num();
	GenerationSummary.ReinforcedNodeCount = Data.ReinforcedNodeIds.Num();
	GenerationSummary.PrimaryWeakPointNodeId = Data.WeakPoints.IsEmpty() ? INDEX_NONE : Data.WeakPoints[0].NodeId;
	GenerationSummary.BestWeakPointScore = Data.BestWeakPointScore;
	GenerationSummary.PredictedWeakCollapseRatio = Data.PredictedWeakCollapseRatio;
	GenerationSummary.PredictedNonWeakEffect = Data.PredictedNonWeakEffect;
	GenerationSummary.EstimatedWeakPointHits = Data.EstimatedWeakPointHits;
	GenerationSummary.DifficultyScore = Data.DifficultyScore;
	if (!Data.WeakPoints.IsEmpty())
	{
		const FABTSM73WeakPointRecord& Primary = Data.WeakPoints[0];
		GenerationSummary.StructuralWeaknessPattern = Primary.StructuralPattern;
		GenerationSummary.PredictedCollapseMode = Primary.CollapseMode;
		GenerationSummary.PrimaryTipMarginCM = Primary.TipMarginCM;
		GenerationSummary.PrimaryReseatRisk = Primary.ReseatRisk;
	}
	GenerationSummary.RejectReason = Error;
}

bool AABTSM73StableBuildingActor::RebuildPreview()
{
	ClearBrickPreviews();
	FoundationFeet->ClearInstances();
	FoundationCap->SetVisibility(false, true);
	FABTSM73GroundContext Context;
	FABTSM73StructureData Data;
	FString Error;
	const bool bAccepted = BuildResolvedStructure(true, Context, Data, Error);
	FillGenerationSummary(Context, Data, bAccepted, Error);
	if (!bAccepted) return false;
	UpdateFoundationComponents(Context, Data);
	if (bShowEditorPreview) UpdatePreviewComponents(Context, Data);
	return true;
}

void AABTSM73StableBuildingActor::ClearBrickPreviews()
{
	for (UHierarchicalInstancedStaticMeshComponent* Preview : {WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get()}) Preview->ClearInstances();
	WeakPointPreview->ClearInstances();
}

UHierarchicalInstancedStaticMeshComponent* AABTSM73StableBuildingActor::GetPreviewForMaterial(const EABTSM7BuildingMaterial Material) const
{
	switch (Material)
	{
	case EABTSM7BuildingMaterial::Stone: return StonePreview;
	case EABTSM7BuildingMaterial::Iron: return IronPreview;
	case EABTSM7BuildingMaterial::Glass: return GlassPreview;
	default: return WoodPreview;
	}
}

void AABTSM73StableBuildingActor::UpdatePreviewComponents(
	const FABTSM73GroundContext& Context,
	const FABTSM73StructureData& Data)
{
	for (UHierarchicalInstancedStaticMeshComponent* Preview : {WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get(), WeakPointPreview.Get()})
	{
		Preview->SetStaticMesh(BrickMesh);
	}
	for (const FABTSM73BrickNode& Node : Data.Bricks)
	{
		if (UHierarchicalInstancedStaticMeshComponent* Preview = GetPreviewForMaterial(Node.Material))
		{
			const FVector Center = Node.LocalCenter + FVector(0.0f, 0.0f, Data.FoundationCapTopCM);
			Preview->AddInstance(WorldBoxTransform(Context, Center, Node.DimensionsCM), true);
		}
	}
	WeakPointPreview->SetVisibility(DifficultySettings.bShowWeakPointDebug, true);
	if (!DifficultySettings.bShowWeakPointDebug) return;
	if (WeakPointDebugMaterial != nullptr)
	{
		WeakPointDebugMID = UMaterialInstanceDynamic::Create(WeakPointDebugMaterial, this);
		if (WeakPointDebugMID != nullptr)
		{
			const FLinearColor DebugColor(1.0f, 0.08f, 0.015f, 1.0f);
			WeakPointDebugMID->SetVectorParameterValue(TEXT("Color"), DebugColor);
			WeakPointDebugMID->SetVectorParameterValue(TEXT("BaseColor"), DebugColor);
			WeakPointPreview->SetMaterial(0, WeakPointDebugMID);
		}
	}
	const float DebugScale = FMath::Clamp(DifficultySettings.WeakPointDebugScale, 1.0f, 1.25f);
	for (const FABTSM73WeakPointRecord& WeakPoint : Data.WeakPoints)
	{
		const FABTSM73BrickNode* Node = Data.Bricks.FindByPredicate([&WeakPoint](const FABTSM73BrickNode& Candidate)
		{
			return Candidate.NodeId == WeakPoint.NodeId;
		});
		if (Node == nullptr) continue;
		const FVector Center = Node->LocalCenter + FVector(0.0f, 0.0f, Data.FoundationCapTopCM);
		WeakPointPreview->AddInstance(WorldBoxTransform(Context, Center, Node->DimensionsCM * DebugScale), true);
	}
}

void AABTSM73StableBuildingActor::UpdateFoundationComponents(
	const FABTSM73GroundContext& Context,
	const FABTSM73StructureData& Data)
{
	FoundationCap->SetCollisionProfileName(TEXT("BlockAll"));
	FoundationCap->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
	FoundationCap->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	FoundationFeet->SetCollisionProfileName(TEXT("BlockAll"));
	FoundationFeet->SetCollisionObjectType(ABTSDeveloperObstacleChannel);
	FoundationFeet->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	const FVector2D Extent = Data.FootprintHalfExtent + FVector2D(FMath::Max(0.0f, GenerationSettings.FoundationMarginCM));
	const float CapHeight = FMath::Max(10.0f, Data.FoundationCapTopCM - Data.FoundationCapBottomCM);
	FoundationCap->SetWorldTransform(WorldBoxTransform(Context,
		FVector(0.0f, 0.0f, (Data.FoundationCapBottomCM + Data.FoundationCapTopCM) * 0.5f),
		FVector(Extent.X * 2.0f, Extent.Y * 2.0f, CapHeight)));
	FoundationCap->SetVisibility(true, true);
	if (FoundationMaterial) FoundationCap->SetMaterial(0, FoundationMaterial);
	FoundationFeet->ClearInstances();
	if (FoundationMaterial) FoundationFeet->SetMaterial(0, FoundationMaterial);
	for (const FABTSM73FoundationFoot& Foot : Data.FoundationFeet)
	{
		const float Height = FMath::Max(1.0f, Foot.TopHeightCM - Foot.BottomHeightCM);
		FoundationFeet->AddInstance(WorldBoxTransform(Context,
			FVector(Foot.LocalXY.X, Foot.LocalXY.Y, (Foot.BottomHeightCM + Foot.TopHeightCM) * 0.5f),
			FVector(GenerationSettings.FoundationFootSizeCM, GenerationSettings.FoundationFootSizeCM, Height)), true);
	}
}

void AABTSM73StableBuildingActor::TryFindRuntimeMaterialSystem()
{
	if (bRuntimeSpawned || GetWorld() == nullptr) return;
	for (TActorIterator<AABTSM7BuildingMaterialSystem> It(GetWorld()); It; ++It)
	{
		InitializeRuntimeBuilding(*It);
		return;
	}
	if (++MaterialSystemSearchAttempts < 50)
	{
		GetWorldTimerManager().SetTimer(MaterialSystemSearchTimer, this, &AABTSM73StableBuildingActor::TryFindRuntimeMaterialSystem, 0.1f, false);
	}
	else UE_LOG(LogABTSRuntime, Error, TEXT("[ABTS][M7.3-A] No MaterialSystem Actor=%s"), *GetName());
}

void AABTSM73StableBuildingActor::InitializeRuntimeBuilding(AABTSM7BuildingMaterialSystem* MaterialSystem)
{
	if (bRuntimeSpawned || MaterialSystem == nullptr) return;
	FABTSM73GroundContext Context;
	FABTSM73StructureData Data;
	FString Error;
	if (!BuildResolvedStructure(false, Context, Data, Error, MaterialSystem))
	{
		FillGenerationSummary(Context, Data, false, Error);
		UE_LOG(LogABTSRuntime, Error, TEXT("[ABTS][M7.3-A][Reject] Actor=%s Reason=%s"), *GetName(), *Error);
		return;
	}
	RuntimeMaterialSystem = MaterialSystem;
	UpdateFoundationComponents(Context, Data);
	RuntimeModules.Reset();
	RuntimeModulesByNodeId.Reset();
	for (const FABTSM73BrickNode& Node : Data.Bricks)
	{
		FABTSM7BrickSpec Spec;
		Spec.Material = Node.Material;
		Spec.DimensionsCM = Node.DimensionsCM;
		const FVector LocalCenter = Node.LocalCenter + FVector(0.0f, 0.0f, Data.FoundationCapTopCM);
		const FTransform WorldTransform(Context.AnchorTransform.GetRotation(), Context.AnchorTransform.TransformPositionNoScale(LocalCenter));
		if (AABTSM7BuildingModule* Module = MaterialSystem->SpawnBrickModule(Spec, WorldTransform))
		{
			RuntimeModules.Add(Module);
			RuntimeModulesByNodeId.Add(Node.NodeId, Module);
		}
	}
	bRuntimeSpawned = RuntimeModules.Num() == Data.Bricks.Num();
	if (!bRuntimeSpawned)
	{
		for (const TWeakObjectPtr<AABTSM7BuildingModule>& Weak : RuntimeModules)
		{
			if (AABTSM7BuildingModule* Module = Weak.Get()) Module->Destroy();
		}
		RuntimeModules.Reset();
		RuntimeModulesByNodeId.Reset();
	}
	ClearBrickPreviews();
	bRuntimePlanar = Context.bPlanar;
	RuntimeGravityReference = Context.bPlanar
		? Context.GravityUp
		: (Context.Planet.IsValid() ? Context.Planet->GetPlanetCenterWorld() : FVector::ZeroVector);
	FillGenerationSummary(Context, Data, bRuntimeSpawned,
		bRuntimeSpawned ? FString() : FString(TEXT("RuntimeModuleSpawnFailed")));
	if (bRuntimeSpawned && bRunIdleChaosValidation) BeginIdleValidation(Context);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.3-A][Generated] Actor=%s Seed=%d Algorithm=%d Silhouette=%d Planar=%d Bricks=%d Supports=%d Ground=%d DAGMacro=%d DAGSparse=%d DAGHash=%u Feet=%d TerrainDelta=%.2f Curvature=%.2f MaxSlope=%.2f Accepted=%d"),
		*GetName(), GenerationSettings.BuildingSeed, static_cast<int32>(GenerationSettings.GenerationAlgorithm), static_cast<int32>(GenerationSettings.Silhouette), Context.bPlanar ? 1 : 0,
		Data.Bricks.Num(), Data.SupportEdges.Num(), Data.GroundNodeIds.Num(), Data.DAGMacroNodeCount, Data.DAGSelectedSupportCount, Data.DAGTopologyHash, Data.FoundationFeet.Num(), Data.TerrainDeltaCM,
		Data.CurvatureDropCM, Data.MaxSlopeDegrees, bRuntimeSpawned ? 1 : 0);
	for (const FABTSM73WeakPointRecord& WeakPoint : Data.WeakPoints)
	{
		const TWeakObjectPtr<AABTSM7BuildingModule>* Module = RuntimeModulesByNodeId.Find(WeakPoint.NodeId);
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-B][WeakPoint] Actor=%s Node=%d Role=%d Module=%s UnsupportedMass=%.3f Exposure=%.3f Hits=%d Score=%.3f Pattern=%d Collapse=%d InitialMargin=%.2f TipMargin=%.2f Reseat=%.3f Affected=%d"),
			*GetName(), WeakPoint.NodeId, static_cast<int32>(WeakPoint.Role),
			Module != nullptr && Module->IsValid() ? *Module->Get()->GetName() : TEXT("None"),
			WeakPoint.UnsupportedMassRatio, WeakPoint.Exposure, WeakPoint.EstimatedHits, WeakPoint.Score,
			static_cast<int32>(WeakPoint.StructuralPattern), static_cast<int32>(WeakPoint.CollapseMode),
			WeakPoint.InitialSupportMarginCM, WeakPoint.TipMarginCM, WeakPoint.ReseatRisk,
			WeakPoint.AffectedNodeIds.Num());
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.3-B][Difficulty] Actor=%s WeakPoints=%d Reinforced=%d WeakCollapse=%.3f NonWeakEffect=%.3f Hits=%d Score=%.3f"),
		*GetName(), Data.WeakPoints.Num(), Data.ReinforcedNodeIds.Num(), Data.PredictedWeakCollapseRatio,
		Data.PredictedNonWeakEffect, Data.EstimatedWeakPointHits, Data.DifficultyScore);
}

void AABTSM73StableBuildingActor::BeginIdleValidation(const FABTSM73GroundContext& Context)
{
	IdleInitialTransforms.Reset();
	TArray<AABTSM7BuildingModule*> PendingModules;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Weak : RuntimeModules)
	{
		AABTSM7BuildingModule* Module = Weak.Get();
		if (Module == nullptr) continue;
		PendingModules.Add(Module);
	}
	const FABTSM7PenetrationValidationStats ContactValidation = RuntimeMaterialSystem.IsValid()
		? RuntimeMaterialSystem->ValidateAndRepairPendingModules(PendingModules)
		: FABTSM7PenetrationValidationStats();
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.3-A][IdlePenetrationValidation] Actor=%s Modules=%d Pairs=%d Repairs=%d LargeErrors=%d RemainingSmall=%d MaxDepth=%.4f"),
		*GetName(), PendingModules.Num(), ContactValidation.DetectedPairCount, ContactValidation.RepairCount,
		ContactValidation.LargeErrorPairCount, ContactValidation.RemainingSmallPairCount,
		ContactValidation.MaximumDetectedDepthCM);
	if (ContactValidation.RepairCount > 0
		|| ContactValidation.LargeErrorPairCount > 0
		|| ContactValidation.RemainingSmallPairCount > 0)
	{
		const FString RejectReason = FString::Printf(
			TEXT("IdlePenetrationInvalid:Repairs=%d:Large=%d:RemainingSmall=%d:MaxDepth=%.4f"),
			ContactValidation.RepairCount, ContactValidation.LargeErrorPairCount, ContactValidation.RemainingSmallPairCount,
			ContactValidation.MaximumDetectedDepthCM);
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7.3-A][IdleValidation] Actor=%s PenetrationRejected=1 Repairs=%d LargeErrors=%d RemainingSmall=%d MaxDepth=%.4f Accepted=0"),
			*GetName(), ContactValidation.RepairCount, ContactValidation.LargeErrorPairCount, ContactValidation.RemainingSmallPairCount,
			ContactValidation.MaximumDetectedDepthCM);
		RejectRuntimeStructure(RejectReason);
		return;
	}
	for (AABTSM7BuildingModule* Module : PendingModules)
	{
		if (!IsValid(Module)) continue;
		IdleInitialTransforms.Add(Module, Module->GetActorTransform());
		Module->SetContactDamageGraceSeconds(FMath::Max(
			GenerationSettings.IdleValidationMaxSeconds,
			GenerationSettings.IdleValidationSeconds + GenerationSettings.IdleStableHoldSeconds) + 0.5f);
		Module->GetMeshComponent()->SetVisibility(false, true);
		if (Context.bPlanar) Module->ActivateDynamicPlanar(FVector::ZeroVector, Context.GravityUp, ValidationGravityCMPerSec2);
		else Module->ActivateDynamic(FVector::ZeroVector, RuntimeGravityReference, ValidationGravityCMPerSec2);
	}
	IdleValidationElapsed = 0.0f;
	IdleStableElapsed = 0.0f;
	bIdleValidationRunning = true;
	SetActorTickEnabled(true);
}

void AABTSM73StableBuildingActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bIdleValidationRunning) return;
	IdleValidationElapsed += DeltaSeconds;
	bool bAnyBodyMoving = false;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Weak : RuntimeModules)
	{
		const AABTSM7BuildingModule* Module = Weak.Get();
		const UStaticMeshComponent* Mesh = Module != nullptr ? Module->GetMeshComponent() : nullptr;
		if (Mesh == nullptr || !Mesh->IsSimulatingPhysics()) continue;
		if (Mesh->GetPhysicsLinearVelocity().Size() > GenerationSettings.IdleLinearSpeedThresholdCMPerSec
			|| Mesh->GetPhysicsAngularVelocityInDegrees().Size() > GenerationSettings.IdleAngularSpeedThresholdDegPerSec)
		{
			bAnyBodyMoving = true;
			break;
		}
	}
	if (IdleValidationElapsed >= GenerationSettings.IdleValidationSeconds && !bAnyBodyMoving)
	{
		IdleStableElapsed += FMath::Max(0.0f, DeltaSeconds);
	}
	else
	{
		IdleStableElapsed = 0.0f;
	}
	if (IdleStableElapsed >= GenerationSettings.IdleStableHoldSeconds)
	{
		FinishIdleValidation(false);
	}
	else if (IdleValidationElapsed >= FMath::Max(
		GenerationSettings.IdleValidationMaxSeconds,
		GenerationSettings.IdleValidationSeconds + GenerationSettings.IdleStableHoldSeconds))
	{
		FinishIdleValidation(true);
	}
}

void AABTSM73StableBuildingActor::FinishIdleValidation(const bool bTimedOut)
{
	float MaxMove = 0.0f;
	float MaxPlanarDrift = 0.0f;
	float MaxSettlement = 0.0f;
	float MaxRotation = 0.0f;
	float MaxLinearSpeed = 0.0f;
	float MaxAngularSpeed = 0.0f;
	int32 AwakeBodyCount = 0;
	TWeakObjectPtr<AABTSM7BuildingModule> MaxMoveModule;
	TWeakObjectPtr<AABTSM7BuildingModule> MaxDriftModule;
	TWeakObjectPtr<AABTSM7BuildingModule> MaxSettlementModule;
	TWeakObjectPtr<AABTSM7BuildingModule> MaxRotationModule;
	FVector MaxMoveDelta = FVector::ZeroVector;
	FVector MaxDriftDelta = FVector::ZeroVector;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Weak : RuntimeModules)
	{
		AABTSM7BuildingModule* Module = Weak.Get();
		if (Module == nullptr) continue;
		if (UStaticMeshComponent* Mesh = Module->GetMeshComponent())
		{
			MaxLinearSpeed = FMath::Max(MaxLinearSpeed, Mesh->GetPhysicsLinearVelocity().Size());
			MaxAngularSpeed = FMath::Max(MaxAngularSpeed, Mesh->GetPhysicsAngularVelocityInDegrees().Size());
			if (Mesh->IsAnyRigidBodyAwake()) ++AwakeBodyCount;
		}
		if (const FTransform* Initial = IdleInitialTransforms.Find(Module))
		{
			const FVector Delta = Module->GetActorLocation() - Initial->GetLocation();
			FVector Up = bRuntimePlanar
				? RuntimeGravityReference.GetSafeNormal()
				: (Initial->GetLocation() - RuntimeGravityReference).GetSafeNormal();
			if (Up.IsNearlyZero()) Up = FVector::UpVector;
			const float Move = Delta.Size();
			const float PlanarDrift = FVector::VectorPlaneProject(Delta, Up).Size();
			const float Settlement = FMath::Abs(FVector::DotProduct(Delta, Up));
			const float Rotation = FMath::RadiansToDegrees(Initial->GetRotation().AngularDistance(Module->GetActorQuat()));
			if (Move > MaxMove) { MaxMove = Move; MaxMoveModule = Module; MaxMoveDelta = Delta; }
			if (PlanarDrift > MaxPlanarDrift) { MaxPlanarDrift = PlanarDrift; MaxDriftModule = Module; MaxDriftDelta = Delta; }
			if (Settlement > MaxSettlement) { MaxSettlement = Settlement; MaxSettlementModule = Module; }
			if (Rotation > MaxRotation) { MaxRotation = Rotation; MaxRotationModule = Module; }
		}
		Module->Freeze();
		Module->GetMeshComponent()->SetVisibility(true, true);
	}
	bIdleValidationRunning = false;
	SetActorTickEnabled(false);
	const auto DescribeModule = [this](const TWeakObjectPtr<AABTSM7BuildingModule>& WeakModule)
	{
		const AABTSM7BuildingModule* Module = WeakModule.Get();
		if (Module == nullptr) return FString(TEXT("None"));
		int32 NodeId = INDEX_NONE;
		for (const TPair<int32, TWeakObjectPtr<AABTSM7BuildingModule>>& Pair : RuntimeModulesByNodeId)
		{
			if (Pair.Value.Get() == Module) { NodeId = Pair.Key; break; }
		}
		return FString::Printf(TEXT("%s(Node=%d)"), *Module->GetName(), NodeId);
	};
	const bool bSpatiallyStable = MaxPlanarDrift <= GenerationSettings.MaxIdleDisplacementCM
		&& MaxSettlement <= GenerationSettings.MaxIdleSettlementCM
		&& MaxRotation <= GenerationSettings.MaxIdleRotationDegrees;
	// Chaos can keep a correctly seated contact stack awake with sub-centimetre
	// oscillation. The timeout is a guard against unbounded motion, not a reason
	// to reject a structure whose measured drift, settlement and rotation all
	// remain inside the authored stability envelope. Every accepted module is
	// frozen below, so this bounded residual cannot leak into live gameplay.
	const bool bAcceptedAfterBoundedTimeout = bTimedOut && bSpatiallyStable;
	const bool bAccepted = !bTimedOut ? bSpatiallyStable : bAcceptedAfterBoundedTimeout;
	GenerationSummary.bAccepted = GenerationSummary.bAccepted && bAccepted;
	if (!bAccepted)
	{
		GenerationSummary.RejectReason = FString::Printf(
			TEXT("IdleChaosUnstable:TimedOut=%d:Move=%.2f:Drift=%.2f:Settlement=%.2f:Rotation=%.2f"),
			bTimedOut ? 1 : 0, MaxMove, MaxPlanarDrift, MaxSettlement, MaxRotation);
	}
	if (bAccepted)
	{
		UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.3-A][IdleValidation] Actor=%s Seconds=%.2f Stable=%.2f TimedOut=%d BoundedTimeout=%d MaxMove=%.2f MoveDelta=%s MoveModule=%s MaxDrift=%.2f DriftDelta=%s DriftModule=%s MaxSettlement=%.2f SettlementModule=%s MaxRotation=%.2f RotationModule=%s MaxLinearSpeed=%.2f MaxAngularSpeed=%.2f Awake=%d Accepted=1"),
			*GetName(), IdleValidationElapsed, IdleStableElapsed, bTimedOut ? 1 : 0, bAcceptedAfterBoundedTimeout ? 1 : 0, MaxMove, *MaxMoveDelta.ToCompactString(), *DescribeModule(MaxMoveModule), MaxPlanarDrift,
			*MaxDriftDelta.ToCompactString(), *DescribeModule(MaxDriftModule), MaxSettlement, *DescribeModule(MaxSettlementModule),
			MaxRotation, *DescribeModule(MaxRotationModule), MaxLinearSpeed, MaxAngularSpeed, AwakeBodyCount);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7.3-A][IdleValidation] Actor=%s Seconds=%.2f Stable=%.2f TimedOut=%d MaxMove=%.2f MoveDelta=%s MoveModule=%s MaxDrift=%.2f DriftDelta=%s DriftModule=%s MaxSettlement=%.2f SettlementModule=%s MaxRotation=%.2f RotationModule=%s MaxLinearSpeed=%.2f MaxAngularSpeed=%.2f Awake=%d Accepted=0"),
			*GetName(), IdleValidationElapsed, IdleStableElapsed, bTimedOut ? 1 : 0, MaxMove, *MaxMoveDelta.ToCompactString(), *DescribeModule(MaxMoveModule), MaxPlanarDrift,
			*MaxDriftDelta.ToCompactString(), *DescribeModule(MaxDriftModule), MaxSettlement, *DescribeModule(MaxSettlementModule),
			MaxRotation, *DescribeModule(MaxRotationModule), MaxLinearSpeed, MaxAngularSpeed, AwakeBodyCount);
		RejectRuntimeStructure(GenerationSummary.RejectReason);
	}
}

void AABTSM73StableBuildingActor::RejectRuntimeStructure(const FString& Reason)
{
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Weak : RuntimeModules)
	{
		if (AABTSM7BuildingModule* Module = Weak.Get()) Module->Destroy();
	}
	RuntimeModules.Reset();
	RuntimeModulesByNodeId.Reset();
	IdleInitialTransforms.Reset();
	bRuntimeSpawned = false;
	bIdleValidationRunning = false;
	SetActorTickEnabled(false);
	FoundationCap->SetVisibility(false, true);
	FoundationCap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FoundationFeet->ClearInstances();
	FoundationFeet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GenerationSummary.bAccepted = false;
	GenerationSummary.RejectReason = Reason;
}
