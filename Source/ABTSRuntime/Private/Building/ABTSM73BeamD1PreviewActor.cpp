// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73BeamD1PreviewActor.h"

#include "ABTSRuntime.h"
#include "ABTSM73BeamD1BrickCompiler.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace ABTSM73BeamD1Preview
{
	void ConfigurePreview(
		UHierarchicalInstancedStaticMeshComponent& Component,
		USceneComponent& Parent)
	{
		Component.SetupAttachment(&Parent);
		Component.SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component.SetGenerateOverlapEvents(false);
		Component.SetCanEverAffectNavigation(false);
		Component.SetHiddenInGame(true);
		Component.SetCastShadow(true);
	}
}

AABTSM73BeamD1PreviewActor::AABTSM73BeamD1PreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetActorEnableCollision(false);
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	WoodPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("WoodBrickPreview"));
	StonePreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("StoneBrickPreview"));
	IronPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("IronBrickPreview"));
	GlassPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("GlassBrickPreview"));
	for (UHierarchicalInstancedStaticMeshComponent* Preview : {
		WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get()})
	{
		ABTSM73BeamD1Preview::ConfigurePreview(*Preview, *Root);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Wood(
		TEXT("/Game/StaticMesh/BrickMaterials/MI_Bricks_Wood.MI_Bricks_Wood"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Stone(
		TEXT("/Game/StaticMesh/BrickMaterials/MI_Bricks_Stone.MI_Bricks_Stone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Iron(
		TEXT("/Game/StaticMesh/BrickMaterials/MI_Bricks_Steel.MI_Bricks_Steel"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Glass(
		TEXT("/Game/StaticMesh/BrickMaterials/MI_Bricks_Glass.MI_Bricks_Glass"));
	for (UHierarchicalInstancedStaticMeshComponent* Preview : {
		WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get()})
	{
		if (Cube.Succeeded())
		{
			Preview->SetStaticMesh(Cube.Object);
		}
	}
	if (Wood.Succeeded()) WoodPreview->SetMaterial(0, Wood.Object);
	if (Stone.Succeeded()) StonePreview->SetMaterial(0, Stone.Object);
	if (Iron.Succeeded()) IronPreview->SetMaterial(0, Iron.Object);
	if (Glass.Succeeded()) GlassPreview->SetMaterial(0, Glass.Object);
}

void AABTSM73BeamD1PreviewActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		RegeneratePreview();
	}
}

void AABTSM73BeamD1PreviewActor::BeginPlay()
{
	Super::BeginPlay();
	RegeneratePreview();
	if (!bSpawnRuntimeModulesInPIE || GetWorld() == nullptr)
	{
		return;
	}
	for (TActorIterator<AABTSM7BuildingMaterialSystem> It(GetWorld()); It; ++It)
	{
		InitializeRuntimeBuilding(*It);
		break;
	}
}

void AABTSM73BeamD1PreviewActor::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Module : RuntimeModules)
	{
		if (Module.IsValid())
		{
			Module->Destroy();
		}
	}
	RuntimeModules.Reset();
	Super::EndPlay(EndPlayReason);
}

void AABTSM73BeamD1PreviewActor::ClearPreview()
{
	for (UHierarchicalInstancedStaticMeshComponent* Preview : {
		WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get()})
	{
		if (Preview != nullptr)
		{
			Preview->ClearInstances();
			Preview->SetVisibility(bShowEditorPreview, true);
		}
	}
}

UHierarchicalInstancedStaticMeshComponent* AABTSM73BeamD1PreviewActor::GetPreview(
	const EABTSM7BuildingMaterial Material) const
{
	switch (Material)
	{
	case EABTSM7BuildingMaterial::Stone: return StonePreview;
	case EABTSM7BuildingMaterial::Iron: return IronPreview;
	case EABTSM7BuildingMaterial::Glass: return GlassPreview;
	default: return WoodPreview;
	}
}

void AABTSM73BeamD1PreviewActor::RegeneratePreview()
{
	ClearPreview();
	CompiledBricks.Reset();
	LastSummary = FABTSM73BeamD1Summary();
	FABTSM73BeamD1GenerationResult Result;
	FString Error;
	FABTSM73BeamD1BrickCompiler Compiler;
	if (!Compiler.Generate(Settings, Result, Error))
	{
		LastSummary = Result.Summary;
		UE_LOG(LogABTSRuntime, Warning,
			TEXT("[ABTS][M7.3-Beam-D1][PreviewRejected] Actor=%s Reason=%s"),
			*GetName(), *Error);
		return;
	}
	CompiledBricks = MoveTemp(Result.Bricks);
	LastSummary = Result.Summary;
	if (bShowEditorPreview)
	{
		for (const FABTSM73BeamD1BrickBinding& Brick : CompiledBricks)
		{
			if (UHierarchicalInstancedStaticMeshComponent* Preview =
				GetPreview(Brick.BrickSpec.Material))
			{
				FTransform InstanceTransform = Brick.LocalTransform;
				InstanceTransform.SetScale3D(
					Brick.BrickSpec.DimensionsCM / 100.0f);
				Preview->AddInstance(InstanceTransform, false);
			}
		}
	}
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7.3-Beam-D1][PreviewGenerated]")
		TEXT(" Actor=%s Profile=%s Tier=%d Members=%d Bricks=%d")
		TEXT(" Target=%d-%d Attempt=%d Volumes=%d Box=%d Prism=%d Pyramid=%d RoofBricks=%d Motifs=%d Spans=%d Certified=%d")
		TEXT(" ClosurePass=%d AddedPosts=%d ContactMismatch=%d SupportViolations=%d Advisory=%d")
		TEXT(" Stations=%d/%d AxisDensity=%.3f ClosureRatio=%.3f Quality=%d")
		TEXT(" Wood=%d Stone=%d Iron=%d Glass=%d Weak=%d Device=%d Hash=%lld"),
		*GetName(), *LastSummary.GameplayProfileId.ToString(),
		LastSummary.DifficultyTier, LastSummary.MemberCount,
		LastSummary.BrickCount, LastSummary.TargetMinimumBrickCount,
		LastSummary.TargetMaximumBrickCount,
		LastSummary.VisualCandidateAttempt,
		LastSummary.SemanticVolumeCount,
		LastSummary.SemanticBoxCount,
		LastSummary.SemanticPrismCount,
		LastSummary.SemanticPyramidCount,
		LastSummary.RoofCourseBrickCount,
		LastSummary.DistinctMotifCount,
		LastSummary.SupportedSpanCount,
		LastSummary.bVisualComplexityCertified ? 1 : 0,
		LastSummary.StructuralClosurePassCount,
		LastSummary.AddedStructuralSupportPostCount,
		LastSummary.RealContactMismatchCount,
		LastSummary.RemainingSupportViolationCount,
		LastSummary.SupportResultantAdvisoryCount,
		LastSummary.XColumnStationCount,
		LastSummary.YColumnStationCount,
		LastSummary.AxisStationDensityRatio,
		LastSummary.StructuralClosurePostRatio,
		LastSummary.bAssemblyQualityCertified ? 1 : 0,
		LastSummary.WoodBrickCount,
		LastSummary.StoneBrickCount, LastSummary.IronBrickCount,
		LastSummary.GlassBrickCount, LastSummary.WeaknessCandidateCount,
		LastSummary.DeviceRoleCount, LastSummary.BrickGeometryHash);
}

