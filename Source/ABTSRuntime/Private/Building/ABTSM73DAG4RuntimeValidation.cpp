// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73StableBuildingActor.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM73DAG4ResponseEvaluator.h"
#include "Building/ABTSM73DAG4SettledContactValidator.h"
#include "Building/ABTSM73DAG4TrialPlanner.h"
#include "Building/ABTSM73GroundAdapter.h"
#include "Building/ABTSM73StructureData.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Building/ABTSM7MaterialProfileLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodyInstance.h"

namespace
{
	uint64 DAG4PairKey(const int32 NodeA, const int32 NodeB)
	{
		const int32 Lower = FMath::Min(NodeA, NodeB);
		const int32 Upper = FMath::Max(NodeA, NodeB);
		return (static_cast<uint64>(static_cast<uint32>(Lower)) << 32)
			| static_cast<uint32>(Upper);
	}

	void DecodeDAG4PairKey(
		const uint64 Key,
		int32& OutNodeA,
		int32& OutNodeB)
	{
		OutNodeA = static_cast<int32>(
			static_cast<uint32>(Key >> 32));
		OutNodeB = static_cast<int32>(
			static_cast<uint32>(Key));
	}

	FString JoinDAG4NodeIds(const TConstArrayView<int32> NodeIds)
	{
		FString Result;
		for (int32 Index = 0; Index < NodeIds.Num(); ++Index)
		{
			if (Index > 0) Result += TEXT(",");
			Result += FString::FromInt(NodeIds[Index]);
		}
		return Result;
	}

	const FABTSM7MaterialProfile* FindDAG4Profile(
		const TConstArrayView<FABTSM7MaterialProfile> Profiles,
		const EABTSM7BuildingMaterial Material)
	{
		return FABTSM7MaterialProfileLibrary::FindProfile(
			Profiles,
			Material);
	}
}

struct FABTSM73DAG4RuntimeState
{
	FABTSM73GroundContext GroundContext;
	FABTSM73StructureData BaselineData;
	TArray<FABTSM7MaterialProfile> MaterialProfiles;
	TArray<FABTSM73DAG4SettledNode> SettledNodes;
	TArray<FABTSM73DAG4SettledContact> SettledContacts;
	TMap<int32, FTransform> SettledWorldTransforms;
	TArray<FABTSM73DAG4TrialPlan> Plans;

	TArray<TWeakObjectPtr<AABTSM7BuildingModule>> ShadowModules;
	TMap<TWeakObjectPtr<AABTSM7BuildingModule>, int32> ShadowNodeIds;
	TMap<int32, FTransform> ShadowInitialTransforms;
	TArray<FABTSM73DAG4NodeOutcome> CurrentOutcomes;
	TSet<uint64> CurrentSecondaryPairs;
	TMap<uint64, float> LastSecondaryPairSeconds;
	TWeakObjectPtr<AStaticMeshActor> ShadowFoundation;

	FVector ShadowOffset = FVector::ZeroVector;
	int32 CurrentTrialIndex = INDEX_NONE;
	float CurrentTrialElapsed = 0.0f;
	float TotalValidationElapsed = 0.0f;
	int32 CurrentTrialTicks = 0;
	bool bCleanupBarrier = false;
};

void FABTSM73DAG4RuntimeStateDeleter::operator()(
	FABTSM73DAG4RuntimeState* State) const
{
	delete State;
}

AABTSM73StableBuildingActor::~AABTSM73StableBuildingActor() = default;

void AABTSM73StableBuildingActor::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	if (JuryDemoFixedSixStaticEntry.IsSet()
		&& (bRuntimeSpawned
			|| JuryDemoFixedSixStaticBrickInstanceCount > 0
			|| !RuntimeModules.IsEmpty()))
	{
		RejectRuntimeStructure(TEXT("FixedSixStaticRegistrationEndPlay"));
	}
	CancelDAG4Validation();
	Super::EndPlay(EndPlayReason);
}

void AABTSM73StableBuildingActor::PrepareDAG4RuntimeState(
	const FABTSM73GroundContext& Context,
	const FABTSM73StructureData& Data,
	const TConstArrayView<FABTSM7MaterialProfile> MaterialProfiles)
{
	CancelDAG4Validation();
	LastDAG4ValidationResult = FABTSM73DAG4ValidationResult();
	LastDAG4ValidationResult.bEnabled =
		DAG4ValidationSettings.bEnableSettledChaosValidation;
	if (!DAG4ValidationSettings.bEnableSettledChaosValidation)
	{
		return;
	}
	DAG4RuntimeState.Reset(new FABTSM73DAG4RuntimeState());
	DAG4RuntimeState->GroundContext = Context;
	DAG4RuntimeState->BaselineData = Data;
	DAG4RuntimeState->MaterialProfiles = MaterialProfiles;
}

