// Copyright Epic Games, Inc. All Rights Reserved.

#include "Slingshot/ABTSM6SlingshotSystem.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM73JuryDemoFixedSixRegistration.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Contracts/ABTSWorldGenerationContracts.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "PhysicsEngine/BodyInstance.h"
#include "Slingshot/ABTSM6DestructibleProxy.h"
#include "Terrain/ABTSM3Planet.h"
#include "TestStage/ABTSM71TestStageActors.h"

namespace
{
struct FABTSM6StartupHISMScanData
{
	UHierarchicalInstancedStaticMeshComponent* Component = nullptr;
	TArray<FTransform> WorldTransforms;
	TSet<int32> SelectedCandidateIndices;
};

struct FABTSM73StartupValidationSummary
{
	int32 Pending = 0;
	int32 Running = 0;
	int32 Accepted = 0;
	int32 Rejected = 0;
	int32 NotRequired = 0;
	bool bContractActive = false;
	bool bContractSealed = true;
	bool bSetupRejected = false;
	int32 ExpectedRequired = 0;
	int32 RegisteredRequired = 0;

	EABTSM6BuildingValidationGate GetGate() const
	{
		return FABTSM6BuildingValidationGate::Classify(
			Pending,
			Running,
			Rejected,
			bContractActive,
			bContractSealed,
			bSetupRejected,
			ExpectedRequired,
			RegisteredRequired,
			Accepted,
			NotRequired);
	}
};

struct FABTSM6FixedSixStaticJointSummary
{
	bool bPresent = false;
	bool bAccepted = false;
	int32 RegisteredBuildingCount = 0;
	int32 StaticModuleCount = 0;
	uint64 RegistrationResultHash = 0;
	FString RejectReason;
};

FABTSM6FixedSixStaticJointSummary ValidateFixedSixStaticJointGate(
	const int32 ExpectedRequired,
	const TArray<TWeakObjectPtr<AABTSM73StableBuildingActor>>& RequiredBuildings)
{
	static const TCHAR* const ExpectedManifestEntryIds[] = {
		TEXT("E2DropTrigger"),
		TEXT("E3SlideRelease"),
		TEXT("E4TipOver"),
		TEXT("E5SeamRelease"),
		TEXT("E1ColumnBreak"),
		TEXT("E6TipOver")
	};
	static constexpr uint64 FrozenRegistrationResultHash =
		FABTSM73JuryDemoFixedSixRegistration::FrozenV3RegistrationResultHash;
	static constexpr int32 FrozenStaticModuleCount =
		FABTSM73JuryDemoFixedSixRegistration::FrozenV3StaticModuleCount;

	FABTSM6FixedSixStaticJointSummary Summary;
	for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& Required
		: RequiredBuildings)
	{
		const AABTSM73StableBuildingActor* Actor = Required.Get();
		if (Actor != nullptr
			&& !Actor->GetJuryDemoFixedSixManifestEntryId().IsNone())
		{
			Summary.bPresent = true;
			break;
		}
	}
	if (!Summary.bPresent)
	{
		return Summary;
	}

	if (ExpectedRequired
			!= FABTSJuryDemoFixedSixContract::ExpectedSiteCount
		|| RequiredBuildings.Num()
			!= FABTSJuryDemoFixedSixContract::ExpectedSiteCount)
	{
		Summary.RejectReason = TEXT("Count");
		return Summary;
	}

	for (int32 Index = 0; Index < RequiredBuildings.Num(); ++Index)
	{
		const AABTSM73StableBuildingActor* Actor =
			RequiredBuildings[Index].Get();
		if (Actor == nullptr)
		{
			Summary.RejectReason = TEXT("ActorMissing");
			return Summary;
		}
		const uint64 ResultHash =
			Actor->GetJuryDemoFixedSixRegistrationResultHash();
		if (!Actor->IsJuryDemoFixedSixStaticRegistrationAccepted()
			|| Actor->GetJuryDemoFixedSixManifestEntryId()
				!= FName(ExpectedManifestEntryIds[Index])
			|| Actor->GetJuryDemoFixedSixEncounterIndex() != Index
			|| ResultHash == 0
			|| (Summary.RegistrationResultHash != 0
				&& Summary.RegistrationResultHash != ResultHash))
		{
			Summary.RejectReason = FString::Printf(
				TEXT("Identity:%d"), Index);
			return Summary;
		}
		Summary.RegistrationResultHash = ResultHash;
		Summary.StaticModuleCount +=
			Actor->GetJuryDemoFixedSixStaticModuleCount();
		++Summary.RegisteredBuildingCount;
	}

	if (Summary.RegistrationResultHash != FrozenRegistrationResultHash)
	{
		Summary.RejectReason = TEXT("ResultHash");
		return Summary;
	}
	if (Summary.StaticModuleCount != FrozenStaticModuleCount)
	{
		Summary.RejectReason = TEXT("ModuleCount");
		return Summary;
	}
	Summary.bAccepted = true;
	return Summary;
}

