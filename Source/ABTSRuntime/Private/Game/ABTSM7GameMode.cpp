// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM7GameMode.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM73JuryDemoFixedSixRegistration.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "PBDRigidsSolver.h"
#include "Contracts/ABTSWorldGenerationContracts.h"
#include "Components/PrimitiveComponent.h"
#include "ProceduralMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Crc.h"
#include "Misc/Parse.h"
#include "PCG/ABTSM3MonthlySatellitePracticeRuntime.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Planet/ABTSM2Planet.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "Terrain/ABTSM3Planet.h"
#include "TimerManager.h"
#include "Engine/TargetPoint.h"
#include "UObject/UObjectGlobals.h"
#include "World/ABTSCollisionChannels.h"

namespace
{
	constexpr double JuryDemoFixedSixProductionDeltaSeconds = 1.0 / 60.0;

	void GetLatitudeLongitudeDegrees(const FVector& WorldLocation, const FVector& PlanetCenter, float& OutLatitudeDegrees, float& OutLongitudeDegrees)
	{
		const FVector Direction = (WorldLocation - PlanetCenter).GetSafeNormal();
		OutLatitudeDegrees = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Direction.Z, -1.0f, 1.0f)));
		OutLongitudeDegrees = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
	}

	EABTSM3TaskType ResolveLegacyTaskType(
		const FABTSGeneratedBuildingSite& Site)
	{
		switch (Site.Purpose)
		{
		case EABTSGeneratedBuildingPurpose::Workshop:
			return EABTSM3TaskType::Workshop;
		case EABTSGeneratedBuildingPurpose::DestructibleTarget:
			return EABTSM3TaskType::TargetBuilding;
		case EABTSGeneratedBuildingPurpose::FurnaceRuins:
			return EABTSM3TaskType::FurnaceRuins;
		case EABTSGeneratedBuildingPurpose::FinaleLaunchReserved:
			return EABTSM3TaskType::LaunchSite;
		default:
			return EABTSM3TaskType::Unassigned;
		}
	}
}

bool FABTSM7SatellitePracticeE1CrystalBindingLifecycle::Start(
	const double NowSeconds)
{
	if (!FMath::IsFinite(NowSeconds)
		|| State != EABTSM7SatellitePracticeE1CrystalBindingState::Inactive)
	{
		return false;
	}
	State = EABTSM7SatellitePracticeE1CrystalBindingState::Waiting;
	StartSeconds = NowSeconds;
	AttemptCount = 0;
	TerminalReason.Reset();
	return true;
}

EABTSM7SatellitePracticeE1CrystalBindingAction
FABTSM7SatellitePracticeE1CrystalBindingLifecycle::Advance(
	const double NowSeconds,
	const FABTSM7SatellitePracticeE1CrystalBindingObservation& Observation,
	FString& OutReason)
{
	OutReason.Reset();
	if (State != EABTSM7SatellitePracticeE1CrystalBindingState::Waiting)
	{
		return EABTSM7SatellitePracticeE1CrystalBindingAction::None;
	}
	++AttemptCount;
	const auto Reject = [this, &OutReason](const TCHAR* Reason)
	{
		OutReason = Reason;
		TerminalReason = OutReason;
		State = EABTSM7SatellitePracticeE1CrystalBindingState::Rejected;
		return EABTSM7SatellitePracticeE1CrystalBindingAction::Reject;
	};
	if (!FMath::IsFinite(NowSeconds)
		|| Observation.AcceptedStaticBuildingCount < 0
		|| Observation.E1OrderedUnionCount < 0
		|| Observation.SatelliteRuntimeCount < 0)
	{
		return Reject(TEXT("InvalidObservation"));
	}
	if (Observation.AcceptedStaticBuildingCount
		> ExpectedStaticBuildingCount)
	{
		return Reject(TEXT("MultipleStaticBuildingSets"));
	}
	if (Observation.E1OrderedUnionCount > 1)
	{
		return Reject(TEXT("MultipleE1OrderedUnions"));
	}
	if (Observation.SatelliteRuntimeCount > 1)
	{
		return Reject(TEXT("MultipleSatelliteRuntimes"));
	}
	if (Observation.AcceptedStaticBuildingCount
		== ExpectedStaticBuildingCount
		&& Observation.E1OrderedUnionCount == 1
		&& Observation.SatelliteRuntimeCount == 1
		&& Observation.bSatelliteRuntimeReady)
	{
		OutReason = TEXT("ReadyToBind");
		State = EABTSM7SatellitePracticeE1CrystalBindingState::Binding;
		return EABTSM7SatellitePracticeE1CrystalBindingAction::Bind;
	}

	const bool bTimedOut = NowSeconds - StartSeconds >= TimeoutSeconds;
	if (Observation.AcceptedStaticBuildingCount
		< ExpectedStaticBuildingCount)
	{
		OutReason = bTimedOut
			? TEXT("StaticBuildingsTimeout")
			: TEXT("StaticBuildingsPending");
	}
	else if (Observation.E1OrderedUnionCount == 0)
	{
		OutReason = bTimedOut
			? TEXT("E1OrderedUnionTimeout")
			: TEXT("E1OrderedUnionPending");
	}
	else if (Observation.SatelliteRuntimeCount == 0)
	{
		OutReason = bTimedOut
			? TEXT("SatelliteRuntimeTimeout")
			: TEXT("SatelliteRuntimePending");
	}
	else
	{
		OutReason = bTimedOut
			? TEXT("SatelliteRuntimeNotReadyTimeout")
			: TEXT("SatelliteRuntimeNotReady");
	}
	if (bTimedOut)
	{
		TerminalReason = OutReason;
		State = EABTSM7SatellitePracticeE1CrystalBindingState::Rejected;
		return EABTSM7SatellitePracticeE1CrystalBindingAction::Reject;
	}
	return EABTSM7SatellitePracticeE1CrystalBindingAction::Wait;
}

void FABTSM7SatellitePracticeE1CrystalBindingLifecycle::MarkBound()
{
	if (State == EABTSM7SatellitePracticeE1CrystalBindingState::Binding)
	{
		State = EABTSM7SatellitePracticeE1CrystalBindingState::Bound;
		TerminalReason = TEXT("Bound");
	}
}

void FABTSM7SatellitePracticeE1CrystalBindingLifecycle::
MarkBindingRejected(const FString& Reason)
{
	if (State == EABTSM7SatellitePracticeE1CrystalBindingState::Inactive
		|| State == EABTSM7SatellitePracticeE1CrystalBindingState::Waiting
		|| State == EABTSM7SatellitePracticeE1CrystalBindingState::Binding)
	{
		State = EABTSM7SatellitePracticeE1CrystalBindingState::Rejected;
		TerminalReason = Reason.IsEmpty()
			? TEXT("BindingRejected")
			: Reason;
	}
}

void FABTSM7SatellitePracticeE1CrystalBindingLifecycle::Cancel()
{
	State = EABTSM7SatellitePracticeE1CrystalBindingState::Cancelled;
	TerminalReason = TEXT("Cancelled");
}

bool FABTSM7TaskGraphDAG23ProfileResolver::IsSupportedBuildingTask(const EABTSM3TaskType TaskType)
{
	return TaskType == EABTSM3TaskType::Workshop
		|| TaskType == EABTSM3TaskType::TargetBuilding
		|| TaskType == EABTSM3TaskType::FurnaceRuins;
}

EABTSM73DAGPreset FABTSM7TaskGraphDAG23ProfileResolver::GetDefaultPreset(const EABTSM3TaskType TaskType)
{
	return TaskType == EABTSM3TaskType::TargetBuilding
		? EABTSM73DAGPreset::TwinTowerBridge
		: EABTSM73DAGPreset::SingleTower;
}

FABTSM7TaskGraphBuildingProfile FABTSM7TaskGraphDAG23ProfileResolver::MakeDefaultProfile(
	const EABTSM3TaskType TaskType,
	const EABTSM7BuildingMaterial Material)
{
	FABTSM7TaskGraphBuildingProfile Profile;
	Profile.TaskType = TaskType;
	Profile.GenerationSettings.GenerationAlgorithm = EABTSM73GenerationAlgorithm::RecursiveSupportDAG;
	Profile.GenerationSettings.PrimaryMaterial = Material;
	Profile.GenerationSettings.bGenerateStructuralWeakness = false;
	Profile.GenerationSettings.Levels = 2;
	Profile.GenerationSettings.Silhouette = TaskType == EABTSM3TaskType::TargetBuilding
		? EABTSM73Silhouette::TwinTowerBridge
		: EABTSM73Silhouette::SingleTower;
	Profile.GenerationSettings.MaxSinglePlatformAngularSpanDegrees = 7.0f;

	// The first production migration intentionally uses the authored seed
	// topologies only. They still execute the complete DAG2.3 cumulative-load,
	// joint-support, module-compile and contact-audit chain, while keeping the
	// runtime body count independent from the TaskGraph-derived seed.
	Profile.DAGGenerationSettings.Preset = GetDefaultPreset(TaskType);
	Profile.DAGGenerationSettings.MinExpansionDepth = 0;
	Profile.DAGGenerationSettings.MaxExpansionDepth = 0;
	Profile.DAGGenerationSettings.ExpansionStepBudget = 0;
	Profile.DAGGenerationSettings.ReservedWeaknessBrickCount = 0;
	Profile.DAGGenerationSettings.DefaultParallelPolicy = EABTSM73DAGParallelPolicy::AllRequired;

	Profile.DAGLayoutSettings.SupportPattern = EABTSM73DAGSupportPattern::ThreeColumnTripod;
	Profile.DAGLayoutSettings.PreferredLogicalSupportsPerLoad = 2;
	Profile.DAGLayoutSettings.MaxLogicalSupportsPerLoad = 2;
	switch (TaskType)
	{
	case EABTSM3TaskType::Workshop:
		Profile.GenerationSettings.MaxBrickCount = 20;
		Profile.DAGLayoutSettings.TargetWidthCM = 360.0f;
		Profile.DAGLayoutSettings.TargetDepthCM = 260.0f;
		Profile.DAGLayoutSettings.TargetHeightCM = 480.0f;
		break;
	case EABTSM3TaskType::TargetBuilding:
		Profile.GenerationSettings.MaxBrickCount = 24;
		Profile.DAGLayoutSettings.TargetWidthCM = 460.0f;
		Profile.DAGLayoutSettings.TargetDepthCM = 300.0f;
		Profile.DAGLayoutSettings.TargetHeightCM = 520.0f;
		break;
	case EABTSM3TaskType::FurnaceRuins:
		Profile.GenerationSettings.MaxBrickCount = 20;
		Profile.DAGLayoutSettings.TargetWidthCM = 400.0f;
		Profile.DAGLayoutSettings.TargetDepthCM = 280.0f;
		Profile.DAGLayoutSettings.TargetHeightCM = 480.0f;
		Profile.DAGLayoutSettings.MinSupportContactAreaRatio =
			FurnaceMinSupportContactAreaRatio;
		break;
	default:
		break;
	}
	Profile.DAGGenerationSettings.MaxEstimatedBrickCount = Profile.GenerationSettings.MaxBrickCount;
	return Profile;
}