bool AABTSM73StableBuildingActor::SpawnDAG4ShadowTrial(
	FString& OutError)
{
	OutError.Reset();
	if (!DAG4RuntimeState
		|| !DAG4RuntimeState->Plans.IsValidIndex(
			DAG4RuntimeState->CurrentTrialIndex)
		|| GetWorld() == nullptr
		|| FoundationCap == nullptr
		|| FoundationCap->GetStaticMesh() == nullptr
		|| !RuntimeMaterialSystem.IsValid())
	{
		OutError = TEXT("DAG4ShadowTrialInputInvalid");
		return false;
	}
	FABTSM73DAG4RuntimeState& State = *DAG4RuntimeState;
	const FABTSM73DAG4TrialPlan& Plan =
		State.Plans[State.CurrentTrialIndex];
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	FTransform FoundationTransform =
		FoundationCap->GetComponentTransform();
	FoundationTransform.AddToTranslation(State.ShadowOffset);
	AStaticMeshActor* ShadowFoundation =
		GetWorld()->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(),
			FoundationTransform,
			SpawnParameters);
	if (ShadowFoundation == nullptr)
	{
		OutError = TEXT("DAG4ShadowFoundationSpawnFailed");
		return false;
	}
	State.ShadowFoundation = ShadowFoundation;
	UStaticMeshComponent* FoundationMesh =
		ShadowFoundation->GetStaticMeshComponent();
	FoundationMesh->SetMobility(EComponentMobility::Movable);
	FoundationMesh->SetStaticMesh(FoundationCap->GetStaticMesh());
	if (FoundationCap->GetMaterial(0) != nullptr)
	{
		FoundationMesh->SetMaterial(0, FoundationCap->GetMaterial(0));
	}
	FoundationMesh->SetCollisionProfileName(TEXT("BlockAll"));
	FoundationMesh->SetCollisionEnabled(
		ECollisionEnabled::QueryAndPhysics);
	FoundationMesh->SetSimulatePhysics(false);
	FoundationMesh->SetVisibility(false, true);
	ShadowFoundation->SetActorHiddenInGame(true);

	State.ShadowModules.Reset();
	State.ShadowNodeIds.Reset();
	State.ShadowInitialTransforms.Reset();
	State.CurrentOutcomes.Reset();
	State.CurrentSecondaryPairs.Reset();
	State.LastSecondaryPairSeconds.Reset();
	for (const FABTSM73DAG4SettledNode& Node : State.SettledNodes)
	{
		if (Plan.RemovedNodeIds.Contains(Node.NodeId))
		{
			continue;
		}
		const TWeakObjectPtr<AABTSM7BuildingModule>* FormalWeak =
			RuntimeModulesByNodeId.Find(Node.NodeId);
		AABTSM7BuildingModule* Formal =
			FormalWeak != nullptr ? FormalWeak->Get() : nullptr;
		UStaticMeshComponent* FormalMesh =
			Formal != nullptr ? Formal->GetMeshComponent() : nullptr;
		const FTransform* SettledWorld =
			State.SettledWorldTransforms.Find(Node.NodeId);
		const FABTSM7MaterialProfile* Profile =
			FindDAG4Profile(State.MaterialProfiles, Node.Material);
		if (!IsValid(Formal)
			|| FormalMesh == nullptr
			|| FormalMesh->GetStaticMesh() == nullptr
			|| SettledWorld == nullptr
			|| Profile == nullptr)
		{
			OutError = FString::Printf(
				TEXT("DAG4ShadowSourceInvalid:%d"),
				Node.NodeId);
			return false;
		}

		FTransform ShadowTransform = *SettledWorld;
		ShadowTransform.AddToTranslation(State.ShadowOffset);
		AABTSM7BuildingModule* Shadow =
			GetWorld()->SpawnActor<AABTSM7BuildingModule>(
				AABTSM7BuildingModule::StaticClass(),
				ShadowTransform,
				SpawnParameters);
		if (Shadow == nullptr)
		{
			OutError = FString::Printf(
				TEXT("DAG4ShadowModuleSpawnFailed:%d"),
				Node.NodeId);
			return false;
		}
		Shadow->ConfigureBrick(
			FormalMesh->GetStaticMesh(),
			FormalMesh->GetMaterial(0),
			Node.Material,
			ShadowTransform);
		Shadow->ConfigureImpactPhysics(*Profile);
		Shadow->ConfigureChaosSolverIterations(
			GenerationSettings.ChaosPositionSolverIterationCount,
			GenerationSettings.ChaosVelocitySolverIterationCount);
		Shadow->SetContactDamageGraceSeconds(
			DAG4ValidationSettings.TrialDurationSeconds + 1.0f);
		Shadow->GetMeshComponent()->SetVisibility(false, true);
		Shadow->GetMeshComponent()->OnComponentHit.AddDynamic(
			this,
			&AABTSM73StableBuildingActor::HandleDAG4ModuleHit);
		State.ShadowModules.Add(Shadow);
		State.ShadowNodeIds.Add(Shadow, Node.NodeId);
		State.ShadowInitialTransforms.Add(Node.NodeId, ShadowTransform);
		FABTSM73DAG4NodeOutcome& Outcome =
			State.CurrentOutcomes.AddDefaulted_GetRef();
		Outcome.NodeId = Node.NodeId;
	}

	TArray<AABTSM7BuildingModule*> PendingModules;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Weak
		: State.ShadowModules)
	{
		if (AABTSM7BuildingModule* Module = Weak.Get())
		{
			PendingModules.Add(Module);
		}
	}
	// Do not run the pre-launch penetration repair here. These transforms are
	// an already settled snapshot, so tiny Chaos contact overlaps are part of
	// the certified initial state. Repairing them would mutate each removal
	// trial differently and invalidate the reversible comparison.
	for (AABTSM7BuildingModule* Module : PendingModules)
	{
		if (bRuntimePlanar)
		{
			Module->ActivateDynamicPlanar(
				FVector::ZeroVector,
				RuntimeGravityReference,
				ValidationGravityCMPerSec2);
		}
		else
		{
			Module->ActivateDynamic(
				FVector::ZeroVector,
				RuntimeGravityReference + State.ShadowOffset,
				ValidationGravityCMPerSec2);
		}
	}
	State.CurrentTrialElapsed = 0.0f;
	State.CurrentTrialTicks = 0;
	const FABTSM73BrickNode* RemovedNode =
		Plan.RemovedNodeIds.Num() == 1
			? State.BaselineData.Bricks.FindByPredicate(
				[&Plan](const FABTSM73BrickNode& Node)
				{
					return Node.NodeId == Plan.RemovedNodeIds[0];
				})
			: nullptr;
	int32 RemovedColumnRole = INDEX_NONE;
	if (RemovedNode != nullptr)
	{
		for (const FABTSM73DAGPhysicalSupportMapping& Mapping
			: State.BaselineData.DAGPhysicalSupportMappings)
		{
			const int32 ColumnIndex =
				Mapping.ColumnNodeIds.IndexOfByKey(
					RemovedNode->NodeId);
			if (Mapping.ColumnRoles.IsValidIndex(ColumnIndex))
			{
				RemovedColumnRole = static_cast<int32>(
					Mapping.ColumnRoles[ColumnIndex]);
				break;
			}
		}
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.3-DAG4][TrialBegin] Actor=%s Type=%s Probe=%d Removed=%s Bodies=%d Predicted=%.3f Macro=%d Semantic=%d ColumnRole=%d Center=(%.1f,%.1f,%.1f) Size=(%.1f,%.1f,%.1f)"),
		*GetName(),
		Plan.Kind == EABTSM73DAG4TrialKind::WeakPoint
			? TEXT("Weak")
			: TEXT("Ordinary"),
		Plan.ProbeIndex,
		*JoinDAG4NodeIds(Plan.RemovedNodeIds),
		PendingModules.Num(),
		Plan.PredictedAffectedMainBodyMassRatio,
		RemovedNode != nullptr ? RemovedNode->MacroNodeId : INDEX_NONE,
		RemovedNode != nullptr
			? static_cast<int32>(RemovedNode->SemanticRole)
			: INDEX_NONE,
		RemovedColumnRole,
		RemovedNode != nullptr ? RemovedNode->LocalCenter.X : 0.0f,
		RemovedNode != nullptr ? RemovedNode->LocalCenter.Y : 0.0f,
		RemovedNode != nullptr ? RemovedNode->LocalCenter.Z : 0.0f,
		RemovedNode != nullptr ? RemovedNode->DimensionsCM.X : 0.0f,
		RemovedNode != nullptr ? RemovedNode->DimensionsCM.Y : 0.0f,
		RemovedNode != nullptr ? RemovedNode->DimensionsCM.Z : 0.0f);
	return true;
}

