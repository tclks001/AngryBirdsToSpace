// Copyright Epic Games, Inc. All Rights Reserved.

#include "Game/ABTSM7GameMode.h"

#include "ABTSRuntime.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "Terrain/ABTSM3Planet.h"

namespace
{
	void GetLatitudeLongitudeDegrees(const FVector& WorldLocation, const FVector& PlanetCenter, float& OutLatitudeDegrees, float& OutLongitudeDegrees)
	{
		const FVector Direction = (WorldLocation - PlanetCenter).GetSafeNormal();
		OutLatitudeDegrees = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Direction.Z, -1.0f, 1.0f)));
		OutLongitudeDegrees = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
	}
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
		// DAG-3 failure-frontier planning is not implemented yet. Do not expose
		// the retired B/B2 structural-weakness switch as if DAG2.3 consumed it.
		OutProfile.GenerationSettings.bGenerateStructuralWeakness = false;
		OutProfile.DAGGenerationSettings.ReservedWeaknessBrickCount = 0;
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
	if (bShowTaskGraphPositionDebug)
	{
		DrawTaskGraphPositionDebug();
	}
}

const FABTSM7TaskGraphBuildingProfile* AABTSM7GameMode::FindTaskGraphBuildingProfile(const EABTSM3TaskType TaskType) const
{
	return TaskGraphBuildingProfiles.FindByPredicate([TaskType](const FABTSM7TaskGraphBuildingProfile& Profile)
	{
		return Profile.bSpawnBuilding && Profile.TaskType == TaskType;
	});
}

int32 AABTSM7GameMode::CountRequiredTaskGraphBuildings(const AABTSM3Planet& Planet) const
{
	const int32 Limit = FMath::Max(0, MaxTaskGraphBuildings);
	int32 RequiredCount = 0;
	for (const FABTSM3BuildingSpawnSite& Site : Planet.GetBuildingSpawnSites())
	{
		if (!FABTSM7TaskGraphDAG23ProfileResolver::IsSupportedBuildingTask(Site.TaskType)) continue;
		if (RequiredCount >= Limit) break;
		++RequiredCount;
	}
	return RequiredCount;
}

int32 AABTSM7GameMode::SpawnTaskGraphBuildings(
	AABTSM3Planet& Planet,
	AABTSM7BuildingMaterialSystem& MaterialSystem,
	AABTSM6SlingshotSystem* SlingshotSystem,
	bool& bOutSetupFailed)
{
	bOutSetupFailed = false;
	TaskGraphBuildingDebugEntries.Reset();
	const int32 RequiredCount = CountRequiredTaskGraphBuildings(Planet);
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
	for (const FABTSM3BuildingSpawnSite& Site : Planet.GetBuildingSpawnSites())
	{
		// M11.0 reserves the certified LaunchSite pad for the unique terminal
		// Space-slingshot slot pair. Keep this guard even when a Blueprint CDO
		// still serializes the retired Glass/TwinTower LaunchSite profile.
		if (Site.TaskType == EABTSM3TaskType::LaunchSite)
		{
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][M11.0][LaunchSite] Certified pad retained; M7 building suppressed Task=%d Cell=%d"),
				Site.TaskId,
				Site.CellId);
			continue;
		}
		if (!FABTSM7TaskGraphDAG23ProfileResolver::IsSupportedBuildingTask(Site.TaskType)) continue;
		if (AttemptedRequiredCount >= RequiredCount) break;
		++AttemptedRequiredCount;
		const FABTSM7TaskGraphBuildingProfile* Profile = FindTaskGraphBuildingProfile(Site.TaskType);
		if (Profile == nullptr)
		{
			bOutSetupFailed = true;
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7][TaskGraphBuilding] DAG23ProfileMissing Task=%d Type=%d Cell=%d"),
				Site.TaskId, static_cast<int32>(Site.TaskType), Site.CellId);
			continue;
		}
		FABTSM7TaskGraphBuildingProfile RuntimeProfile;
		bool bMigratedLegacyProfile = false;
		if (!FABTSM7TaskGraphDAG23ProfileResolver::ResolveRuntimeProfile(
			Site.TaskType, *Profile, RuntimeProfile, bMigratedLegacyProfile))
		{
			bOutSetupFailed = true;
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7][TaskGraphBuilding] DAG23ProfileRejected Task=%d Type=%d Cell=%d"),
				Site.TaskId, static_cast<int32>(Site.TaskType), Site.CellId);
			continue;
		}

		const uint32 SeedHash = HashCombineFast(
			GetTypeHash(Planet.WorldSeed), HashCombineFast(GetTypeHash(Site.TaskId), GetTypeHash(Site.CellId)));
		FABTSM73GenerationSettings GenerationSettings = RuntimeProfile.GenerationSettings;
		GenerationSettings.BuildingSeed = static_cast<int32>(SeedHash & MAX_int32);
		if (Planet.BuildingPadSettings.bEnableTerrainFlattening)
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
		DebugEntry.TaskType = Site.TaskType;
		DebugEntry.CellId = Site.CellId;
		++SpawnedCount;
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7][TaskGraphBuilding] Task=%d Type=%d Cell=%d Material=%d Seed=%d Pad=%d Algorithm=%d DAGPreset=%d DAGBudget=%d DAGDepth=%d DAGMinContact=%.3f MigratedLegacy=%d Spawned=%s"),
			Site.TaskId, static_cast<int32>(Site.TaskType), Site.CellId,
			static_cast<int32>(GenerationSettings.PrimaryMaterial), GenerationSettings.BuildingSeed,
			Site.bTerrainPadApplied ? 1 : 0,
			static_cast<int32>(GenerationSettings.GenerationAlgorithm),
			static_cast<int32>(DAGGenerationSettings.Preset),
			DAGGenerationSettings.ExpansionStepBudget,
			DAGGenerationSettings.MaxExpansionDepth,
			RuntimeProfile.DAGLayoutSettings.MinSupportContactAreaRatio,
			bMigratedLegacyProfile ? 1 : 0,
			*Building->GetName());
	}
	bOutSetupFailed = bOutSetupFailed
		|| AttemptedRequiredCount != RequiredCount
		|| SpawnedCount != RequiredCount;
	return SpawnedCount;
}