bool FABTSM7TaskGraphDAG23ProfileResolver::ResolveRuntimeProfile(
	const EABTSM3TaskType TaskType,
	const FABTSM7TaskGraphBuildingProfile& SourceProfile,
	FABTSM7TaskGraphBuildingProfile& OutProfile,
	bool& bOutMigratedLegacy)
{
	bOutMigratedLegacy = false;
	if (!IsSupportedBuildingTask(TaskType)
		|| SourceProfile.TaskType != TaskType
		|| !SourceProfile.bSpawnBuilding)
	{
		return false;
	}

	if (SourceProfile.GenerationSettings.GenerationAlgorithm
		== EABTSM73GenerationAlgorithm::RecursiveSupportDAG)
	{
		OutProfile = SourceProfile;
		// DAG3-B exists only as an explicit pure-data candidate path. Do not
		// expose the retired B/B2 switch as if it enabled DAG3-C gameplay,
		// material routing or production weak points.
		OutProfile.GenerationSettings.bGenerateStructuralWeakness = false;
		OutProfile.DAGGenerationSettings.ReservedWeaknessBrickCount = 0;
		if (OutProfile.DAGFailurePlayabilitySettings.bEnablePlayabilityRouting
			&& (!OutProfile.DAGFailureFrontierSettings.bEnableAnalysis
				|| !OutProfile.DAGFailureFrontierSettings.bEnableGeneralizedSmallCutSearch
				|| !OutProfile.DAGFailurePatternSettings.bEnableGeometryRewrite))
		{
			return false;
		}
		if (OutProfile.DAG4ValidationSettings.bEnableSettledChaosValidation
			&& (!OutProfile.DAGFailureFrontierSettings.bEnableAnalysis
				|| !OutProfile.DAGFailureFrontierSettings.bEnableGeneralizedSmallCutSearch
				|| !OutProfile.DAGFailurePatternSettings.bEnableGeometryRewrite
				|| !OutProfile.DAGFailurePlayabilitySettings.bEnablePlayabilityRouting))
		{
			return false;
		}
		// Existing Blueprint CDOs may already contain an explicitly authored DAG
		// profile with the old 4% value. Preserve authored topology and scale, but
		// apply the production iron-building contact floor at this runtime boundary.
		if (TaskType == EABTSM3TaskType::FurnaceRuins)
		{
			OutProfile.DAGLayoutSettings.MinSupportContactAreaRatio = FMath::Max(
				OutProfile.DAGLayoutSettings.MinSupportContactAreaRatio,
				FurnaceMinSupportContactAreaRatio);
		}
		return true;
	}

	// Blueprint arrays serialize native defaults. A saved M7/M9/M10 GameMode
	// therefore keeps Algorithm=Legacy after the constructor changes. Upgrade
	// that retired entry to a known-safe task preset at the runtime boundary.
	OutProfile = MakeDefaultProfile(TaskType, SourceProfile.GenerationSettings.PrimaryMaterial);
	OutProfile.bSpawnBuilding = SourceProfile.bSpawnBuilding;
	OutProfile.DifficultySettings = SourceProfile.DifficultySettings;
	bOutMigratedLegacy = true;
	return true;
}

AABTSM7GameMode::AABTSM7GameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	BuildingMaterialSystemClass = AABTSM7BuildingMaterialSystem::StaticClass();
	StableBuildingClass = AABTSM73StableBuildingActor::StaticClass();
	const auto AddDefaultProfile = [this](
		const EABTSM3TaskType TaskType,
		const EABTSM7BuildingMaterial Material)
	{
		TaskGraphBuildingProfiles.Add(
			FABTSM7TaskGraphDAG23ProfileResolver::MakeDefaultProfile(TaskType, Material));
	};
	AddDefaultProfile(EABTSM3TaskType::Workshop, EABTSM7BuildingMaterial::Wood);
	AddDefaultProfile(EABTSM3TaskType::TargetBuilding, EABTSM7BuildingMaterial::Stone);
	AddDefaultProfile(EABTSM3TaskType::FurnaceRuins, EABTSM7BuildingMaterial::Iron);
}

void AABTSM7GameMode::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateProductionFlowTiming(DeltaSeconds);
	UpdateJuryDemoFixedSixProductionChaosBatch();
	if (!UE_BUILD_SHIPPING && bShowTaskGraphPositionDebug)
	{
		DrawTaskGraphPositionDebug();
	}
}

void AABTSM7GameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearSatellitePracticeE1CrystalTargetBindingTimer();
	RestoreJuryDemoFixedSixTerrainBuildingCollisionOverride(TEXT("EndPlay"));
	RestoreJuryDemoFixedSixProductionChaosFixedStep();
	SatellitePracticeE1CrystalBindingLifecycle.Cancel();
	LastSatellitePracticeE1CrystalBindingWaitReason.Reset();
	JuryDemoFixedSixChaosBuildings.Reset();
	bJuryDemoFixedSixChaosBatchActive = false;
	JuryDemoFixedSixChaosActiveIndex = INDEX_NONE;
	bProductionFlowTimingActive = false;
	Super::EndPlay(EndPlayReason);
}

const FABTSM7TaskGraphBuildingProfile* AABTSM7GameMode::FindTaskGraphBuildingProfile(const EABTSM3TaskType TaskType) const
{
	return TaskGraphBuildingProfiles.FindByPredicate([TaskType](const FABTSM7TaskGraphBuildingProfile& Profile)
	{
		return Profile.bSpawnBuilding && Profile.TaskType == TaskType;
	});
}

int32 AABTSM7GameMode::CountRequiredTaskGraphBuildings(
	const FABTSBuildingGenerationContract& Contract) const
{
	const int32 Limit = FMath::Max(0, MaxTaskGraphBuildings);
	int32 RequiredCount = 0;
	for (const FABTSGeneratedBuildingSite& Site : Contract.Sites)
	{
		const EABTSM3TaskType TaskType = ResolveLegacyTaskType(Site);
		if (!FABTSM7TaskGraphDAG23ProfileResolver::IsSupportedBuildingTask(TaskType)) continue;
		if (RequiredCount >= Limit) break;
		++RequiredCount;
	}
	return RequiredCount;
}

int32 AABTSM7GameMode::SpawnTaskGraphBuildings(
	AABTSM3Planet& Planet,
	const FABTSBuildingGenerationContract& Contract,
	AABTSM7BuildingMaterialSystem& MaterialSystem,
	AABTSM6SlingshotSystem* SlingshotSystem,
	bool& bOutSetupFailed)
{
	bOutSetupFailed = false;
	TaskGraphBuildingDebugEntries.Reset();
	const int32 RequiredCount = CountRequiredTaskGraphBuildings(Contract);
	if (RequiredCount <= 0) return 0;
	if (!StableBuildingClass)
	{
		bOutSetupFailed = true;
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7][TaskGraphBuilding] RequiredClassMissing Expected=%d"),
			RequiredCount);
		return 0;
	}
	int32 SpawnedCount = 0;
	int32 AttemptedRequiredCount = 0;
	for (const FABTSGeneratedBuildingSite& Site : Contract.Sites)
	{
		const EABTSM3TaskType TaskType = ResolveLegacyTaskType(Site);
		// M11.0 reserves the certified LaunchSite pad for the unique terminal
		// Space-slingshot slot pair. Keep this guard even when a Blueprint CDO
		// still serializes the retired Glass/TwinTower LaunchSite profile.
		if (Site.Purpose
				== EABTSGeneratedBuildingPurpose::FinaleLaunchReserved
			|| TaskType == EABTSM3TaskType::LaunchSite)
		{
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][M11.0][LaunchSite] Certified pad retained; M7 building suppressed Task=%d Cell=%d"),
				Site.TaskId,
				Site.CellId);
			continue;
		}
		if (!FABTSM7TaskGraphDAG23ProfileResolver::IsSupportedBuildingTask(TaskType)) continue;
		if (AttemptedRequiredCount >= RequiredCount) break;
		++AttemptedRequiredCount;
		const FABTSM7TaskGraphBuildingProfile* Profile = FindTaskGraphBuildingProfile(TaskType);
		if (Profile == nullptr)
		{
			bOutSetupFailed = true;
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7][TaskGraphBuilding] DAG23ProfileMissing Task=%d Type=%d Cell=%d"),
				Site.TaskId, static_cast<int32>(TaskType), Site.CellId);
			continue;
		}
		FABTSM7TaskGraphBuildingProfile RuntimeProfile;
		bool bMigratedLegacyProfile = false;
		if (!FABTSM7TaskGraphDAG23ProfileResolver::ResolveRuntimeProfile(
			TaskType, *Profile, RuntimeProfile, bMigratedLegacyProfile))
		{
			bOutSetupFailed = true;
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7][TaskGraphBuilding] DAG23ProfileRejected Task=%d Type=%d Cell=%d"),
				Site.TaskId, static_cast<int32>(TaskType), Site.CellId);
			continue;
		}

		FABTSM73GenerationSettings GenerationSettings = RuntimeProfile.GenerationSettings;
		GenerationSettings.BuildingSeed = Site.DeterministicSeed;
		if (Site.bTerrainPadApplied)
		{
			GenerationSettings.MaxSinglePlatformAngularSpanDegrees = FMath::Max(
				GenerationSettings.MaxSinglePlatformAngularSpanDegrees,
				MaxTaskGraphBuildingAngularSpanDegrees);
		}
		FABTSM73DAGGenerationSettings DAGGenerationSettings = RuntimeProfile.DAGGenerationSettings;
		DAGGenerationSettings.BuildingSeed = GenerationSettings.BuildingSeed;

		AABTSM73StableBuildingActor* Building = GetWorld()->SpawnActorDeferred<AABTSM73StableBuildingActor>(
			StableBuildingClass, Site.WorldTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Building == nullptr)
		{
			bOutSetupFailed = true;
			UE_LOG(LogABTSRuntime, Error, TEXT("[ABTS][M7][TaskGraphBuilding] SpawnDeferredFailed Task=%d Cell=%d"), Site.TaskId, Site.CellId);
			continue;
		}
		Building->ConfigureTaskGraphGeneration(
			GenerationSettings,
			DAGGenerationSettings,
			RuntimeProfile.DAGLayoutSettings,
			RuntimeProfile.DAGFailureFrontierSettings,
			RuntimeProfile.DAGFailurePatternSettings,
			RuntimeProfile.DAGFailurePlayabilitySettings,
			RuntimeProfile.DAG4ValidationSettings,
			RuntimeProfile.DifficultySettings);
		Building->ConfigureSphericalAnchor(&Planet, Site.CellId, Site.WorldTransform);
		if (SlingshotSystem)
		{
			SlingshotSystem->RegisterRequiredBuilding(*Building);
		}
		else
		{
			bOutSetupFailed = true;
		}
		UGameplayStatics::FinishSpawningActor(Building, Site.WorldTransform);
		Building->InitializeRuntimeBuilding(&MaterialSystem);
		FABTSM7TaskGraphBuildingDebugEntry& DebugEntry = TaskGraphBuildingDebugEntries.AddDefaulted_GetRef();
		DebugEntry.Building = Building;
		DebugEntry.TaskId = Site.TaskId;
		DebugEntry.TaskType = TaskType;
		DebugEntry.CellId = Site.CellId;
		++SpawnedCount;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7][TaskGraphBuilding] Task=%d Type=%d Cell=%d Material=%d Seed=%d Pad=%d Algorithm=%d DAGPreset=%d DAGBudget=%d DAGDepth=%d DAGMinContact=%.3f DAG3Enabled=%d DAG3BEnabled=%d DAG3CEnabled=%d DAG4Enabled=%d MigratedLegacy=%d Spawned=%s"),
			Site.TaskId, static_cast<int32>(TaskType), Site.CellId,
			static_cast<int32>(GenerationSettings.PrimaryMaterial), GenerationSettings.BuildingSeed,
			Site.bTerrainPadApplied ? 1 : 0,
			static_cast<int32>(GenerationSettings.GenerationAlgorithm),
			static_cast<int32>(DAGGenerationSettings.Preset),
			DAGGenerationSettings.ExpansionStepBudget,
			DAGGenerationSettings.MaxExpansionDepth,
			RuntimeProfile.DAGLayoutSettings.MinSupportContactAreaRatio,
			RuntimeProfile.DAGFailureFrontierSettings.bEnableAnalysis ? 1 : 0,
			RuntimeProfile.DAGFailurePatternSettings.bEnableGeometryRewrite ? 1 : 0,
			RuntimeProfile.DAGFailurePlayabilitySettings.bEnablePlayabilityRouting ? 1 : 0,
			RuntimeProfile.DAG4ValidationSettings.bEnableSettledChaosValidation ? 1 : 0,
			bMigratedLegacyProfile ? 1 : 0,
			*Building->GetName());
	}
	bOutSetupFailed = bOutSetupFailed
		|| AttemptedRequiredCount != RequiredCount
		|| SpawnedCount != RequiredCount;
	return SpawnedCount;
}