void AABTSM73StableBuildingActor::CleanupDAG4ShadowTrial()
{
	if (!DAG4RuntimeState)
	{
		return;
	}
	FABTSM73DAG4RuntimeState& State = *DAG4RuntimeState;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Weak
		: State.ShadowModules)
	{
		if (AABTSM7BuildingModule* Module = Weak.Get())
		{
			if (UStaticMeshComponent* Mesh =
				Module->GetMeshComponent())
			{
				Mesh->OnComponentHit.RemoveDynamic(
					this,
					&AABTSM73StableBuildingActor::
						HandleDAG4ModuleHit);
			}
			Module->Freeze();
			Module->Destroy();
		}
	}
	State.ShadowModules.Reset();
	State.ShadowNodeIds.Reset();
	State.ShadowInitialTransforms.Reset();
	State.CurrentOutcomes.Reset();
	State.CurrentSecondaryPairs.Reset();
	State.LastSecondaryPairSeconds.Reset();
	if (AStaticMeshActor* Foundation = State.ShadowFoundation.Get())
	{
		Foundation->Destroy();
	}
	State.ShadowFoundation.Reset();
}

bool AABTSM73StableBuildingActor::BeginDAG4ValidationAfterIdle()
{
	auto Reject = [this](const FString& Reason)
	{
		LastDAG4ValidationResult.bEnabled = true;
		LastDAG4ValidationResult.bAccepted = false;
		LastDAG4ValidationResult.RejectReason = Reason;
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7.3-DAG4][Reject] Actor=%s Stage=Settled Reason=%s"),
			*GetName(),
			*Reason);
		CancelDAG4Validation();
		return false;
	};

	if (!DAG4ValidationSettings.bEnableSettledChaosValidation
		|| !DAG4RuntimeState)
	{
		return Reject(TEXT("DAG4RuntimeStateMissing"));
	}
	FABTSM73DAG4RuntimeState& State = *DAG4RuntimeState;
	const FABTSM73StructureData& Data = State.BaselineData;
	if (DAG4ValidationSettings.NonWeakProbeCount < 3
		|| DAG4ValidationSettings.MaxTrialCount
			< DAG4ValidationSettings.NonWeakProbeCount + 1
		|| DAG4ValidationSettings.TrialDurationSeconds <= 0.0f
		|| DAG4ValidationSettings.MaxTrialTickCount <= 0
		|| DAG4ValidationSettings.MaxTotalValidationSeconds
			< DAG4ValidationSettings.TrialDurationSeconds
				* static_cast<float>(
					DAG4ValidationSettings.NonWeakProbeCount + 1))
	{
		return Reject(TEXT("DAG4SettingsInvalid"));
	}
	if (Data.Bricks.Num() <= 0
		|| Data.Bricks.Num()
			> DAG4ValidationSettings.MaxSettledBodyCount)
	{
		return Reject(FString::Printf(
			TEXT("DAG4BodyBudgetExceeded:%d:%d"),
			Data.Bricks.Num(),
			DAG4ValidationSettings.MaxSettledBodyCount));
	}
	const int64 RequiredPairQueries =
		static_cast<int64>(Data.Bricks.Num())
		* static_cast<int64>(Data.Bricks.Num() - 1) / 2;
	if (RequiredPairQueries
		> DAG4ValidationSettings.MaxContactPairQueryCount)
	{
		return Reject(FString::Printf(
			TEXT("DAG4ContactQueryBudgetExceeded:%lld:%d"),
			RequiredPairQueries,
			DAG4ValidationSettings.MaxContactPairQueryCount));
	}

	State.SettledNodes.Reset();
	State.SettledWorldTransforms.Reset();
	TSet<int32> SeenNodeIds;
	for (const FABTSM73BrickNode& Node : Data.Bricks)
	{
		if (Node.NodeId < 0 || SeenNodeIds.Contains(Node.NodeId))
		{
			return Reject(TEXT("DAG4SettledNodeIdentityInvalid"));
		}
		SeenNodeIds.Add(Node.NodeId);
		const TWeakObjectPtr<AABTSM7BuildingModule>* WeakModule =
			RuntimeModulesByNodeId.Find(Node.NodeId);
		AABTSM7BuildingModule* Module =
			WeakModule != nullptr ? WeakModule->Get() : nullptr;
		UStaticMeshComponent* Mesh =
			Module != nullptr ? Module->GetMeshComponent() : nullptr;
		if (!IsValid(Module)
			|| Mesh == nullptr
			|| Module->GetModuleKind() != EABTSM7ModuleKind::Brick
			|| Module->GetBuildingMaterial() != Node.Material
			|| Module->IsActorBeingDestroyed())
		{
			return Reject(FString::Printf(
				TEXT("DAG4SettledModuleMissingOrInvalid:%d"),
				Node.NodeId));
		}
		const FVector ExpectedScale =
			Node.DimensionsCM / 100.0f;
		if (!Mesh->GetComponentScale().GetAbs().Equals(
			ExpectedScale.GetAbs(),
			0.025f))
		{
			return Reject(FString::Printf(
				TEXT("DAG4SettledModuleScaleMismatch:%d"),
				Node.NodeId));
		}
		const FABTSM7MaterialProfile* Profile =
			FindDAG4Profile(State.MaterialProfiles, Node.Material);
		if (Profile == nullptr
			|| !FMath::IsFinite(Profile->DensityGPerCubicCM)
			|| Profile->DensityGPerCubicCM <= 0.0f)
		{
			return Reject(FString::Printf(
				TEXT("DAG4MaterialProfileInvalid:%d"),
				Node.NodeId));
		}

		const FTransform WorldTransform =
			Module->GetActorTransform();
		const FVector AnchorLocalCenter =
			State.GroundContext.AnchorTransform
				.InverseTransformPositionNoScale(
					WorldTransform.GetLocation());
		const FQuat LocalRotation =
			State.GroundContext.AnchorTransform.GetRotation().Inverse()
			* WorldTransform.GetRotation();
		FABTSM73DAG4SettledNode& Settled =
			State.SettledNodes.AddDefaulted_GetRef();
		Settled.NodeId = Node.NodeId;
		Settled.MacroNodeId = Node.MacroNodeId;
		Settled.Material = Node.Material;
		Settled.DimensionsCM = Node.DimensionsCM;
		Settled.LocalTransform = FTransform(
			LocalRotation.GetNormalized(),
			AnchorLocalCenter
				- FVector(0.0f, 0.0f, Data.FoundationCapTopCM),
			FVector::OneVector);
		Settled.Mass =
			static_cast<double>(Node.DimensionsCM.X)
			* Node.DimensionsCM.Y
			* Node.DimensionsCM.Z
			* Profile->DensityGPerCubicCM;
		Settled.bMainBody =
			Node.bFailureFrontierMainBody;
		State.SettledWorldTransforms.Add(
			Node.NodeId,
			WorldTransform);
	}
	State.SettledNodes.Sort(
		[](const FABTSM73DAG4SettledNode& A,
			const FABTSM73DAG4SettledNode& B)
		{
			return A.NodeId < B.NodeId;
		});

	FABTSM73DAG4SettledContactInput ContactInput;
	ContactInput.Nodes = State.SettledNodes;
	ContactInput.BaselineAllowedContacts =
		Data.SupportEdges;
	for (const FABTSM73DAGPhysicalSupportMapping& Mapping
		: Data.DAGPhysicalSupportMappings)
	{
		for (const int32 ColumnNodeId : Mapping.ColumnNodeIds)
		{
			for (const TPair<int32, int32>& Pair : {
				TPair<int32, int32>(
					Mapping.SupportPlateNodeId,
					ColumnNodeId),
				TPair<int32, int32>(
					ColumnNodeId,
					Mapping.LoadPlateNodeId)})
			{
				FABTSM73SupportEdge Required;
				Required.LowerNodeId = Pair.Key;
				Required.UpperNodeId = Pair.Value;
				if (const FABTSM73SupportEdge* Baseline =
					Data.SupportEdges.FindByPredicate(
						[&Pair](
							const FABTSM73SupportEdge& Edge)
						{
							return Edge.LowerNodeId == Pair.Key
								&& Edge.UpperNodeId == Pair.Value;
						}))
				{
					Required.ContactAreaCM2 =
						Baseline->ContactAreaCM2;
				}
				ContactInput.RequiredContacts.Add(Required);
			}
		}
	}
	ContactInput.BaselineGroundNodeIds =
		Data.GroundNodeIds;
	ContactInput.WeakNodeIds =
		Data.DAGFailurePatternResult.WeakNodeIds;
	ContactInput.RemainingSupportNodeIds =
		Data.DAGFailurePatternResult.RemainingSupportNodeIds;
	ContactInput.ExpectedAffectedNodeIds =
		Data.DAGFailurePlayabilityResult.AffectedNodeIds;
	ContactInput.ExpectedAffectedMainBodyNodeIds =
		Data.DAGFailurePatternResult.AffectedMainBodyNodeIds;
	ContactInput.LoadPlateNodeId =
		Data.DAGFailurePatternResult.LoadPlateNodeId;
	ContactInput.RealizedPatternHash =
		Data.DAGFailurePatternResult.RealizedPatternHash;
	ContactInput.Pattern =
		Data.DAGFailurePatternResult.Pattern;
	ContactInput.ExpectedMotion =
		Data.DAGFailurePatternResult.ExpectedMotion;
	ContactInput.ExpectedFailureDirectionLocal =
		Data.DAGFailurePatternResult.ExpectedFailureDirectionLocal;
	ContactInput.MinInitialSupportMarginCM =
		DifficultySettings.MinInitialSupportMarginCM;
	ContactInput.MinPostFailureTipMarginCM =
		DifficultySettings.MinTipMarginCM;
	ContactInput.MaxReseatRisk =
		DifficultySettings.MaxReseatRisk;

	FABTSM73DAG4SettledContactResult ContactResult;
	FString Error;
	FABTSM73DAG4SettledContactValidator ContactValidator;
	if (!ContactValidator.RebuildAndValidate(
		DAG4ValidationSettings,
		ContactInput,
		ContactResult,
		Error))
	{
		return Reject(
			Error.IsEmpty()
				? FString(TEXT("DAG4SettledContactRejected"))
				: Error);
	}
	State.SettledContacts = ContactResult.Contacts;
	LastDAG4ValidationResult.bSettledContactAccepted = true;
	LastDAG4ValidationResult.SettledNodeCount =
		State.SettledNodes.Num();
	LastDAG4ValidationResult.SettledContactCount =
		ContactResult.Contacts.Num();
	LastDAG4ValidationResult.MissingRequiredContactCount =
		ContactResult.MissingRequiredLowerNodeIds.Num();
	LastDAG4ValidationResult.NewContactCount =
		ContactResult.NewContactLowerNodeIds.Num();
	LastDAG4ValidationResult.FrontierBypassCount =
		ContactResult.FrontierBypassNodeIds.Num();
	LastDAG4ValidationResult.BaselineContactHash =
		static_cast<int64>(ContactResult.BaselineContactHash);
	LastDAG4ValidationResult.SettledContactHash =
		static_cast<int64>(ContactResult.SettledContactHash);
	LastDAG4ValidationResult.SettledInitialSupportMarginCM =
		ContactResult.InitialSupportMarginCM;
	LastDAG4ValidationResult.SettledPostFailureTipMarginCM =
		ContactResult.PostFailureTipMarginCM;
	LastDAG4ValidationResult.SettledReseatRisk =
		ContactResult.ReseatRisk;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.3-DAG4][Settled] Actor=%s Nodes=%d Required=%d Missing=%d New=%d Bypass=%d Ground=%d BeforeHash=%u AfterHash=%u InitialMargin=%.2f TipMargin=%.2f Reseat=%.3f"),
		*GetName(),
		State.SettledNodes.Num(),
		ContactInput.RequiredContacts.Num(),
		ContactResult.MissingRequiredLowerNodeIds.Num(),
		ContactResult.NewContactLowerNodeIds.Num(),
		ContactResult.FrontierBypassNodeIds.Num(),
		ContactResult.GroundNodeIds.Num(),
		ContactResult.BaselineContactHash,
		ContactResult.SettledContactHash,
		ContactResult.InitialSupportMarginCM,
		ContactResult.PostFailureTipMarginCM,
		ContactResult.ReseatRisk);

	FABTSM73DAG4TrialPlanningInput PlanningInput;
	PlanningInput.Nodes = State.SettledNodes;
	PlanningInput.Contacts = State.SettledContacts;
	PlanningInput.GroundNodeIds =
		ContactResult.GroundNodeIds;
	PlanningInput.WeakNodeIds =
		Data.DAGFailurePatternResult.WeakNodeIds;
	PlanningInput.RemainingSupportNodeIds =
		Data.DAGFailurePatternResult.RemainingSupportNodeIds;
	PlanningInput.ExpectedAffectedMainBodyNodeIds =
		Data.DAGFailurePatternResult.AffectedMainBodyNodeIds;
	PlanningInput.AttackDirectionLocal =
		Data.DAGFailurePlayabilityResult
			.AcceptedAttackDirectionLocal;
	PlanningInput.ProjectileRadiusCM =
		DAGFailurePlayabilitySettings.ProjectileRadiusCM;
	PlanningInput.AttackApproachDistanceCM =
		DAGFailurePlayabilitySettings
			.AttackApproachDistanceCM;
	FABTSM73DAG4TrialPlanner TrialPlanner;
	if (!TrialPlanner.BuildPlans(
		DAG4ValidationSettings,
		PlanningInput,
		State.Plans,
		Error))
	{
		return Reject(
			Error.IsEmpty()
				? FString(TEXT("DAG4TrialPlanningRejected"))
				: Error);
	}

	// Every building uses the same world-space translation so simultaneous
	// islands preserve the exact pairwise separation of their formal actors.
	// A name-hash slot would be probabilistic and would distort those relative
	// positions on a spherical world.
	State.ShadowOffset = FVector(250000.0f, -250000.0f, 250000.0f);
	State.CurrentTrialIndex = 0;
	State.TotalValidationElapsed = 0.0f;
	State.bCleanupBarrier = false;
	bDAG4ValidationRunning = true;
	IdleValidationState = EABTSM73IdleValidationState::Running;
	SetActorTickEnabled(true);

	if (!SpawnDAG4ShadowTrial(Error))
	{
		return Reject(
			Error.IsEmpty()
				? FString(TEXT("DAG4ShadowTrialSpawnFailed"))
				: Error);
	}
	return true;
}

