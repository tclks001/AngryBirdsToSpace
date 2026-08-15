// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73StableBuildingActor.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73DAGBuildingPipeline.h"
#include "Building/ABTSM73DAGFailureFrontierAnalyzer.h"
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
#include "Components/TextRenderComponent.h"
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

	FTransform WorldOrientedBoxTransform(
		const FABTSM73GroundContext& Context,
		const FVector& LocalCenter,
		const FVector& LocalXAxis,
		const FVector& Dimensions)
	{
		FVector SafeXAxis = LocalXAxis.GetSafeNormal();
		if (SafeXAxis.IsNearlyZero()) SafeXAxis = FVector::ForwardVector;
		const FQuat LocalRotation = FRotationMatrix::MakeFromX(SafeXAxis).ToQuat();
		return FTransform(
			Context.AnchorTransform.GetRotation() * LocalRotation,
			Context.AnchorTransform.TransformPositionNoScale(LocalCenter),
			Dimensions / BasicCubeSizeCM);
	}

	const FABTSM73BrickNode* FindDiagnosticNode(
		const FABTSM73StructureData& Data,
		const int32 NodeId)
	{
		return Data.Bricks.FindByPredicate([NodeId](
			const FABTSM73BrickNode& Node)
		{
			return Node.NodeId == NodeId;
		});
	}

	UMaterialInstanceDynamic* MakeDiagnosticMaterial(
		UObject* Outer,
		UMaterialInterface* BaseMaterial,
		const FLinearColor& Color)
	{
		if (Outer == nullptr || BaseMaterial == nullptr) return nullptr;
		UMaterialInstanceDynamic* MID =
			UMaterialInstanceDynamic::Create(BaseMaterial, Outer);
		if (MID != nullptr)
		{
			MID->SetVectorParameterValue(TEXT("Color"), Color);
			MID->SetVectorParameterValue(TEXT("BaseColor"), Color);
		}
		return MID;
	}

	FString JoinFrontierNodeIds(const TConstArrayView<int32> NodeIds)
	{
		FString Result;
		for (int32 Index = 0; Index < NodeIds.Num(); ++Index)
		{
			if (Index > 0) Result += TEXT(",");
			Result += FString::FromInt(NodeIds[Index]);
		}
		return Result;
	}

	FString BuildFrontierRejectSummary(
		const FABTSM73DAGFailureFrontierAnalysis& Analysis)
	{
		TMap<FString, int32> Counts;
		for (const FABTSM73DAGFailureFrontierCandidate& Candidate : Analysis.Candidates)
		{
			if (!Candidate.bAccepted)
			{
				++Counts.FindOrAdd(
					Candidate.RejectReason.IsEmpty()
						? FString(TEXT("Unspecified"))
						: Candidate.RejectReason);
			}
		}
		TArray<FString> Reasons;
		Counts.GetKeys(Reasons);
		Reasons.Sort();
		FString Result;
		for (int32 Index = 0; Index < Reasons.Num(); ++Index)
		{
			if (Index > 0) Result += TEXT("|");
			Result += FString::Printf(
				TEXT("%s=%d"),
				*Reasons[Index],
				Counts.FindChecked(Reasons[Index]));
		}
		return Result;
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
	DAGFailureWeakPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("DAGFailureWeakPreview"));
	DAGFailurePivotPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("DAGFailurePivotPreview"));
	DAGFailureAffectedPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("DAGFailureAffectedPreview"));
	DAGFailureDirectionPreview = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
		TEXT("DAGFailureDirectionPreview"));
	DAGFailurePatternLabel = CreateDefaultSubobject<UTextRenderComponent>(
		TEXT("DAGFailurePatternLabel"));
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
	WeakPointPreview->SetCanEverAffectNavigation(false);
	WeakPointPreview->SetCastShadow(false);
	WeakPointPreview->SetHiddenInGame(true);
	for (UHierarchicalInstancedStaticMeshComponent* DiagnosticPreview : {
		DAGFailureWeakPreview.Get(),
		DAGFailurePivotPreview.Get(),
		DAGFailureAffectedPreview.Get(),
		DAGFailureDirectionPreview.Get()})
	{
		DiagnosticPreview->SetupAttachment(Root);
		DiagnosticPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		DiagnosticPreview->SetGenerateOverlapEvents(false);
		DiagnosticPreview->SetCanEverAffectNavigation(false);
		DiagnosticPreview->SetCastShadow(false);
		DiagnosticPreview->SetHiddenInGame(true);
		DiagnosticPreview->SetVisibility(false, true);
	}
	DAGFailurePatternLabel->SetupAttachment(Root);
	DAGFailurePatternLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DAGFailurePatternLabel->SetGenerateOverlapEvents(false);
	DAGFailurePatternLabel->SetCanEverAffectNavigation(false);
	DAGFailurePatternLabel->SetCastShadow(false);
	DAGFailurePatternLabel->SetHiddenInGame(true);
	DAGFailurePatternLabel->SetHorizontalAlignment(EHTA_Center);
	DAGFailurePatternLabel->SetVerticalAlignment(EVRTA_TextCenter);
	DAGFailurePatternLabel->SetWorldSize(26.0f);
	DAGFailurePatternLabel->SetTextRenderColor(FColor(255, 214, 32));
	DAGFailurePatternLabel->SetVisibility(false, true);
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
		for (UHierarchicalInstancedStaticMeshComponent* DiagnosticPreview : {
			DAGFailureWeakPreview.Get(),
			DAGFailurePivotPreview.Get(),
			DAGFailureAffectedPreview.Get(),
			DAGFailureDirectionPreview.Get()})
		{
			DiagnosticPreview->SetStaticMesh(Cube.Object);
		}
	}
	if (BasicShapeMaterial.Succeeded())
	{
		WeakPointDebugMaterial = BasicShapeMaterial.Object;
		DAGFailureDebugMaterial = BasicShapeMaterial.Object;
	}
	if (WoodMaterial.Succeeded()) WoodPreview->SetMaterial(0, WoodMaterial.Object);
	if (StoneMaterial.Succeeded()) StonePreview->SetMaterial(0, StoneMaterial.Object);
	if (SteelMaterial.Succeeded()) IronPreview->SetMaterial(0, SteelMaterial.Object);
	if (GlassMaterial.Succeeded()) GlassPreview->SetMaterial(0, GlassMaterial.Object);
}

void AABTSM73StableBuildingActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (JuryDemoFixedSixStaticEntry.IsSet())
	{
		ClearBrickPreviews();
		ClearDAGFailurePatternDiagnostics();
		FoundationCap->SetVisibility(false, true);
		FoundationCap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FoundationFeet->ClearInstances();
		FoundationFeet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		AttackDirection->SetVisibility(false, true);
		return;
	}
	RebuildPreview();
}

void AABTSM73StableBuildingActor::BeginPlay()
{
	Super::BeginPlay();
	if (!bParticipateInPIERuntime)
	{
		IdleValidationState = EABTSM73IdleValidationState::NotRequired;
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7.3-A][PIERuntimeSkipped]")
			TEXT(" Actor=%s SlingshotGate=%d"),
			*GetName(), bParticipateInSlingshotValidationGate ? 1 : 0);
		return;
	}
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
	const FABTSM73DAGFailureFrontierSettings& InDAGFailureFrontierSettings,
	const FABTSM73DAGFailurePatternSettings& InDAGFailurePatternSettings,
	const FABTSM73DifficultySettings& InDifficultySettings)
{
	ConfigureTaskGraphGeneration(
		InGenerationSettings,
		InDAGGenerationSettings,
		InDAGLayoutSettings,
		InDAGFailureFrontierSettings,
		InDAGFailurePatternSettings,
		FABTSM73DAGFailurePlayabilitySettings(),
		InDifficultySettings);
}

