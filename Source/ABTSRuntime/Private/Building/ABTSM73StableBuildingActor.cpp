// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73StableBuildingActor.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73GroundAdapter.h"
#include "Building/ABTSM73StabilityValidator.h"
#include "Building/ABTSM73StructureBuilder.h"
#include "Building/ABTSM73StructureData.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Components/ArrowComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Terrain/ABTSM3Planet.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

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
	FoundationCap = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FoundationCap"));
	FoundationFeet = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("FoundationFeet"));
	for (UHierarchicalInstancedStaticMeshComponent* Preview : {WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get()})
	{
		Preview->SetupAttachment(Root);
		Preview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Preview->SetGenerateOverlapEvents(false);
	}
	FoundationCap->SetupAttachment(Root);
	FoundationCap->SetCollisionProfileName(TEXT("BlockAll"));
	FoundationCap->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	// These components are non-simulating supports, but the generator is freely
	// transformable in the editor and rebuilds their world transforms.
	FoundationCap->SetMobility(EComponentMobility::Movable);
	FoundationFeet->SetupAttachment(Root);
	FoundationFeet->SetCollisionProfileName(TEXT("BlockAll"));
	FoundationFeet->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	FoundationFeet->SetMobility(EComponentMobility::Movable);
	FoundationFeet->SetGenerateOverlapEvents(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		BrickMesh = Cube.Object;
		FoundationCap->SetStaticMesh(Cube.Object);
		FoundationFeet->SetStaticMesh(Cube.Object);
		for (UHierarchicalInstancedStaticMeshComponent* Preview : {WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get()}) Preview->SetStaticMesh(Cube.Object);
	}
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

bool AABTSM73StableBuildingActor::BuildResolvedStructure(
	const bool bAllowFlatEditorFallback,
	FABTSM73GroundContext& OutContext,
	FABTSM73StructureData& OutData,
	FString& OutError)
{
	FABTSM73StructureBuilder Builder;
	if (!Builder.Build(GenerationSettings, OutData, OutError)) return false;
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
	FABTSM73StabilityValidator Validator;
	if (!Validator.Validate(GenerationSettings, OutData, OutError)) return false;
	return true;
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
	GenerationSummary = FABTSM73GenerationSummary();
	GenerationSummary.bAccepted = bAccepted;
	GenerationSummary.bPlanar = Context.bPlanar;
	GenerationSummary.BrickCount = Data.Bricks.Num();
	GenerationSummary.SupportEdgeCount = Data.SupportEdges.Num();
	GenerationSummary.GroundNodeCount = Data.GroundNodeIds.Num();
	GenerationSummary.FoundationFootCount = Data.FoundationFeet.Num();
	GenerationSummary.FootprintTerrainDeltaCM = Data.TerrainDeltaCM;
	GenerationSummary.CurvatureDropCM = Data.CurvatureDropCM;
	GenerationSummary.MaxSlopeDegrees = Data.MaxSlopeDegrees;
	GenerationSummary.MaxFoundationDepthCM = Data.MaxFoundationDepthCM;
	GenerationSummary.RejectReason = Error;
	if (!bAccepted) return false;
	UpdateFoundationComponents(Context, Data);
	if (bShowEditorPreview) UpdatePreviewComponents(Context, Data);
	return true;
}

void AABTSM73StableBuildingActor::ClearBrickPreviews()
{
	for (UHierarchicalInstancedStaticMeshComponent* Preview : {WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get()}) Preview->ClearInstances();
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
	for (const FABTSM73BrickNode& Node : Data.Bricks)
	{
		if (UHierarchicalInstancedStaticMeshComponent* Preview = GetPreviewForMaterial(Node.Material))
		{
			const FVector Center = Node.LocalCenter + FVector(0.0f, 0.0f, Data.FoundationCapTopCM);
			Preview->AddInstance(WorldBoxTransform(Context, Center, Node.DimensionsCM), true);
		}
	}
}