void AABTSM73StableBuildingActor::TickDAG4Validation(
	const float DeltaSeconds)
{
	if (!bDAG4ValidationRunning
		|| !DAG4RuntimeState)
	{
		return;
	}
	FABTSM73DAG4RuntimeState& State = *DAG4RuntimeState;
	auto Reject = [this](const FString& Reason)
	{
		LastDAG4ValidationResult.bAccepted = false;
		LastDAG4ValidationResult.RejectReason = Reason;
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7.3-DAG4][Reject] Actor=%s Stage=Rollout Reason=%s"),
			*GetName(),
			*Reason);
		RejectRuntimeStructure(Reason);
	};
	if (LastDAG4ValidationResult.RejectReason
		== TEXT("DAG4ContactEventBudgetExceeded"))
	{
		Reject(LastDAG4ValidationResult.RejectReason);
		return;
	}
	const float SafeDelta = FMath::Max(0.0f, DeltaSeconds);
	State.TotalValidationElapsed += SafeDelta;
	if (State.TotalValidationElapsed
		> DAG4ValidationSettings.MaxTotalValidationSeconds)
	{
		Reject(TEXT("DAG4TotalValidationTimeBudgetExceeded"));
		return;
	}
	if (State.bCleanupBarrier)
	{
		State.bCleanupBarrier = false;
		++State.CurrentTrialIndex;
		if (State.CurrentTrialIndex >= State.Plans.Num())
		{
			FString Error;
			FABTSM73DAG4ResponseEvaluator Evaluator;
			if (!Evaluator.CertifyComparison(
				DAG4ValidationSettings,
				State.BaselineData.DAGFailurePatternResult
					.ExpectedMotion,
				LastDAG4ValidationResult.Trials,
				LastDAG4ValidationResult,
				Error))
			{
				Reject(
					Error.IsEmpty()
						? FString(
							TEXT("DAG4ComparisonRejected"))
						: Error);
				return;
			}
			LastDAG4ValidationResult.TotalValidationSeconds =
				State.TotalValidationElapsed;
			CompleteAcceptedIdleValidation();
			return;
		}
		FString Error;
		if (!SpawnDAG4ShadowTrial(Error))
		{
			CleanupDAG4ShadowTrial();
			Reject(
				Error.IsEmpty()
					? FString(TEXT("DAG4ShadowTrialSpawnFailed"))
					: Error);
		}
		return;
	}

	if (!State.Plans.IsValidIndex(State.CurrentTrialIndex))
	{
		Reject(TEXT("DAG4TrialIndexInvalid"));
		return;
	}
	State.CurrentTrialElapsed += SafeDelta;
	++State.CurrentTrialTicks;
	if (State.CurrentTrialTicks
		> DAG4ValidationSettings.MaxTrialTickCount)
	{
		CleanupDAG4ShadowTrial();
		Reject(TEXT("DAG4RolloutTickBudgetExceeded"));
		return;
	}

	const FVector UpWorld = bRuntimePlanar
		? RuntimeGravityReference.GetSafeNormal()
		: (State.GroundContext.AnchorTransform.GetLocation()
			- RuntimeGravityReference).GetSafeNormal();
	const FVector ExpectedDirectionLocal =
		State.BaselineData.DAGFailurePatternResult
			.ExpectedFailureDirectionLocal.GetSafeNormal();
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Weak
		: State.ShadowModules)
	{
		AABTSM7BuildingModule* Module = Weak.Get();
		if (!IsValid(Module) || Module->IsActorBeingDestroyed())
		{
			CleanupDAG4ShadowTrial();
			Reject(TEXT("DAG4ShadowModuleLost"));
			return;
		}
		const int32* NodeId =
			State.ShadowNodeIds.Find(Module);
		if (NodeId == nullptr) continue;
		const FTransform* Initial =
			State.ShadowInitialTransforms.Find(*NodeId);
		FABTSM73DAG4NodeOutcome* Outcome =
			State.CurrentOutcomes.FindByPredicate(
				[NodeId](
					const FABTSM73DAG4NodeOutcome& Candidate)
				{
					return Candidate.NodeId == *NodeId;
				});
		if (Initial == nullptr || Outcome == nullptr) continue;
		const FVector WorldDelta =
			Module->GetActorLocation()
			- Initial->GetLocation();
		const FVector LocalDelta =
			State.GroundContext.AnchorTransform
				.InverseTransformVectorNoScale(WorldDelta);
		const float RotationDegrees =
			FMath::RadiansToDegrees(
				Initial->GetRotation().AngularDistance(
					Module->GetActorQuat()));
		Outcome->FinalDisplacementLocal = LocalDelta;
		Outcome->FinalRotationDegrees = RotationDegrees;
		Outcome->MaxDisplacementCM = FMath::Max(
			Outcome->MaxDisplacementCM,
			LocalDelta.Size());
		Outcome->MaxRotationDegrees = FMath::Max(
			Outcome->MaxRotationDegrees,
			RotationDegrees);
		Outcome->MaxDropDistanceCM = FMath::Max(
			Outcome->MaxDropDistanceCM,
			FMath::Max(
				0.0f,
				-FVector::DotProduct(WorldDelta, UpWorld)));
		Outcome->MaxExpectedDirectionSlideCM = FMath::Max(
			Outcome->MaxExpectedDirectionSlideCM,
			FMath::Max(
				0.0f,
				FVector::DotProduct(
					LocalDelta,
					ExpectedDirectionLocal)));
	}

	if (State.CurrentTrialElapsed
		+ KINDA_SMALL_NUMBER
		< DAG4ValidationSettings.TrialDurationSeconds)
	{
		return;
	}
	const FABTSM73DAG4TrialPlan& Plan =
		State.Plans[State.CurrentTrialIndex];
	FABTSM73DAG4TrialEvaluationInput EvaluationInput;
	EvaluationInput.Nodes = State.SettledNodes;
	EvaluationInput.SettledContacts =
		State.SettledContacts;
	EvaluationInput.Plan = Plan;
	EvaluationInput.Outcomes = State.CurrentOutcomes;
	EvaluationInput.ExpectedMotion =
		State.BaselineData.DAGFailurePatternResult
			.ExpectedMotion;
	EvaluationInput.ExpectedFailureDirectionLocal =
		State.BaselineData.DAGFailurePatternResult
			.ExpectedFailureDirectionLocal;
	EvaluationInput.DurationSeconds =
		State.CurrentTrialElapsed;
	EvaluationInput.TickCount = State.CurrentTrialTicks;
	for (const uint64 Pair : State.CurrentSecondaryPairs)
	{
		int32 NodeA = INDEX_NONE;
		int32 NodeB = INDEX_NONE;
		DecodeDAG4PairKey(Pair, NodeA, NodeB);
		EvaluationInput.SecondaryContactNodePairs.Add(NodeA);
		EvaluationInput.SecondaryContactNodePairs.Add(NodeB);
	}
	FABTSM73DAG4TrialMetrics Metrics;
	FString Error;
	FABTSM73DAG4ResponseEvaluator Evaluator;
	if (!Evaluator.EvaluateTrial(
		DAG4ValidationSettings,
		EvaluationInput,
		Metrics,
		Error))
	{
		CleanupDAG4ShadowTrial();
		Reject(
			Error.IsEmpty()
				? FString(TEXT("DAG4TrialReductionFailed"))
				: Error);
		return;
	}
	LastDAG4ValidationResult.Trials.Add(Metrics);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.3-DAG4][Trial] Actor=%s Type=%s Probe=%d Removed=%s Mass=%.3f Realized=%.3f Move=%.2f Rotation=%.2f Drop=%.2f Slide=%.2f Depth=%d Direction=%.3f Secondary=%d Score=%.3f"),
		*GetName(),
		Metrics.Kind == EABTSM73DAG4TrialKind::WeakPoint
			? TEXT("Weak")
			: TEXT("Ordinary"),
		Metrics.ProbeIndex,
		*JoinDAG4NodeIds(Metrics.RemovedNodeIds),
		Metrics.AffectedMainBodyMassRatio,
		Metrics.PredictedAffectedRealizationRatio,
		Metrics.MaxDisplacementCM,
		Metrics.MaxRotationDegrees,
		Metrics.MaxDropDistanceCM,
		Metrics.MaxExpectedDirectionSlideCM,
		Metrics.PropagationDepth,
		Metrics.DirectionAlignment,
		Metrics.SecondaryContactCount,
		Metrics.ResponseScore);
	CleanupDAG4ShadowTrial();
	State.bCleanupBarrier = true;
}