FABTSM73StartupValidationSummary GetBuildingValidationSummary(
	UWorld& World,
	const bool bContractActive,
	const bool bContractSealed,
	const bool bSetupRejected,
	const int32 ExpectedRequired,
	const TArray<TWeakObjectPtr<AABTSM73StableBuildingActor>>& RequiredBuildings)
{
	FABTSM73StartupValidationSummary Summary;
	Summary.bContractActive = bContractActive;
	Summary.bContractSealed = bContractSealed;
	Summary.bSetupRejected = bSetupRejected;
	Summary.ExpectedRequired = ExpectedRequired;
	if (bContractActive)
	{
		// Once M7 opens the production contract, only its explicitly registered
		// required actors define readiness. Unrelated M7.3 test actors must not
		// block the world, and every registered actor must reach Accepted.
		for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& Required : RequiredBuildings)
		{
			if (!Required.IsValid()) continue;
			++Summary.RegisteredRequired;
			switch (Required->GetIdleValidationState())
			{
			case EABTSM73IdleValidationState::Pending: ++Summary.Pending; break;
			case EABTSM73IdleValidationState::Running: ++Summary.Running; break;
			case EABTSM73IdleValidationState::Accepted: ++Summary.Accepted; break;
			case EABTSM73IdleValidationState::Rejected: ++Summary.Rejected; break;
			case EABTSM73IdleValidationState::NotRequired: ++Summary.NotRequired; break;
			default: break;
			}
		}
		return Summary;
	}

	// M6 and historical isolated tests do not open an M7 contract. Preserve
	// their compatibility by observing any M7.3 actor that exists in the world.
	for (TActorIterator<AABTSM73StableBuildingActor> It(&World); It; ++It)
	{
		switch (It->GetIdleValidationState())
		{
		case EABTSM73IdleValidationState::Pending: ++Summary.Pending; break;
		case EABTSM73IdleValidationState::Running: ++Summary.Running; break;
		case EABTSM73IdleValidationState::Accepted: ++Summary.Accepted; break;
		case EABTSM73IdleValidationState::Rejected: ++Summary.Rejected; break;
		case EABTSM73IdleValidationState::NotRequired: ++Summary.NotRequired; break;
		default: break;
		}
	}
	return Summary;
}

bool ABTSStartupBodiesOverlap(
	UHierarchicalInstancedStaticMeshComponent& ComponentA,
	const int32 InstanceA,
	const FTransform& TransformA,
	UHierarchicalInstancedStaticMeshComponent& ComponentB,
	const int32 InstanceB)
{
	FBodyInstance* BodyA = ComponentA.GetBodyInstance(NAME_None, false, InstanceA);
	FBodyInstance* BodyB = ComponentB.GetBodyInstance(NAME_None, false, InstanceB);
	if (BodyA == nullptr || BodyB == nullptr || !BodyA->IsValidBodyInstance() || !BodyB->IsValidBodyInstance())
	{
		return false;
	}
	return BodyA->OverlapTestForBody(
		TransformA.GetLocation(), TransformA.GetRotation().GetNormalized(), BodyB, false);
}
}

void AABTSM6SlingshotSystem::BeginRequiredBuildingContract(const int32 InExpectedRequiredBuildingCount)
{
	bRequiredBuildingContractActive = true;
	bRequiredBuildingContractSealed = false;
	bRequiredBuildingSetupRejected = false;
	ExpectedRequiredBuildingCount = FMath::Max(0, InExpectedRequiredBuildingCount);
	RequiredBuildingActors.Reset();
	bFixedSixStaticJointGateAccepted = false;
	FixedSixStaticJointRegistrationResultHash = 0;
	FixedSixStaticJointRegisteredBuildingCount = 0;
	FixedSixStaticJointModuleCount = 0;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][StartupPhysics] BuildingContractBegin Expected=%d"),
		ExpectedRequiredBuildingCount);
}

void AABTSM6SlingshotSystem::RegisterRequiredBuilding(AABTSM73StableBuildingActor& Building)
{
	if (!bRequiredBuildingContractActive || bRequiredBuildingContractSealed)
	{
		bRequiredBuildingSetupRejected = true;
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][StartupPhysics] BuildingContractRegisterRejected Actor=%s Active=%d Sealed=%d"),
			*Building.GetName(),
			bRequiredBuildingContractActive ? 1 : 0,
			bRequiredBuildingContractSealed ? 1 : 0);
		return;
	}
	RequiredBuildingActors.AddUnique(&Building);
}