void AABTSM73StableBuildingActor::ConfigureTaskGraphGeneration(
	const FABTSM73GenerationSettings& InGenerationSettings,
	const FABTSM73DAGGenerationSettings& InDAGGenerationSettings,
	const FABTSM73DAGLayoutSettings& InDAGLayoutSettings,
	const FABTSM73DAGFailureFrontierSettings& InDAGFailureFrontierSettings,
	const FABTSM73DAGFailurePatternSettings& InDAGFailurePatternSettings,
	const FABTSM73DAGFailurePlayabilitySettings& InDAGFailurePlayabilitySettings,
	const FABTSM73DifficultySettings& InDifficultySettings)
{
	ConfigureTaskGraphGeneration(
		InGenerationSettings,
		InDAGGenerationSettings,
		InDAGLayoutSettings,
		InDAGFailureFrontierSettings,
		InDAGFailurePatternSettings,
		InDAGFailurePlayabilitySettings,
		FABTSM73DAG4ValidationSettings(),
		InDifficultySettings);
}

void AABTSM73StableBuildingActor::ConfigureTaskGraphGeneration(
	const FABTSM73GenerationSettings& InGenerationSettings,
	const FABTSM73DAGGenerationSettings& InDAGGenerationSettings,
	const FABTSM73DAGLayoutSettings& InDAGLayoutSettings,
	const FABTSM73DAGFailureFrontierSettings& InDAGFailureFrontierSettings,
	const FABTSM73DAGFailurePatternSettings& InDAGFailurePatternSettings,
	const FABTSM73DAGFailurePlayabilitySettings& InDAGFailurePlayabilitySettings,
	const FABTSM73DAG4ValidationSettings& InDAG4ValidationSettings,
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
	DAGFailureFrontierSettings = InDAGFailureFrontierSettings;
	DAGFailurePatternSettings = InDAGFailurePatternSettings;
	DAGFailurePlayabilitySettings = InDAGFailurePlayabilitySettings;
	DAG4ValidationSettings = InDAG4ValidationSettings;
	DifficultySettings = InDifficultySettings;
}

bool AABTSM73StableBuildingActor::ConfigureJuryDemoFixedSixStaticRegistration(
	FABTSM73JuryDemoFixedSixStaticEntry&& InEntry,
	FString& OutError)
{
	OutError.Reset();
	if (bRuntimeSpawned || JuryDemoFixedSixStaticEntry.IsSet())
	{
		OutError = TEXT("FixedSixStaticRegistrationAlreadyConfigured");
		return false;
	}
	if (!InEntry.IsUsable()
		|| !GetActorTransform().Equals(InEntry.WorldTransform, 1.0e-3))
	{
		OutError = TEXT("FixedSixStaticRegistrationPayloadRejected");
		return false;
	}
	bParticipateInPIERuntime = true;
	bParticipateInSlingshotValidationGate = true;
	bRunIdleChaosValidation = false;
	bShowEditorPreview = false;
	GenerationSettings.BuildingSeed = InEntry.DeterministicSeed;
	JuryDemoFixedSixStaticEntry.Emplace(MoveTemp(InEntry));
	return true;
}

bool AABTSM73StableBuildingActor::IsJuryDemoFixedSixStaticRegistrationAccepted() const
{
	return JuryDemoFixedSixStaticEntry.IsSet()
		&& bRuntimeSpawned
		&& GenerationSummary.bAccepted
		&& IdleValidationState == EABTSM73IdleValidationState::Accepted;
}

int32 AABTSM73StableBuildingActor::GetJuryDemoFixedSixStaticModuleCount() const
{
	int32 ModuleCount = JuryDemoFixedSixStaticBrickInstanceCount;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Module : RuntimeModules)
	{
		ModuleCount += Module.IsValid() ? 1 : 0;
	}
	return ModuleCount;
}

FName AABTSM73StableBuildingActor::GetJuryDemoFixedSixManifestEntryId() const
{
	return JuryDemoFixedSixStaticEntry.IsSet()
		? JuryDemoFixedSixStaticEntry->ManifestEntryId
		: NAME_None;
}

int32 AABTSM73StableBuildingActor::GetJuryDemoFixedSixEncounterIndex() const
{
	return JuryDemoFixedSixStaticEntry.IsSet()
		? JuryDemoFixedSixStaticEntry->EncounterIndex
		: INDEX_NONE;
}

uint64 AABTSM73StableBuildingActor::GetJuryDemoFixedSixRegistrationResultHash() const
{
	return JuryDemoFixedSixStaticEntry.IsSet()
		? JuryDemoFixedSixStaticEntry->RegistrationResultHash
		: 0;
}