void AABTSM73StableBuildingActor::HandleDAG4ModuleHit(
	UPrimitiveComponent* HitComponent,
	AActor*,
	UPrimitiveComponent* OtherComponent,
	FVector,
	const FHitResult& Hit)
{
	if (!bDAG4ValidationRunning
		|| !DAG4RuntimeState
		|| HitComponent == nullptr)
	{
		return;
	}
	FABTSM73DAG4RuntimeState& State = *DAG4RuntimeState;
	if (State.CurrentTrialElapsed
		< DAG4ValidationSettings.TrialWarmupSeconds)
	{
		return;
	}
	AABTSM7BuildingModule* ModuleA =
		Cast<AABTSM7BuildingModule>(
			HitComponent->GetOwner());
	const int32* NodeA =
		ModuleA != nullptr
			? State.ShadowNodeIds.Find(ModuleA)
			: nullptr;
	if (NodeA == nullptr) return;

	AABTSM7BuildingModule* ModuleB =
		OtherComponent != nullptr
			? Cast<AABTSM7BuildingModule>(
				OtherComponent->GetOwner())
			: nullptr;
	const int32* FoundNodeB =
		ModuleB != nullptr
			? State.ShadowNodeIds.Find(ModuleB)
			: nullptr;
	const int32 NodeB =
		FoundNodeB != nullptr ? *FoundNodeB : INDEX_NONE;
	if (NodeB == *NodeA) return;
	if (NodeB != INDEX_NONE)
	{
		const bool bBaselineContact =
			State.SettledContacts.ContainsByPredicate(
				[NodeA, NodeB](
					const FABTSM73DAG4SettledContact& Contact)
				{
					return (Contact.LowerNodeId == *NodeA
							&& Contact.UpperNodeId == NodeB)
						|| (Contact.LowerNodeId == NodeB
							&& Contact.UpperNodeId == *NodeA);
				});
		if (bBaselineContact) return;
	}
	const FVector VelocityA =
		HitComponent->IsSimulatingPhysics()
			? HitComponent->GetPhysicsLinearVelocityAtPoint(
				Hit.ImpactPoint)
			: FVector::ZeroVector;
	const FVector VelocityB =
		OtherComponent != nullptr
			&& OtherComponent->IsSimulatingPhysics()
			? OtherComponent->GetPhysicsLinearVelocityAtPoint(
				Hit.ImpactPoint)
			: FVector::ZeroVector;
	const float RelativeNormalSpeed =
		FMath::Abs(FVector::DotProduct(
			VelocityA - VelocityB,
			Hit.ImpactNormal.GetSafeNormal()));
	if (RelativeNormalSpeed
		< DAG4ValidationSettings
			.MinSecondaryContactSpeedCMPerSec)
	{
		return;
	}
	const uint64 Pair = DAG4PairKey(*NodeA, NodeB);
	const float* LastSeconds =
		State.LastSecondaryPairSeconds.Find(Pair);
	if (LastSeconds != nullptr
		&& State.CurrentTrialElapsed - *LastSeconds
			< DAG4ValidationSettings
				.SecondaryContactDebounceSeconds)
	{
		return;
	}
	State.LastSecondaryPairSeconds.Add(
		Pair,
		State.CurrentTrialElapsed);
	if (State.CurrentSecondaryPairs.Num()
		>= DAG4ValidationSettings.MaxContactEventCount
		&& !State.CurrentSecondaryPairs.Contains(Pair))
	{
		LastDAG4ValidationResult.RejectReason =
			TEXT("DAG4ContactEventBudgetExceeded");
		return;
	}
	State.CurrentSecondaryPairs.Add(Pair);
}