void AABTSM6SlingshotSystem::SealRequiredBuildingContract(const bool bInSetupRejected)
{
	if (!bRequiredBuildingContractActive)
	{
		bRequiredBuildingSetupRejected = true;
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][StartupPhysics] BuildingContractSealRejected Reason=NotActive"));
		return;
	}
	int32 RegisteredRequired = 0;
	for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& Required : RequiredBuildingActors)
	{
		if (Required.IsValid()) ++RegisteredRequired;
	}
	const FABTSM6FixedSixStaticJointSummary FixedSixJoint =
		ValidateFixedSixStaticJointGate(
			ExpectedRequiredBuildingCount, RequiredBuildingActors);
	bFixedSixStaticJointGateAccepted =
		FixedSixJoint.bPresent && FixedSixJoint.bAccepted;
	FixedSixStaticJointRegistrationResultHash =
		FixedSixJoint.bAccepted ? FixedSixJoint.RegistrationResultHash : 0;
	FixedSixStaticJointRegisteredBuildingCount =
		FixedSixJoint.bAccepted ? FixedSixJoint.RegisteredBuildingCount : 0;
	FixedSixStaticJointModuleCount =
		FixedSixJoint.bAccepted ? FixedSixJoint.StaticModuleCount : 0;
	bRequiredBuildingContractSealed = true;
	bRequiredBuildingSetupRejected = bInSetupRejected
		|| RegisteredRequired != ExpectedRequiredBuildingCount
		|| (FixedSixJoint.bPresent && !FixedSixJoint.bAccepted);
	if (FixedSixJoint.bPresent)
	{
		if (FixedSixJoint.bAccepted)
		{
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][StartupPhysics] FixedSixStaticJointGate")
				TEXT(" Expected=6 Registered=%d Accepted=6 Modules=%d")
				TEXT(" Layout=%llu ResultHash=%llu")
				TEXT(" Authority=StaticRegistration Chaos=NotEvaluated Gate=Accepted"),
				FixedSixJoint.RegisteredBuildingCount,
				FixedSixJoint.StaticModuleCount,
				FABTSJuryDemoFixedSixContract::FrozenV3LayoutHash,
				FixedSixJoint.RegistrationResultHash);
		}
		else
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][StartupPhysics] FixedSixStaticJointGateRejected")
				TEXT(" Reason=%s Expected=6 Registered=%d Modules=%d")
				TEXT(" ResultHash=%llu Gate=Rejected"),
				*FixedSixJoint.RejectReason,
				FixedSixJoint.RegisteredBuildingCount,
				FixedSixJoint.StaticModuleCount,
				FixedSixJoint.RegistrationResultHash);
		}
	}
	if (bRequiredBuildingSetupRejected)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][StartupPhysics] BuildingContractSealed Expected=%d Registered=%d SetupRejected=1"),
			ExpectedRequiredBuildingCount,
			RegisteredRequired);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][StartupPhysics] BuildingContractSealed Expected=%d Registered=%d SetupRejected=0"),
			ExpectedRequiredBuildingCount,
			RegisteredRequired);
	}
}

bool AABTSM6SlingshotSystem::CopyFixedSixStaticJointGateResult(
	uint64& OutRegistrationResultHash,
	int32& OutRegisteredBuildingCount,
	int32& OutStaticModuleCount) const
{
	OutRegistrationResultHash = 0;
	OutRegisteredBuildingCount = 0;
	OutStaticModuleCount = 0;
	if (!bRequiredBuildingContractActive
		|| !bRequiredBuildingContractSealed
		|| bRequiredBuildingSetupRejected
		|| !bFixedSixStaticJointGateAccepted)
	{
		return false;
	}
	OutRegistrationResultHash =
		FixedSixStaticJointRegistrationResultHash;
	OutRegisteredBuildingCount =
		FixedSixStaticJointRegisteredBuildingCount;
	OutStaticModuleCount = FixedSixStaticJointModuleCount;
	return true;
}

bool AABTSM6SlingshotSystem::AreRuntimeBuildingsReadyForLaunch() const
{
	if (GetWorld() == nullptr) return false;
	const FABTSM73StartupValidationSummary Validation = GetBuildingValidationSummary(
		*GetWorld(),
		bRequiredBuildingContractActive,
		bRequiredBuildingContractSealed,
		bRequiredBuildingSetupRejected,
		ExpectedRequiredBuildingCount,
		RequiredBuildingActors);
	if (Validation.GetGate() == EABTSM6BuildingValidationGate::Rejected)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][StartupPhysics] Launch blocked: generated building validation failed. Pending=%d Running=%d Accepted=%d Rejected=%d NotRequired=%d Contract=%d Sealed=%d SetupRejected=%d Expected=%d Registered=%d"),
			Validation.Pending, Validation.Running, Validation.Accepted,
			Validation.Rejected, Validation.NotRequired,
			Validation.bContractActive ? 1 : 0,
			Validation.bContractSealed ? 1 : 0,
			Validation.bSetupRejected ? 1 : 0,
			Validation.ExpectedRequired,
			Validation.RegisteredRequired);
		return false;
	}
	if (Validation.GetGate() == EABTSM6BuildingValidationGate::Waiting)
	{
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][StartupPhysics] Launch blocked: generated building validation is still in progress. Pending=%d Running=%d Accepted=%d Contract=%d Sealed=%d Expected=%d Registered=%d"),
			Validation.Pending, Validation.Running, Validation.Accepted,
			Validation.bContractActive ? 1 : 0,
			Validation.bContractSealed ? 1 : 0,
			Validation.ExpectedRequired,
			Validation.RegisteredRequired);
		return false;
	}
	return true;
}

