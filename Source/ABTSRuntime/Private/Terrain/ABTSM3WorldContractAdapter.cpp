// Copyright Epic Games, Inc. All Rights Reserved.

#include "Terrain/ABTSM3Planet.h"

#include "ABTSRuntime.h"
#include "Contracts/ABTSWorldGenerationContracts.h"
#include "Math/RotationMatrix.h"

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

bool BuildJuryDemoFixedSixContractSnapshot(
	const AABTSM3Planet& Planet,
	FABTSJuryDemoFixedSixContract& OutSnapshot,
	FString& OutFailure)
{
	OutSnapshot = FABTSJuryDemoFixedSixContract();
	OutFailure.Reset();
	const FABTSM3JuryFixedSixLayoutResult& Source =
		Planet.MonthlyJuryFixedSixLayoutResult;
	const TConstArrayView<FABTSM3JuryBuildingPlacementFixture> Fixtures =
		FABTSM3JuryFixedSixLayoutBuilder::GetFrozenPlacementFixtures();

	if (!Source.bPlacementReady
		|| Source.RejectReason != EABTSM3JuryFixedSixRejectReason::None
		|| Source.SchemaVersion
			!= FABTSM3JuryFixedSixLayoutBuilder::SchemaVersion
		|| Source.FixedSixContractVersion
			!= FABTSJuryDemoFixedSixContract::SupportedV2ContractVersion
		|| Source.WorldSeed != FABTSJuryDemoFixedSixContract::FrozenWorldSeed
		|| Source.WorldSeed != Planet.WorldSeed
		|| Source.SourceCandidateId
			!= FABTSJuryDemoFixedSixContract::FrozenCandidateId
		|| static_cast<uint64>(Source.SourceSpatialResultHash)
			!= FABTSM3JuryFixedSixLayoutBuilder::FrozenSourceSpatialResultHash
		|| static_cast<uint64>(Source.SourceSpatialCandidateHash)
			!= FABTSM3JuryFixedSixLayoutBuilder::FrozenSourceSpatialCandidateHash
		|| Source.M7PlacementSchemaVersion
			!= FABTSJuryDemoFixedSixContract::FrozenPlacementSchemaVersion
		|| Source.M7SourceManifestVersion
			!= FABTSJuryDemoFixedSixContract::FrozenDemoManifestVersion
		|| static_cast<uint64>(Source.M7SourceManifestHash)
			!= FABTSJuryDemoFixedSixContract::FrozenDemoManifestHash
		|| static_cast<uint64>(Source.M7PlacementCatalogHash)
			!= FABTSJuryDemoFixedSixContract::FrozenV2PlacementCatalogHash
		|| static_cast<uint64>(Source.LayoutHash)
			!= FABTSJuryDemoFixedSixContract::FrozenV2LayoutHash
		|| static_cast<uint64>(Source.LayoutHash)
			!= FABTSM3JuryFixedSixLayoutBuilder::ComputeLayoutHash(Source)
		|| Source.Placements.Num()
			!= FABTSJuryDemoFixedSixContract::ExpectedSiteCount
		|| Fixtures.Num() != FABTSJuryDemoFixedSixContract::ExpectedSiteCount)
	{
		OutFailure = TEXT("FrozenIdentity");
		return false;
	}

	OutSnapshot.ContractVersion =
		FABTSJuryDemoFixedSixContract::SupportedV2ContractVersion;
	OutSnapshot.PlacementSchemaVersion = Source.M7PlacementSchemaVersion;
	OutSnapshot.DemoManifestVersion = Source.M7SourceManifestVersion;
	OutSnapshot.DemoManifestHash =
		static_cast<uint64>(Source.M7SourceManifestHash);
	OutSnapshot.PlacementCatalogHash =
		static_cast<uint64>(Source.M7PlacementCatalogHash);
	OutSnapshot.WorldSeed = Source.WorldSeed;
	OutSnapshot.CandidateId = Source.SourceCandidateId;
	OutSnapshot.LayoutHash = static_cast<uint64>(Source.LayoutHash);
	OutSnapshot.Sites.Reserve(Source.Placements.Num());

	TSet<FName> ManifestEntryIds;
	for (int32 Index = 0; Index < Source.Placements.Num(); ++Index)
	{
		const FABTSM3JuryBuildingPlacement& Placement =
			Source.Placements[Index];
		const FABTSM3JuryBuildingPlacementFixture& Fixture = Fixtures[Index];
		if (Placement.EncounterIndex != Index
			|| Placement.ManifestEntryId != Fixture.ManifestEntryId
			|| Placement.StableId != Fixture.StableId
			|| Placement.DifficultyTier != Fixture.DifficultyTier
			|| Placement.BuildingSeed != Fixture.BuildingSeed
			|| Placement.SourceDescriptorHash != Fixture.SourceDescriptorHash
			|| Placement.StaticGeometryHash != Fixture.StaticGeometryHash
			|| Placement.ProductionIdentityHash
				!= Fixture.ProductionIdentityHash
			|| Placement.DeviceAssemblyHash != Fixture.DeviceAssemblyHash
			|| Placement.bDynamicEnvelopeRequired
				!= Fixture.bDynamicEnvelopeRequired
			|| Placement.ReservedDynamicEnvelopeCellIds.IsEmpty()
			|| !Placement.PhysicalBounds.Min.Equals(
				Fixture.PhysicalBounds.Min, 1.0e-4)
			|| !Placement.PhysicalBounds.Max.Equals(
				Fixture.PhysicalBounds.Max, 1.0e-4)
			|| !Placement.EffectBounds.Min.Equals(
				Fixture.EffectBounds.Min, 1.0e-4)
			|| !Placement.EffectBounds.Max.Equals(
				Fixture.EffectBounds.Max, 1.0e-4)
			|| !Placement.RequiredPadHalfExtentCM.Equals(
				Fixture.RequiredPadHalfExtentCM, 1.0e-4)
			|| static_cast<uint64>(Placement.PlacementHash)
				!= FABTSM3JuryFixedSixLayoutBuilder::ComputePlacementHash(
					Placement)
			|| ManifestEntryIds.Contains(Placement.ManifestEntryId))
		{
			OutFailure = FString::Printf(TEXT("SiteIdentity:%d"), Index);
			return false;
		}
		ManifestEntryIds.Add(Placement.ManifestEntryId);

		FABTSJuryDemoFixedSixBuildingSite& Site =
			OutSnapshot.Sites.AddDefaulted_GetRef();
		Site.ManifestEntryId = Placement.ManifestEntryId;
		Site.EncounterIndex = Placement.EncounterIndex;
		Site.WorldTransform = FTransform(
			FRotationMatrix::MakeFromXZ(
				Placement.WorldForwardAxis,
				Placement.WorldUpAxis).ToQuat(),
			Placement.WorldLocationCM);
		Site.PadHalfExtentCM = Placement.RequiredPadHalfExtentCM;
		Site.LocalBounds = Fixture.LocalBounds;
		Site.DifficultyTier = Placement.DifficultyTier;
		Site.DeterministicSeed = Placement.BuildingSeed;
		Site.DescriptorHash =
			static_cast<uint64>(Placement.SourceDescriptorHash);
		Site.V2Envelope.StaticGeometryHash =
			static_cast<uint64>(Placement.StaticGeometryHash);
		Site.V2Envelope.ProductionIdentityHash =
			static_cast<uint64>(Placement.ProductionIdentityHash);
		Site.V2Envelope.DeviceAssemblyHash =
			static_cast<uint64>(Placement.DeviceAssemblyHash);
		Site.V2Envelope.PhysicalBounds = Placement.PhysicalBounds;
		Site.V2Envelope.EffectBounds = Placement.EffectBounds;
		Site.V2Envelope.bDynamicEnvelopeRequired =
			Placement.bDynamicEnvelopeRequired;
		if (!Site.IsUsableForContractVersion(OutSnapshot.ContractVersion))
		{
			OutFailure = FString::Printf(TEXT("SiteFrame:%d"), Index);
			return false;
		}
	}

	if (!OutSnapshot.IsUsable())
	{
		OutFailure = TEXT("SnapshotValidation");
		return false;
	}
	return true;
}
}