void AABTSM73StableBuildingActor::CancelDAG4Validation()
{
	bDAG4ValidationRunning = false;
	if (!DAG4RuntimeState) return;
	CleanupDAG4ShadowTrial();
	DAG4RuntimeState.Reset();
}

void AABTSM73StableBuildingActor::CompleteAcceptedIdleValidation()
{
	const float TotalValidationSeconds =
		LastDAG4ValidationResult.TotalValidationSeconds;
	LastDAG4ValidationResult.bChaosComparisonAccepted = true;
	LastDAG4ValidationResult.bAccepted = true;
	LastDAG4ValidationResult.RejectReason.Reset();
	bDAG4ValidationRunning = false;
	for (const TWeakObjectPtr<AABTSM7BuildingModule>& Weak
		: RuntimeModules)
	{
		if (AABTSM7BuildingModule* Module = Weak.Get())
		{
			Module->Freeze();
			Module->GetMeshComponent()->SetVisibility(true, true);
		}
	}
	GenerationSummary.bAccepted = true;
	GenerationSummary.RejectReason.Reset();
	GenerationSummary.bDAG4ValidationEnabled = true;
	GenerationSummary.bDAG4SettledContactAccepted =
		LastDAG4ValidationResult.bSettledContactAccepted;
	GenerationSummary.bDAG4ChaosComparisonAccepted = true;
	GenerationSummary.DAG4ValidationHash =
		LastDAG4ValidationResult.ValidationHash;
	GenerationSummary.DAG4WeakResponseScore =
		LastDAG4ValidationResult.WeakResponseScore;
	GenerationSummary.DAG4MaxOrdinaryResponseScore =
		LastDAG4ValidationResult.MaxOrdinaryResponseScore;
	GenerationSummary.DAG4WeakResponseAdvantage =
		LastDAG4ValidationResult.WeakResponseAdvantage;
	IdleValidationState =
		EABTSM73IdleValidationState::Accepted;
	SetActorTickEnabled(false);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.3-DAG4][Complete] Actor=%s Trials=%d WeakScore=%.3f OrdinaryMax=%.3f OrdinaryMass=%.3f Advantage=%.3f Hash=%lld Seconds=%.2f Accepted=1"),
		*GetName(),
		LastDAG4ValidationResult.Trials.Num(),
		LastDAG4ValidationResult.WeakResponseScore,
		LastDAG4ValidationResult.MaxOrdinaryResponseScore,
		LastDAG4ValidationResult.MaxOrdinaryAffectedMassRatio,
		LastDAG4ValidationResult.WeakResponseAdvantage,
		LastDAG4ValidationResult.ValidationHash,
		TotalValidationSeconds);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7.3-A][IdleValidation] Actor=%s DAG4Complete=1 Accepted=1"),
		*GetName());
	CancelDAG4Validation();
}