void AABTSM6SlingshotSystem::UpdateStartupPhysicsWarmup(const float DeltaSeconds)
{
	if (GetWorld() == nullptr || bStartupPhysicsWarmupComplete) return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (!bStartupPhysicsWarmupStarted)
	{
		if (!ResolveDependencies() || Now < StartupPhysicsWarmupEligibleTimeSeconds)
		{
			if (!bStartupPhysicsWarmupWaitingLogged && Now >= StartupPhysicsWarmupEligibleTimeSeconds)
			{
				bStartupPhysicsWarmupWaitingLogged = true;
				UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][StartupPhysics] Waiting for party/planet dependencies before bounded Chaos warmup."));
			}
			return;
		}
		BeginStartupPhysicsWarmup();
		return;
	}

	TArray<UPrimitiveComponent*> Bodies;
	CollectDynamicPhysicsBodies(Bodies);
	FABTSM6PhysicsActivitySummary Summary;
	const EABTSM6PhysicsSettleResult Result = StartupPhysicsSettleMonitor.Update(DeltaSeconds, Now, Bodies, Summary);
	const FABTSM73StartupValidationSummary BuildingValidation = GetBuildingValidationSummary(
		*GetWorld(),
		bRequiredBuildingContractActive,
		bRequiredBuildingContractSealed,
		bRequiredBuildingSetupRejected,
		ExpectedRequiredBuildingCount,
		RequiredBuildingActors);
	if (Now >= NextStartupWarmupDiagnosticTimeSeconds)
	{
		NextStartupWarmupDiagnosticTimeSeconds = Now + 1.0f;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][StartupPhysics] Phase=%s Batch=%d Bodies=%d Moving=%d Awake=%d MaxLinear=%.1f MaxAngular=%.1f Stable=%.2f Elapsed=%.2f Remaining=%d BuildingPending=%d BuildingRunning=%d BuildingAccepted=%d BuildingRejected=%d"),
			bStartupBuildingSettlementActive ? TEXT("Buildings") : TEXT("HISM"),
			StartupHISMWarmupBatchIndex, Summary.ActiveBodyCount, Summary.MovingBodyCount, Summary.AwakeBodyCount,
			Summary.MaximumLinearSpeedCMPerSec, Summary.MaximumAngularSpeedDegPerSec,
			Summary.StableElapsedSeconds, Summary.SettlementElapsedSeconds,
			FMath::Max(0, StartupHISMWarmupTotalCandidates - StartupHISMWarmupPromotedTotal),
			BuildingValidation.Pending, BuildingValidation.Running,
			BuildingValidation.Accepted, BuildingValidation.Rejected);
	}

	if (bStartupBuildingSettlementActive)
	{
		// M7.3 owns the hidden settle, historical displacement checks and final
		// Freeze for its modules. The coarser M6 monitor may observe "settled"
		// earlier, but must never manufacture a quiet window by freezing them.
		if (BuildingValidation.GetGate() == EABTSM6BuildingValidationGate::Waiting) return;
		if (BuildingValidation.GetGate() == EABTSM6BuildingValidationGate::Rejected)
		{
			bStartupBuildingSettlementActive = false;
			bStartupPhysicsWarmupFailed = true;
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][StartupPhysics] WorldReadyBlocked Reason=BuildingGateRejected Pending=%d Running=%d Accepted=%d Rejected=%d NotRequired=%d Contract=%d Sealed=%d SetupRejected=%d Expected=%d Registered=%d"),
				BuildingValidation.Pending, BuildingValidation.Running,
				BuildingValidation.Accepted, BuildingValidation.Rejected, BuildingValidation.NotRequired,
				BuildingValidation.bContractActive ? 1 : 0,
				BuildingValidation.bContractSealed ? 1 : 0,
				BuildingValidation.bSetupRejected ? 1 : 0,
				BuildingValidation.ExpectedRequired, BuildingValidation.RegisteredRequired);
			return;
		}
		const bool bNoActiveBuildingBodies = Bodies.IsEmpty();
		if (!bNoActiveBuildingBodies
			&& Result != EABTSM6PhysicsSettleResult::Settled
			&& Result != EABTSM6PhysicsSettleResult::TimedOut) return;
		if (Result == EABTSM6PhysicsSettleResult::TimedOut && !bNoActiveBuildingBodies)
		{
			++StartupHISMWarmupTimedOutBatches;
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][StartupPhysics] BuildingTimeout Limit=%.1fs Bodies=%d Moving=%d Awake=%d MaxLinear=%.1f MaxAngular=%.1f; forcing structures static."),
				StartupSettleDiagnosticPeriodSeconds, Summary.ActiveBodyCount, Summary.MovingBodyCount,
				Summary.AwakeBodyCount, Summary.MaximumLinearSpeedCMPerSec, Summary.MaximumAngularSpeedDegPerSec);
		}
		else if (bNoActiveBuildingBodies)
		{
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][StartupPhysics] BuildingsSettled NoDynamicBodies=1 Elapsed=%.2f"),
				Summary.SettlementElapsedSeconds);
		}
		// Any remaining body here is not owned by a live M7.3 validator (for
		// example the M7.1 material gallery), so the legacy global freeze remains
		// valid for that residual set.
		if (!bNoActiveBuildingBodies) FreezeDynamicProxies();
		bStartupBuildingSettlementActive = false;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][StartupPhysics] BuildingValidationTerminal Pending=%d Running=%d Accepted=%d Rejected=%d NotRequired=%d"),
			BuildingValidation.Pending, BuildingValidation.Running,
			BuildingValidation.Accepted, BuildingValidation.Rejected, BuildingValidation.NotRequired);
		if (HasPendingStartupHISMCandidates() && StartNextStartupHISMWarmupBatch() > 0) return;
		FinishStartupPhysicsWarmup(Summary);
		return;
	}

	const float RelaxationSeconds = FMath::Clamp(
		StartupHISMBatchRelaxationSeconds, 0.1f, FMath::Max(0.1f, StartupSettleDiagnosticPeriodSeconds));
	if (Result != EABTSM6PhysicsSettleResult::Settled
		&& Summary.SettlementElapsedSeconds < RelaxationSeconds) return;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][StartupPhysics] BatchRelaxed Batch=%d Duration=%.2f Bodies=%d MovingBeforeFreeze=%d"),
		StartupHISMWarmupBatchIndex, Summary.SettlementElapsedSeconds,
		Summary.ActiveBodyCount, Summary.MovingBodyCount);
	const int32 RestoredInstanceCount = RestoreStartupHISMProxies();
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][StartupPhysics] BatchRestored Batch=%d HISMInstances=%d DynamicProxies=%d"),
		StartupHISMWarmupBatchIndex, RestoredInstanceCount, DynamicProxies.Num());
	if (HasPendingStartupHISMCandidates() && StartNextStartupHISMWarmupBatch() > 0) return;
	FinishStartupPhysicsWarmup(Summary);
}