bool AABTSM73BeamD1PreviewActor::InitializeRuntimeBuilding(
	AABTSM7BuildingMaterialSystem* MaterialSystem)
{
	if (MaterialSystem == nullptr || !RuntimeModules.IsEmpty())
	{
		return false;
	}
	if (!LastSummary.bAccepted || CompiledBricks.IsEmpty())
	{
		RegeneratePreview();
	}
	if (!LastSummary.bAccepted)
	{
		return false;
	}
	for (const FABTSM73BeamD1BrickBinding& Brick : CompiledBricks)
	{
		const FTransform WorldTransform =
			Brick.LocalTransform * GetActorTransform();
		AABTSM7BuildingModule* Module = MaterialSystem->SpawnBrickModule(
			Brick.BrickSpec, WorldTransform);
		if (Module == nullptr)
		{
			for (const TWeakObjectPtr<AABTSM7BuildingModule>& Spawned : RuntimeModules)
			{
				if (Spawned.IsValid()) Spawned->Destroy();
			}
			RuntimeModules.Reset();
			return false;
		}
		RuntimeModules.Add(Module);
	}
	ClearPreview();
	return RuntimeModules.Num() == CompiledBricks.Num();
}

int32 AABTSM73BeamD1PreviewActor::GetRuntimeModuleCountForValidation() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Module : RuntimeModules)
	{
		Count += Module.IsValid() ? 1 : 0;
	}
	return Count;
}