int32 AABTSM7GameMode::SpawnJuryDemoFixedSixStaticBuildings(
	const FABTSBuildingGenerationContract& Contract,
	AABTSM7BuildingMaterialSystem& MaterialSystem,
	AABTSM6SlingshotSystem* SlingshotSystem,
	bool& bOutSetupFailed)
{
	bOutSetupFailed = false;
	TaskGraphBuildingDebugEntries.Reset();
	if (SlingshotSystem == nullptr || !StableBuildingClass)
	{
		bOutSetupFailed = true;
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7][FixedSixV2] RegistrationPrerequisiteMissing")
			TEXT(" Slingshot=%d BuildingClass=%d"),
			SlingshotSystem ? 1 : 0, StableBuildingClass ? 1 : 0);
		return 0;
	}

	FABTSM73JuryDemoFixedSixStaticPlan Plan;
	FString Error;
	if (!FABTSM73JuryDemoFixedSixRegistration::BuildStaticPlan(
		Contract, Plan, Error))
	{
		bOutSetupFailed = true;
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7][FixedSixV2] StaticPlanRejected Reason=%s")
			TEXT(" Fallback=Forbidden"), *Error);
		return 0;
	}
	const uint64 RegistrationResultHash = Plan.RegistrationResultHash;
	const uint64 LayoutHash = Plan.LayoutHash;
	TArray<TWeakObjectPtr<AABTSM73StableBuildingActor>> Actors;
	if (!FABTSM73JuryDemoFixedSixRegistration::SpawnStaticActors(
		*GetWorld(), MaterialSystem, StableBuildingClass,
		MoveTemp(Plan), Actors, Error))
	{
		bOutSetupFailed = true;
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7][FixedSixV2] StaticActorBatchRejected")
			TEXT(" Reason=%s RolledBack=1 Fallback=Forbidden"), *Error);
		return 0;
	}

	int32 RegisteredCount = 0;
	int32 StaticModuleCount = 0;
	for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& WeakActor : Actors)
	{
		AABTSM73StableBuildingActor* Actor = WeakActor.Get();
		if (Actor == nullptr
			|| !Actor->IsJuryDemoFixedSixStaticRegistrationAccepted())
		{
			bOutSetupFailed = true;
			break;
		}
		StaticModuleCount += Actor->GetJuryDemoFixedSixStaticModuleCount();
	}
	if (bOutSetupFailed)
	{
		for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& WeakActor : Actors)
		{
			if (AABTSM73StableBuildingActor* Actor = WeakActor.Get())
			{
				Actor->RollbackJuryDemoFixedSixStaticRegistration(
					TEXT("FixedSixV2PreRegistrationActorLost"));
			}
		}
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7][FixedSixV2] AtomicRegistrationRejected")
			TEXT(" Registered=0 Expected=%d SetupRejected=1"),
			FABTSJuryDemoFixedSixContract::ExpectedSiteCount);
		return 0;
	}
	for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& WeakActor : Actors)
	{
		AABTSM73StableBuildingActor* Actor = WeakActor.Get();
		check(Actor != nullptr);
		SlingshotSystem->RegisterRequiredBuilding(*Actor);
		FABTSM7TaskGraphBuildingDebugEntry& DebugEntry =
			TaskGraphBuildingDebugEntries.AddDefaulted_GetRef();
		DebugEntry.Building = Actor;
		DebugEntry.TaskId = Actor->GetJuryDemoFixedSixEncounterIndex();
		DebugEntry.TaskType = EABTSM3TaskType::Unassigned;
		DebugEntry.CellId = INDEX_NONE;
		++RegisteredCount;
	}
	if (RegisteredCount != FABTSJuryDemoFixedSixContract::ExpectedSiteCount)
	{
		// RegisterRequiredBuilding has no removal API by design. This branch can
		// only be reached if a validated Actor disappears in the same frame; the
		// shared seal therefore remains rejected and blocks WorldReady.
		bOutSetupFailed = true;
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7][FixedSixV2] AtomicRegistrationRejected")
			TEXT(" Registered=%d Expected=%d SetupRejected=1"),
			RegisteredCount,
			FABTSJuryDemoFixedSixContract::ExpectedSiteCount);
		return RegisteredCount;
	}

	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7][FixedSixV2] StaticRegistrationComplete")
		TEXT(" ContractVersion=2 Buildings=%d Modules=%d Layout=%llu")
		TEXT(" ResultHash=%llu Authority=StaticRegistration")
		TEXT(" Chaos=NotEvaluated Accepted=1"),
		RegisteredCount, StaticModuleCount, LayoutHash,
		RegistrationResultHash);
	return RegisteredCount;
}

void AABTSM7GameMode::ScheduleSatellitePracticeE1CrystalTargetBinding()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		SatellitePracticeE1CrystalBindingLifecycle.MarkBindingRejected(
			TEXT("BindingWorldMissing"));
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][IntegrationV3][E1CrystalTarget] Rejected Reason=BindingWorldMissing"));
		return;
	}
	if (!SatellitePracticeE1CrystalBindingLifecycle.Start(
		World->GetTimeSeconds()))
	{
		return;
	}
	SatellitePracticeE1CrystalBindingWorld = World;
	LastSatellitePracticeE1CrystalBindingWaitReason.Reset();
	World->GetTimerManager().SetTimer(
		SatellitePracticeE1CrystalBindingTimerHandle,
		this,
		&AABTSM7GameMode::TryBindSatellitePracticeE1CrystalTarget,
		SatellitePracticeE1CrystalBindingRetrySeconds,
		true,
		SatellitePracticeE1CrystalBindingRetrySeconds);
}

void AABTSM7GameMode::ClearSatellitePracticeE1CrystalTargetBindingTimer()
{
	if (UWorld* BindingWorld = SatellitePracticeE1CrystalBindingWorld.Get())
	{
		BindingWorld->GetTimerManager().ClearTimer(
			SatellitePracticeE1CrystalBindingTimerHandle);
	}
	SatellitePracticeE1CrystalBindingTimerHandle.Invalidate();
	SatellitePracticeE1CrystalBindingWorld.Reset();
}