void AABTSM6SlingshotSystem::BeginStartupPhysicsWarmup()
{
	if (bStartupPhysicsWarmupStarted || GetWorld() == nullptr) return;
	bStartupPhysicsWarmupStarted = true;
	StartupHISMWarmupQueues.Reset();
	StartupProxySourceHISMs.Reset();
	StartupHISMWarmupTotalCandidates = 0;
	StartupHISMWarmupPromotedTotal = 0;
	StartupHISMWarmupBatchIndex = 0;
	StartupHISMWarmupTimedOutBatches = 0;
	bStartupBuildingSettlementActive = BuildingMaterialSystem.IsValid();
	const float Now = GetWorld()->GetTimeSeconds();
	NextStartupWarmupDiagnosticTimeSeconds = Now;

	if (BuildingMaterialSystem.IsValid())
	{
		BuildingMaterialSystem->BeginLaunchPhysics(
			bPlanarTestMode,
			bPlanarTestMode ? PlanarUp : Planet->GetPlanetCenterWorld(),
			LaunchObjectGravityAccelerationCMPerSec2);
		// This is a non-gameplay solve. Initial contacts cannot become damage.
		BuildingMaterialSystem->SetDynamicContactDamageGraceSeconds(1000000.0f);
	}

	TArray<UHierarchicalInstancedStaticMeshComponent*> StartupHISMs;
	if (bPlanarTestMode)
	{
		for (TActorIterator<AABTSM71PlaceableHISMActor> It(GetWorld()); It; ++It)
		{
			if (UHierarchicalInstancedStaticMeshComponent* HISM = It->GetHISM()) StartupHISMs.Add(HISM);
		}
	}
	else if (Planet.IsValid())
	{
		if (Planet->ForestHISM) StartupHISMs.Add(Planet->ForestHISM);
		if (Planet->RockHISM) StartupHISMs.Add(Planet->RockHISM);
	}

	int32 OverlapPairCount = 0;
	int32 FallbackPairCount = 0;
	const double ScanStartSeconds = FPlatformTime::Seconds();
	StartupHISMWarmupTotalCandidates = BuildStartupHISMOverlapQueues(
		StartupHISMs, OverlapPairCount, FallbackPairCount);
	const double ScanMilliseconds = (FPlatformTime::Seconds() - ScanStartSeconds) * 1000.0;
	int32 TotalHISMInstanceCount = 0;
	for (const UHierarchicalInstancedStaticMeshComponent* HISM : StartupHISMs)
	{
		TotalHISMInstanceCount += HISM ? HISM->GetInstanceCount() : 0;
	}
	int32 FirstBatchCount = 0;
	if (bStartupBuildingSettlementActive)
	{
		// Structural modules get the strict velocity-based settlement first. HISM
		// relaxation begins only after structures are frozen, keeping each phase bounded.
		StartupPhysicsSettleMonitor.BeginSettlement(Now);
	}
	else
	{
		FirstBatchCount = StartNextStartupHISMWarmupBatch();
		if (FirstBatchCount <= 0)
		{
			FABTSM6PhysicsActivitySummary EmptySummary;
			FinishStartupPhysicsWarmup(EmptySummary);
			return;
		}
	}

	TArray<UPrimitiveComponent*> Bodies;
	CollectDynamicPhysicsBodies(Bodies);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][StartupPhysics] Begin Planar=%d BuildingSystem=%d HISMInstances=%d OverlapPairs=%d FallbackPairs=%d Candidates=%d BatchLimit=%d Relaxation=%.2f FirstBatch=%d DynamicBodies=%d ScanMs=%.2f BuildingHardLimit=%.1f"),
		bPlanarTestMode ? 1 : 0, BuildingMaterialSystem.IsValid() ? 1 : 0,
		TotalHISMInstanceCount,
		OverlapPairCount, FallbackPairCount, StartupHISMWarmupTotalCandidates,
		FMath::Clamp(StartupHISMMaxSimultaneousBodies, 8, 512), StartupHISMBatchRelaxationSeconds,
		FirstBatchCount, Bodies.Num(),
		ScanMilliseconds, StartupSettleDiagnosticPeriodSeconds);
}