void AABTSM73StableBuildingActor::UpdateFoundationComponents(
	const FABTSM73GroundContext& Context,
	const FABTSM73StructureData& Data)
{
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
	if (!BuildResolvedStructure(false, Context, Data, Error))
	{
		GenerationSummary.bAccepted = false;
		GenerationSummary.RejectReason = Error;
		UE_LOG(LogABTSRuntime, Error, TEXT("[ABTS][M7.3-A][Reject] Actor=%s Reason=%s"), *GetName(), *Error);
		return;
	}
	RuntimeMaterialSystem = MaterialSystem;
	UpdateFoundationComponents(Context, Data);
	RuntimeModules.Reset();
	for (const FABTSM73BrickNode& Node : Data.Bricks)
	{
		FABTSM7BrickSpec Spec;
		Spec.Material = Node.Material;
		Spec.DimensionsCM = Node.DimensionsCM;
		const FVector LocalCenter = Node.LocalCenter + FVector(0.0f, 0.0f, Data.FoundationCapTopCM);
		const FTransform WorldTransform(Context.AnchorTransform.GetRotation(), Context.AnchorTransform.TransformPositionNoScale(LocalCenter));
		if (AABTSM7BuildingModule* Module = MaterialSystem->SpawnBrickModule(Spec, WorldTransform)) RuntimeModules.Add(Module);
	}
	bRuntimeSpawned = RuntimeModules.Num() == Data.Bricks.Num();
	ClearBrickPreviews();
	bRuntimePlanar = Context.bPlanar;
	RuntimeGravityReference = Context.bPlanar
		? Context.GravityUp
		: (Context.Planet.IsValid() ? Context.Planet->GetPlanetCenterWorld() : FVector::ZeroVector);
	GenerationSummary.bAccepted = bRuntimeSpawned;
	GenerationSummary.BrickCount = Data.Bricks.Num();
	GenerationSummary.SupportEdgeCount = Data.SupportEdges.Num();
	GenerationSummary.GroundNodeCount = Data.GroundNodeIds.Num();
	GenerationSummary.FoundationFootCount = Data.FoundationFeet.Num();
	GenerationSummary.FootprintTerrainDeltaCM = Data.TerrainDeltaCM;
	GenerationSummary.CurvatureDropCM = Data.CurvatureDropCM;
	GenerationSummary.MaxSlopeDegrees = Data.MaxSlopeDegrees;
	GenerationSummary.MaxFoundationDepthCM = Data.MaxFoundationDepthCM;
	GenerationSummary.RejectReason = bRuntimeSpawned ? FString() : TEXT("RuntimeModuleSpawnFailed");
	if (bRuntimeSpawned && bRunIdleChaosValidation) BeginIdleValidation(Context);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.3-A][Generated] Actor=%s Seed=%d Silhouette=%d Planar=%d Bricks=%d Supports=%d Ground=%d Feet=%d TerrainDelta=%.2f Curvature=%.2f MaxSlope=%.2f Accepted=%d"),
		*GetName(), GenerationSettings.BuildingSeed, static_cast<int32>(GenerationSettings.Silhouette), Context.bPlanar ? 1 : 0,
		Data.Bricks.Num(), Data.SupportEdges.Num(), Data.GroundNodeIds.Num(), Data.FoundationFeet.Num(), Data.TerrainDeltaCM,
		Data.CurvatureDropCM, Data.MaxSlopeDegrees, bRuntimeSpawned ? 1 : 0);
}

void AABTSM73StableBuildingActor::BeginIdleValidation(const FABTSM73GroundContext& Context)
{
	IdleInitialTransforms.Reset();
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Weak : RuntimeModules)
	{
		AABTSM7BuildingModule* Module = Weak.Get();
		if (Module == nullptr) continue;
		IdleInitialTransforms.Add(Module->GetActorTransform());
		Module->SetContactDamageGraceSeconds(GenerationSettings.IdleValidationSeconds + 0.5f);
		Module->GetMeshComponent()->SetVisibility(false, true);
		if (Context.bPlanar) Module->ActivateDynamicPlanar(FVector::ZeroVector, Context.GravityUp, ValidationGravityCMPerSec2);
		else Module->ActivateDynamic(FVector::ZeroVector, RuntimeGravityReference, ValidationGravityCMPerSec2);
	}
	IdleValidationElapsed = 0.0f;
	bIdleValidationRunning = true;
	SetActorTickEnabled(true);
}

void AABTSM73StableBuildingActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bIdleValidationRunning) return;
	IdleValidationElapsed += DeltaSeconds;
	if (IdleValidationElapsed >= GenerationSettings.IdleValidationSeconds) FinishIdleValidation();
}

void AABTSM73StableBuildingActor::FinishIdleValidation()
{
	float MaxMove = 0.0f;
	float MaxRotation = 0.0f;
	int32 ValidIndex = 0;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Weak : RuntimeModules)
	{
		AABTSM7BuildingModule* Module = Weak.Get();
		if (Module == nullptr) continue;
		if (IdleInitialTransforms.IsValidIndex(ValidIndex))
		{
			const FTransform& Initial = IdleInitialTransforms[ValidIndex];
			MaxMove = FMath::Max(MaxMove, FVector::Distance(Initial.GetLocation(), Module->GetActorLocation()));
			MaxRotation = FMath::Max(MaxRotation, FMath::RadiansToDegrees(Initial.GetRotation().AngularDistance(Module->GetActorQuat())));
		}
		++ValidIndex;
		Module->Freeze();
		Module->GetMeshComponent()->SetVisibility(true, true);
	}
	bIdleValidationRunning = false;
	SetActorTickEnabled(false);
	const bool bAccepted = MaxMove <= GenerationSettings.MaxIdleDisplacementCM && MaxRotation <= GenerationSettings.MaxIdleRotationDegrees;
	GenerationSummary.bAccepted = GenerationSummary.bAccepted && bAccepted;
	if (!bAccepted) GenerationSummary.RejectReason = FString::Printf(TEXT("IdleChaosUnstable:Move=%.2f:Rotation=%.2f"), MaxMove, MaxRotation);
	if (bAccepted)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-A][IdleValidation] Actor=%s Seconds=%.2f MaxMove=%.2f MaxRotation=%.2f Accepted=1"),
			*GetName(), IdleValidationElapsed, MaxMove, MaxRotation);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7.3-A][IdleValidation] Actor=%s Seconds=%.2f MaxMove=%.2f MaxRotation=%.2f Accepted=0"),
			*GetName(), IdleValidationElapsed, MaxMove, MaxRotation);
	}
}