void AABTSM7GameMode::TryBindSatellitePracticeE1CrystalTarget()
{
	UWorld* World = GetWorld();
	if (World == nullptr
		|| SatellitePracticeE1CrystalBindingWorld.Get() != World)
	{
		ClearSatellitePracticeE1CrystalTargetBindingTimer();
		SatellitePracticeE1CrystalBindingLifecycle.Cancel();
		return;
	}

	AABTSM73StableBuildingActor* E1Building = nullptr;
	FTransform SiteRecoveryAnchorTransform = FTransform::Identity;
	FVector SiteRecoveryAnchorHalfExtentCM = FVector::ZeroVector;
	FABTSM73E1OrderedBrickUnionBinding OrderedUnion;
	FABTSM7SatellitePracticeE1CrystalBindingObservation Observation;
	for (const FABTSM7TaskGraphBuildingDebugEntry& Entry :
		TaskGraphBuildingDebugEntries)
	{
		AABTSM73StableBuildingActor* Building = Entry.Building.Get();
		if (Building == nullptr
			|| !Building->IsJuryDemoFixedSixStaticRegistrationAccepted())
		{
			continue;
		}
		++Observation.AcceptedStaticBuildingCount;
		FABTSM73E1OrderedBrickUnionBinding CandidateUnion;
		FTransform CandidateAnchorTransform = FTransform::Identity;
		FVector CandidateAnchorHalfExtentCM = FVector::ZeroVector;
		if (Building->CopyJuryDemoE1OrderedBrickUnionBinding(
			CandidateUnion,
			CandidateAnchorTransform,
			CandidateAnchorHalfExtentCM))
		{
			++Observation.E1OrderedUnionCount;
			if (E1Building == nullptr)
			{
				E1Building = Building;
				OrderedUnion = MoveTemp(CandidateUnion);
				SiteRecoveryAnchorTransform = CandidateAnchorTransform;
				SiteRecoveryAnchorHalfExtentCM =
					CandidateAnchorHalfExtentCM;
			}
		}
	}

	AABTSM3MonthlySatellitePracticeRuntime* SatelliteRuntime = nullptr;
	for (TActorIterator<AABTSM3MonthlySatellitePracticeRuntime> It(World);
		It; ++It)
	{
		if (!IsValid(*It) || It->IsActorBeingDestroyed())
		{
			continue;
		}
		++Observation.SatelliteRuntimeCount;
		if (SatelliteRuntime == nullptr)
		{
			SatelliteRuntime = *It;
		}
	}
	Observation.bSatelliteRuntimeReady = SatelliteRuntime != nullptr
		&& SatelliteRuntime->IsRuntimeReady();

	FString Reason;
	const EABTSM7SatellitePracticeE1CrystalBindingAction Action =
		SatellitePracticeE1CrystalBindingLifecycle.Advance(
			World->GetTimeSeconds(), Observation, Reason);
	if (Action == EABTSM7SatellitePracticeE1CrystalBindingAction::Wait)
	{
		if (Reason != LastSatellitePracticeE1CrystalBindingWaitReason)
		{
			LastSatellitePracticeE1CrystalBindingWaitReason = Reason;
			UE_LOG(LogABTSRuntime, Verbose,
				TEXT("[ABTS][IntegrationV3][E1BrickUnionTarget] Waiting Reason=%s Attempt=%d Buildings=%d OrderedUnions=%d SatelliteRuntimes=%d RuntimeReady=%d"),
				*Reason,
				SatellitePracticeE1CrystalBindingLifecycle.GetAttemptCount(),
				Observation.AcceptedStaticBuildingCount,
				Observation.E1OrderedUnionCount,
				Observation.SatelliteRuntimeCount,
				Observation.bSatelliteRuntimeReady ? 1 : 0);
		}
		return;
	}
	if (Action == EABTSM7SatellitePracticeE1CrystalBindingAction::Reject)
	{
		ClearSatellitePracticeE1CrystalTargetBindingTimer();
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][IntegrationV3][E1BrickUnionTarget] Rejected Reason=%s Attempt=%d Buildings=%d OrderedUnions=%d SatelliteRuntimes=%d RuntimeReady=%d"),
			*Reason,
			SatellitePracticeE1CrystalBindingLifecycle.GetAttemptCount(),
			Observation.AcceptedStaticBuildingCount,
			Observation.E1OrderedUnionCount,
			Observation.SatelliteRuntimeCount,
			Observation.bSatelliteRuntimeReady ? 1 : 0);
		FinishProductionFlow(false, Reason);
		return;
	}
	if (Action != EABTSM7SatellitePracticeE1CrystalBindingAction::Bind)
	{
		return;
	}
	FActorSpawnParameters AnchorSpawnParameters;
	AnchorSpawnParameters.Owner = E1Building;
	AnchorSpawnParameters.ObjectFlags |= RF_Transient;
	AnchorSpawnParameters.Name = MakeUniqueObjectName(
		World, ATargetPoint::StaticClass(),
		FName(TEXT("ABTSM7_E1UnionSiteRecoveryAnchor")));
	ATargetPoint* SiteRecoveryAnchor = E1Building != nullptr
		? World->SpawnActor<ATargetPoint>(
			ATargetPoint::StaticClass(), SiteRecoveryAnchorTransform,
			AnchorSpawnParameters)
		: nullptr;
	if (SiteRecoveryAnchor != nullptr)
	{
		SiteRecoveryAnchor->SetActorEnableCollision(false);
	}
	bool bAnchorHasCollision = false;
	if (SiteRecoveryAnchor != nullptr)
	{
		TInlineComponentArray<UPrimitiveComponent*> AnchorPrimitives;
		SiteRecoveryAnchor->GetComponents(AnchorPrimitives);
		for (const UPrimitiveComponent* Primitive : AnchorPrimitives)
		{
			bAnchorHasCollision |= Primitive != nullptr
				&& Primitive->GetCollisionEnabled()
					!= ECollisionEnabled::NoCollision;
		}
	}
	const bool bAnchorExact = SiteRecoveryAnchor != nullptr
		&& SiteRecoveryAnchor->GetOwner() == E1Building
		&& SiteRecoveryAnchor->GetActorTransform().Equals(
			SiteRecoveryAnchorTransform, 0.001)
		&& !bAnchorHasCollision;
	const bool bM3Bound = bAnchorExact && SatelliteRuntime != nullptr
		&& SatelliteRuntime->BindProductionE1BuildingModuleTarget(
			*SiteRecoveryAnchor, SiteRecoveryAnchorHalfExtentCM);
	if (SiteRecoveryAnchor != nullptr)
	{
		SiteRecoveryAnchor->Destroy();
	}
	if (bM3Bound)
	{
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][IntegrationV3][E1BrickUnionTarget] Bound Attempt=%d Target=%s Bricks=%d Geometry=%u AnchorHalfExtent=%s AnchorCollision=0 CapsDevicesExcluded=1 StandInRetired=1"),
			SatellitePracticeE1CrystalBindingLifecycle.GetAttemptCount(),
			*GetNameSafe(E1Building), OrderedUnion.OrderedBricks.Num(),
			OrderedUnion.ComputeOrderedGeometryHash(),
			*SiteRecoveryAnchorHalfExtentCM.ToCompactString());
		if (BeginJuryDemoFixedSixProductionChaosBatch())
		{
			SatellitePracticeE1CrystalBindingLifecycle.MarkBound();
			ClearSatellitePracticeE1CrystalTargetBindingTimer();
			return;
		}
		SatellitePracticeE1CrystalBindingLifecycle.MarkBindingRejected(
			TEXT("FixedSixChaosBatchStartRejected"));
		ClearSatellitePracticeE1CrystalTargetBindingTimer();
		FinishProductionFlow(false, TEXT("FixedSixChaosBatchStartRejected"));
		return;
	}
	SatellitePracticeE1CrystalBindingLifecycle.MarkBindingRejected(
		TEXT("SatelliteRuntimeBindingRejected"));
	ClearSatellitePracticeE1CrystalTargetBindingTimer();
	UE_LOG(LogABTSRuntime, Error,
		TEXT("[ABTS][IntegrationV3][E1BrickUnionTarget] Rejected Reason=SatelliteRuntimeBindingRejected Attempt=%d Target=%s Runtime=%s AnchorExact=%d Bricks=%d Geometry=%u"),
		SatellitePracticeE1CrystalBindingLifecycle.GetAttemptCount(),
		*GetNameSafe(E1Building), *GetNameSafe(SatelliteRuntime),
		bAnchorExact ? 1 : 0, OrderedUnion.OrderedBricks.Num(),
		OrderedUnion.ComputeOrderedGeometryHash());
	FinishProductionFlow(false, TEXT("SatelliteRuntimeBindingRejected"));
}

bool AABTSM7GameMode::BeginJuryDemoFixedSixProductionChaosBatch()
{
	JuryDemoFixedSixChaosBuildings.Reset();
	for (const FABTSM7TaskGraphBuildingDebugEntry& DebugEntry :
		TaskGraphBuildingDebugEntries)
	{
		if (AABTSM73StableBuildingActor* Building = DebugEntry.Building.Get();
			Building != nullptr
			&& Building->IsJuryDemoFixedSixStaticRegistrationAccepted())
		{
			JuryDemoFixedSixChaosBuildings.Add(Building);
		}
	}
	JuryDemoFixedSixChaosBuildings.Sort([](
		const TWeakObjectPtr<AABTSM73StableBuildingActor>& Left,
		const TWeakObjectPtr<AABTSM73StableBuildingActor>& Right)
	{
		const AABTSM73StableBuildingActor* LeftActor = Left.Get();
		const AABTSM73StableBuildingActor* RightActor = Right.Get();
		return static_cast<int32>(LeftActor != nullptr
			? LeftActor->GetJuryDemoFixedSixComplexityId()
			: EABTSM73BeamDemoBuilding::Custom)
			< static_cast<int32>(RightActor != nullptr
				? RightActor->GetJuryDemoFixedSixComplexityId()
				: EABTSM73BeamDemoBuilding::Custom);
	});
	if (JuryDemoFixedSixChaosBuildings.Num()
		!= FABTSJuryDemoFixedSixContract::ExpectedSiteCount)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7][FixedSixProductionChaos][BatchRejected]")
			TEXT(" Reason=BuildingCount Actual=%d Expected=%d"),
			JuryDemoFixedSixChaosBuildings.Num(),
			FABTSJuryDemoFixedSixContract::ExpectedSiteCount);
		return false;
	}
	for (int32 Index = 0; Index < JuryDemoFixedSixChaosBuildings.Num();
		++Index)
	{
		const AABTSM73StableBuildingActor* Building =
			JuryDemoFixedSixChaosBuildings[Index].Get();
		if (Building == nullptr
			|| static_cast<int32>(Building->GetJuryDemoFixedSixComplexityId())
				!= Index + 1)
		{
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7][FixedSixProductionChaos][BatchRejected]")
				TEXT(" Reason=ComplexityOrder Index=%d Actor=%s Complexity=%d"),
				Index, *GetNameSafe(Building),
				Building != nullptr
					? static_cast<int32>(
						Building->GetJuryDemoFixedSixComplexityId())
					: INDEX_NONE);
			return false;
		}
	}

	const bool bAsyncLoadingBeforeDrain = IsAsyncLoading();
	const double DrainStartSeconds = FPlatformTime::Seconds();
	FlushAsyncLoading();
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7][FixedSixProductionChaos][BackgroundDrain]")
		TEXT(" AsyncBefore=%d AsyncAfter=%d WallMS=%.3f Accepted=%d"),
		bAsyncLoadingBeforeDrain ? 1 : 0,
		IsAsyncLoading() ? 1 : 0,
		(FPlatformTime::Seconds() - DrainStartSeconds) * 1000.0,
		IsAsyncLoading() ? 0 : 1);
	if (IsAsyncLoading())
	{
		return false;
	}
	if (!EnterJuryDemoFixedSixProductionChaosFixedStep())
	{
		return false;
	}

	FString FailureReason;
	for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& WeakBuilding :
		JuryDemoFixedSixChaosBuildings)
	{
		AABTSM73StableBuildingActor* Building = WeakBuilding.Get();
		if (Building == nullptr
			|| !Building->PrepareJuryDemoFixedSixChaosValidation(
				980.0f, FailureReason))
		{
			if (FailureReason.IsEmpty())
			{
				FailureReason = TEXT("PreparationActorMissing");
			}
			for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& Cleanup :
				JuryDemoFixedSixChaosBuildings)
			{
				if (AABTSM73StableBuildingActor* CleanupActor = Cleanup.Get())
				{
					CleanupActor->RejectJuryDemoFixedSixChaosValidation(
						FailureReason);
				}
			}
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7][FixedSixProductionChaos][BatchRejected]")
				TEXT(" Phase=Prepare Reason=%s"), *FailureReason);
			RestoreJuryDemoFixedSixProductionChaosFixedStep();
			return false;
		}
	}
	LogProductionFlowSegment(TEXT("ChaosPrepared"));
	if (!ApplyJuryDemoFixedSixTerrainBuildingCollisionOverride(FailureReason))
	{
		if (FailureReason.IsEmpty())
		{
			FailureReason = TEXT("TerrainBuildingCollisionOverrideRejected");
		}
		for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& Cleanup :
			JuryDemoFixedSixChaosBuildings)
		{
			if (AABTSM73StableBuildingActor* CleanupActor = Cleanup.Get())
			{
				CleanupActor->RejectJuryDemoFixedSixChaosValidation(
					FailureReason);
			}
		}
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7][FixedSixProductionChaos][BatchRejected]")
			TEXT(" Phase=TerrainCollisionOverride Reason=%s"),
			*FailureReason);
		RestoreJuryDemoFixedSixTerrainBuildingCollisionOverride(
			TEXT("SetupFailed"));
		RestoreJuryDemoFixedSixProductionChaosFixedStep();
		return false;
	}

	for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& WeakBuilding :
		JuryDemoFixedSixChaosBuildings)
	{
		AABTSM73StableBuildingActor* Building = WeakBuilding.Get();
		if (Building != nullptr
			&& Building->MarkPreparedJuryDemoFixedSixChaosDeferred(FailureReason))
		{
			continue;
		}
		if (FailureReason.IsEmpty())
		{
			FailureReason = TEXT("DeferredStaticReadyActorMissing");
		}
		for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& Cleanup :
			JuryDemoFixedSixChaosBuildings)
		{
			if (AABTSM73StableBuildingActor* CleanupActor = Cleanup.Get())
			{
				CleanupActor->RejectJuryDemoFixedSixChaosValidation(
					FailureReason);
			}
		}
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7][FixedSixProductionChaos][BatchRejected]")
			TEXT(" Phase=DeferredStaticReady Reason=%s"), *FailureReason);
		RestoreJuryDemoFixedSixTerrainBuildingCollisionOverride(
			TEXT("ActivationFailed"));
		RestoreJuryDemoFixedSixProductionChaosFixedStep();
		return false;
	}
	JuryDemoFixedSixChaosActiveIndex = INDEX_NONE;
	bJuryDemoFixedSixChaosBatchActive = false;
	bJuryDemoFixedSixChaosBatchTerminal = true;
	RestoreJuryDemoFixedSixProductionChaosFixedStep();
	LogProductionFlowSegment(TEXT("ChaosDeferredReady"));
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7][FixedSixDeferredChaos][StartupAccepted]")
		TEXT(" Registered=6 StaticReady=6 ChaosDeferred=6 WorldReady=1")
		TEXT(" StartupChaosCertified=0 E1ExactUnion=54 StandInRetired=1")
		TEXT(" FirstHitActivation=AtomicPerBuilding SiteUniformGravity=1"));
	return true;
}

