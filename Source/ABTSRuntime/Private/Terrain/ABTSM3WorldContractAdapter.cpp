// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/ABTSM3Planet.h"

#include "Contracts/ABTSWorldGenerationContracts.h"

namespace
{
FABTSGeneratedWorldIdentity MakeWorldIdentity(const AABTSM3Planet& Planet)
{
	FABTSGeneratedWorldIdentity Identity;
	Identity.WorldSeed = Planet.WorldSeed;
	Identity.GeneratorVersion = Planet.PCGSummary.GeneratorVersion;
	Identity.GenerationAttempt = Planet.PCGSummary.AttemptIndex;
	Identity.bSourceWorldAccepted =
		Planet.IsPlanetReady() && Planet.PCGSummary.bAccepted;
	return Identity;
}

EABTSGeneratedBuildingPurpose ResolveBuildingPurpose(
	const EABTSM3TaskType TaskType)
{
	switch (TaskType)
	{
	case EABTSM3TaskType::Workshop:
		return EABTSGeneratedBuildingPurpose::Workshop;
	case EABTSM3TaskType::TargetBuilding:
		return EABTSGeneratedBuildingPurpose::DestructibleTarget;
	case EABTSM3TaskType::FurnaceRuins:
		return EABTSGeneratedBuildingPurpose::FurnaceRuins;
	case EABTSM3TaskType::LaunchSite:
		return EABTSGeneratedBuildingPurpose::FinaleLaunchReserved;
	default:
		return EABTSGeneratedBuildingPurpose::Unsupported;
	}
}
}

bool AABTSM3Planet::TryExportBuildingGenerationContract(
	FABTSBuildingGenerationContract& OutContract) const
{
	OutContract = FABTSBuildingGenerationContract();
	OutContract.Identity = MakeWorldIdentity(*this);
	if (!OutContract.Identity.IsUsable())
	{
		return false;
	}

	OutContract.Sites.Reserve(BuildingSpawnSites.Num());
	for (const FABTSM3BuildingSpawnSite& Source : BuildingSpawnSites)
	{
		FABTSGeneratedBuildingSite& Site =
			OutContract.Sites.AddDefaulted_GetRef();
		const uint32 StableHash = HashCombineFast(
			GetTypeHash(WorldSeed),
			HashCombineFast(
				GetTypeHash(Source.TaskId),
				GetTypeHash(Source.CellId)));
		Site.SiteId =
			(static_cast<uint64>(static_cast<uint32>(Source.TaskId)) << 32)
			| static_cast<uint32>(Source.CellId);
		Site.TaskId = Source.TaskId;
		Site.CellId = Source.CellId;
		Site.SourceTaskTypeValue = static_cast<int32>(Source.TaskType);
		Site.Purpose = ResolveBuildingPurpose(Source.TaskType);
		Site.DeterministicSeed = static_cast<int32>(StableHash & MAX_int32);
		Site.WorldTransform = Source.WorldTransform;
		Site.MaxSlopeDegrees = Source.MaxSlopeDegrees;
		Site.AnchorDirection = Source.AnchorDirection;
		Site.TangentForward = Source.TangentForward;
		Site.TangentRight = Source.TangentRight;
		Site.PadHalfExtentCM = Source.PadHalfExtentCM;
		Site.PadEdgeBlendWidthCM = Source.PadEdgeBlendWidthCM;
		Site.PadTargetRadiusCM = Source.PadTargetRadiusCM;
		Site.bTerrainPadApplied = Source.bTerrainPadApplied;

		if (Site.Purpose == EABTSGeneratedBuildingPurpose::Workshop)
		{
			Site.EncounterIndex = 0;
			Site.DifficultyTier = 0;
		}
		else if (Site.Purpose
			== EABTSGeneratedBuildingPurpose::DestructibleTarget)
		{
			Site.EncounterIndex = 1;
			Site.DifficultyTier = 1;
		}
		else if (Site.Purpose
			== EABTSGeneratedBuildingPurpose::FurnaceRuins)
		{
			Site.EncounterIndex = 2;
			Site.DifficultyTier = 2;
		}
	}
	return OutContract.IsUsable();
}

bool AABTSM3Planet::TryExportFinaleWorldContract(
	FABTSFinaleWorldContract& OutContract) const
{
	OutContract = FABTSFinaleWorldContract();
	OutContract.Identity = MakeWorldIdentity(*this);
	OutContract.PrimaryRadiusCM = GetPlanetRadiusCM();
	OutContract.LaunchFrame = FinaleLaunchFrame;
	return OutContract.IsUsable();
}