void AABTSM7GameMode::DrawTaskGraphPositionDebug()
{
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
	const int32 ExpectedRequiredBuildingCount = bSpawnTaskGraphBuildings && Planet
		? CountRequiredTaskGraphBuildings(*Planet)
		: bUseLegacySingleBuildingTest ? 1 : 0;
	if (SlingshotSystem)
	{
		SlingshotSystem->BeginRequiredBuildingContract(ExpectedRequiredBuildingCount);
	}
	bool bBuildingSetupFailed = SlingshotSystem == nullptr || Planet == nullptr;
	if (bSpawnTaskGraphBuildings && MaxTaskGraphBuildings > 0
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
	if (System && Planet && bSpawnTaskGraphBuildings)
	{
		bool bTaskGraphSetupFailed = false;
		TaskGraphBuildingCount = SpawnTaskGraphBuildings(
			*Planet,
			*System,
			SlingshotSystem,
			bTaskGraphSetupFailed);
		bBuildingSetupFailed = bBuildingSetupFailed || bTaskGraphSetupFailed;
	}

	AABTSM73StableBuildingActor* StableBuilding = nullptr;
	if (System && bUseLegacySingleBuildingTest && StableBuildingClass)
	{
		if (Planet && !Planet->GetBuildingSpawnSites().IsEmpty())
		{
			const FABTSM3BuildingSpawnSite& Site = Planet->GetBuildingSpawnSites()[0];
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
	if (bUseLegacySingleBuildingTest && StableBuilding == nullptr)
	{
		bBuildingSetupFailed = true;
	}
	if (SlingshotSystem)
	{
		SlingshotSystem->SealRequiredBuildingContract(bBuildingSetupFailed);
	}
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][M7] Entry ready=%d StartCell=%d TestSet=%d ExpectedBuildings=%d TaskGraphBuildings=%d LegacyM73A=%d SetupRejected=%d"),
		System ? 1 : 0,
		SpawnCellId,
		bSpawnBuildingMaterialTestSet ? 1 : 0,
		ExpectedRequiredBuildingCount,
		TaskGraphBuildingCount,
		StableBuilding ? 1 : 0,
		bBuildingSetupFailed ? 1 : 0);
}