bool AABTSM7GameMode::EnterJuryDemoFixedSixProductionChaosFixedStep()
{
	if (bJuryDemoFixedSixChaosOwnsFixedStep
		|| bJuryDemoFixedSixChaosOwnsSolverDeterminism)
	{
		FPhysScene* ExistingScene = GetWorld() != nullptr
			? GetWorld()->GetPhysicsScene()
			: nullptr;
		Chaos::FPhysicsSolver* ExistingSolver = ExistingScene != nullptr
			? ExistingScene->GetSolver()
			: nullptr;
		return bJuryDemoFixedSixChaosOwnsFixedStep
			&& bJuryDemoFixedSixChaosOwnsSolverDeterminism
			&& ExistingSolver != nullptr
			&& ExistingSolver->IsDetemerministic()
			&& FApp::UseFixedTimeStep()
			&& FMath::IsNearlyEqual(
				FApp::GetFixedDeltaTime(),
				JuryDemoFixedSixProductionDeltaSeconds,
				UE_DOUBLE_SMALL_NUMBER);
	}
	FPhysScene* PhysicsScene = GetWorld() != nullptr
		? GetWorld()->GetPhysicsScene()
		: nullptr;
	Chaos::FPhysicsSolver* PhysicsSolver = PhysicsScene != nullptr
		? PhysicsScene->GetSolver()
		: nullptr;
	if (PhysicsSolver == nullptr)
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7][FixedSixProductionChaos][SolverDeterminism]")
			TEXT(" Entered=0 Reason=SolverMissing"));
		return false;
	}
	bJuryDemoFixedSixPreviousSolverDeterminism =
		PhysicsSolver->IsDetemerministic();
	PhysicsSolver->SetIsDeterministic(true);
	bJuryDemoFixedSixChaosOwnsSolverDeterminism = true;
	if (!PhysicsSolver->IsDetemerministic())
	{
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7][FixedSixProductionChaos][SolverDeterminism]")
			TEXT(" Entered=0 Reason=EnhancedDeterminismRejected"));
		RestoreJuryDemoFixedSixProductionChaosFixedStep();
		return false;
	}

	bJuryDemoFixedSixPreviousUseFixedTimeStep = FApp::UseFixedTimeStep();
	JuryDemoFixedSixPreviousFixedDeltaSeconds = FApp::GetFixedDeltaTime();
	FApp::SetFixedDeltaTime(JuryDemoFixedSixProductionDeltaSeconds);
	FApp::SetUseFixedTimeStep(true);
	bJuryDemoFixedSixChaosOwnsFixedStep = true;
	const bool bExact = FApp::UseFixedTimeStep()
		&& FMath::IsNearlyEqual(
			FApp::GetFixedDeltaTime(),
			JuryDemoFixedSixProductionDeltaSeconds,
			UE_DOUBLE_SMALL_NUMBER);
	const FString Evidence = FString::Printf(
		TEXT("[ABTS][M7][FixedSixProductionChaos][FixedStep]")
		TEXT(" Entered=%d SimulationHz=60 SimulationDT=%.9f")
		TEXT(" PreviousUseFixed=%d PreviousDT=%.9f")
		TEXT(" EnhancedDeterminism=1 PreviousDeterminism=%d"),
		bExact ? 1 : 0, FApp::GetFixedDeltaTime(),
		bJuryDemoFixedSixPreviousUseFixedTimeStep ? 1 : 0,
		JuryDemoFixedSixPreviousFixedDeltaSeconds,
		bJuryDemoFixedSixPreviousSolverDeterminism ? 1 : 0);
	if (bExact)
	{
		UE_LOG(LogABTSRuntime, Display, TEXT("%s"), *Evidence);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error, TEXT("%s"), *Evidence);
	}
	if (!bExact)
	{
		RestoreJuryDemoFixedSixProductionChaosFixedStep();
	}
	return bExact;
}

void AABTSM7GameMode::RestoreJuryDemoFixedSixProductionChaosFixedStep()
{
	if (!bJuryDemoFixedSixChaosOwnsFixedStep
		&& !bJuryDemoFixedSixChaosOwnsSolverDeterminism)
	{
		return;
	}
	if (bJuryDemoFixedSixChaosOwnsFixedStep)
	{
		FApp::SetFixedDeltaTime(JuryDemoFixedSixPreviousFixedDeltaSeconds);
		FApp::SetUseFixedTimeStep(
			bJuryDemoFixedSixPreviousUseFixedTimeStep);
	}
	bool bSolverRestored = !bJuryDemoFixedSixChaosOwnsSolverDeterminism;
	if (bJuryDemoFixedSixChaosOwnsSolverDeterminism)
	{
		FPhysScene* PhysicsScene = GetWorld() != nullptr
			? GetWorld()->GetPhysicsScene()
			: nullptr;
		Chaos::FPhysicsSolver* PhysicsSolver = PhysicsScene != nullptr
			? PhysicsScene->GetSolver()
			: nullptr;
		if (PhysicsSolver != nullptr)
		{
			PhysicsSolver->SetIsDeterministic(
				bJuryDemoFixedSixPreviousSolverDeterminism);
			bSolverRestored = PhysicsSolver->IsDetemerministic()
				== bJuryDemoFixedSixPreviousSolverDeterminism;
		}
	}
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7][FixedSixProductionChaos][FixedStep]")
		TEXT(" Restored=1 UseFixed=%d FixedDT=%.9f")
		TEXT(" SolverRestored=%d PreviousDeterminism=%d"),
		FApp::UseFixedTimeStep() ? 1 : 0, FApp::GetFixedDeltaTime(),
		bSolverRestored ? 1 : 0,
		bJuryDemoFixedSixPreviousSolverDeterminism ? 1 : 0);
	bJuryDemoFixedSixChaosOwnsFixedStep = false;
	bJuryDemoFixedSixChaosOwnsSolverDeterminism = false;
	bJuryDemoFixedSixPreviousUseFixedTimeStep = false;
	bJuryDemoFixedSixPreviousSolverDeterminism = false;
	JuryDemoFixedSixPreviousFixedDeltaSeconds = 0.0;
}

bool AABTSM7GameMode::ApplyJuryDemoFixedSixTerrainBuildingCollisionOverride(
	FString& OutError)
{
	OutError.Reset();
	if (JuryDemoFixedSixProductionGenerationToken == 0)
	{
		OutError = TEXT("TerrainCollisionGenerationTokenInvalid");
		return false;
	}
	for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& WeakBuilding :
		JuryDemoFixedSixChaosBuildings)
	{
		const AABTSM73StableBuildingActor* Building = WeakBuilding.Get();
		if (Building == nullptr
			|| !Building->
				IsJuryDemoFixedSixFrozenTangentSupportBlockingBuildingChannel())
		{
			OutError = TEXT("FrozenTangentPadBuildingResponseNotBlock");
			return false;
		}
	}

	if (bJuryDemoFixedSixOwnsTerrainBuildingCollisionOverride)
	{
		if (JuryDemoFixedSixTerrainOverrideGenerationToken
			!= JuryDemoFixedSixProductionGenerationToken)
		{
			OutError = TEXT("TerrainCollisionOverrideStaleGeneration");
			return false;
		}
		for (const TWeakObjectPtr<UProceduralMeshComponent>& WeakSurface :
			JuryDemoFixedSixTerrainSurfaces)
		{
			const UProceduralMeshComponent* Surface = WeakSurface.Get();
			if (Surface == nullptr
				|| Surface->GetCollisionResponseToChannel(
					ABTSDeveloperObstacleChannel) != ECR_Ignore)
			{
				OutError = TEXT("TerrainCollisionOverrideIdempotencyDrift");
				return false;
			}
		}
		return true;
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutError = TEXT("TerrainCollisionWorldMissing");
		return false;
	}
	TArray<UProceduralMeshComponent*> Surfaces;
	for (TActorIterator<AABTSM2Planet> It(World); It; ++It)
	{
		AABTSM2Planet* Planet = *It;
		if (IsValid(Planet) && !Planet->IsActorBeingDestroyed()
			&& IsValid(Planet->ContinuousSurface))
		{
			Surfaces.AddUnique(Planet->ContinuousSurface);
		}
	}
	Surfaces.Sort([](
		const UProceduralMeshComponent& Left,
		const UProceduralMeshComponent& Right)
	{
		return Left.GetPathName() < Right.GetPathName();
	});
	if (Surfaces.Num() < 2)
	{
		OutError = TEXT("TerrainCollisionPrimaryOrSatelliteSurfaceMissing");
		return false;
	}

	TArray<TEnumAsByte<ECollisionResponse>> PreviousBuildingResponses;
	TArray<TEnumAsByte<ECollisionResponse>> PreviousPawnResponses;
	TArray<TEnumAsByte<ECollisionResponse>> PreviousWorldStaticResponses;
	TArray<TEnumAsByte<ECollisionResponse>> PreviousPhysicsBodyResponses;
	for (const UProceduralMeshComponent* Surface : Surfaces)
	{
		if (Surface == nullptr
			|| Surface->GetCollisionEnabled()
				!= ECollisionEnabled::QueryAndPhysics
			|| Surface->GetCollisionObjectType() != ECC_WorldStatic)
		{
			OutError = TEXT("TerrainCollisionSurfaceIdentityInvalid");
			return false;
		}
		PreviousBuildingResponses.Add(
			Surface->GetCollisionResponseToChannel(
				ABTSDeveloperObstacleChannel));
		PreviousPawnResponses.Add(
			Surface->GetCollisionResponseToChannel(ECC_Pawn));
		PreviousWorldStaticResponses.Add(
			Surface->GetCollisionResponseToChannel(ECC_WorldStatic));
		PreviousPhysicsBodyResponses.Add(
			Surface->GetCollisionResponseToChannel(ECC_PhysicsBody));
	}

	JuryDemoFixedSixTerrainSurfaces.Reset(Surfaces.Num());
	JuryDemoFixedSixTerrainPreviousBuildingResponses =
		PreviousBuildingResponses;
	for (UProceduralMeshComponent* Surface : Surfaces)
	{
		JuryDemoFixedSixTerrainSurfaces.Add(Surface);
	}
	bJuryDemoFixedSixOwnsTerrainBuildingCollisionOverride = true;
	JuryDemoFixedSixTerrainOverrideGenerationToken =
		JuryDemoFixedSixProductionGenerationToken;
	JuryDemoFixedSixTerrainCollisionRestoredGenerationToken = 0;
	for (UProceduralMeshComponent* Surface : Surfaces)
	{
		Surface->SetCollisionResponseToChannel(
			ABTSDeveloperObstacleChannel, ECR_Ignore);
	}
	for (int32 Index = 0; Index < Surfaces.Num(); ++Index)
	{
		const UProceduralMeshComponent* Surface = Surfaces[Index];
		if (Surface->GetCollisionResponseToChannel(
				ABTSDeveloperObstacleChannel) != ECR_Ignore
			|| Surface->GetCollisionResponseToChannel(ECC_Pawn)
				!= PreviousPawnResponses[Index]
			|| Surface->GetCollisionResponseToChannel(ECC_WorldStatic)
				!= PreviousWorldStaticResponses[Index]
			|| Surface->GetCollisionResponseToChannel(ECC_PhysicsBody)
				!= PreviousPhysicsBodyResponses[Index])
		{
			OutError = TEXT("TerrainCollisionResponseMutationRejected");
			RestoreJuryDemoFixedSixTerrainBuildingCollisionOverride(
				TEXT("ApplyFailed"));
			return false;
		}
	}
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7][FixedSixProductionChaos][CollisionAuthority]")
		TEXT(" Generation=%llu Surfaces=%d")
		TEXT(" TerrainBuildingResponse=Ignore PadsBuildingResponse=Block")
		TEXT(" PawnResponseUnchanged=1 WorldStaticResponseUnchanged=1")
		TEXT(" PhysicsBodyResponseUnchanged=1 Accepted=1"),
		JuryDemoFixedSixTerrainOverrideGenerationToken, Surfaces.Num());
	return true;
}