bool AABTSM3Planet::TryExportBuildingGenerationContract(
	FABTSBuildingGenerationContract& OutContract) const
{
	OutContract = FABTSBuildingGenerationContract();
	FABTSBuildingGenerationContract CandidateContract;
	CandidateContract.Identity = MakeWorldIdentity(*this);
	if (!CandidateContract.Identity.IsUsable())
	{
		return false;
	}

	CandidateContract.Sites.Reserve(BuildingSpawnSites.Num());
	for (const FABTSM3BuildingSpawnSite& Source : BuildingSpawnSites)
	{
		FABTSGeneratedBuildingSite& Site =
			CandidateContract.Sites.AddDefaulted_GetRef();
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

	if (WorldSeed == FABTSJuryDemoFixedSixContract::FrozenWorldSeed)
	{
		FString FixedSixFailure;
		if (!BuildJuryDemoFixedSixContractSnapshot(
				*this,
				CandidateContract.JuryDemoFixedSix,
				FixedSixFailure))
		{
			UE_LOG(LogABTSRuntime, Warning,
				TEXT("[ABTS][Contracts][JuryFixedSix] Exported=0 Seed=%d Failure=%s Authority=FailClosed"),
				WorldSeed,
				*FixedSixFailure);
			return false;
		}
	}

	if (!CandidateContract.IsUsable())
	{
		return false;
	}
	OutContract = MoveTemp(CandidateContract);
	return true;
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