int32 AABTSM6SlingshotSystem::BuildStartupHISMOverlapQueues(
	const TArray<UHierarchicalInstancedStaticMeshComponent*>& HISMs,
	int32& OutOverlapPairCount,
	int32& OutFallbackPairCount)
{
	OutOverlapPairCount = 0;
	OutFallbackPairCount = 0;
	StartupHISMWarmupQueues.Reset();
	const float SearchRadiusCM = FMath::Max(10.0f, StartupHISMOverlapSearchRadiusCM);
	const float SearchRadiusSquared = FMath::Square(SearchRadiusCM);
	const float FallbackDistanceSquared = FMath::Square(FMath::Max(0.0f, StartupHISMFallbackCenterDistanceCM));

	TArray<FABTSM6StartupHISMScanData> ScanData;
	for (UHierarchicalInstancedStaticMeshComponent* HISM : HISMs)
	{
		if (HISM == nullptr || HISM->GetInstanceCount() <= 0) continue;
		FABTSM6StartupHISMScanData& Data = ScanData.AddDefaulted_GetRef();
		Data.Component = HISM;
		Data.WorldTransforms.SetNum(HISM->GetInstanceCount());
		for (int32 InstanceIndex = 0; InstanceIndex < HISM->GetInstanceCount(); ++InstanceIndex)
		{
			HISM->GetInstanceTransform(InstanceIndex, Data.WorldTransforms[InstanceIndex], true);
		}
	}

	for (int32 ComponentAIndex = 0; ComponentAIndex < ScanData.Num(); ++ComponentAIndex)
	{
		FABTSM6StartupHISMScanData& DataA = ScanData[ComponentAIndex];
		for (int32 InstanceA = 0; InstanceA < DataA.WorldTransforms.Num(); ++InstanceA)
		{
			if (DataA.SelectedCandidateIndices.Contains(InstanceA)) continue;
			const FTransform& TransformA = DataA.WorldTransforms[InstanceA];
			bool bSelectedCurrentInstance = false;
			for (int32 ComponentBIndex = ComponentAIndex; ComponentBIndex < ScanData.Num(); ++ComponentBIndex)
			{
				FABTSM6StartupHISMScanData& DataB = ScanData[ComponentBIndex];
				TArray<int32> NearbyInstances = DataB.Component->GetInstancesOverlappingSphere(
					TransformA.GetLocation(), SearchRadiusCM, true);
				NearbyInstances.Sort();
				for (const int32 InstanceB : NearbyInstances)
				{
					if (!DataB.WorldTransforms.IsValidIndex(InstanceB)) continue;
					if (ComponentAIndex == ComponentBIndex && InstanceB <= InstanceA) continue;
					if (DataB.SelectedCandidateIndices.Contains(InstanceB)) continue;
					const FVector Delta = DataB.WorldTransforms[InstanceB].GetLocation() - TransformA.GetLocation();
					const float CenterDistanceSquared = Delta.SizeSquared();
					if (CenterDistanceSquared > SearchRadiusSquared) continue;

					const bool bExactOverlap = ABTSStartupBodiesOverlap(
						*DataA.Component, InstanceA, TransformA, *DataB.Component, InstanceB);
					const bool bFallbackOverlap = !bExactOverlap
						&& FallbackDistanceSquared > 0.0f
						&& CenterDistanceSquared <= FallbackDistanceSquared;
					if (!bExactOverlap && !bFallbackOverlap) continue;
					++OutOverlapPairCount;
					OutFallbackPairCount += bFallbackOverlap ? 1 : 0;

					if (ComponentAIndex == ComponentBIndex)
					{
						// Keep the lower index as the static anchor; removing descending indices later
						// therefore never invalidates an unprocessed queue entry.
						DataB.SelectedCandidateIndices.Add(InstanceB);
					}
					else
					{
						// One dynamic body is sufficient to resolve a pair. Prefer the earlier
						// component (forest before rock on the spherical map) as the movable side.
						DataA.SelectedCandidateIndices.Add(InstanceA);
						bSelectedCurrentInstance = true;
						break;
					}
				}
				if (bSelectedCurrentInstance) break;
			}
		}
	}

	int32 TotalCandidateCount = 0;
	for (FABTSM6StartupHISMScanData& Data : ScanData)
	{
		if (Data.SelectedCandidateIndices.IsEmpty()) continue;
		FABTSM6StartupHISMWarmupQueue& Queue = StartupHISMWarmupQueues.AddDefaulted_GetRef();
		Queue.Component = Data.Component;
		Queue.CandidateIndicesDescending = Data.SelectedCandidateIndices.Array();
		Queue.CandidateIndicesDescending.Sort(TGreater<int32>());
		TotalCandidateCount += Queue.CandidateIndicesDescending.Num();
	}
	return TotalCandidateCount;
}