void AABTSM7GameMode::RestoreJuryDemoFixedSixTerrainBuildingCollisionOverride(
	const TCHAR* Reason)
{
	if (!bJuryDemoFixedSixOwnsTerrainBuildingCollisionOverride)
	{
		return;
	}
	bool bRestored = JuryDemoFixedSixTerrainSurfaces.Num()
		== JuryDemoFixedSixTerrainPreviousBuildingResponses.Num();
	int32 LiveSurfaceCount = 0;
	for (int32 Index = 0;
		Index < JuryDemoFixedSixTerrainSurfaces.Num(); ++Index)
	{
		UProceduralMeshComponent* Surface =
			JuryDemoFixedSixTerrainSurfaces[Index].Get();
		if (Surface == nullptr)
		{
			continue;
		}
		++LiveSurfaceCount;
		if (!JuryDemoFixedSixTerrainPreviousBuildingResponses.IsValidIndex(Index))
		{
			bRestored = false;
			continue;
		}
		Surface->SetCollisionResponseToChannel(
			ABTSDeveloperObstacleChannel,
			JuryDemoFixedSixTerrainPreviousBuildingResponses[Index]);
		bRestored = bRestored
			&& Surface->GetCollisionResponseToChannel(
				ABTSDeveloperObstacleChannel)
				== JuryDemoFixedSixTerrainPreviousBuildingResponses[Index];
	}
	const FString RestoreEvidence = FString::Printf(
		TEXT("[ABTS][M7][FixedSixProductionChaos][CollisionAuthority]")
		TEXT(" Generation=%llu Reason=%s Surfaces=%d LiveSurfaces=%d")
		TEXT(" Restored=%d"),
		JuryDemoFixedSixTerrainOverrideGenerationToken,
		Reason != nullptr ? Reason : TEXT("Unspecified"),
		JuryDemoFixedSixTerrainSurfaces.Num(), LiveSurfaceCount,
		bRestored ? 1 : 0);
	if (bRestored)
	{
		UE_LOG(LogABTSRuntime, Display, TEXT("%s"), *RestoreEvidence);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error, TEXT("%s"), *RestoreEvidence);
	}
	JuryDemoFixedSixTerrainSurfaces.Reset();
	JuryDemoFixedSixTerrainPreviousBuildingResponses.Reset();
	bJuryDemoFixedSixOwnsTerrainBuildingCollisionOverride = false;
	JuryDemoFixedSixTerrainOverrideGenerationToken = 0;
}

bool AABTSM7GameMode::
RestoreJuryDemoFixedSixTerrainCollisionForDeferredFirstHit(
	FString& OutError)
{
	OutError.Reset();
	if (!bJuryDemoFixedSixOwnsTerrainBuildingCollisionOverride
		&& JuryDemoFixedSixTerrainCollisionRestoredGenerationToken
			== JuryDemoFixedSixProductionGenerationToken
		&& JuryDemoFixedSixProductionGenerationToken != 0)
	{
		// The terrain override is global to the six-building generation.  The
		// first building hit restores it; later buildings must verify and reuse
		// that restored authority instead of rejecting their own first hit.
		UWorld* World = GetWorld();
		int32 VerifiedSurfaceCount = 0;
		if (World == nullptr)
		{
			OutError = TEXT("DeferredFirstHitTerrainCollisionWorldMissing");
			return false;
		}
		for (TActorIterator<AABTSM2Planet> It(World); It; ++It)
		{
			const AABTSM2Planet* Planet = *It;
			const UProceduralMeshComponent* Surface = IsValid(Planet)
				? Planet->ContinuousSurface.Get()
				: nullptr;
			if (Surface == nullptr) continue;
			++VerifiedSurfaceCount;
			if (Surface->GetCollisionEnabled()
					!= ECollisionEnabled::QueryAndPhysics
				|| Surface->GetCollisionObjectType() != ECC_WorldStatic
				|| Surface->GetCollisionResponseToChannel(
					ABTSDeveloperObstacleChannel) != ECR_Block)
			{
				OutError = TEXT("DeferredFirstHitRestoredTerrainCollisionDrift");
				return false;
			}
		}
		if (VerifiedSurfaceCount < 2)
		{
			OutError = TEXT("DeferredFirstHitRestoredTerrainSurfaceMissing");
			return false;
		}
		for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& WeakBuilding :
			JuryDemoFixedSixChaosBuildings)
		{
			const AABTSM73StableBuildingActor* Building = WeakBuilding.Get();
			if (Building == nullptr || !Building
				->IsJuryDemoFixedSixFrozenTangentSupportBlockingBuildingChannel())
			{
				OutError = TEXT("DeferredFirstHitRestoredPadCollisionDrift");
				return false;
			}
		}
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7][FixedSixDeferredChaos][CollisionAuthority]")
			TEXT(" Generation=%llu TerrainBuildingResponse=Block")
			TEXT(" PadsBuildingResponse=Block FirstHitPromotion=1")
			TEXT(" IdempotentReuse=1 Surfaces=%d Accepted=1"),
			JuryDemoFixedSixProductionGenerationToken,
			VerifiedSurfaceCount);
		return true;
	}
	if (!bJuryDemoFixedSixOwnsTerrainBuildingCollisionOverride
		|| JuryDemoFixedSixTerrainOverrideGenerationToken == 0
		|| JuryDemoFixedSixTerrainOverrideGenerationToken
			!= JuryDemoFixedSixProductionGenerationToken
		|| JuryDemoFixedSixTerrainSurfaces.IsEmpty()
		|| JuryDemoFixedSixTerrainSurfaces.Num()
			!= JuryDemoFixedSixTerrainPreviousBuildingResponses.Num())
	{
		OutError = TEXT("DeferredFirstHitTerrainCollisionOverrideUnavailable");
		return false;
	}
	TArray<TWeakObjectPtr<UProceduralMeshComponent>> Surfaces =
		JuryDemoFixedSixTerrainSurfaces;
	for (int32 Index = 0; Index < Surfaces.Num(); ++Index)
	{
		const UProceduralMeshComponent* Surface = Surfaces[Index].Get();
		if (Surface == nullptr
			|| JuryDemoFixedSixTerrainPreviousBuildingResponses[Index]
				!= ECR_Block
			|| Surface->GetCollisionEnabled()
				!= ECollisionEnabled::QueryAndPhysics
			|| Surface->GetCollisionObjectType() != ECC_WorldStatic)
		{
			OutError = TEXT("DeferredFirstHitTerrainCollisionBaselineInvalid");
			return false;
		}
	}
	for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& WeakBuilding :
		JuryDemoFixedSixChaosBuildings)
	{
		const AABTSM73StableBuildingActor* Building = WeakBuilding.Get();
		if (Building == nullptr || !Building
			->IsJuryDemoFixedSixFrozenTangentSupportBlockingBuildingChannel())
		{
			OutError = TEXT("DeferredFirstHitFrozenPadCollisionInvalid");
			return false;
		}
	}
	const uint64 GenerationToken = JuryDemoFixedSixTerrainOverrideGenerationToken;
	RestoreJuryDemoFixedSixTerrainBuildingCollisionOverride(
		TEXT("DeferredFirstHitBeforePromotion"));
	if (bJuryDemoFixedSixOwnsTerrainBuildingCollisionOverride)
	{
		OutError = TEXT("DeferredFirstHitTerrainCollisionRestoreIncomplete");
		return false;
	}
	for (const TWeakObjectPtr<UProceduralMeshComponent>& WeakSurface : Surfaces)
	{
		const UProceduralMeshComponent* Surface = WeakSurface.Get();
		if (Surface == nullptr
			|| Surface->GetCollisionEnabled()
				!= ECollisionEnabled::QueryAndPhysics
			|| Surface->GetCollisionObjectType() != ECC_WorldStatic
			|| Surface->GetCollisionResponseToChannel(
				ABTSDeveloperObstacleChannel) != ECR_Block)
		{
			OutError = TEXT("DeferredFirstHitTerrainCollisionBlockVerificationFailed");
			return false;
		}
	}
	JuryDemoFixedSixTerrainCollisionRestoredGenerationToken = GenerationToken;
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7][FixedSixDeferredChaos][CollisionAuthority]")
		TEXT(" Generation=%llu TerrainBuildingResponse=Block")
		TEXT(" PadsBuildingResponse=Block FirstHitPromotion=1 Accepted=1"),
		GenerationToken);
	return true;
}