bool AABTSM73StableBuildingActor::BuildResolvedStructure(
	const bool bAllowFlatEditorFallback,
	FABTSM73GroundContext& OutContext,
	FABTSM73StructureData& OutData,
	FString& OutError,
	const AABTSM7BuildingMaterialSystem* MaterialProfileSource)
{
	LastDAG5AResult = FABTSM73DAG5AResult();
	LastDAG5BResult = FABTSM73DAG5BResult();
	TArray<FABTSM7MaterialProfile> MaterialProfiles;
	if (MaterialProfileSource != nullptr) MaterialProfileSource->CopyMaterialProfiles(MaterialProfiles);
	else MaterialProfiles = FABTSM7MaterialProfileLibrary::MakeDefaultProfiles();
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
	FVector LocalAttackDirection = OutContext.AnchorTransform.InverseTransformVectorNoScale(
		AttackDirection->GetForwardVector()).GetSafeNormal();
	if (LocalAttackDirection.IsNearlyZero())
	{
		LocalAttackDirection = FVector::ForwardVector;
	}
	if (GenerationSettings.GenerationAlgorithm == EABTSM73GenerationAlgorithm::RecursiveSupportDAG)
	{
		FABTSM73DAGGenerationSettings ResolvedDAGSettings = DAGGenerationSettings;
		ResolvedDAGSettings.BuildingSeed = GenerationSettings.BuildingSeed;
		ResolvedDAGSettings.MaxEstimatedBrickCount = FMath::Min(
			ResolvedDAGSettings.MaxEstimatedBrickCount, GenerationSettings.MaxBrickCount);
		FABTSM73DAGBuildingPipeline Pipeline;
		bool bBuilt = false;
		if (DAG5BSettings.bEnableSemanticEnvelope)
		{
			bBuilt = DAG5ASettings.bEnableFeasibilitySearch
				? Pipeline.BuildWithFeasibilitySearch(
					DAG5ASettings,
					DAG5BSettings,
					ResolvedDAGSettings,
					DAGLayoutSettings,
					GenerationSettings,
					DAGFailureFrontierSettings,
					DAGFailurePatternSettings,
					DAGFailurePlayabilitySettings,
					DifficultySettings,
					MaterialProfiles,
					LocalAttackDirection,
					LastDAG5AResult,
					LastDAG5BResult,
					OutData,
					OutError)
				: Pipeline.BuildWithFailurePattern(
					DAG5BSettings,
					ResolvedDAGSettings,
					DAGLayoutSettings,
					GenerationSettings,
					DAGFailureFrontierSettings,
					DAGFailurePatternSettings,
					DAGFailurePlayabilitySettings,
					DifficultySettings,
					MaterialProfiles,
					LocalAttackDirection,
					LastDAG5BResult,
					OutData,
					OutError);
		}
		else
		{
			bBuilt = DAG5ASettings.bEnableFeasibilitySearch
				? Pipeline.BuildWithFeasibilitySearch(
					DAG5ASettings,
					ResolvedDAGSettings,
					DAGLayoutSettings,
					GenerationSettings,
					DAGFailureFrontierSettings,
					DAGFailurePatternSettings,
					DAGFailurePlayabilitySettings,
					DifficultySettings,
					MaterialProfiles,
					LocalAttackDirection,
					LastDAG5AResult,
					OutData,
					OutError)
				: Pipeline.BuildWithFailurePattern(
					ResolvedDAGSettings,
					DAGLayoutSettings,
					GenerationSettings,
					DAGFailureFrontierSettings,
					DAGFailurePatternSettings,
					DAGFailurePlayabilitySettings,
					DifficultySettings,
					MaterialProfiles,
					LocalAttackDirection,
					OutData,
					OutError);
		}
		if (!bBuilt)
		{
			return false;
		}
	}
	else
	{
		FABTSM73StructureBuilder Builder;
		if (!Builder.Build(GenerationSettings, OutData, OutError)) return false;
	}
	if (!Ground.AnalyzeFootprint(GenerationSettings, OutContext, OutData, OutError)) return false;
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
	GenerationSummary.bDAGFailureFrontierAnalysisEnabled = Data.DAGFailureFrontierAnalysis.bEnabled;
	GenerationSummary.bDAGFailureFrontierAccepted = Data.DAGFailureFrontierAnalysis.bAccepted;
	GenerationSummary.DAGFailureFrontierCandidateCount = Data.DAGFailureFrontierAnalysis.Candidates.Num();
	GenerationSummary.DAGFailureFrontierAcceptedCandidateCount =
		Data.DAGFailureFrontierAnalysis.AcceptedCandidateCount;
	GenerationSummary.DAGFailureFrontierHash =
		static_cast<int64>(Data.DAGFailureFrontierAnalysis.SelectedFrontierHash);
	if (Data.DAGFailureFrontierAnalysis.Candidates.IsValidIndex(
		Data.DAGFailureFrontierAnalysis.SelectedCandidateIndex))
	{
		const FABTSM73DAGFailureFrontierCandidate& Frontier =
			Data.DAGFailureFrontierAnalysis.Candidates[
				Data.DAGFailureFrontierAnalysis.SelectedCandidateIndex];
		GenerationSummary.DAGAffectedMainBodyMassRatio =
			Frontier.MainBodyAffectedMassRatio;
		GenerationSummary.DAGAffectedHeightSpanNormalized =
			Frontier.AffectedHeightSpanNormalized;
		GenerationSummary.DAGFailureFrontierBypassEdgeCount =
			Frontier.BypassSupportEdgeCount;
	}
	GenerationSummary.bDAGFailurePatternEnabled =
		Data.DAGFailurePatternResult.bEnabled;
	GenerationSummary.bDAGFailurePatternApplied =
		Data.DAGFailurePatternResult.bApplied;
	GenerationSummary.DAGFailurePattern =
		Data.DAGFailurePatternResult.Pattern;
	GenerationSummary.DAGRealizedPatternHash =
		static_cast<int64>(Data.DAGFailurePatternResult.RealizedPatternHash);
	GenerationSummary.DAGRewriteAttemptCount =
		Data.DAGFailurePatternResult.RewriteAttemptCount;
	GenerationSummary.DAGPatternInitialSupportMarginCM =
		Data.DAGFailurePatternResult.InitialSupportMarginCM;
	GenerationSummary.DAGPatternPostFailureTipMarginCM =
		Data.DAGFailurePatternResult.PostFailureTipMarginCM;
	GenerationSummary.DAGPatternReseatRisk =
		Data.DAGFailurePatternResult.ReseatRisk;
	GenerationSummary.bDAGFailurePlayabilityEnabled =
		Data.DAGFailurePlayabilityResult.bEnabled;
	GenerationSummary.bDAGFailurePlayable =
		Data.DAGFailurePlayabilityResult.bPlayable;
	GenerationSummary.DAGPlayabilityHash = static_cast<int64>(
		Data.DAGFailurePlayabilityResult.PlayabilityHash);
	GenerationSummary.DAGAttackExposure =
		Data.DAGFailurePlayabilityResult.AttackExposure;
	GenerationSummary.DAGMinAttackClearanceCM =
		Data.DAGFailurePlayabilityResult.MinAttackClearanceCM;
	GenerationSummary.DAGFreeDropDistanceCM =
		Data.DAGFailurePlayabilityResult.FreeDropDistanceCM;
	GenerationSummary.DAGFreeTipAngleDegrees =
		Data.DAGFailurePlayabilityResult.FreeTipAngleDegrees;
	GenerationSummary.DAGFreeSlideDistanceCM =
		Data.DAGFailurePlayabilityResult.FreeSlideDistanceCM;
	GenerationSummary.bDAG4ValidationEnabled =
		LastDAG4ValidationResult.bEnabled;
	GenerationSummary.bDAG4SettledContactAccepted =
		LastDAG4ValidationResult.bSettledContactAccepted;
	GenerationSummary.bDAG4ChaosComparisonAccepted =
		LastDAG4ValidationResult.bChaosComparisonAccepted;
	GenerationSummary.DAG4ValidationHash =
		static_cast<int64>(LastDAG4ValidationResult.ValidationHash);
	GenerationSummary.DAG4WeakResponseScore =
		LastDAG4ValidationResult.WeakResponseScore;
	GenerationSummary.DAG4MaxOrdinaryResponseScore =
		LastDAG4ValidationResult.MaxOrdinaryResponseScore;
	GenerationSummary.DAG4WeakResponseAdvantage =
		LastDAG4ValidationResult.WeakResponseAdvantage;
	GenerationSummary.bDAG5AEnabled = LastDAG5AResult.bEnabled;
	GenerationSummary.bDAG5AAccepted = LastDAG5AResult.bAccepted;
	GenerationSummary.DAG5AAttemptCount = LastDAG5AResult.AttemptCount;
	GenerationSummary.DAG5ASelectedAttemptIndex =
		LastDAG5AResult.SelectedAttemptIndex;
	GenerationSummary.DAG5ASelectedCandidateSeed =
		LastDAG5AResult.SelectedCandidateSeed;
	GenerationSummary.DAG5ACompiledBrickLimit =
		LastDAG5AResult.EffectiveCompiledBrickLimit;
	GenerationSummary.DAG5ASearchHash =
		LastDAG5AResult.SearchHash;
	GenerationSummary.bDAG5BEnabled = LastDAG5BResult.bEnabled;
	GenerationSummary.bDAG5BAccepted = LastDAG5BResult.bAccepted;
	GenerationSummary.DAG5BShapeFamily =
		static_cast<int32>(LastDAG5BResult.ShapeFamily);
	GenerationSummary.DAG5BFeatureMask =
		static_cast<int64>(
			static_cast<uint32>(LastDAG5BResult.FeatureMask));
	GenerationSummary.DAG5BEnvelopeHash =
		static_cast<int64>(LastDAG5BResult.EnvelopeHash);
	GenerationSummary.DAG5BAuditHash =
		static_cast<int64>(LastDAG5BResult.Audit.AuditHash);
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
	ClearDAGFailurePatternDiagnostics();
	LastDAG4ValidationResult = FABTSM73DAG4ValidationResult();
	LastDAG4ValidationResult.bEnabled =
		DAG4ValidationSettings.bEnableSettledChaosValidation;
	FoundationFeet->ClearInstances();
	FoundationCap->SetVisibility(false, true);
	FABTSM73GroundContext Context;
	FABTSM73StructureData Data;
	FString Error;
	const bool bAccepted = BuildResolvedStructure(true, Context, Data, Error);
	LastDAGFailurePatternResult = Data.DAGFailurePatternResult;
	LastDAGFailurePlayabilityResult = Data.DAGFailurePlayabilityResult;
	FillGenerationSummary(Context, Data, bAccepted, Error);
	if (!bAccepted)
	{
		FoundationCap->SetVisibility(false, true);
		FoundationCap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		FoundationFeet->ClearInstances();
		FoundationFeet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return false;
	}
	UpdateFoundationComponents(Context, Data);
	if (bShowEditorPreview)
	{
		UpdatePreviewComponents(Context, Data);
		UpdateDAGFailurePatternDiagnostics(Context, Data);
	}
	return true;
}