int32 AABTSM6SlingshotSystem::StartNextStartupHISMWarmupBatch()
{
	const int32 BatchLimit = FMath::Clamp(StartupHISMMaxSimultaneousBodies, 8, 512);
	int32 PromotedCount = 0;
	for (FABTSM6StartupHISMWarmupQueue& Queue : StartupHISMWarmupQueues)
	{
		UHierarchicalInstancedStaticMeshComponent* HISM = Queue.Component.Get();
		if (HISM == nullptr)
		{
			Queue.NextCandidateOffset = Queue.CandidateIndicesDescending.Num();
			continue;
		}
		const EABTSM6ImpactMaterial Material = ResolveMaterial(HISM);
		const FABTSM6MaterialImpactProfile& Profile = GetMaterialProfile(Material);
		while (Queue.NextCandidateOffset < Queue.CandidateIndicesDescending.Num() && PromotedCount < BatchLimit)
		{
			const int32 InstanceIndex = Queue.CandidateIndicesDescending[Queue.NextCandidateOffset++];
			const int32 ProxyCountBeforePromotion = DynamicProxies.Num();
			if (PromoteOrBreakHISM(*HISM, InstanceIndex, Material, Profile,
				0.0f, FVector::ZeroVector, 0.0f, BIG_NUMBER, 0.0f))
			{
				++PromotedCount;
				if (DynamicProxies.Num() > ProxyCountBeforePromotion)
				{
					if (AABTSM6DestructibleProxy* Proxy = DynamicProxies.Last().Get())
					{
						StartupProxySourceHISMs.Add(Proxy, HISM);
					}
				}
			}
		}
		if (PromotedCount >= BatchLimit) break;
	}

	if (PromotedCount > 0)
	{
		++StartupHISMWarmupBatchIndex;
		StartupHISMWarmupPromotedTotal += PromotedCount;
		const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
		StartupPhysicsSettleMonitor.BeginSettlement(Now);
		NextStartupWarmupDiagnosticTimeSeconds = Now;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][StartupPhysics] BatchBegin Batch=%d Promoted=%d TotalPromoted=%d/%d Limit=%d"),
			StartupHISMWarmupBatchIndex, PromotedCount, StartupHISMWarmupPromotedTotal,
			StartupHISMWarmupTotalCandidates, BatchLimit);
	}
	return PromotedCount;
}

bool AABTSM6SlingshotSystem::HasPendingStartupHISMCandidates() const
{
	for (const FABTSM6StartupHISMWarmupQueue& Queue : StartupHISMWarmupQueues)
	{
		if (Queue.NextCandidateOffset < Queue.CandidateIndicesDescending.Num()) return true;
	}
	return false;
}

int32 AABTSM6SlingshotSystem::RestoreStartupHISMProxies()
{
	TMap<UHierarchicalInstancedStaticMeshComponent*, TArray<FTransform>> RestoredTransformsByHISM;
	TSet<AABTSM6DestructibleProxy*> RestoredProxies;
	for (const TPair<TWeakObjectPtr<AABTSM6DestructibleProxy>, TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent>>& Pair
		: StartupProxySourceHISMs)
	{
		AABTSM6DestructibleProxy* Proxy = Pair.Key.Get();
		UHierarchicalInstancedStaticMeshComponent* HISM = Pair.Value.Get();
		if (Proxy == nullptr || HISM == nullptr) continue;
		const UStaticMeshComponent* Mesh = Proxy->GetMeshComponent();
		if (Mesh == nullptr) continue;
		RestoredTransformsByHISM.FindOrAdd(HISM).Add(Mesh->GetComponentTransform());
		RestoredProxies.Add(Proxy);
	}

	int32 RestoredInstanceCount = 0;
	for (TPair<UHierarchicalInstancedStaticMeshComponent*, TArray<FTransform>>& Pair : RestoredTransformsByHISM)
	{
		if (Pair.Key == nullptr || Pair.Value.IsEmpty()) continue;
		Pair.Key->AddInstances(Pair.Value, false, true, false);
		RestoredInstanceCount += Pair.Value.Num();
	}
	for (AABTSM6DestructibleProxy* Proxy : RestoredProxies)
	{
		if (Proxy == nullptr) continue;
		Proxy->Freeze();
		Proxy->Destroy();
	}
	DynamicProxies.RemoveAllSwap([&RestoredProxies](const TWeakObjectPtr<AABTSM6DestructibleProxy>& Entry)
	{
		return !Entry.IsValid() || RestoredProxies.Contains(Entry.Get());
	});
	StartupProxySourceHISMs.Reset();
	return RestoredInstanceCount;
}