void AABTSM7GameMode::UpdateJuryDemoFixedSixProductionChaosBatch()
{
	if (!bJuryDemoFixedSixChaosBatchActive
		|| bJuryDemoFixedSixChaosBatchTerminal)
	{
		return;
	}
	if (!JuryDemoFixedSixChaosBuildings.IsValidIndex(
		JuryDemoFixedSixChaosActiveIndex))
	{
		bJuryDemoFixedSixChaosBatchActive = false;
		bJuryDemoFixedSixChaosBatchTerminal = true;
		RestoreJuryDemoFixedSixProductionChaosFixedStep();
		FinishProductionFlow(false, TEXT("FixedSixChaosActiveIndexInvalid"));
		return;
	}
	AABTSM73StableBuildingActor* ActiveBuilding =
		JuryDemoFixedSixChaosBuildings[JuryDemoFixedSixChaosActiveIndex].Get();
	if (ActiveBuilding == nullptr)
	{
		bJuryDemoFixedSixChaosBatchActive = false;
		bJuryDemoFixedSixChaosBatchTerminal = true;
		RestoreJuryDemoFixedSixProductionChaosFixedStep();
		FinishProductionFlow(false, TEXT("FixedSixChaosActorLost"));
		return;
	}
	const EABTSM73IdleValidationState ActiveState =
		ActiveBuilding->GetIdleValidationState();
	if (ActiveState == EABTSM73IdleValidationState::Pending
		|| ActiveState == EABTSM73IdleValidationState::Running)
	{
		return;
	}

	FABTSM73JuryDemoFixedSixChaosResult ActiveResult;
	const bool bActiveAccepted =
		ActiveBuilding->CopyJuryDemoFixedSixChaosResult(ActiveResult)
		&& ActiveResult.bAccepted;
	const int32 NextIndex = JuryDemoFixedSixChaosActiveIndex + 1;
	if (bActiveAccepted
		&& JuryDemoFixedSixChaosBuildings.IsValidIndex(NextIndex))
	{
		AABTSM73StableBuildingActor* NextBuilding =
			JuryDemoFixedSixChaosBuildings[NextIndex].Get();
		FString ActivationError;
		if (NextBuilding == nullptr
			|| !NextBuilding->ActivatePreparedJuryDemoFixedSixChaosValidation(
				ActivationError))
		{
			if (ActivationError.IsEmpty())
			{
				ActivationError = TEXT("ActivationActorMissing");
			}
			for (int32 CleanupIndex = NextIndex;
				CleanupIndex < JuryDemoFixedSixChaosBuildings.Num();
				++CleanupIndex)
			{
				if (AABTSM73StableBuildingActor* Cleanup =
					JuryDemoFixedSixChaosBuildings[CleanupIndex].Get())
				{
					Cleanup->RejectJuryDemoFixedSixChaosValidation(
						ActivationError);
				}
			}
			bJuryDemoFixedSixChaosBatchActive = false;
			bJuryDemoFixedSixChaosBatchTerminal = true;
			RestoreJuryDemoFixedSixProductionChaosFixedStep();
			FinishProductionFlow(false, ActivationError);
			return;
		}
		JuryDemoFixedSixChaosActiveIndex = NextIndex;
		UE_LOG(LogABTSRuntime, Display,
			TEXT("[ABTS][M7][FixedSixProductionChaos][BatchAdvance]")
			TEXT(" Completed=E%d Activated=E%d ActivationBarrier=1"),
			NextIndex, NextIndex + 1);
		return;
	}
	if (!bActiveAccepted)
	{
		for (int32 CleanupIndex = NextIndex;
			CleanupIndex < JuryDemoFixedSixChaosBuildings.Num();
			++CleanupIndex)
		{
			if (AABTSM73StableBuildingActor* Cleanup =
				JuryDemoFixedSixChaosBuildings[CleanupIndex].Get())
			{
				Cleanup->RejectJuryDemoFixedSixChaosValidation(
					TEXT("FixedSixChaosPriorBuildingRejected"));
			}
		}
	}

	bJuryDemoFixedSixChaosBatchActive = false;
	bJuryDemoFixedSixChaosBatchTerminal = true;
	JuryDemoFixedSixChaosActiveIndex = INDEX_NONE;
	bool bAllAccepted = true;
	uint32 AggregateResultHash = 0;
	for (int32 Index = 0; Index < JuryDemoFixedSixChaosBuildings.Num();
		++Index)
	{
		const AABTSM73StableBuildingActor* Building =
			JuryDemoFixedSixChaosBuildings[Index].Get();
		FABTSM73JuryDemoFixedSixChaosResult Result;
		const bool bHasResult = Building != nullptr
			&& Building->CopyJuryDemoFixedSixChaosResult(Result);
		bAllAccepted = bAllAccepted && bHasResult && Result.bAccepted;
		AggregateResultHash = FCrc::MemCrc32(
			&Result.ResultHash, sizeof(Result.ResultHash), AggregateResultHash);
		UE_LOG(LogABTSRuntime,
			Log,
			TEXT("[ABTS][M7][FixedSixProductionChaos][StableResult]")
			TEXT(" Order=%d Complexity=E%d Entry=%s Candidate=%u Result=%u")
			TEXT(" Accepted=%d Final=%.3f/%.3f/%.3f Peak=%.3f/%.3f/%.3f")
			TEXT(" Awake=%d Internal=%.3f Wall=%.3f Visible=%d Bodies=%d Assembly=%llu"),
			Index + 1, static_cast<int32>(Result.ComplexityId),
			*Result.ManifestEntryId.ToString(), Result.CandidateHash,
			Result.ResultHash, bHasResult && Result.bAccepted ? 1 : 0,
			Result.FinalPlanarDriftCM, Result.FinalSettlementCM,
			Result.FinalRotationDegrees, Result.PeakPlanarDriftCM,
			Result.PeakSettlementCM, Result.PeakRotationDegrees,
			Result.FinalAwakeBodyCount, Result.InternalSeconds,
			Result.WallSeconds, Result.VisibleModuleCount,
			Result.PhysicsBodyCount, Result.PhysicsAssemblyHash);
	}
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7][FixedSixProductionChaos][BatchResult]")
		TEXT(" Accepted=%d Buildings=6 StableOrder=E1,E2,E3,E4,E5,E6")
		TEXT(" AggregateResult=%u BuildingValidation=%s"),
		bAllAccepted ? 1 : 0, AggregateResultHash,
		bAllAccepted ? TEXT("Accepted") : TEXT("Rejected"));
	LogProductionFlowSegment(
		bAllAccepted ? TEXT("ChaosAccepted") : TEXT("ChaosRejected"));
	RestoreJuryDemoFixedSixProductionChaosFixedStep();
	if (!bAllAccepted)
	{
		FinishProductionFlow(false, TEXT("FixedSixChaosHardGateRejected"));
	}
}

void AABTSM7GameMode::UpdateProductionFlowTiming(
	const float DeltaSeconds)
{
	(void)DeltaSeconds;
	if (!bProductionFlowTimingActive || bProductionFlowTerminal)
	{
		return;
	}
	const FCPUTime CPUTime = FPlatformTime::GetCPUTime();
	const double NowSeconds = FPlatformTime::Seconds();
	const double EffectiveWallSeconds = FMath::Max(
		0.0, NowSeconds - ProductionFlowLastCPUSampleWallSeconds);
	ProductionFlowLastCPUSampleWallSeconds = NowSeconds;
	ProductionFlowAccumulatedTickWallSeconds += EffectiveWallSeconds;
	ProductionFlowEstimatedCPUSeconds += EffectiveWallSeconds
		* FMath::Max(0.0f, CPUTime.CPUTimePctRelative) / 100.0f;
	AABTSM6SlingshotSystem* SlingshotSystem =
		ProductionFlowSlingshotSystem.Get();
	if (SlingshotSystem == nullptr)
	{
		FinishProductionFlow(false, TEXT("StartupPhysicsAuthorityLost"));
		return;
	}
	if (SlingshotSystem->HasStartupPhysicsWarmupFailed())
	{
		FinishProductionFlow(false, TEXT("M6StartupPhysicsFailed"));
		return;
	}
	if (bJuryDemoFixedSixChaosBatchTerminal
		&& SlingshotSystem->IsStartupPhysicsWarmupComplete())
	{
		FinishProductionFlow(true, TEXT("M6StartupPhysicsReady"));
	}
}

void AABTSM7GameMode::LogProductionFlowSegment(const TCHAR* Segment)
{
	if (!bProductionFlowTimingActive
		|| Segment == nullptr
		|| ProductionFlowStartWallSeconds <= 0.0)
	{
		return;
	}
	const double NowSeconds = FPlatformTime::Seconds();
	const FCPUTime CPUTime = FPlatformTime::GetCPUTime();
	const double SegmentWallSeconds = FMath::Max(
		0.0, NowSeconds - ProductionFlowLastSegmentWallSeconds);
	const double TickCoveredSeconds = FMath::Max(
		0.0, ProductionFlowAccumulatedTickWallSeconds
			- ProductionFlowLastSegmentTickWallSeconds);
	const double SynchronousWallSeconds = FMath::Max(
		0.0, SegmentWallSeconds - TickCoveredSeconds);
	ProductionFlowEstimatedCPUSeconds += SynchronousWallSeconds
		* FMath::Max(0.0f, CPUTime.CPUTimePctRelative) / 100.0f;
	UE_LOG(LogABTSRuntime, Display,
		TEXT("[ABTS][M7][FlowTiming][P0]")
		TEXT(" Segment=%s WallFromStartMS=%.3f WallSegmentMS=%.3f")
		TEXT(" CPUCoreSecondsEstimate=%.6f CPUSegmentEstimate=%.6f")
		TEXT(" CPUProcessPct=%.3f CPUCorePct=%.3f"),
		Segment,
		(NowSeconds - ProductionFlowStartWallSeconds) * 1000.0,
		SegmentWallSeconds * 1000.0,
		ProductionFlowEstimatedCPUSeconds,
		ProductionFlowEstimatedCPUSeconds
			- ProductionFlowLastSegmentCPUSeconds,
		CPUTime.CPUTimePct,
		CPUTime.CPUTimePctRelative);
	ProductionFlowLastSegmentWallSeconds = NowSeconds;
	ProductionFlowLastSegmentCPUSeconds =
		ProductionFlowEstimatedCPUSeconds;
	ProductionFlowLastSegmentTickWallSeconds =
		ProductionFlowAccumulatedTickWallSeconds;
	ProductionFlowLastCPUSampleWallSeconds = NowSeconds;
}

void AABTSM7GameMode::FinishProductionFlow(
	const bool bReady,
	const FString& Reason)
{
	RestoreJuryDemoFixedSixProductionChaosFixedStep();
	if (!bReady)
	{
		RestoreJuryDemoFixedSixTerrainBuildingCollisionOverride(
			TEXT("FlowFailed"));
	}
	if (!bProductionFlowTimingActive || bProductionFlowTerminal)
	{
		return;
	}
	LogProductionFlowSegment(bReady ? TEXT("FlowReady") : TEXT("FlowFailed"));
	bProductionFlowTerminal = true;
	bProductionFlowTimingActive = false;
	const FString TerminalMessage = FString::Printf(
		TEXT("[ABTS][M7][FlowTiming][P0][Terminal]")
		TEXT(" Ready=%d Reason=%s WallTotalMS=%.3f CPUCoreSecondsEstimate=%.6f"),
		bReady ? 1 : 0, *Reason,
		(FPlatformTime::Seconds() - ProductionFlowStartWallSeconds) * 1000.0,
		ProductionFlowEstimatedCPUSeconds);
	if (bReady)
	{
		UE_LOG(LogABTSRuntime, Display, TEXT("%s"), *TerminalMessage);
	}
	else
	{
		UE_LOG(LogABTSRuntime, Error, TEXT("%s"), *TerminalMessage);
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("ABTSM7ExitOnFlowReady")))
	{
		FGenericPlatformMisc::RequestExitWithStatus(
			false, bReady ? 0 : 2,
			bReady ? TEXT("M7FlowReady") : TEXT("M7FlowFailed"));
	}
}