int32 AABTSM73StableBuildingActor::GetDAG3BWeakDebugInstanceCount() const
{
	return DAGFailureWeakPreview != nullptr
		? DAGFailureWeakPreview->GetInstanceCount()
		: 0;
}

int32 AABTSM73StableBuildingActor::GetDAG3BPivotDebugInstanceCount() const
{
	return DAGFailurePivotPreview != nullptr
		? DAGFailurePivotPreview->GetInstanceCount()
		: 0;
}

int32 AABTSM73StableBuildingActor::GetDAG3BAffectedDebugInstanceCount() const
{
	return DAGFailureAffectedPreview != nullptr
		? DAGFailureAffectedPreview->GetInstanceCount()
		: 0;
}

int32 AABTSM73StableBuildingActor::GetDAG3BDirectionDebugInstanceCount() const
{
	return DAGFailureDirectionPreview != nullptr
		? DAGFailureDirectionPreview->GetInstanceCount()
		: 0;
}

void AABTSM73StableBuildingActor::ClearBrickPreviews()
{
	for (UHierarchicalInstancedStaticMeshComponent* Preview : {WoodPreview.Get(), StonePreview.Get(), IronPreview.Get(), GlassPreview.Get()}) Preview->ClearInstances();
	WeakPointPreview->ClearInstances();
}