void AABTSM6SlingshotSystem::FinishStartupPhysicsWarmup(const FABTSM6PhysicsActivitySummary& Summary)
{
	if (bStartupPhysicsWarmupComplete) return;
	if (GetWorld() != nullptr)
	{
		const FABTSM73StartupValidationSummary BuildingValidation = GetBuildingValidationSummary(
			*GetWorld(),
			bRequiredBuildingContractActive,
			bRequiredBuildingContractSealed,
			bRequiredBuildingSetupRejected,
			ExpectedRequiredBuildingCount,
			RequiredBuildingActors);
		if (BuildingValidation.GetGate() == EABTSM6BuildingValidationGate::Rejected)
		{
			bStartupBuildingSettlementActive = false;
			bStartupPhysicsWarmupFailed = true;
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][StartupPhysics] WorldReadyBlocked Reason=BuildingGateRejected Pending=%d Running=%d Accepted=%d Rejected=%d NotRequired=%d Contract=%d Sealed=%d SetupRejected=%d Expected=%d Registered=%d"),
				BuildingValidation.Pending, BuildingValidation.Running,
				BuildingValidation.Accepted, BuildingValidation.Rejected, BuildingValidation.NotRequired,
				BuildingValidation.bContractActive ? 1 : 0,
				BuildingValidation.bContractSealed ? 1 : 0,
				BuildingValidation.bSetupRejected ? 1 : 0,
				BuildingValidation.ExpectedRequired, BuildingValidation.RegisteredRequired);
			return;
		}
		if (BuildingValidation.GetGate() == EABTSM6BuildingValidationGate::Waiting)
		{
			// This also covers a delayed/missing MaterialSystem with no HISM
			// batches: enter an explicit wait phase and re-check every tick.
			bStartupBuildingSettlementActive = true;
			StartupPhysicsSettleMonitor.BeginSettlement(GetWorld()->GetTimeSeconds());
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][StartupPhysics] WorldReadyWaiting BuildingPending=%d BuildingRunning=%d BuildingAccepted=%d BuildingRejected=%d Contract=%d Sealed=%d Expected=%d Registered=%d"),
				BuildingValidation.Pending, BuildingValidation.Running,
				BuildingValidation.Accepted, BuildingValidation.Rejected,
				BuildingValidation.bContractActive ? 1 : 0,
				BuildingValidation.bContractSealed ? 1 : 0,
				BuildingValidation.ExpectedRequired, BuildingValidation.RegisteredRequired);
			return;
		}
	}
	FreezeDynamicProxies();
	bStartupPhysicsWarmupComplete = true;
	const FABTSM73StartupValidationSummary BuildingValidation = GetWorld() != nullptr
		? GetBuildingValidationSummary(
			*GetWorld(),
			bRequiredBuildingContractActive,
			bRequiredBuildingContractSealed,
			bRequiredBuildingSetupRejected,
			ExpectedRequiredBuildingCount,
			RequiredBuildingActors)
		: FABTSM73StartupValidationSummary();
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][StartupPhysics] Complete WorldReady=1 LastBatchBodies=%d Stable=%.2f Elapsed=%.2f Candidates=%d Promoted=%d Batches=%d TimedOutBatches=%d StaticProxies=%d BuildingPending=%d BuildingRunning=%d BuildingAccepted=%d BuildingRejected=%d BuildingNotRequired=%d BuildingExpected=%d BuildingRegistered=%d"),
		Summary.ActiveBodyCount, Summary.StableElapsedSeconds, Summary.SettlementElapsedSeconds,
		StartupHISMWarmupTotalCandidates, StartupHISMWarmupPromotedTotal,
		StartupHISMWarmupBatchIndex, StartupHISMWarmupTimedOutBatches, DynamicProxies.Num(),
		BuildingValidation.Pending, BuildingValidation.Running,
		BuildingValidation.Accepted, BuildingValidation.Rejected, BuildingValidation.NotRequired,
		BuildingValidation.ExpectedRequired, BuildingValidation.RegisteredRequired);
}