void AABTSM7GameMode::DrawTaskGraphPositionDebug()
{
	if (UE_BUILD_SHIPPING)
	{
		return;
	}
	AABTSM3Planet* Planet = TaskGraphDebugPlanet.Get();
	ACharacter* Player = TaskGraphDebugPlayer.Get();
	if (Planet == nullptr || Player == nullptr || !Planet->IsPlanetReady() || GEngine == nullptr) return;

	const FVector PlanetCenter = Planet->GetPlanetCenterWorld();
	float PlayerLatitude = 0.0f;
	float PlayerLongitude = 0.0f;
	GetLatitudeLongitudeDegrees(Player->GetActorLocation(), PlanetCenter, PlayerLatitude, PlayerLongitude);
	GEngine->AddOnScreenDebugMessage(7100, 0.0f, FColor::Cyan,
		FString::Printf(TEXT("[TaskGraph 位置] 玩家  纬度 %+07.2f°  经度 %+08.2f°"), PlayerLatitude, PlayerLongitude),
		true, FVector2D(TaskGraphPositionDebugTextScale));

	int32 DebugIndex = 0;
	for (const FABTSM7TaskGraphBuildingDebugEntry& Entry : TaskGraphBuildingDebugEntries)
	{
		AABTSM73StableBuildingActor* Building = Entry.Building.Get();
		if (Building == nullptr) continue;
		float Latitude = 0.0f;
		float Longitude = 0.0f;
		GetLatitudeLongitudeDegrees(Building->GetActorLocation(), PlanetCenter, Latitude, Longitude);
		const FString Text = FString::Printf(
			TEXT("建筑%d  Task=%d Cell=%d  纬度 %+07.2f°  经度 %+08.2f°"),
			DebugIndex, Entry.TaskId, Entry.CellId, Latitude, Longitude);
		GEngine->AddOnScreenDebugMessage(7101 + DebugIndex, 0.0f, FColor::Yellow, Text, true, FVector2D(TaskGraphPositionDebugTextScale));
		if (bDrawTaskGraphBuildingWorldLabels)
		{
			const FVector RadialUp = (Building->GetActorLocation() - PlanetCenter).GetSafeNormal();
			DrawDebugString(GetWorld(), Building->GetActorLocation() + RadialUp * TaskGraphBuildingWorldLabelHeightCM,
				FString::Printf(TEXT("B%d  Task=%d\\nLat %+0.2f  Lon %+0.2f"), DebugIndex, Entry.TaskId, Latitude, Longitude),
				nullptr, FColor::Yellow, 0.0f, true, TaskGraphPositionDebugTextScale);
		}
		++DebugIndex;
	}
}

void AABTSM7GameMode::OnInitialPlayerPlaced(ACharacter& Character, const FTransform& SpawnTransform, const int32 SpawnCellId)
{
	ClearSatellitePracticeE1CrystalTargetBindingTimer();
	SatellitePracticeE1CrystalBindingLifecycle.Cancel();
	SatellitePracticeE1CrystalBindingLifecycle =
		FABTSM7SatellitePracticeE1CrystalBindingLifecycle();
	RestoreJuryDemoFixedSixTerrainBuildingCollisionOverride(
		TEXT("GenerationRetry"));
	++JuryDemoFixedSixProductionGenerationToken;
	if (JuryDemoFixedSixProductionGenerationToken == 0)
	{
		++JuryDemoFixedSixProductionGenerationToken;
	}
	ProductionFlowStartWallSeconds = FPlatformTime::Seconds();
	ProductionFlowLastSegmentWallSeconds = ProductionFlowStartWallSeconds;
	ProductionFlowEstimatedCPUSeconds = 0.0;
	ProductionFlowLastSegmentCPUSeconds = 0.0;
	ProductionFlowAccumulatedTickWallSeconds = 0.0;
	ProductionFlowLastSegmentTickWallSeconds = 0.0;
	ProductionFlowLastCPUSampleWallSeconds =
		ProductionFlowStartWallSeconds;
	bProductionFlowTimingActive = true;
	bProductionFlowTerminal = false;
	bJuryDemoFixedSixChaosBatchActive = false;
	bJuryDemoFixedSixChaosBatchTerminal = false;
	JuryDemoFixedSixChaosActiveIndex = INDEX_NONE;
	LogProductionFlowSegment(TEXT("FlowStart"));
	Super::OnInitialPlayerPlaced(Character, SpawnTransform, SpawnCellId);
	AABTSM6SlingshotSystem* SlingshotSystem = GetRuntimeSlingshotSystem();
	AABTSM3Planet* Planet = nullptr;
	for (TActorIterator<AABTSM3Planet> It(GetWorld()); It; ++It)
	{
		if (It->IsPlanetReady())
		{
			Planet = *It;
			break;
		}
	}
	const bool bUseLegacySingleBuildingTest =
		!bSpawnTaskGraphBuildings && bSpawnStableBuildingAtFirstAnchor;
	const bool bNeedsBuildingContract =
		(bSpawnTaskGraphBuildings && MaxTaskGraphBuildings > 0)
		|| bUseLegacySingleBuildingTest;
	FABTSBuildingGenerationContract BuildingContract;
	const bool bBuildingContractReady =
		Planet != nullptr
		&& Planet->TryExportBuildingGenerationContract(BuildingContract);
	const bool bFixedSixSnapshotPresent = bBuildingContractReady
		&& !BuildingContract.JuryDemoFixedSix.IsEmpty();
	if (bFixedSixSnapshotPresent)
	{
		ProductionFlowSlingshotSystem = SlingshotSystem;
		LogProductionFlowSegment(TEXT("ContractReady"));
	}
	else
	{
		bProductionFlowTimingActive = false;
	}
	const int32 ExpectedRequiredBuildingCount =
		bFixedSixSnapshotPresent
		? FABTSJuryDemoFixedSixContract::ExpectedSiteCount
		: bSpawnTaskGraphBuildings && bBuildingContractReady
		? CountRequiredTaskGraphBuildings(BuildingContract)
		: bUseLegacySingleBuildingTest ? 1 : 0;
	if (SlingshotSystem)
	{
		SlingshotSystem->BeginRequiredBuildingContract(ExpectedRequiredBuildingCount);
	}
	bool bBuildingSetupFailed = SlingshotSystem == nullptr
		|| Planet == nullptr
		|| (bNeedsBuildingContract && !bBuildingContractReady);
	if (!bFixedSixSnapshotPresent
		&& bSpawnTaskGraphBuildings && MaxTaskGraphBuildings > 0
		&& ExpectedRequiredBuildingCount == 0)
	{
		bBuildingSetupFailed = true;
		UE_LOG(LogABTSRuntime, Error,
			TEXT("[ABTS][M7][TaskGraphBuilding] RequiredSitesMissing Max=%d"),
			MaxTaskGraphBuildings);
	}

	AABTSM7BuildingMaterialSystem* System = GetWorld()->SpawnActorDeferred<AABTSM7BuildingMaterialSystem>(
		BuildingMaterialSystemClass, FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (System)
	{
		System->ConfigureTestSet(bSpawnBuildingMaterialTestSet, SpawnTransform);
		UGameplayStatics::FinishSpawningActor(System, FTransform::Identity);
	}
	if (System == nullptr) bBuildingSetupFailed = true;
	TaskGraphDebugPlayer = &Character;
	TaskGraphDebugPlanet = Planet;
	int32 TaskGraphBuildingCount = 0;
	int32 FixedSixStaticBuildingCount = 0;
	if (System && bFixedSixSnapshotPresent)
	{
		bool bFixedSixSetupFailed = false;
		FixedSixStaticBuildingCount = SpawnJuryDemoFixedSixStaticBuildings(
			BuildingContract,
			*System,
			SlingshotSystem,
			bFixedSixSetupFailed);
		bBuildingSetupFailed = bBuildingSetupFailed || bFixedSixSetupFailed;
		if (!bFixedSixSetupFailed)
		{
			LogProductionFlowSegment(TEXT("StaticRegistered"));
		}
	}
	else if (System && Planet && bSpawnTaskGraphBuildings
		&& bBuildingContractReady)
	{
		bool bTaskGraphSetupFailed = false;
		TaskGraphBuildingCount = SpawnTaskGraphBuildings(
			*Planet,
			BuildingContract,
			*System,
			SlingshotSystem,
			bTaskGraphSetupFailed);
		bBuildingSetupFailed = bBuildingSetupFailed || bTaskGraphSetupFailed;
	}

	AABTSM73StableBuildingActor* StableBuilding = nullptr;
	if (System && !bFixedSixSnapshotPresent
		&& bUseLegacySingleBuildingTest && StableBuildingClass)
	{
		if (Planet && bBuildingContractReady
			&& !BuildingContract.Sites.IsEmpty())
		{
			const FABTSGeneratedBuildingSite& Site =
				BuildingContract.Sites[0];
			StableBuilding = GetWorld()->SpawnActorDeferred<AABTSM73StableBuildingActor>(
				StableBuildingClass, Site.WorldTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (StableBuilding)
			{
				StableBuilding->ConfigureSphericalAnchor(Planet, Site.CellId, Site.WorldTransform);
				if (SlingshotSystem)
				{
					SlingshotSystem->RegisterRequiredBuilding(*StableBuilding);
				}
				else
				{
					bBuildingSetupFailed = true;
				}
				UGameplayStatics::FinishSpawningActor(StableBuilding, Site.WorldTransform);
				StableBuilding->InitializeRuntimeBuilding(System);
			}
		}
	}
	if (!bFixedSixSnapshotPresent
		&& bUseLegacySingleBuildingTest && StableBuilding == nullptr)
	{
		bBuildingSetupFailed = true;
	}
	if (SlingshotSystem)
	{
		SlingshotSystem->SealRequiredBuildingContract(bBuildingSetupFailed);
	}
	if (bFixedSixSnapshotPresent)
	{
		LogProductionFlowSegment(TEXT("ContractSealed"));
		if (bBuildingSetupFailed)
		{
			FinishProductionFlow(false, TEXT("FixedSixStaticSetupRejected"));
		}
		else
		{
			// M3 exact-union certification must observe the immutable HISMs.
			// The successful callback starts the same-batch Chaos promotion.
			ScheduleSatellitePracticeE1CrystalTargetBinding();
			LogProductionFlowSegment(TEXT("AwaitingE1ExactUnionBinding"));
		}
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7] Entry ready=%d StartCell=%d TestSet=%d")
		TEXT(" ExpectedBuildings=%d FixedSixV2=%d FixedSixStatic=%d")
		TEXT(" TaskGraphBuildings=%d LegacyM73A=%d SetupRejected=%d"),
		System ? 1 : 0,
		SpawnCellId,
		bSpawnBuildingMaterialTestSet ? 1 : 0,
		ExpectedRequiredBuildingCount,
		bFixedSixSnapshotPresent ? 1 : 0,
		FixedSixStaticBuildingCount,
		TaskGraphBuildingCount,
		StableBuilding ? 1 : 0,
		bBuildingSetupFailed ? 1 : 0);
}