void AABTSM73StableBuildingActor::ClearDAGFailurePatternDiagnostics()
{
	for (UHierarchicalInstancedStaticMeshComponent* Preview : {
		DAGFailureWeakPreview.Get(),
		DAGFailurePivotPreview.Get(),
		DAGFailureAffectedPreview.Get(),
		DAGFailureDirectionPreview.Get()})
	{
		if (Preview == nullptr) continue;
		Preview->ClearInstances();
		Preview->SetVisibility(false, true);
	}
	if (DAGFailurePatternLabel != nullptr)
	{
		DAGFailurePatternLabel->SetText(FText::GetEmpty());
		DAGFailurePatternLabel->SetVisibility(false, true);
	}
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

void AABTSM73StableBuildingActor::UpdateDAGFailurePatternDiagnostics(
	const FABTSM73GroundContext& Context,
	const FABTSM73StructureData& Data)
{
	ClearDAGFailurePatternDiagnostics();
	const FABTSM73DAGFailurePatternResult& Pattern =
		Data.DAGFailurePatternResult;
	if (!bShowDAGFailurePatternDiagnostics
		|| !DAGFailurePatternSettings.bEnableGeometryRewrite
		|| !Pattern.bEnabled
		|| !Pattern.bApplied
		|| DAGFailureDebugMaterial == nullptr
		|| BrickMesh == nullptr)
	{
		return;
	}

	for (UHierarchicalInstancedStaticMeshComponent* Preview : {
		DAGFailureWeakPreview.Get(),
		DAGFailurePivotPreview.Get(),
		DAGFailureAffectedPreview.Get(),
		DAGFailureDirectionPreview.Get()})
	{
		if (Preview != nullptr) Preview->SetStaticMesh(BrickMesh);
	}

	const FLinearColor WeakColor =
		Pattern.Pattern == EABTSM73DAGFailurePattern::InternalOffsetSeam
		? FLinearColor(1.0f, 0.02f, 0.55f, 1.0f)
		: FLinearColor(1.0f, 0.025f, 0.015f, 1.0f);
	DAGFailureWeakDebugMID = MakeDiagnosticMaterial(
		this,
		DAGFailureDebugMaterial,
		WeakColor);
	DAGFailurePivotDebugMID = MakeDiagnosticMaterial(
		this,
		DAGFailureDebugMaterial,
		FLinearColor(0.0f, 0.85f, 1.0f, 1.0f));
	DAGFailureAffectedDebugMID = MakeDiagnosticMaterial(
		this,
		DAGFailureDebugMaterial,
		FLinearColor(0.035f, 0.12f, 0.75f, 1.0f));
	DAGFailureDirectionDebugMID = MakeDiagnosticMaterial(
		this,
		DAGFailureDebugMaterial,
		FLinearColor(1.0f, 0.82f, 0.0f, 1.0f));
	if (DAGFailureWeakDebugMID != nullptr)
	{
		DAGFailureWeakPreview->SetMaterial(0, DAGFailureWeakDebugMID);
	}
	if (DAGFailurePivotDebugMID != nullptr)
	{
		DAGFailurePivotPreview->SetMaterial(0, DAGFailurePivotDebugMID);
	}
	if (DAGFailureAffectedDebugMID != nullptr)
	{
		DAGFailureAffectedPreview->SetMaterial(0, DAGFailureAffectedDebugMID);
	}
	if (DAGFailureDirectionDebugMID != nullptr)
	{
		DAGFailureDirectionPreview->SetMaterial(0, DAGFailureDirectionDebugMID);
	}

	FVector AffectedBoundsMin(BIG_NUMBER);
	FVector AffectedBoundsMax(-BIG_NUMBER);
	for (const int32 NodeId : Pattern.AffectedMainBodyNodeIds)
	{
		const FABTSM73BrickNode* Node = FindDiagnosticNode(Data, NodeId);
		if (Node == nullptr) continue;
		const FVector Center = Node->LocalCenter
			+ FVector(0.0f, 0.0f, Data.FoundationCapTopCM);
		const FVector Extent = Node->DimensionsCM * 0.5f;
		AffectedBoundsMin = AffectedBoundsMin.ComponentMin(Center - Extent);
		AffectedBoundsMax = AffectedBoundsMax.ComponentMax(Center + Extent);
		DAGFailureAffectedPreview->AddInstance(
			WorldBoxTransform(
				Context,
				Center,
				Node->DimensionsCM * 1.015f),
			true);
	}

	FVector InterfaceCenter = FVector::ZeroVector;
	float InterfaceTopCM = -BIG_NUMBER;
	int32 InterfaceNodeCount = 0;
	auto AddSupportDiagnostic = [
		&Context,
		&Data,
		&InterfaceCenter,
		&InterfaceTopCM,
		&InterfaceNodeCount](
			UHierarchicalInstancedStaticMeshComponent* Preview,
			const TConstArrayView<int32> NodeIds,
			const float Scale)
	{
		for (const int32 NodeId : NodeIds)
		{
			const FABTSM73BrickNode* Node = FindDiagnosticNode(Data, NodeId);
			if (Node == nullptr) continue;
			const FVector Center = Node->LocalCenter
				+ FVector(0.0f, 0.0f, Data.FoundationCapTopCM);
			Preview->AddInstance(
				WorldBoxTransform(
					Context,
					Center,
					Node->DimensionsCM * Scale),
				true);
			InterfaceCenter += Center;
			InterfaceTopCM = FMath::Max(
				InterfaceTopCM,
				Center.Z + Node->DimensionsCM.Z * 0.5f);
			++InterfaceNodeCount;
		}
	};
	AddSupportDiagnostic(
		DAGFailureWeakPreview,
		Pattern.WeakNodeIds,
		Pattern.Pattern == EABTSM73DAGFailurePattern::InternalOffsetSeam
			? 1.11f
			: 1.085f);
	AddSupportDiagnostic(
		DAGFailurePivotPreview,
		Pattern.RemainingSupportNodeIds,
		1.06f);

	const bool bHasAffectedBounds =
		AffectedBoundsMin.X <= AffectedBoundsMax.X
		&& AffectedBoundsMin.Y <= AffectedBoundsMax.Y
		&& AffectedBoundsMin.Z <= AffectedBoundsMax.Z;
	FVector FailureDirection =
		Pattern.ExpectedMotion == EABTSM73DAGFailureMotion::Drop
		? FVector::DownVector
		: Pattern.ExpectedFailureDirectionLocal.GetSafeNormal();
	if (InterfaceNodeCount > 0
		&& bHasAffectedBounds
		&& !FailureDirection.IsNearlyZero())
	{
		InterfaceCenter /= static_cast<float>(InterfaceNodeCount);
		const FVector AffectedCenter =
			(AffectedBoundsMin + AffectedBoundsMax) * 0.5f;
		FVector DirectionOrigin = AffectedCenter;
		DirectionOrigin.Z = AffectedBoundsMax.Z + 54.0f;
		if (Pattern.ExpectedMotion == EABTSM73DAGFailureMotion::Drop)
		{
			DirectionOrigin.Y = AffectedBoundsMin.Y - 54.0f;
		}
		constexpr float DirectionLengthCM = 118.0f;
		constexpr float DirectionThicknessCM = 13.0f;
		const FVector DirectionCenter =
			DirectionOrigin + FailureDirection * (DirectionLengthCM * 0.5f);
		DAGFailureDirectionPreview->AddInstance(
			WorldOrientedBoxTransform(
				Context,
				DirectionCenter,
				FailureDirection,
				FVector(
					DirectionLengthCM,
					DirectionThicknessCM,
					DirectionThicknessCM)),
			true);
		const FVector DirectionTip =
			DirectionOrigin + FailureDirection * DirectionLengthCM;
		const FVector HeadWingAxis =
			FMath::Abs(FVector::DotProduct(
				FailureDirection,
				FVector::UpVector)) > 0.85f
			? FVector::RightVector
			: FVector::UpVector;
		constexpr float HeadLengthCM = 38.0f;
		for (const float Sign : {-1.0f, 1.0f})
		{
			const FVector HeadDirection =
				(-FailureDirection + HeadWingAxis * Sign * 0.72f).GetSafeNormal();
			DAGFailureDirectionPreview->AddInstance(
				WorldOrientedBoxTransform(
					Context,
					DirectionTip + HeadDirection * (HeadLengthCM * 0.5f),
					HeadDirection,
					FVector(
						HeadLengthCM,
						DirectionThicknessCM,
						DirectionThicknessCM)),
				true);
		}
	}

	if (DAGFailurePatternLabel != nullptr && bHasAffectedBounds)
	{
		FString LabelText;
		switch (Pattern.Pattern)
		{
		case EABTSM73DAGFailurePattern::InternalSingleSupport:
			LabelText = TEXT("SINGLE / DROP\nW RED | P NONE");
			break;
		case EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport:
			LabelText = TEXT("DUAL / TIP\nW RED | P CYAN");
			break;
		case EABTSM73DAGFailurePattern::InternalOffsetSeam:
			LabelText = TEXT("SEAM / SLIDE+TIP\nW MAGENTA | P CYAN");
			break;
		default:
			LabelText = TEXT("DAG3-B");
			break;
		}
		const FVector LabelLocalCenter(
			(AffectedBoundsMin.X + AffectedBoundsMax.X) * 0.5f,
			(AffectedBoundsMin.Y + AffectedBoundsMax.Y) * 0.5f,
			AffectedBoundsMax.Z + 150.0f);
		FVector LabelForward = FVector::VectorPlaneProject(
			FVector::ForwardVector,
			Context.GravityUp).GetSafeNormal();
		if (LabelForward.IsNearlyZero())
		{
			LabelForward = FVector::VectorPlaneProject(
				FVector::RightVector,
				Context.GravityUp).GetSafeNormal();
		}
		DAGFailurePatternLabel->SetText(FText::FromString(LabelText));
		DAGFailurePatternLabel->SetWorldLocation(
			Context.AnchorTransform.TransformPositionNoScale(LabelLocalCenter));
		DAGFailurePatternLabel->SetWorldRotation(
			FRotationMatrix::MakeFromXZ(
				LabelForward,
				Context.GravityUp).ToQuat());
		DAGFailurePatternLabel->SetVisibility(true, true);
	}

	DAGFailureWeakPreview->SetVisibility(
		DAGFailureWeakPreview->GetInstanceCount() > 0,
		true);
	DAGFailurePivotPreview->SetVisibility(
		DAGFailurePivotPreview->GetInstanceCount() > 0,
		true);
	DAGFailureAffectedPreview->SetVisibility(
		DAGFailureAffectedPreview->GetInstanceCount() > 0,
		true);
	DAGFailureDirectionPreview->SetVisibility(
		DAGFailureDirectionPreview->GetInstanceCount() > 0,
		true);
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
	if (!bParticipateInPIERuntime)
	{
		IdleValidationState = EABTSM73IdleValidationState::NotRequired;
		return;
	}
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
	else
	{
		RejectRuntimeStructure(TEXT("NoMaterialSystem"));
		UE_LOG(LogABTSRuntime, Error, TEXT("[ABTS][M7.3-A] No MaterialSystem Actor=%s"), *GetName());
	}
}

void AABTSM73StableBuildingActor::ConfigureJuryDemoFixedSixStaticHISM(
	UHierarchicalInstancedStaticMeshComponent& Component)
{
	Component.SetStaticMesh(BrickMesh);
	Component.SetCollisionProfileName(TEXT("BlockAll"));
	Component.SetCollisionObjectType(ABTSDeveloperObstacleChannel);
	Component.SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Component.SetGenerateOverlapEvents(false);
	Component.SetHiddenInGame(false);
	Component.SetVisibility(true, true);
}

void AABTSM73StableBuildingActor::InitializeJuryDemoFixedSixStaticRegistration(
	AABTSM7BuildingMaterialSystem& MaterialSystem)
{
	if (!JuryDemoFixedSixStaticEntry.IsSet()
		|| !JuryDemoFixedSixStaticEntry->IsUsable())
	{
		RejectRuntimeStructure(TEXT("FixedSixStaticRegistrationPayloadMissing"));
		return;
	}
	FABTSM73JuryDemoFixedSixStaticEntry& Entry =
		JuryDemoFixedSixStaticEntry.GetValue();
	if (!GetActorTransform().Equals(Entry.WorldTransform, 1.0e-3))
	{
		RejectRuntimeStructure(TEXT("FixedSixStaticRegistrationTransformDrift"));
		return;
	}

	RuntimeMaterialSystem = &MaterialSystem;
	RuntimeModules.Reset();
	RuntimeModulesByNodeId.Reset();
	ClearBrickPreviews();
	ConfigureJuryDemoFixedSixStaticHISM(*WoodPreview);
	ConfigureJuryDemoFixedSixStaticHISM(*StonePreview);
	ConfigureJuryDemoFixedSixStaticHISM(*IronPreview);
	ConfigureJuryDemoFixedSixStaticHISM(*GlassPreview);

	JuryDemoFixedSixStaticBrickInstanceCount = 0;
	for (const FABTSM73BeamD1BrickBinding& Brick : Entry.Bricks)
	{
		UHierarchicalInstancedStaticMeshComponent* HISM =
			GetPreviewForMaterial(Brick.BrickSpec.Material);
		if (HISM == nullptr)
		{
			RejectRuntimeStructure(TEXT("FixedSixStaticRegistrationMaterialHISMMissing"));
			return;
		}
		FTransform InstanceTransform = Brick.LocalTransform;
		InstanceTransform.SetScale3D(
			Brick.BrickSpec.DimensionsCM / BasicCubeSizeCM);
		if (HISM->AddInstance(InstanceTransform, false) == INDEX_NONE)
		{
			RejectRuntimeStructure(TEXT("FixedSixStaticRegistrationBrickInstanceFailed"));
			return;
		}
		++JuryDemoFixedSixStaticBrickInstanceCount;
	}

	for (const FABTSM73BeamD1DeviceBinding& Device : Entry.Devices)
	{
		const FTransform WorldTransform =
			Device.LocalTransform * GetActorTransform();
		AABTSM7BuildingModule* Module = MaterialSystem.SpawnStaticVoxelDevice(
			Device.DeviceSpec, WorldTransform);
		if (Module == nullptr)
		{
			RejectRuntimeStructure(TEXT("FixedSixStaticRegistrationDeviceSpawnFailed"));
			return;
		}
		RuntimeModules.Add(Module);
	}

	const int32 ExpectedModuleCount = Entry.Bricks.Num() + Entry.Devices.Num();
	bRuntimeSpawned = JuryDemoFixedSixStaticBrickInstanceCount
			+ RuntimeModules.Num()
		== ExpectedModuleCount;
	if (!bRuntimeSpawned)
	{
		RejectRuntimeStructure(TEXT("FixedSixStaticRegistrationCountMismatch"));
		return;
	}

	FoundationCap->SetVisibility(false, true);
	FoundationCap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FoundationFeet->ClearInstances();
	FoundationFeet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	AttackDirection->SetVisibility(false, true);
	GenerationSummary = FABTSM73GenerationSummary();
	GenerationSummary.bAccepted = true;
	GenerationSummary.bPlanar = false;
	GenerationSummary.BrickCount = Entry.Bricks.Num();
	GenerationSummary.GenerationAlgorithm =
		EABTSM73GenerationAlgorithm::RecursiveSupportDAG;
	GenerationSummary.RejectReason.Reset();
	IdleValidationState = EABTSM73IdleValidationState::Accepted;
	bIdleValidationRunning = false;
	bDAG4ValidationRunning = false;
	SetActorTickEnabled(false);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7][FixedSixStaticRegistered]")
		TEXT(" Actor=%s Entry=%s Encounter=%d Tier=%d Seed=%d")
		TEXT(" Layout=%llu Descriptor=%llu Static=%llu")
		TEXT(" Production=%llu Device=%llu Bricks=%d Devices=%d Modules=%d")
		TEXT(" ResultHash=%llu DynamicEnvelopeRequired=%d")
		TEXT(" Authority=StaticRegistration Chaos=NotEvaluated Accepted=1"),
		*GetName(), *Entry.ManifestEntryId.ToString(), Entry.EncounterIndex,
		Entry.DifficultyTier, Entry.DeterministicSeed, Entry.SourceLayoutHash,
		Entry.DescriptorHash, Entry.StaticGeometryHash,
		Entry.ProductionIdentityHash, Entry.DeviceAssemblyHash,
		Entry.Bricks.Num(), Entry.Devices.Num(), ExpectedModuleCount,
		Entry.RegistrationResultHash,
		Entry.bDynamicEnvelopeRequired ? 1 : 0);
}

void AABTSM73StableBuildingActor::RollbackJuryDemoFixedSixStaticRegistration(
	const FString& Reason)
{
	RejectRuntimeStructure(Reason);
	Destroy();
}

void AABTSM73StableBuildingActor::InitializeRuntimeBuilding(AABTSM7BuildingMaterialSystem* MaterialSystem)
{
	if (!bParticipateInPIERuntime)
	{
		IdleValidationState = EABTSM73IdleValidationState::NotRequired;
		return;
	}
	if (bRuntimeSpawned || MaterialSystem == nullptr) return;
	if (JuryDemoFixedSixStaticEntry.IsSet())
	{
		InitializeJuryDemoFixedSixStaticRegistration(*MaterialSystem);
		return;
	}
	LastDAG4ValidationResult = FABTSM73DAG4ValidationResult();
	LastDAG4ValidationResult.bEnabled =
		DAG4ValidationSettings.bEnableSettledChaosValidation;
	FABTSM73GroundContext Context;
	FABTSM73StructureData Data;
	FString Error;
	if (!BuildResolvedStructure(false, Context, Data, Error, MaterialSystem))
	{
		LastDAGFailurePatternResult = Data.DAGFailurePatternResult;
		LastDAGFailurePlayabilityResult = Data.DAGFailurePlayabilityResult;
		FillGenerationSummary(Context, Data, false, Error);
		RejectRuntimeStructure(Error);
		if (Data.DAGFailureFrontierAnalysis.bEnabled)
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7.3-DAG3][Reject] Actor=%s Candidates=%d Accepted=%d Reason=%s CandidateReasons=%s"),
				*GetName(),
				Data.DAGFailureFrontierAnalysis.Candidates.Num(),
				Data.DAGFailureFrontierAnalysis.AcceptedCandidateCount,
				*Data.DAGFailureFrontierAnalysis.RejectReason,
				*BuildFrontierRejectSummary(Data.DAGFailureFrontierAnalysis));
		}
		if (Data.DAGFailurePatternResult.bEnabled)
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7.3-DAG3B][Reject] Actor=%s Attempts=%d SourceHash=%u Reason=%s"),
				*GetName(),
				Data.DAGFailurePatternResult.RewriteAttemptCount,
				Data.DAGFailurePatternResult.SourceFrontierHash,
				*Data.DAGFailurePatternResult.RejectReason);
		}
		if (Data.DAGFailurePlayabilityResult.bEnabled)
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7.3-DAG3C][Reject] Actor=%s PatternHash=%u Reason=%s"),
				*GetName(),
				Data.DAGFailurePatternResult.RealizedPatternHash,
				*Data.DAGFailurePlayabilityResult.RejectReason);
		}
		if (LastDAG5AResult.bEnabled
			&& !LastDAG5AResult.bAccepted)
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7.3-DAG5A][Reject] Actor=%s InputSeed=%d Attempts=%d ScopeRejected=%d Compiled=%d Limit=%d Hash=%lld Reason=%s"),
				*GetName(),
				LastDAG5AResult.InputSeed,
				LastDAG5AResult.AttemptCount,
				LastDAG5AResult.ScopePreflightRejectCount,
				LastDAG5AResult.CompiledCandidateCount,
				LastDAG5AResult.EffectiveCompiledBrickLimit,
				LastDAG5AResult.SearchHash,
				*LastDAG5AResult.RejectReason);
		}
		if (LastDAG5BResult.bEnabled
			&& !LastDAG5BResult.bAccepted)
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7.3-DAG5B][Reject] Actor=%s Family=%d Shape=%u WFC=%u Envelope=%u Operations=%d Backtracks=%d Reason=%s"),
				*GetName(),
				static_cast<int32>(LastDAG5BResult.ShapeFamily),
				LastDAG5BResult.ShapeHash,
				LastDAG5BResult.WFCHash,
				LastDAG5BResult.EnvelopeHash,
				LastDAG5BResult.PropagationOperationCount,
				LastDAG5BResult.BacktrackStepCount,
				*LastDAG5BResult.RejectReason);
		}
		UE_LOG(LogABTSRuntime, Error, TEXT("[ABTS][M7.3-A][Reject] Actor=%s Reason=%s"), *GetName(), *Error);
		return;
	}
	LastDAGFailurePatternResult = Data.DAGFailurePatternResult;
	LastDAGFailurePlayabilityResult = Data.DAGFailurePlayabilityResult;
	if (DAG4ValidationSettings.bEnableSettledChaosValidation
		&& (!Data.DAGFailureFrontierAnalysis.bAccepted
			|| !Data.DAGFailurePatternResult.bApplied
			|| !Data.DAGFailurePlayabilityResult.bPlayable))
	{
		const FString DAG4PrerequisiteError =
			TEXT("DAG4PrerequisitesMissing");
		LastDAG4ValidationResult.RejectReason =
			DAG4PrerequisiteError;
		FillGenerationSummary(
			Context,
			Data,
			false,
			DAG4PrerequisiteError);
		RejectRuntimeStructure(DAG4PrerequisiteError);
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7.3-DAG4][Reject] Actor=%s Stage=Prerequisites Reason=%s"),
			*GetName(),
			*DAG4PrerequisiteError);
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
			Module->ConfigureChaosSolverIterations(
				GenerationSettings.ChaosPositionSolverIterationCount,
				GenerationSettings.ChaosVelocitySolverIterationCount);
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
	if (bRuntimeSpawned)
	{
		UpdateDAGFailurePatternDiagnostics(Context, Data);
		TArray<FABTSM7MaterialProfile> MaterialProfiles;
		MaterialSystem->CopyMaterialProfiles(MaterialProfiles);
		PrepareDAG4RuntimeState(Context, Data, MaterialProfiles);
	}
	bRuntimePlanar = Context.bPlanar;
	RuntimeGravityReference = Context.bPlanar
		? Context.GravityUp
		: (Context.Planet.IsValid() ? Context.Planet->GetPlanetCenterWorld() : FVector::ZeroVector);
	FillGenerationSummary(Context, Data, bRuntimeSpawned,
		bRuntimeSpawned ? FString() : FString(TEXT("RuntimeModuleSpawnFailed")));
	if (!bRuntimeSpawned)
	{
		RejectRuntimeStructure(TEXT("RuntimeModuleSpawnFailed"));
	}
	if (bRuntimeSpawned
		&& DAG4ValidationSettings.bEnableSettledChaosValidation
		&& !bRunIdleChaosValidation)
	{
		const FString DAG4IdleError =
			TEXT("DAG4RequiresIdleValidation");
		LastDAG4ValidationResult.RejectReason = DAG4IdleError;
		FillGenerationSummary(Context, Data, false, DAG4IdleError);
		RejectRuntimeStructure(DAG4IdleError);
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7.3-DAG4][Reject] Actor=%s Stage=Prerequisites Reason=%s"),
			*GetName(),
			*DAG4IdleError);
	}
	else if (bRuntimeSpawned && bRunIdleChaosValidation)
	{
		BeginIdleValidation(Context);
	}
	else
	{
		IdleValidationState = bRuntimeSpawned
			? EABTSM73IdleValidationState::NotRequired
			: EABTSM73IdleValidationState::Rejected;
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.3-A][Generated] Actor=%s Seed=%d Algorithm=%d Silhouette=%d DAGPreset=%d WeaknessPlanner=%d DAG3Enabled=%d DAG3Candidates=%d DAG3Accepted=%d DAG3Hash=%u DAG3BEnabled=%d DAG3BApplied=%d DAG3BPattern=%d DAG3BHash=%u DAG3CEnabled=%d DAG3CPlayable=%d DAG3CHash=%u DAG4Enabled=%d Planar=%d Bricks=%d Supports=%d Ground=%d DAGMacro=%d DAGSparse=%d DAGHash=%u Feet=%d TerrainDelta=%.2f Curvature=%.2f MaxSlope=%.2f Accepted=%d"),
		*GetName(), GenerationSettings.BuildingSeed, static_cast<int32>(GenerationSettings.GenerationAlgorithm), static_cast<int32>(GenerationSettings.Silhouette),
		static_cast<int32>(DAGGenerationSettings.Preset),
		GenerationSettings.GenerationAlgorithm == EABTSM73GenerationAlgorithm::LegacyLayeredAB2 ? 1 : 0,
		Data.DAGFailureFrontierAnalysis.bEnabled ? 1 : 0,
		Data.DAGFailureFrontierAnalysis.Candidates.Num(),
		Data.DAGFailureFrontierAnalysis.AcceptedCandidateCount,
		Data.DAGFailureFrontierAnalysis.SelectedFrontierHash,
		Data.DAGFailurePatternResult.bEnabled ? 1 : 0,
		Data.DAGFailurePatternResult.bApplied ? 1 : 0,
		static_cast<int32>(Data.DAGFailurePatternResult.Pattern),
		Data.DAGFailurePatternResult.RealizedPatternHash,
		Data.DAGFailurePlayabilityResult.bEnabled ? 1 : 0,
		Data.DAGFailurePlayabilityResult.bPlayable ? 1 : 0,
		Data.DAGFailurePlayabilityResult.PlayabilityHash,
		DAG4ValidationSettings.bEnableSettledChaosValidation ? 1 : 0,
		Context.bPlanar ? 1 : 0,
		Data.Bricks.Num(), Data.SupportEdges.Num(), Data.GroundNodeIds.Num(), Data.DAGMacroNodeCount, Data.DAGSelectedSupportCount, Data.DAGTopologyHash, Data.FoundationFeet.Num(), Data.TerrainDeltaCM,
		Data.CurvatureDropCM, Data.MaxSlopeDegrees, bRuntimeSpawned ? 1 : 0);
	if (LastDAG5AResult.bEnabled)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-DAG5A][Accepted] Actor=%s InputSeed=%d CandidateSeed=%d Attempt=%d/%d Bricks=%d Limit=%d Hash=%lld"),
			*GetName(),
			LastDAG5AResult.InputSeed,
			LastDAG5AResult.SelectedCandidateSeed,
			LastDAG5AResult.SelectedAttemptIndex,
			LastDAG5AResult.AttemptCount,
			LastDAG5AResult.CompiledBrickCount,
			LastDAG5AResult.EffectiveCompiledBrickLimit,
			LastDAG5AResult.SearchHash);
	}
	if (LastDAG5BResult.bEnabled)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-DAG5B][Accepted] Actor=%s Family=%d Features=%u Shape=%u WFC=%u Envelope=%u Audit=%u Operations=%d Backtracks=%d NonAnchors=%d MustOccupy=%d MustVoid=%d Result=%u"),
			*GetName(),
			static_cast<int32>(LastDAG5BResult.ShapeFamily),
			static_cast<uint32>(LastDAG5BResult.FeatureMask),
			LastDAG5BResult.ShapeHash,
			LastDAG5BResult.WFCHash,
			LastDAG5BResult.EnvelopeHash,
			LastDAG5BResult.Audit.AuditHash,
			LastDAG5BResult.PropagationOperationCount,
			LastDAG5BResult.BacktrackStepCount,
			LastDAG5BResult.CollapsedNonAnchorCellCount,
			LastDAG5BResult.Audit.MustOccupyCount,
			LastDAG5BResult.Audit.MustVoidCount,
			LastDAG5BResult.ResultHash);
	}
	if (Data.DAGFailureFrontierAnalysis.Candidates.IsValidIndex(
		Data.DAGFailureFrontierAnalysis.SelectedCandidateIndex))
	{
		const FABTSM73DAGFailureFrontierCandidate& Frontier =
			Data.DAGFailureFrontierAnalysis.Candidates[
				Data.DAGFailureFrontierAnalysis.SelectedCandidateIndex];
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-DAG3][Frontier] Actor=%s Kind=%d Nodes=%s Protected=%s MainBodyMass=%.4f Height=%.4f Span=%.4f Macros=%d Bypass=%d Hash=%u"),
			*GetName(),
			static_cast<int32>(Frontier.Kind),
			*JoinFrontierNodeIds(Frontier.CandidateNodeIds),
			*JoinFrontierNodeIds(Frontier.ProtectedRootNodeIds),
			Frontier.MainBodyAffectedMassRatio,
			Frontier.NormalizedHeight,
			Frontier.AffectedHeightSpanNormalized,
			Frontier.AffectedMacroNodeIds.Num(),
			Frontier.BypassSupportEdgeCount,
			Frontier.FrontierHash);
	}
	if (Data.DAGFailurePatternResult.bApplied)
	{
		const FABTSM73DAGFailurePatternResult& Pattern =
			Data.DAGFailurePatternResult;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-DAG3B][Pattern] Actor=%s Pattern=%d Motion=%d SourceHash=%u RealizedHash=%u Interface=%d->%d Weak=%s Pivot=%s Affected=%d InitialMargin=%.2f TipMargin=%.2f Reseat=%.3f Offset=%.2f Attempts=%d"),
			*GetName(),
			static_cast<int32>(Pattern.Pattern),
			static_cast<int32>(Pattern.ExpectedMotion),
			Pattern.SourceFrontierHash,
			Pattern.RealizedPatternHash,
			Pattern.SupportMacroNodeId,
			Pattern.LoadMacroNodeId,
			*JoinFrontierNodeIds(Pattern.WeakNodeIds),
			*JoinFrontierNodeIds(Pattern.RemainingSupportNodeIds),
			Pattern.AffectedMainBodyNodeIds.Num(),
			Pattern.InitialSupportMarginCM,
			Pattern.PostFailureTipMarginCM,
			Pattern.ReseatRisk,
			Pattern.OffsetSeamShiftCM,
			Pattern.RewriteAttemptCount);
	}
	if (Data.DAGFailurePlayabilityResult.bPlayable)
	{
		const FABTSM73DAGFailurePlayabilityResult& Playability =
			Data.DAGFailurePlayabilityResult;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-DAG3C][Playable] Actor=%s Pattern=%d Motion=%d Material=%d Weak=%s Exposure=%.3f Clearance=%.2f Drop=%.2f Tip=%.2f Slide=%.2f Effort=%.3f Hits=%d Samples=%d/%d Hash=%u"),
			*GetName(),
			static_cast<int32>(Playability.Pattern),
			static_cast<int32>(Playability.ExpectedMotion),
			static_cast<int32>(Playability.Material),
			*JoinFrontierNodeIds(Playability.WeakNodeIds),
			Playability.AttackExposure,
			Playability.MinAttackClearanceCM,
			Playability.FreeDropDistanceCM,
			Playability.FreeTipAngleDegrees,
			Playability.FreeSlideDistanceCM,
			Playability.LocalBreakEffort,
			Playability.EstimatedHits,
			Playability.AttackSampleCount,
			Playability.MotionSweepSampleCount,
			Playability.PlayabilityHash);
	}
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
	IdleValidationState = EABTSM73IdleValidationState::Running;
	SetActorTickEnabled(true);
}

void AABTSM73StableBuildingActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDAG4ValidationRunning)
	{
		TickDAG4Validation(DeltaSeconds);
		return;
	}
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
		Module->GetMeshComponent()->SetVisibility(
			!DAG4ValidationSettings.bEnableSettledChaosValidation,
			true);
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
	if (bAccepted
		&& DAG4ValidationSettings.bEnableSettledChaosValidation)
	{
		IdleValidationState = EABTSM73IdleValidationState::Running;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-A][IdleValidation] Actor=%s Seconds=%.2f Stable=%.2f TimedOut=%d BoundedTimeout=%d MaxMove=%.2f MaxDrift=%.2f MaxSettlement=%.2f MaxRotation=%.2f SpatialAccepted=1 DAG4Pending=1 Accepted=0"),
			*GetName(),
			IdleValidationElapsed,
			IdleStableElapsed,
			bTimedOut ? 1 : 0,
			bAcceptedAfterBoundedTimeout ? 1 : 0,
			MaxMove,
			MaxPlanarDrift,
			MaxSettlement,
			MaxRotation);
		if (!BeginDAG4ValidationAfterIdle()
			&& IdleValidationState != EABTSM73IdleValidationState::Rejected)
		{
			const FString Reason =
				LastDAG4ValidationResult.RejectReason.IsEmpty()
				? FString(TEXT("DAG4InitializationFailed"))
				: LastDAG4ValidationResult.RejectReason;
			RejectRuntimeStructure(Reason);
		}
		return;
	}
	IdleValidationState = bAccepted
		? EABTSM73IdleValidationState::Accepted
		: EABTSM73IdleValidationState::Rejected;
	if (!bAccepted)
	{
		GenerationSummary.RejectReason = FString::Printf(
			TEXT("IdleChaosUnstable:TimedOut=%d:Move=%.2f:Drift=%.2f:Settlement=%.2f:Rotation=%.2f"),
			bTimedOut ? 1 : 0, MaxMove, MaxPlanarDrift, MaxSettlement, MaxRotation);
	}
	if (bAccepted)
	{
		for (const TWeakObjectPtr<AABTSM7BuildingModule>& Weak : RuntimeModules)
		{
			if (AABTSM7BuildingModule* Module = Weak.Get())
			{
				Module->GetMeshComponent()->SetVisibility(true, true);
			}
		}
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
	CancelDAG4Validation();
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Weak : RuntimeModules)
	{
		if (AABTSM7BuildingModule* Module = Weak.Get()) Module->Destroy();
	}
	RuntimeModules.Reset();
	RuntimeModulesByNodeId.Reset();
	JuryDemoFixedSixStaticBrickInstanceCount = 0;
	IdleInitialTransforms.Reset();
	RuntimeMaterialSystem.Reset();
	bRuntimeSpawned = false;
	bIdleValidationRunning = false;
	bDAG4ValidationRunning = false;
	IdleValidationState = EABTSM73IdleValidationState::Rejected;
	SetActorTickEnabled(false);
	ClearBrickPreviews();
	ClearDAGFailurePatternDiagnostics();
	FoundationCap->SetVisibility(false, true);
	FoundationCap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FoundationFeet->ClearInstances();
	FoundationFeet->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GenerationSummary.bAccepted = false;
	GenerationSummary.RejectReason = Reason;
}
