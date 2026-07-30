// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "ABTSRuntime.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3MonthlyWitness.h"
#include "PCG/ABTSM3R4AcceptanceManifest.h"
#include "Planet/ABTSM2Planet.h"

namespace ABTSM3MonthlyWitnessTests
{
constexpr float ReferencePlanetRadiusCM = 10000.0f;
constexpr int32 DisplaySeed =
	FABTSM3R4AcceptanceManifest::DisplaySeed;
constexpr int32 SweepSeedCount =
	FABTSM3R4AcceptanceManifest::SweepSeedCount;

struct FSourceFixture
{
	FABTSM3MonthlyRoutePool RoutePool;
	FABTSM3MonthlySpatialResult SpatialResult;
	FABTSM3MonthlySlingshotFieldResult FieldResult;
};

TArray<FABTSM2Cell> BuildLogicalCells()
{
	AABTSM2Planet::FUnitSphereMesh Mesh;
	AABTSM2Planet::BuildUnitIcosphere(5, Mesh);
	TArray<FABTSM2Cell> Cells;
	Cells.SetNum(Mesh.Vertices.Num());
	for (int32 CellId = 0; CellId < Mesh.Vertices.Num(); ++CellId)
	{
		Cells[CellId].UnitCenter = Mesh.Vertices[CellId];
	}
	const auto AddNeighbor = [&Cells](
		const int32 CellA,
		const int32 CellB)
	{
		Cells[CellA].NeighborCellIds.AddUnique(CellB);
		Cells[CellB].NeighborCellIds.AddUnique(CellA);
	};
	for (const FIntVector& Triangle : Mesh.Triangles)
	{
		AddNeighbor(Triangle.X, Triangle.Y);
		AddNeighbor(Triangle.Y, Triangle.Z);
		AddNeighbor(Triangle.Z, Triangle.X);
	}
	for (FABTSM2Cell& Cell : Cells)
	{
		Cell.NeighborCellIds.Sort();
		Cell.bIsPentagon = Cell.NeighborCellIds.Num() == 5;
	}
	return Cells;
}

const TArray<FABTSM2Cell>& GetLogicalCells()
{
	static const TArray<FABTSM2Cell> Cells =
		BuildLogicalCells();
	return Cells;
}

FABTSM3MonthlyRouteConfig MakeRouteConfig()
{
	FABTSM3MonthlyRouteConfig Config;
	Config.bEmitRouteLogs = false;
	return Config;
}

FABTSM3MonthlyEncounterSpatialConfig MakeSpatialConfig()
{
	FABTSM3MonthlyEncounterSpatialConfig Config;
	Config.bEmitSpatialLogs = false;
	return Config;
}

FABTSM3MonthlySlingshotFieldConfig MakeFieldConfig()
{
	FABTSM3MonthlySlingshotFieldConfig Config;
	Config.bEmitSlingshotFieldLogs = false;
	return Config;
}

FABTSM3MonthlyWitnessConfig MakeWitnessConfig()
{
	FABTSM3MonthlyWitnessConfig Config;
	Config.bBuildGameplayFinalize = true;
	Config.bEmitWitnessLogs = false;
	return Config;
}

bool BuildSourceFixture(
	const int32 Seed,
	FSourceFixture& OutFixture,
	FString& OutFailure)
{
	OutFixture = FSourceFixture();
	const FABTSM3MonthlyRouteConfig RouteConfig =
		MakeRouteConfig();
	if (!FABTSM3MonthlyRouteBuilder::Build(
		Seed,
		RouteConfig,
		GetLogicalCells(),
		ReferencePlanetRadiusCM,
		FABTSM3MonthlyRoadContext(),
		OutFixture.RoutePool,
		OutFailure))
	{
		return false;
	}
	if (!FABTSM3MonthlyEncounterBuilder::Build(
		Seed,
		MakeSpatialConfig(),
		RouteConfig,
		GetLogicalCells(),
		ReferencePlanetRadiusCM,
		OutFixture.RoutePool,
		FABTSM3MonthlySpatialFaultInjection(),
		OutFixture.SpatialResult,
		OutFailure))
	{
		return false;
	}
	return FABTSM3MonthlySlingshotFieldBuilder::Build(
		Seed,
		MakeFieldConfig(),
		GetLogicalCells(),
		ReferencePlanetRadiusCM,
		OutFixture.SpatialResult,
		OutFixture.FieldResult,
		OutFailure);
}

bool BuildDisplaySource(
	FSourceFixture& OutFixture,
	FString& OutFailure)
{
	static bool bBuilt = false;
	static FSourceFixture Cached;
	static FString CachedFailure;
	if (!bBuilt)
	{
		bBuilt = true;
		BuildSourceFixture(
			DisplaySeed,
			Cached,
			CachedFailure);
	}
	OutFixture = Cached;
	OutFailure = CachedFailure;
	return Cached.SpatialResult.bSpatialResultValid
		&& Cached.FieldResult.bSlingshotFieldResultValid;
}

bool KeepFirstJoinedCandidate(
	FSourceFixture& Fixture,
	FString& OutFailure)
{
	if (Fixture.SpatialResult.RetainedCandidates.IsEmpty())
	{
		OutFailure = TEXT("NoSpatialCandidate");
		return false;
	}
	const int32 SourceId =
		Fixture.SpatialResult.RetainedCandidates[0].
			SourceRouteCandidateId;
	const int64 SpatialHash =
		Fixture.SpatialResult.RetainedCandidates[0].
			SpatialCandidateHash;
	const FABTSM3MonthlySlingshotFieldCandidate* Joined =
		Fixture.FieldResult.RetainedCandidates.FindByPredicate(
			[SourceId, SpatialHash](
				const FABTSM3MonthlySlingshotFieldCandidate&
					Candidate)
			{
				return Candidate.SourceRouteCandidateId == SourceId
					&& Candidate.SourceSpatialCandidateHash
						== SpatialHash;
			});
	if (Joined == nullptr)
	{
		OutFailure = TEXT("NoJoinedFieldCandidate");
		return false;
	}
	const FABTSM3MonthlySpatialCandidate SpatialCandidate =
		Fixture.SpatialResult.RetainedCandidates[0];
	const FABTSM3MonthlySlingshotFieldCandidate FieldCandidate =
		*Joined;
	Fixture.SpatialResult.RetainedCandidates = {
		SpatialCandidate
	};
	Fixture.SpatialResult.SpatialResultHash =
		static_cast<int64>(
			FABTSM3MonthlyEncounterBuilder::
				ComputeResultHash(
					Fixture.SpatialResult));
	Fixture.FieldResult.RetainedCandidates = {
		FieldCandidate
	};
	Fixture.FieldResult.SourceSpatialResultHash =
		Fixture.SpatialResult.SpatialResultHash;
	Fixture.FieldResult.ResultHash = static_cast<int64>(
		FABTSM3MonthlySlingshotFieldBuilder::
			ComputeResultHash(Fixture.FieldResult));
	return true;
}

FABTSM3WitnessItemAmount Item(
	const EABTSItemId ItemId,
	const int32 Quantity)
{
	FABTSM3WitnessItemAmount Result;
	Result.ItemId = ItemId;
	Result.Quantity = Quantity;
	return Result;
}

FABTSM3WitnessRecipe Recipe(
	const TCHAR* RecipeId,
	const TArray<FABTSM3WitnessItemAmount>& Inputs,
	const TArray<FABTSM3WitnessItemAmount>& Outputs,
	const TArray<EABTSM3ProgressKey>& RequiredKeys,
	const TArray<EABTSM3ProgressKey>& GrantedKeys,
	const EABTSCraftingStationType RequiredStation =
		EABTSCraftingStationType::None)
{
	FABTSM3WitnessRecipe Result;
	Result.RecipeId = FName(RecipeId);
	Result.RequiredStation = RequiredStation;
	Result.Inputs = Inputs;
	Result.Outputs = Outputs;
	Result.RequiredKeys = RequiredKeys;
	Result.GrantedKeys = GrantedKeys;
	return Result;
}

FABTSM3WitnessEncounterReward Reward(
	const int32 EncounterId,
	const int32 EncounterOrder,
	const TArray<FABTSM3WitnessItemAmount>& Items,
	const TArray<EABTSM3ProgressKey>& RequiredKeys,
	const TArray<EABTSM3ProgressKey>& GrantedKeys)
{
	FABTSM3WitnessEncounterReward Result;
	Result.EncounterId = EncounterId;
	Result.EncounterOrder = EncounterOrder;
	Result.Items = Items;
	Result.RequiredKeys = RequiredKeys;
	Result.GrantedKeys = GrantedKeys;
	return Result;
}

FABTSM3WitnessProgressionSnapshot BuildProgressionSnapshot(
	const FABTSM3MonthlySpatialResult& SpatialResult)
{
	FABTSM3WitnessProgressionSnapshot Snapshot;
	Snapshot.bWorkbenchStationAvailable = true;
	Snapshot.bFurnaceStationAvailable = true;
	check(!SpatialResult.RetainedCandidates.IsEmpty());
	const TArray<FABTSM3MonthlySpatialEncounter>& Encounters =
		SpatialResult.RetainedCandidates[0].Encounters;
	check(Encounters.Num() == 6);
	Snapshot.Recipes = {
		Recipe(
			TEXT("M3R4_Workbench"),
			{},
			{},
			{},
			{ EABTSM3ProgressKey::BuildWorkbench }),
		Recipe(
			TEXT("M3R4_SimpleSlingshot"),
			{},
			{},
			{ EABTSM3ProgressKey::BuildWorkbench },
			{ EABTSM3ProgressKey::SimpleSlingshotReady }),
		Recipe(
			TEXT("M3R4_Bridge"),
			{ Item(EABTSItemId::Wood, 1) },
			{},
			{ EABTSM3ProgressKey::TargetDestroyed,
				EABTSM3ProgressKey::HaveWood },
			{ EABTSM3ProgressKey::BridgeBuilt }),
		Recipe(
			TEXT("M3R4_ReinforcedSlingshot"),
			{},
			{},
			{ EABTSM3ProgressKey::BridgeBuilt },
			{ EABTSM3ProgressKey::ReinforcedSlingshotReady }),
		Recipe(
			TEXT("SpaceStakePair"),
			{ Item(EABTSItemId::MetalParts, 6),
				Item(EABTSItemId::Wood, 5) },
			{ Item(EABTSItemId::SpaceStake, 2) },
			{ EABTSM3ProgressKey::SatelliteShotSolved,
				EABTSM3ProgressKey::
					ReinforcedSlingshotReady },
			{},
			EABTSCraftingStationType::Furnace),
		Recipe(
			TEXT("SpaceCord"),
			{ Item(EABTSItemId::MetalParts, 2),
				Item(EABTSItemId::CrystalCore, 1) },
			{ Item(EABTSItemId::SpaceCord, 1) },
			{ EABTSM3ProgressKey::HaveCrystalCore,
				EABTSM3ProgressKey::SatelliteShotSolved,
				EABTSM3ProgressKey::
					ReinforcedSlingshotReady },
			{},
			EABTSCraftingStationType::Furnace)
	};
	Snapshot.EncounterRewards = {
		Reward(
			Encounters[0].Contract.EncounterId,
			0,
			{},
			{ EABTSM3ProgressKey::SimpleSlingshotReady },
			{ EABTSM3ProgressKey::TargetDestroyed }),
		Reward(
			Encounters[1].Contract.EncounterId,
			1,
			{ Item(EABTSItemId::Wood, 6) },
			{ EABTSM3ProgressKey::TargetDestroyed },
			{}),
		Reward(
			Encounters[2].Contract.EncounterId,
			2,
			{},
			{ EABTSM3ProgressKey::TargetDestroyed },
			{}),
		Reward(
			Encounters[3].Contract.EncounterId,
			3,
			{},
			{ EABTSM3ProgressKey::
				ReinforcedSlingshotReady },
			{}),
		Reward(
			Encounters[4].Contract.EncounterId,
			4,
			{ Item(EABTSItemId::CrystalCore, 1) },
			{ EABTSM3ProgressKey::
				ReinforcedSlingshotReady },
			{ EABTSM3ProgressKey::SatelliteShotSolved }),
		Reward(
			Encounters[5].Contract.EncounterId,
			5,
			{ Item(EABTSItemId::MetalParts, 8) },
			{ EABTSM3ProgressKey::SatelliteShotSolved },
			{})
	};
	Snapshot.BridgeEvidence.BarrierId =
		FName(TEXT("M3R4_FixtureBridgeCut"));
	Snapshot.BridgeEvidence.GateCellId = 101;
	Snapshot.BridgeEvidence.PreBridgeCellId = 100;
	Snapshot.BridgeEvidence.PostBridgeCellId = 102;
	Snapshot.BridgeEvidence.bBlockedBeforeBridge = true;
	Snapshot.BridgeEvidence.bReachableAfterBridge = true;
	Snapshot.BridgeEvidence.bNoBypassBeforeBridge = true;
	Snapshot.BridgeEvidence.EvidenceHash = static_cast<int64>(
		FABTSM3MonthlyWitnessBuilder::
			ComputeBridgeEvidenceHash(
				Snapshot.BridgeEvidence));
	Snapshot.CatalogHash = static_cast<int64>(
		FABTSM3MonthlyWitnessBuilder::
			ComputeProgressionCatalogHash(Snapshot));
	return Snapshot;
}

FABTSM3WitnessProfileCatalog BuildProfileCatalog(
	const FABTSM3MonthlySpatialResult& SpatialResult)
{
	FABTSM3WitnessProfileCatalog Catalog;
	Catalog.SpatialSourceCatalogHash =
		SpatialResult.ProfileCatalogHash;
	const TConstArrayView<FABTSM3MonthlyProfileDescriptorFixture>
		SourceDescriptors =
			FABTSM3MonthlyEncounterBuilder::
				GetFixtureProfileCatalog();
	for (int32 DescriptorIndex = 0;
		DescriptorIndex < SourceDescriptors.Num();
		++DescriptorIndex)
	{
		const FABTSM3MonthlyProfileDescriptorFixture& Source =
			SourceDescriptors[DescriptorIndex];
		FABTSM3WitnessProfileDescriptor Descriptor;
		Descriptor.ProfileId = Source.ProfileId;
		Descriptor.BoundsExtentCM = Source.BoundsExtentCM;
		FABTSM3WitnessAttackFace Face;
		Face.FaceId = FName(TEXT("FixtureAttackFace"));
		Face.LocalCenterCM = FVector::ZeroVector;
		Face.LocalNormal = FVector::ForwardVector;
		Face.RadiusCM = 150.0f;
		Face.MinimumImpactSpeedCMPerSec = 100.0f;
		Face.bRequiresBird =
			DescriptorIndex == 3 || DescriptorIndex == 4;
		Face.RequiredBird = EABTSBirdId::Red;
		if (Face.bRequiresBird)
		{
			Face.RequiredBird = EABTSBirdId::Black;
		}
		Face.FaceHash = static_cast<int64>(
			FABTSM3MonthlyWitnessBuilder::
				ComputeAttackFaceHash(Face));
		Descriptor.AttackFaces.Add(Face);
		Descriptor.DescriptorHash = static_cast<int64>(
			FABTSM3MonthlyWitnessBuilder::
				ComputeProfileDescriptorHash(Descriptor));
		Catalog.Descriptors.Add(MoveTemp(Descriptor));
	}
	Catalog.FullCatalogHash = static_cast<int64>(
		FABTSM3MonthlyWitnessBuilder::
			ComputeProfileCatalogHash(Catalog));
	return Catalog;
}

int64 EncounterKey(
	const int32 SourceRouteCandidateId,
	const int32 EncounterId)
{
	return static_cast<int64>(SourceRouteCandidateId) << 32
		| static_cast<uint32>(EncounterId);
}

FIntPoint NormalizePair(
	const int32 CellA,
	const int32 CellB)
{
	return CellA < CellB
		? FIntPoint(CellA, CellB)
		: FIntPoint(CellB, CellA);
}

class FScriptedWitnessServices final
	: public IABTSM3MonthlyWitnessServices
{
public:
	explicit FScriptedWitnessServices(
		const FABTSM3MonthlySpatialResult& SpatialResult)
		: BaseCatalog(BuildProfileCatalog(SpatialResult))
		, BaseProgression(
			BuildProgressionSnapshot(SpatialResult))
	{
		Identity.Authority = EABTSM3WitnessAuthority::Fixture;
		Identity.ServiceSchemaVersion = 1;
		Identity.SolverHash = static_cast<int64>(
			0x13572468abcdef01ull);
		Identity.GeometryHash =
			SpatialResult.SpatialResultHash
				^ static_cast<int64>(0x24681357u);
		if (Identity.GeometryHash == 0)
		{
			Identity.GeometryHash = 1;
		}
		Identity.GravitySnapshotHash = static_cast<int64>(
			0x55aa55aa33cc33ccull);
		Identity.ProfileCatalogHash =
			BaseCatalog.FullCatalogHash;
		Identity.ProgressionCatalogHash =
			BaseProgression.CatalogHash;
		Identity.BirdCatalogHash =
			static_cast<int64>(
				FABTSM3MonthlyWitnessBuilder::
					ComputeV1BirdCatalogHash());
		Identity.bCertified = false;
	}

	bool bIdentityInvalid = false;
	bool bFixtureClaimsCertified = false;
	bool bUseIntegrationAuthority = false;
	bool bIntegrationSourceCatalogMismatch = false;
	bool bCatalogIdentityCorrupt = false;
	bool bProfileBoundsMismatch = false;
	bool bMissingM9Evidence = false;
	bool bIgnoreM9Disable = false;
	bool bNeverHit = false;
	bool bBridgeInvalid = false;
	bool bProgressionMissingRecipe = false;
	bool bInitialInventoryNonEmpty = false;
	bool bMissingM9ForbiddenVolume = false;
	bool bFurnaceUnavailable = false;
	bool bMissingEligibleBird = false;
	bool bExtraEligibleBird = false;
	bool bInvalidAblationTermination = false;
	bool bInvalidPriorTermination = false;
	bool bImpactSpeedOnlyAfterEntry = false;

	mutable int32 EvaluationCount = 0;
	mutable TMap<int64, FIntPoint> PreferredPairs;

	virtual bool GetIdentity(
		FABTSM3WitnessServiceIdentity& OutIdentity,
		FString& OutFailure) const override
	{
		OutFailure.Reset();
		OutIdentity = Identity;
		if (bUseIntegrationAuthority)
		{
			OutIdentity.Authority =
				EABTSM3WitnessAuthority::Integration;
			OutIdentity.bCertified = true;
		}
		if (bIdentityInvalid)
		{
			OutIdentity.SolverHash = 0;
		}
		if (!bUseIntegrationAuthority)
		{
			OutIdentity.bCertified =
				bFixtureClaimsCertified;
		}
		if (bProfileBoundsMismatch
			|| bIntegrationSourceCatalogMismatch
			|| bImpactSpeedOnlyAfterEntry)
		{
			OutIdentity.ProfileCatalogHash =
				BuildEffectiveCatalog().FullCatalogHash;
		}
		if (bProgressionMissingRecipe
			|| bInitialInventoryNonEmpty
			|| bFurnaceUnavailable)
		{
			OutIdentity.ProgressionCatalogHash =
				BuildEffectiveProgression(0).CatalogHash;
		}
		return true;
	}

	virtual bool GetProfileCatalog(
		FABTSM3WitnessProfileCatalog& OutCatalog,
		FString& OutFailure) const override
	{
		OutFailure.Reset();
		OutCatalog = BuildEffectiveCatalog();
		if (bCatalogIdentityCorrupt)
		{
			OutCatalog.FullCatalogHash ^= 1;
		}
		return true;
	}

	virtual bool ResolveEncounterGeometry(
		const int32 SourceRouteCandidateId,
		const FABTSM3MonthlySpatialEncounter& Encounter,
		const FABTSM3MonthlySlingshotField& Field,
		FABTSM3ResolvedWitnessGeometry& OutGeometry,
		FString& OutFailure) const override
	{
		OutFailure.Reset();
		OutGeometry = FABTSM3ResolvedWitnessGeometry();
		OutGeometry.EncounterId =
			Encounter.Contract.EncounterId;
		OutGeometry.EncounterOrder =
			Encounter.Contract.OrderIndex;
		for (int32 SlotIndex = 0;
			SlotIndex < Field.SlotCellIds.Num();
			++SlotIndex)
		{
			FABTSM3WitnessSlotGeometry Slot;
			Slot.CellId = Field.SlotCellIds[SlotIndex];
			Slot.CordSocketWorldCM = FVector(
				SlotIndex * 100.0,
				SourceRouteCandidateId * 10.0,
				Encounter.Contract.OrderIndex * 10.0);
			OutGeometry.Slots.Add(Slot);
		}
		OutGeometry.TargetWorldTransform.SetLocation(FVector(
			10000.0 + Encounter.Contract.OrderIndex * 1000.0,
			SourceRouteCandidateId * 100.0,
			500.0));
		if (Encounter.Contract.OrderIndex == 4
			&& !bMissingM9ForbiddenVolume)
		{
			FABTSM3WitnessForbiddenSphere Satellite;
			Satellite.VolumeId =
				FName(TEXT("M9PracticeSatellite"));
			Satellite.CenterWorldCM =
				OutGeometry.TargetWorldTransform.GetLocation()
				+ FVector(0.0, 5000.0, 0.0);
			Satellite.RadiusCM = 250.0f;
			OutGeometry.ForbiddenSpheres.Add(Satellite);
			OutGeometry.M9SatelliteForbiddenVolumeId =
				Satellite.VolumeId;
		}
		OutGeometry.GeometryHash = static_cast<int64>(
			FABTSM3MonthlyWitnessBuilder::
				ComputeResolvedGeometryHash(OutGeometry));

		TArray<int32> NonAnchorCells = Field.SlotCellIds;
		NonAnchorCells.Remove(Field.AnchorCellId);
		NonAnchorCells.Sort();
		if (NonAnchorCells.Num() < 2)
		{
			OutFailure = TEXT("FixtureNeedsTwoNonAnchorSlots");
			return false;
		}
		PreferredPairs.Add(
			EncounterKey(
				SourceRouteCandidateId,
				Encounter.Contract.OrderIndex),
			NormalizePair(
				NonAnchorCells[NonAnchorCells.Num() - 2],
				NonAnchorCells.Last()));
		return true;
	}

	virtual bool GetEligibleBirds(
		const EABTSSlingshotTier Tier,
		TArray<EABTSBirdId>& OutBirds,
		FString& OutFailure) const override
	{
		OutFailure.Reset();
		OutBirds.Reset();
		switch (Tier)
		{
		case EABTSSlingshotTier::Simple:
			OutBirds.Add(EABTSBirdId::Red);
			OutBirds.Add(EABTSBirdId::Blue);
			OutBirds.Add(EABTSBirdId::Yellow);
			break;
		case EABTSSlingshotTier::Reinforced:
			OutBirds.Add(EABTSBirdId::Red);
			OutBirds.Add(EABTSBirdId::Blue);
			OutBirds.Add(EABTSBirdId::Yellow);
			OutBirds.Add(EABTSBirdId::Black);
			break;
		default:
			OutBirds.Add(EABTSBirdId::Red);
			break;
		}
		if (bMissingEligibleBird && !OutBirds.IsEmpty())
		{
			OutBirds.Pop();
		}
		if (bExtraEligibleBird)
		{
			OutBirds.Add(EABTSBirdId::Black);
		}
		return true;
	}

	virtual bool EvaluateTrajectory(
		const FABTSM3WitnessTrajectoryRequest& Request,
		FABTSM3WitnessTrajectoryResult& OutResult,
		FString& OutFailure) const override
	{
		OutFailure.Reset();
		++EvaluationCount;
		OutResult = FABTSM3WitnessTrajectoryResult();
		OutResult.SolverHashEcho = Request.SolverHash;
		OutResult.GravitySnapshotHashEcho =
			Request.GravitySnapshotHash;

		const FIntPoint* Preferred = PreferredPairs.Find(
			EncounterKey(
				Request.SourceRouteCandidateId,
				Request.LaunchInput.EncounterOrder));
		const bool bPreferredPair = Preferred != nullptr
			&& *Preferred == NormalizePair(
				Request.LaunchInput.SlotACellId,
				Request.LaunchInput.SlotBCellId);
		const bool bCanonicalInput =
			Request.LaunchInput.LaunchSideSign == -1
			&& Request.LaunchInput.PullAlphaQ == 1000
			&& Request.LaunchInput.AimRightQ == 0
			&& Request.LaunchInput.AimUpQ == 0;
		const bool bPriorTierDomain =
			(Request.LaunchInput.EncounterOrder == 3
				|| Request.LaunchInput.EncounterOrder == 4)
			&& Request.LaunchInput.Tier
				== EABTSSlingshotTier::Simple;
		const bool bM9Ablation =
			Request.LaunchInput.EncounterOrder == 4
			&& !Request.LaunchInput.bEnableSatelliteGravity;
		const bool bHit =
			!bNeverHit
			&& bPreferredPair
			&& bCanonicalInput
			&& !bPriorTierDomain
			&& !bM9Ablation;

		const float Span = FMath::Max(
			200.0f,
			Request.TargetRadiusCM * 2.0f);
		const FVector Offset = bHit
			? FVector::ZeroVector
			: FVector(
				0.0,
				Request.TargetRadiusCM + 500.0f,
				0.0);
		FABTSM3WitnessTrajectorySample Start;
		Start.TimeSeconds = 0.0f;
		Start.PositionWorldCM =
			Request.TargetCenterWorldCM
			+ Offset
			- FVector(Span, 0.0, 0.0);
		Start.VelocityWorldCMPerSec =
			bImpactSpeedOnlyAfterEntry
				? FVector::ZeroVector
				: FVector(1000.0, 0.0, 0.0);
		FABTSM3WitnessTrajectorySample End;
		End.TimeSeconds = 1.0f;
		End.PositionWorldCM =
			Request.TargetCenterWorldCM
			+ Offset
			+ FVector(Span, 0.0, 0.0);
		End.VelocityWorldCMPerSec =
			bImpactSpeedOnlyAfterEntry
				? FVector(2000.0, 0.0, 0.0)
				: FVector(1000.0, 0.0, 0.0);
		OutResult.Samples = { Start, End };
		OutResult.Termination = bHit
			? EABTSM3TrajectoryTermination::TargetHit
			: EABTSM3TrajectoryTermination::TimeLimit;
		if ((bInvalidAblationTermination && bM9Ablation)
			|| (bInvalidPriorTermination
				&& bPriorTierDomain))
		{
			OutResult.Termination =
				EABTSM3TrajectoryTermination::Invalid;
		}
		OutResult.LandingWorldCM = End.PositionWorldCM;
		if (Request.LaunchInput.EncounterOrder == 4
			&& (Request.LaunchInput.bEnableSatelliteGravity
				|| bIgnoreM9Disable)
			&& !bMissingM9Evidence)
		{
			OutResult.M9QueryCount = 2;
			OutResult.M9NonZeroAccelerationCount = 2;
			OutResult.PeakM9AccelerationCMPerSecSq = 25.0f;
		}
		return true;
	}

	virtual bool GetProgressionSnapshot(
		const int32 SourceRouteCandidateId,
		FABTSM3WitnessProgressionSnapshot& OutSnapshot,
		FString& OutFailure) const override
	{
		OutFailure.Reset();
		OutSnapshot =
			BuildEffectiveProgression(
				SourceRouteCandidateId);
		return true;
	}

private:
	FABTSM3WitnessProfileCatalog BuildEffectiveCatalog() const
	{
		FABTSM3WitnessProfileCatalog Catalog = BaseCatalog;
		if (bProfileBoundsMismatch
			&& !Catalog.Descriptors.IsEmpty())
		{
			Catalog.Descriptors[0].BoundsExtentCM.X += 1.0;
			Catalog.Descriptors[0].DescriptorHash =
				static_cast<int64>(
					FABTSM3MonthlyWitnessBuilder::
						ComputeProfileDescriptorHash(
							Catalog.Descriptors[0]));
		}
		if (bIntegrationSourceCatalogMismatch)
		{
			Catalog.SpatialSourceCatalogHash ^= 1;
		}
		if (bImpactSpeedOnlyAfterEntry)
		{
			for (FABTSM3WitnessProfileDescriptor& Descriptor :
				Catalog.Descriptors)
			{
				for (FABTSM3WitnessAttackFace& Face :
					Descriptor.AttackFaces)
				{
					Face.MinimumImpactSpeedCMPerSec =
						900.0f;
					Face.FaceHash = static_cast<int64>(
						FABTSM3MonthlyWitnessBuilder::
							ComputeAttackFaceHash(Face));
				}
				Descriptor.DescriptorHash = static_cast<int64>(
					FABTSM3MonthlyWitnessBuilder::
						ComputeProfileDescriptorHash(
							Descriptor));
			}
		}
		if (bProfileBoundsMismatch
			|| bIntegrationSourceCatalogMismatch
			|| bImpactSpeedOnlyAfterEntry)
		{
			Catalog.FullCatalogHash = static_cast<int64>(
				FABTSM3MonthlyWitnessBuilder::
					ComputeProfileCatalogHash(Catalog));
		}
		return Catalog;
	}

	FABTSM3WitnessProgressionSnapshot
	BuildEffectiveProgression(
		const int32 SourceRouteCandidateId) const
	{
		FABTSM3WitnessProgressionSnapshot Snapshot =
			BaseProgression;
		Snapshot.BridgeEvidence.SourceRouteCandidateId =
			SourceRouteCandidateId;
		if (bBridgeInvalid)
		{
			Snapshot.BridgeEvidence.bNoBypassBeforeBridge = false;
		}
		Snapshot.BridgeEvidence.EvidenceHash =
			static_cast<int64>(
				FABTSM3MonthlyWitnessBuilder::
					ComputeBridgeEvidenceHash(
						Snapshot.BridgeEvidence));
		if (bProgressionMissingRecipe)
		{
			Snapshot.Recipes.RemoveAll([](
				const FABTSM3WitnessRecipe& Entry)
			{
				return Entry.RecipeId
					== FName(TEXT("SpaceCord"));
			});
		}
		if (bInitialInventoryNonEmpty)
		{
			Snapshot.InitialItems.Add(
				Item(EABTSItemId::Branch, 1));
		}
		if (bFurnaceUnavailable)
		{
			Snapshot.bFurnaceStationAvailable = false;
		}
		Snapshot.CatalogHash = static_cast<int64>(
			FABTSM3MonthlyWitnessBuilder::
				ComputeProgressionCatalogHash(Snapshot));
		return Snapshot;
	}

	FABTSM3WitnessServiceIdentity Identity;
	FABTSM3WitnessProfileCatalog BaseCatalog;
	FABTSM3WitnessProgressionSnapshot BaseProgression;
};

bool BuildWitness(
	const int32 Seed,
	const FABTSM3MonthlyWitnessConfig& Config,
	FSourceFixture& Fixture,
	FScriptedWitnessServices& Services,
	FABTSM3MonthlyWitnessResult& OutResult,
	FString& OutFailure)
{
	return FABTSM3MonthlyWitnessBuilder::Build(
		Seed,
		Config,
		Fixture.SpatialResult,
		Fixture.FieldResult,
		&Services,
		OutResult,
		OutFailure);
}

bool ResultsEqual(
	const FABTSM3MonthlyWitnessResult& A,
	const FABTSM3MonthlyWitnessResult& B)
{
	return FABTSM3MonthlyWitnessResult::StaticStruct()
		->CompareScriptStruct(&A, &B, PPF_None);
}

uint64 MixOracle(uint64 Hash, const uint64 Value)
{
	for (int32 Shift = 0; Shift < 64; Shift += 8)
	{
		Hash ^= static_cast<uint8>(
			(Value >> Shift) & 0xffull);
		Hash *= 1099511628211ull;
	}
	return Hash;
}

int32 GetItemQuantity(
	const TArray<FABTSM3WitnessItemAmount>& Items,
	const EABTSItemId ItemId)
{
	const FABTSM3WitnessItemAmount* Found =
		Items.FindByPredicate(
			[ItemId](const FABTSM3WitnessItemAmount& ItemEntry)
			{
				return ItemEntry.ItemId == ItemId;
			});
	return Found != nullptr ? Found->Quantity : 0;
}

bool ValidateGeneratedEncounterShape(
	FAutomationTestBase& Test,
	const FABTSM3MonthlyWitnessConfig& Config,
	const FABTSM3MonthlySpatialCandidate& Source,
	const FABTSM3MonthlyGameplayCandidate& Candidate)
{
	bool bValid = true;
	const auto Check = [&Test, &bValid, &Source](
		const bool bCondition,
		const int32 EncounterOrder,
		const TCHAR* Label)
	{
		if (!bCondition)
		{
			Test.AddError(FString::Printf(
				TEXT("Candidate %d E%d generated shape: %s"),
				Source.SourceRouteCandidateId,
				EncounterOrder + 1,
				Label));
			bValid = false;
		}
	};
	for (int32 EncounterOrder = 0;
		EncounterOrder < Candidate.Encounters.Num();
		++EncounterOrder)
	{
		const FABTSM3MonthlyEncounterGameplay& Encounter =
			Candidate.Encounters[EncounterOrder];
		const FABTSM3BallisticWitness& Witness =
			Encounter.PositiveWitness;
		const FABTSM3PriorTierInfeasibilityCertificate&
			Certificate = Encounter.PriorTierCertificate;
		const bool bPriorRequired =
			Config.PriorTierRequiredEncounterOrders.Contains(
				EncounterOrder);
		Check(
			Source.Encounters.IsValidIndex(EncounterOrder),
			EncounterOrder,
			TEXT("source encounter missing"));
		if (!Source.Encounters.IsValidIndex(EncounterOrder))
		{
			continue;
		}
		Check(
			Encounter.EncounterId
				== Source.Encounters[EncounterOrder].
					Contract.EncounterId,
			EncounterOrder,
			TEXT("stable encounter id"));
		Check(
			Encounter.EncounterOrder == EncounterOrder,
			EncounterOrder,
			TEXT("encounter order"));
		Check(
			Witness.LaunchInput.EncounterId
				== Encounter.EncounterId,
			EncounterOrder,
			TEXT("launch stable id"));
		Check(
			Witness.LaunchInput.EncounterOrder
				== EncounterOrder,
			EncounterOrder,
			TEXT("launch order"));
		Check(
			Witness.ProfileId == Encounter.ResolvedProfileId,
			EncounterOrder,
			TEXT("profile id"));
		Check(
			Witness.AttackFaceId == Encounter.AttackFaceId,
			EncounterOrder,
			TEXT("attack face id"));
		Check(
			Witness.ProfileDescriptorHash
				== Encounter.ProfileDescriptorHash,
			EncounterOrder,
			TEXT("profile descriptor hash"));
		Check(
			Witness.ResolvedGeometryHash != 0,
			EncounterOrder,
			TEXT("geometry hash"));
		Check(
			Witness.AttackFaceHash != 0,
			EncounterOrder,
			TEXT("attack face hash"));
		Check(
			Witness.SearchEvaluationCount
				== Encounter.TotalTrajectoryEvaluations,
			EncounterOrder,
			TEXT("evaluation count"));
		Check(
			Witness.LaunchInput.SlotACellId
				< Witness.LaunchInput.SlotBCellId,
			EncounterOrder,
			TEXT("canonical slot pair"));
		if (bPriorRequired)
		{
			Check(
				Certificate.State
					== EABTSM3PriorTierCertificateState::
						CompleteInfeasible,
				EncounterOrder,
				TEXT("prior state"));
			Check(
				Certificate.ResolvedGeometryHash
					== Witness.ResolvedGeometryHash,
				EncounterOrder,
				TEXT("prior geometry binding"));
			Check(
				Certificate.ProfileDescriptorHash
					== Encounter.ProfileDescriptorHash,
				EncounterOrder,
				TEXT("prior profile binding"));
			Check(
				Certificate.AttackFaceHash
					== Witness.AttackFaceHash,
				EncounterOrder,
				TEXT("prior face binding"));
		}
		else
		{
			Check(
				Certificate.State
					== EABTSM3PriorTierCertificateState::
						NotRequired
					&& Certificate.CertificateHash == 0
					&& Certificate.ResolvedGeometryHash == 0,
				EncounterOrder,
				TEXT("non-gate certificate empty"));
		}
	}
	return bValid;
}

bool BuildSingleDisplayFixture(
	FSourceFixture& OutFixture,
	FString& OutFailure)
{
	return BuildDisplaySource(OutFixture, OutFailure)
		&& KeepFirstJoinedCandidate(OutFixture, OutFailure);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyWitnessDefaultsTest,
	"ABTS.M3.Monthly.EncounterWitness.01DefaultsAndDisabled",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyWitnessDefaultsTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3MonthlyWitnessTests;
	FString ManifestFailure;
	TestTrue(
		FString::Printf(
			TEXT("R4 acceptance manifest validates: %s"),
			*ManifestFailure),
		FABTSM3R4AcceptanceManifest::Validate(
			ManifestFailure));
	const FABTSM3MonthlyWitnessConfig DefaultConfig;
	TestFalse(
		TEXT("Gameplay finalization defaults disabled"),
		DefaultConfig.bBuildGameplayFinalize);
	FABTSM3MonthlyWitnessConfig LogVariant = DefaultConfig;
	LogVariant.bEmitWitnessLogs =
		!DefaultConfig.bEmitWitnessLogs;
	TestEqual(
		TEXT("Diagnostic log flag is outside config identity"),
		FABTSM3MonthlyWitnessBuilder::ComputeConfigHash(
			DefaultConfig),
		FABTSM3MonthlyWitnessBuilder::ComputeConfigHash(
			LogVariant));

	FABTSM3MonthlyWitnessResult Result;
	FABTSM3MonthlySpatialResult EmptySpatial;
	EmptySpatial.WorldSeed = DisplaySeed;
	FABTSM3MonthlySlingshotFieldResult EmptyField;
	EmptyField.WorldSeed = DisplaySeed;
	FString Failure;
	TestTrue(
		TEXT("Disabled build is a valid no-op"),
		FABTSM3MonthlyWitnessBuilder::Build(
			DisplaySeed,
			DefaultConfig,
			EmptySpatial,
			EmptyField,
			nullptr,
			Result,
			Failure));
	TestEqual(
		TEXT("Disabled result is not evaluated"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::NotEvaluated);
	TestFalse(
		TEXT("Disabled result has no gameplay authority"),
		Result.bGameplayFinalizeValid);
	TestFalse(
		TEXT("Disabled result is not externally certified"),
		Result.bExternalInputsCertified);
	TestFalse(
		TEXT("R4 never accepts the monthly world"),
		Result.bMonthlyWorldAccepted);
	TestTrue(
		TEXT("Disabled result retains no candidates"),
		Result.RetainedCandidates.IsEmpty());
	EABTSM3MonthlyWitnessRejectReason Reason =
		EABTSM3MonthlyWitnessRejectReason::None;
	TestTrue(
		TEXT("Disabled result validates"),
		FABTSM3MonthlyWitnessBuilder::Validate(
			DefaultConfig,
			EmptySpatial,
			EmptyField,
			Result,
			Reason,
			Failure));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyWitnessSixEncounterJoinTest,
	"ABTS.M3.Monthly.EncounterWitness.02SixWitnessesAndExactJoin",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyWitnessSixEncounterJoinTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3MonthlyWitnessTests;
	FSourceFixture Fixture;
	FString Failure;
	if (!BuildDisplaySource(Fixture, Failure))
	{
		AddError(Failure);
		return false;
	}
	Algo::Reverse(Fixture.FieldResult.RetainedCandidates);
	Fixture.FieldResult.ResultHash = static_cast<int64>(
		FABTSM3MonthlySlingshotFieldBuilder::
			ComputeResultHash(Fixture.FieldResult));
	FScriptedWitnessServices Services(Fixture.SpatialResult);
	const FABTSM3MonthlyWitnessConfig Config =
		MakeWitnessConfig();
	FABTSM3MonthlyWitnessResult Result;
	const bool bBuilt = BuildWitness(
		DisplaySeed,
		Config,
		Fixture,
		Services,
		Result,
		Failure);
	TestTrue(
		FString::Printf(
			TEXT("Exact-join display build: %s"),
			*Failure),
		bBuilt);
	if (!bBuilt)
	{
		return false;
	}
	TestTrue(
		TEXT("Fixture result is gameplay-valid"),
		Result.bGameplayFinalizeValid);
	TestEqual(
		TEXT("Authority is explicitly Fixture"),
		Result.Authority,
		EABTSM3WitnessAuthority::Fixture);
	TestFalse(
		TEXT("Fixture cannot certify external inputs"),
		Result.bExternalInputsCertified);
	TestFalse(
		TEXT("R4 cannot publish the monthly world"),
		Result.bMonthlyWorldAccepted);
	TestEqual(
		TEXT("Display config identity is frozen"),
		FABTSM3MonthlyWitnessBuilder::ComputeConfigHash(Config),
		FABTSM3R4AcceptanceManifest::
			FrozenDisplayConfigHash);
	TestEqual(
		TEXT("Display result identity is frozen"),
		static_cast<uint64>(Result.ResultHash),
		FABTSM3R4AcceptanceManifest::
			FrozenDisplayResultHash);
	TestEqual(
		TEXT("Display selected candidate identity is frozen"),
		static_cast<uint64>(
			Result.RetainedCandidates[0].CandidateHash),
		FABTSM3R4AcceptanceManifest::
			FrozenDisplayCandidateHash);
	TestEqual(
		TEXT("Display gameplay layout identity is frozen"),
		static_cast<uint64>(Result.GameplayLayoutHash),
		FABTSM3R4AcceptanceManifest::
			FrozenDisplayGameplayLayoutHash);
	TestEqual(
		TEXT("Fixture evaluates every joined source and retains the configured hard-pass Top-N"),
		Result.RetainedCandidates.Num(),
		FMath::Min(
			Config.MaximumRetainedCandidates,
			Fixture.SpatialResult.RetainedCandidates.Num()));
	for (const FABTSM3MonthlyGameplayCandidate& Candidate :
		Result.RetainedCandidates)
	{
		TestEqual(
			TEXT("Each candidate has six witnesses"),
			Candidate.Encounters.Num(),
			FABTSM3MonthlyWitnessBuilder::
				RequiredEncounterCount);
		const FABTSM3MonthlySpatialCandidate* Spatial =
			Fixture.SpatialResult.RetainedCandidates.
				FindByPredicate(
					[&Candidate](
						const FABTSM3MonthlySpatialCandidate&
							Entry)
					{
						return Entry.SourceRouteCandidateId
							== Candidate.SourceRouteCandidateId
							&& Entry.SpatialCandidateHash
								== Candidate.
									SourceSpatialCandidateHash;
					});
		TestNotNull(
			TEXT("Gameplay candidate joins exact spatial identity"),
			Spatial);
		const FABTSM3MonthlySlingshotFieldCandidate* Field =
			Fixture.FieldResult.RetainedCandidates.
				FindByPredicate(
					[&Candidate](
						const
						FABTSM3MonthlySlingshotFieldCandidate&
							Entry)
					{
						return Entry.SourceRouteCandidateId
							== Candidate.SourceRouteCandidateId
							&& Entry.CandidateHash
								== Candidate.
									SourceSlingshotFieldCandidateHash;
					});
		TestNotNull(
			TEXT("Gameplay candidate joins exact field identity"),
			Field);
		if (Spatial != nullptr)
		{
			ValidateGeneratedEncounterShape(
				*this,
				Config,
				*Spatial,
				Candidate);
		}
	}
	EABTSM3MonthlyWitnessRejectReason Reason =
		EABTSM3MonthlyWitnessRejectReason::None;
	TestTrue(
		FString::Printf(
			TEXT("Accepted result validates: %s"),
			*Failure),
		FABTSM3MonthlyWitnessBuilder::Validate(
			Config,
			Fixture.SpatialResult,
			Fixture.FieldResult,
			Result,
			Reason,
			Failure));
	if (!Result.RetainedCandidates.IsEmpty())
	{
		UE_LOG(
			LogABTSRuntime,
			Display,
			TEXT("[ABTS][M3R4][DisplayIdentity] Config=%016llX Result=%016llX Candidate=%016llX GameplayLayout=%016llX"),
			static_cast<unsigned long long>(
				FABTSM3MonthlyWitnessBuilder::
					ComputeConfigHash(Config)),
			static_cast<unsigned long long>(Result.ResultHash),
			static_cast<unsigned long long>(
				Result.RetainedCandidates[0].CandidateHash),
			static_cast<unsigned long long>(
				Result.GameplayLayoutHash));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyWitnessNonAnchorPairTest,
	"ABTS.M3.Monthly.EncounterWitness.03NonFirstNonAnchorSlotPair",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyWitnessNonAnchorPairTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3MonthlyWitnessTests;
	FSourceFixture Fixture;
	FString Failure;
	if (!BuildSingleDisplayFixture(Fixture, Failure))
	{
		AddError(Failure);
		return false;
	}
	FScriptedWitnessServices Services(Fixture.SpatialResult);
	const FABTSM3MonthlyWitnessConfig Config =
		MakeWitnessConfig();
	FABTSM3MonthlyWitnessResult Result;
	if (!BuildWitness(
		DisplaySeed,
		Config,
		Fixture,
		Services,
		Result,
		Failure))
	{
		AddError(Failure);
		return false;
	}
	const FABTSM3BallisticWitness& Witness =
		Result.RetainedCandidates[0].Encounters[0].
			PositiveWitness;
	const int32 E1EncounterId =
		Fixture.SpatialResult.RetainedCandidates[0].
			Encounters[0].Contract.EncounterId;
	const FABTSM3MonthlySlingshotField* Field =
		Fixture.FieldResult.RetainedCandidates[0].
			Fields.FindByPredicate([E1EncounterId](
				const FABTSM3MonthlySlingshotField& Entry)
			{
				return Entry.Kind
					== EABTSM3MonthlySlingshotFieldKind::
						EncounterRequired
					&& Entry.EncounterId == E1EncounterId;
			});
	TestNotNull(TEXT("E1 field exists"), Field);
	if (Field == nullptr)
	{
		return false;
	}
	TestNotEqual(
		TEXT("Witness slot A is not the prescribed anchor"),
		Witness.LaunchInput.SlotACellId,
		Field->AnchorCellId);
	TestNotEqual(
		TEXT("Witness slot B is not the prescribed anchor"),
		Witness.LaunchInput.SlotBCellId,
		Field->AnchorCellId);
	TArray<int32> OrderedCells = Field->SlotCellIds;
	OrderedCells.Sort();
	const FIntPoint FirstPair = NormalizePair(
		OrderedCells[0],
		OrderedCells[1]);
	TestNotEqual(
		TEXT("Scripted success is not the first enumerated pair"),
		NormalizePair(
			Witness.LaunchInput.SlotACellId,
			Witness.LaunchInput.SlotBCellId),
		FirstPair);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyWitnessPriorDomainTest,
	"ABTS.M3.Monthly.EncounterWitness.04PriorTierFullDomainAndBudget",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyWitnessPriorDomainTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3MonthlyWitnessTests;
	FSourceFixture Fixture;
	FString Failure;
	if (!BuildSingleDisplayFixture(Fixture, Failure))
	{
		AddError(Failure);
		return false;
	}
	FScriptedWitnessServices Services(Fixture.SpatialResult);
	const FABTSM3MonthlyWitnessConfig Config =
		MakeWitnessConfig();
	FABTSM3MonthlyWitnessResult Result;
	if (!BuildWitness(
		DisplaySeed,
		Config,
		Fixture,
		Services,
		Result,
		Failure))
	{
		AddError(Failure);
		return false;
	}
	const FABTSM3MonthlyGameplayCandidate& Candidate =
		Result.RetainedCandidates[0];
	for (int32 EncounterId = 0;
		EncounterId < Candidate.Encounters.Num();
		++EncounterId)
	{
		const FABTSM3MonthlyEncounterGameplay& Encounter =
			Candidate.Encounters[EncounterId];
		TestEqual(
			TEXT("Gameplay encounter order remains array order"),
			Encounter.EncounterOrder,
			EncounterId);
		TestEqual(
			TEXT("Gameplay encounter keeps R3 stable identity"),
			Encounter.EncounterId,
			Fixture.SpatialResult.RetainedCandidates[0].
				Encounters[EncounterId].Contract.EncounterId);
		const bool bPriorRequired =
			Config.PriorTierRequiredEncounterOrders.Contains(
				EncounterId);
		if (bPriorRequired)
		{
			TestEqual(
				TEXT("Prior certificate is complete infeasible"),
				Encounter.PriorTierCertificate.State,
				EABTSM3PriorTierCertificateState::
					CompleteInfeasible);
			TestEqual(
				TEXT("Prior domain completed every planned input"),
				Encounter.PriorTierCertificate.
					CompletedInputCount,
				Encounter.PriorTierCertificate.
					PlannedInputCount);
			TestEqual(
				TEXT("Seven-slot fixture covers all 21 pairs, both sides, every pull/aim sample, and all three Simple birds"),
				Encounter.PriorTierCertificate.
					PlannedInputCount,
				21 * 2 * Config.PullAlphaSampleCount * 5 * 3);
		}
		else
		{
			TestEqual(
				TEXT("Non-gate encounter does not fabricate a prior certificate"),
				Encounter.PriorTierCertificate.State,
				EABTSM3PriorTierCertificateState::
					NotRequired);
		}
		TestTrue(
			TEXT("Per-encounter evaluation count stays within hard budget"),
			Encounter.TotalTrajectoryEvaluations > 0
				&& Encounter.TotalTrajectoryEvaluations
					<= Config.
						MaxWitnessEvaluationsPerEncounter);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyWitnessFlowClosureTest,
	"ABTS.M3.Monthly.EncounterWitness.05FlowResourceAndBridgeClosure",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyWitnessFlowClosureTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3MonthlyWitnessTests;
	FSourceFixture Fixture;
	FString Failure;
	if (!BuildSingleDisplayFixture(Fixture, Failure))
	{
		AddError(Failure);
		return false;
	}
	FScriptedWitnessServices Services(Fixture.SpatialResult);
	FABTSM3MonthlyWitnessResult Result;
	if (!BuildWitness(
		DisplaySeed,
		MakeWitnessConfig(),
		Fixture,
		Services,
		Result,
		Failure))
	{
		AddError(Failure);
		return false;
	}
	const FABTSM3MonthlyFlowClosure& Flow =
		Result.RetainedCandidates[0].FlowClosure;
	TestTrue(TEXT("Flow closure valid"), Flow.bFlowValid);
	TestTrue(
		TEXT("Flow proves the workbench station is available"),
		Flow.bWorkbenchStationAvailable);
	TestTrue(
		TEXT("Flow proves the finale furnace is available"),
		Flow.bFurnaceStationAvailable);
	TestTrue(
		TEXT("Bridge is blocked before crafting"),
		Flow.bBridgeBlockedBefore);
	TestTrue(
		TEXT("Bridge is reachable after crafting"),
		Flow.bBridgeReachableAfter);
	TestTrue(
		TEXT("Bridge cut proves there is no pre-craft bypass"),
		Flow.bBridgeNoBypass);
	TestEqual(
		TEXT("Default branch count is zero"),
		Flow.BranchCount,
		0);
	TestEqual(
		TEXT("Default branch is explicitly not required"),
		Flow.BranchUtility,
		EABTSM3BranchUtilityState::NotRequired);
	TestEqual(
		TEXT("Flow persists the candidate-addressed bridge proof"),
		Flow.BridgeEvidence.SourceRouteCandidateId,
		Result.RetainedCandidates[0].SourceRouteCandidateId);
	TestEqual(
		TEXT("Flow records the exact 15-step closure"),
		Flow.Steps.Num(),
		15);
	TestEqual(
		TEXT("Final ledger has two Space stakes"),
		GetItemQuantity(
			Flow.FinalItems,
			EABTSItemId::SpaceStake),
		2);
	TestEqual(
		TEXT("Final ledger has one Space cord"),
		GetItemQuantity(
			Flow.FinalItems,
			EABTSItemId::SpaceCord),
		1);
	TestEqual(
		TEXT("All finale metal is consumed"),
		GetItemQuantity(
			Flow.FinalItems,
			EABTSItemId::MetalParts),
		0);
	TestEqual(
		TEXT("All finale wood is consumed"),
		GetItemQuantity(
			Flow.FinalItems,
			EABTSItemId::Wood),
		0);
	TestEqual(
		TEXT("The Space cord consumes the crystal core"),
		GetItemQuantity(
			Flow.FinalItems,
			EABTSItemId::CrystalCore),
		0);
	TestFalse(
		TEXT("Consumed crystal core no longer grants HaveCrystalCore"),
		Flow.FinalKeys.Contains(
			EABTSM3ProgressKey::HaveCrystalCore));
	for (const FABTSM3WitnessFlowStep& Step : Flow.Steps)
	{
		const bool bFinaleRecipe =
			Step.StepId == FName(TEXT("SpaceStakePair"))
			|| Step.StepId == FName(TEXT("SpaceCord"));
		TestEqual(
			TEXT("Only finale recipes require the furnace"),
			Step.RequiredStation,
			bFinaleRecipe
				? EABTSCraftingStationType::Furnace
				: EABTSCraftingStationType::None);
		if (Step.Kind
			== EABTSM3FlowStepKind::EncounterReward)
		{
			TestTrue(
				TEXT("Reward step order is in range"),
				Step.EncounterOrder >= 0
					&& Step.EncounterOrder < 6);
			if (Step.EncounterOrder >= 0
				&& Step.EncounterOrder < 6)
			{
				TestEqual(
					TEXT("Reward step uses stable R3 encounter identity"),
					Step.EncounterId,
					Result.RetainedCandidates[0].
						Encounters[Step.EncounterOrder].
							EncounterId);
			}
		}
	}
	TestTrue(
		TEXT("Flow records a bridge gate"),
		Flow.Steps.ContainsByPredicate([](
			const FABTSM3WitnessFlowStep& Step)
		{
			return Step.Kind
				== EABTSM3FlowStepKind::BridgeGate;
		}));
	TestTrue(
		TEXT("Flow reaches finale entry"),
		Flow.Steps.ContainsByPredicate([](
			const FABTSM3WitnessFlowStep& Step)
		{
			return Step.Kind
				== EABTSM3FlowStepKind::FinaleEntry;
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyWitnessM9CausalityTest,
	"ABTS.M3.Monthly.EncounterWitness.06M9CausalAblation",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyWitnessM9CausalityTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3MonthlyWitnessTests;
	FSourceFixture Fixture;
	FString Failure;
	if (!BuildSingleDisplayFixture(Fixture, Failure))
	{
		AddError(Failure);
		return false;
	}
	FScriptedWitnessServices Services(Fixture.SpatialResult);
	const FABTSM3MonthlyWitnessConfig Config =
		MakeWitnessConfig();
	FABTSM3MonthlyWitnessResult Result;
	if (!BuildWitness(
		DisplaySeed,
		Config,
		Fixture,
		Services,
		Result,
		Failure))
	{
		AddError(Failure);
		return false;
	}
	const FABTSM3BallisticWitness& E5 =
		Result.RetainedCandidates[0].Encounters[
			Config.M9PracticeEncounterOrder].PositiveWitness;
	TestTrue(
		TEXT("Winning E5 query samples M9"),
		E5.M9QueryCount > 0
			&& E5.M9NonZeroAccelerationCount > 0
			&& E5.PeakM9AccelerationCMPerSecSq > 0.0f);
	TestTrue(
		TEXT("Same launch misses without M9 by required margin"),
		E5.M9AblationMissCM
			>= Config.MinimumM9AblationMissCM);
	TestTrue(
		TEXT("Recorded winning launch keeps M9 enabled"),
		E5.LaunchInput.bEnableSatelliteGravity);
	TestFalse(
		TEXT("Stored counterfactual disables M9"),
		E5.M9AblationEvidence.LaunchInput.
			bEnableSatelliteGravity);
	TestTrue(
		TEXT("Stored counterfactual carries complete trajectory"),
		E5.M9AblationEvidence.Samples.Num() >= 2
			&& E5.M9AblationEvidence.Termination
				!= EABTSM3TrajectoryTermination::TargetHit
			&& E5.M9AblationEvidence.EvidenceHash != 0);
	TestFalse(
		TEXT("E5 persists the satellite collider as forbidden geometry"),
		E5.M9SatelliteForbiddenSphere.VolumeId.IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyWitnessDeterminismTest,
	"ABTS.M3.Monthly.EncounterWitness.07DeterminismAndTamper",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyWitnessDeterminismTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3MonthlyWitnessTests;
	FSourceFixture Fixture;
	FString Failure;
	if (!BuildSingleDisplayFixture(Fixture, Failure))
	{
		AddError(Failure);
		return false;
	}
	const FABTSM3MonthlyWitnessConfig Config =
		MakeWitnessConfig();
	FScriptedWitnessServices FirstServices(
		Fixture.SpatialResult);
	FScriptedWitnessServices SecondServices(
		Fixture.SpatialResult);
	FABTSM3MonthlyWitnessResult First;
	FABTSM3MonthlyWitnessResult Second;
	const bool bFirst = BuildWitness(
		DisplaySeed,
		Config,
		Fixture,
		FirstServices,
		First,
		Failure);
	const bool bSecond = BuildWitness(
		DisplaySeed,
		Config,
		Fixture,
		SecondServices,
		Second,
		Failure);
	TestTrue(TEXT("First deterministic build"), bFirst);
	TestTrue(TEXT("Second deterministic build"), bSecond);
	if (!bFirst || !bSecond)
	{
		return false;
	}
	TestTrue(
		TEXT("Whole witness result is deterministic"),
		ResultsEqual(First, Second));

	FABTSM3MonthlyWitnessResult Tampered = First;
	Tampered.RetainedCandidates[0].Encounters[0].
		PositiveWitness.WitnessHash ^= 1;
	EABTSM3MonthlyWitnessRejectReason Reason =
		EABTSM3MonthlyWitnessRejectReason::None;
	TestFalse(
		TEXT("Witness payload tamper fails closed"),
		FABTSM3MonthlyWitnessBuilder::Validate(
			Config,
			Fixture.SpatialResult,
			Fixture.FieldResult,
			Tampered,
			Reason,
			Failure));
	TestEqual(
		TEXT("Tamper reports hash mismatch"),
		Reason,
		EABTSM3MonthlyWitnessRejectReason::HashMismatch);
	FABTSM3MonthlyWitnessResult AblationTampered = First;
	AblationTampered.RetainedCandidates[0].Encounters[4].
		PositiveWitness.M9AblationEvidence.Samples[0].
			PositionWorldCM.X += 1.0;
	TestFalse(
		TEXT("M9 counterfactual sample tamper fails deep validation"),
		FABTSM3MonthlyWitnessBuilder::Validate(
			Config,
			Fixture.SpatialResult,
			Fixture.FieldResult,
			AblationTampered,
			Reason,
			Failure));
	FABTSM3MonthlyWitnessResult DefaultEvidenceTampered =
		First;
	DefaultEvidenceTampered.RetainedCandidates[0].
		Encounters[0].PositiveWitness.
			M9AblationEvidence.LaunchInput.Bird =
				EABTSBirdId::Black;
	TestFalse(
		TEXT("Unhashed non-E5 default-evidence tamper fails closed"),
		FABTSM3MonthlyWitnessBuilder::Validate(
			Config,
			Fixture.SpatialResult,
			Fixture.FieldResult,
			DefaultEvidenceTampered,
			Reason,
			Failure));
	FABTSM3MonthlyWitnessResult DefaultCertificateTampered =
		First;
	DefaultCertificateTampered.RetainedCandidates[0].
		Encounters[0].PriorTierCertificate.ClosestMissCM = 123;
	TestFalse(
		TEXT("Unhashed NotRequired certificate tamper fails closed"),
		FABTSM3MonthlyWitnessBuilder::Validate(
			Config,
			Fixture.SpatialResult,
			Fixture.FieldResult,
			DefaultCertificateTampered,
			Reason,
			Failure));
	FABTSM3MonthlyWitnessResult DefaultBirdCatalogTampered =
		First;
	DefaultBirdCatalogTampered.RetainedCandidates[0].
		Encounters[0].PriorTierCertificate.
			EligibleBirdCatalogHash = 1;
	TestFalse(
		TEXT("Unhashed NotRequired bird-domain tamper fails closed"),
		FABTSM3MonthlyWitnessBuilder::Validate(
			Config,
			Fixture.SpatialResult,
			Fixture.FieldResult,
			DefaultBirdCatalogTampered,
			Reason,
			Failure));
	FABTSM3MonthlyWitnessResult FlowTampered = First;
	FABTSM3MonthlyGameplayCandidate& FlowCandidate =
		FlowTampered.RetainedCandidates[0];
	check(FlowCandidate.FlowClosure.Steps.Num() > 4);
	check(!FlowCandidate.FlowClosure.Steps[4].
		ItemDeltas.IsEmpty());
	++FlowCandidate.FlowClosure.Steps[4].
		ItemDeltas[0].Quantity;
	FlowCandidate.FlowClosure.FlowHash = static_cast<int64>(
		FABTSM3MonthlyWitnessBuilder::
			ComputeFlowClosureHash(
				FlowCandidate.FlowClosure));
	FlowCandidate.CandidateHash = static_cast<int64>(
		FABTSM3MonthlyWitnessBuilder::ComputeCandidateHash(
			FlowCandidate));
	FlowTampered.GameplayLayoutHash = static_cast<int64>(
		FABTSM3MonthlyWitnessBuilder::
			ComputeGameplayLayoutHash(FlowTampered));
	FlowTampered.ResultHash = static_cast<int64>(
		FABTSM3MonthlyWitnessBuilder::ComputeResultHash(
			FlowTampered));
	TestFalse(
		TEXT("Self-consistent flow delta tamper fails deep replay"),
		FABTSM3MonthlyWitnessBuilder::Validate(
			Config,
			Fixture.SpatialResult,
			Fixture.FieldResult,
			FlowTampered,
			Reason,
			Failure));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyWitnessSweep100Test,
	"ABTS.M3.Monthly.EncounterWitness.08Sweep100",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyWitnessSweep100Test::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3MonthlyWitnessTests;
	FABTSM3MonthlyWitnessConfig Config =
		MakeWitnessConfig();
	TArray<double> DurationsMS;
	uint64 Oracle = 14695981039346656037ull;
	int32 Accepted = 0;
	const TConstArrayView<int32> SweepSeeds =
		FABTSM3R4AcceptanceManifest::GetSweepSeeds();
	for (const int32 Seed : SweepSeeds)
	{
		const double StartSeconds = FPlatformTime::Seconds();
		FSourceFixture Fixture;
		FString Failure;
		if (!BuildSourceFixture(Seed, Fixture, Failure))
		{
			AddError(FString::Printf(
				TEXT("Seed %d source failed: %s"),
				Seed,
				*Failure));
			continue;
		}
		FScriptedWitnessServices Services(
			Fixture.SpatialResult);
		FABTSM3MonthlyWitnessResult Result;
		const bool bBuilt = BuildWitness(
			Seed,
			Config,
			Fixture,
			Services,
			Result,
			Failure);
		DurationsMS.Add(
			(FPlatformTime::Seconds() - StartSeconds) * 1000.0);
		if (!bBuilt)
		{
			AddError(FString::Printf(
				TEXT("Seed %d witness failed: %s"),
				Seed,
				*Failure));
			continue;
		}
		++Accepted;
		EABTSM3MonthlyWitnessRejectReason ValidateReason =
			EABTSM3MonthlyWitnessRejectReason::None;
		TestTrue(
			TEXT("Every sweep result passes deep validation"),
			FABTSM3MonthlyWitnessBuilder::Validate(
				Config,
				Fixture.SpatialResult,
				Fixture.FieldResult,
				Result,
				ValidateReason,
				Failure));
		TestEqual(
			TEXT("Every source candidate hard-passes"),
			Result.HardPassCandidateCount,
			Fixture.SpatialResult.RetainedCandidates.Num());
		TestEqual(
			TEXT("Top-N retains the expected hard-pass subset"),
			Result.RetainedCandidates.Num(),
			FMath::Min(
				Config.MaximumRetainedCandidates,
				Fixture.SpatialResult.
					RetainedCandidates.Num()));
		TestFalse(
			TEXT("Sweep fixture never becomes externally certified"),
			Result.bExternalInputsCertified);
		TestFalse(
			TEXT("Sweep fixture never accepts monthly world"),
			Result.bMonthlyWorldAccepted);
		Oracle = MixOracle(
			Oracle,
			static_cast<uint32>(Seed));
		Oracle = MixOracle(
			Oracle,
			static_cast<uint64>(Result.ResultHash));
	}
	DurationsMS.Sort();
	const int32 P95Index = FMath::Clamp(
		FMath::CeilToInt(
			DurationsMS.Num() * 0.95) - 1,
		0,
		FMath::Max(0, DurationsMS.Num() - 1));
	const double P95MS = DurationsMS.IsEmpty()
		? 0.0
		: DurationsMS[P95Index];
	const double MaxMS = DurationsMS.IsEmpty()
		? 0.0
		: DurationsMS.Last();
	TestEqual(
		TEXT("Sweep terminal count"),
		DurationsMS.Num(),
		SweepSeeds.Num());
	TestEqual(
		TEXT("Sweep accepted count"),
		Accepted,
		SweepSeeds.Num());
	TestTrue(
		TEXT("Sweep P95 stays below external 2s gate"),
		P95MS < 2000.0);
	TestTrue(
		TEXT("Sweep maximum stays below external 5s gate"),
		MaxMS < 5000.0);
	TestEqual(
		TEXT("Sweep oracle is frozen"),
		Oracle,
		FABTSM3R4AcceptanceManifest::
			FrozenSweepOracleHash);
	UE_LOG(
		LogABTSRuntime,
		Display,
		TEXT("[ABTS][M3R4][WitnessSweep] Terminal=%d Accepted=%d P95MS=%.3f MaxMS=%.3f Oracle=%016llX"),
		DurationsMS.Num(),
		Accepted,
		P95MS,
		MaxMS,
		static_cast<unsigned long long>(Oracle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyWitnessProviderFailureTest,
	"ABTS.M3.Monthly.EncounterWitnessFailure.Provider",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyWitnessProviderFailureTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3MonthlyWitnessTests;
	FSourceFixture Fixture;
	FString Failure;
	if (!BuildSingleDisplayFixture(Fixture, Failure))
	{
		AddError(Failure);
		return false;
	}
	const FABTSM3MonthlyWitnessConfig Config =
		MakeWitnessConfig();
	FABTSM3MonthlyWitnessResult Result;
	TestFalse(
		TEXT("Missing provider fails closed"),
		FABTSM3MonthlyWitnessBuilder::Build(
			DisplaySeed,
			Config,
			Fixture.SpatialResult,
			Fixture.FieldResult,
			nullptr,
			Result,
			Failure));
	TestEqual(
		TEXT("Missing provider reason"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			ProviderUnavailable);
	FScriptedWitnessServices Invalid(Fixture.SpatialResult);
	Invalid.bIdentityInvalid = true;
	TestFalse(
		TEXT("Invalid provider identity fails closed"),
		BuildWitness(
			DisplaySeed,
			Config,
			Fixture,
			Invalid,
			Result,
			Failure));
	TestEqual(
		TEXT("Invalid identity reason"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			ProviderIdentityMismatch);
	FScriptedWitnessServices FixtureClaimsCertified(
		Fixture.SpatialResult);
	FixtureClaimsCertified.bFixtureClaimsCertified = true;
	TestFalse(
		TEXT("Fixture authority cannot claim external certification"),
		BuildWitness(
			DisplaySeed,
			Config,
			Fixture,
			FixtureClaimsCertified,
			Result,
			Failure));
	TestEqual(
		TEXT("Fixture certification claim fails provider identity"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			ProviderIdentityMismatch);
	FScriptedWitnessServices MissingBird(
		Fixture.SpatialResult);
	MissingBird.bMissingEligibleBird = true;
	TestFalse(
		TEXT("Incomplete eligible-bird domain fails closed"),
		BuildWitness(
			DisplaySeed,
			Config,
			Fixture,
			MissingBird,
			Result,
			Failure));
	TestEqual(
		TEXT("Missing bird is a provider identity failure"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			ProviderIdentityMismatch);
	FScriptedWitnessServices ExtraBird(
		Fixture.SpatialResult);
	ExtraBird.bExtraEligibleBird = true;
	TestFalse(
		TEXT("Expanded or duplicate eligible-bird domain fails closed"),
		BuildWitness(
			DisplaySeed,
			Config,
			Fixture,
			ExtraBird,
			Result,
			Failure));
	TestEqual(
		TEXT("Extra bird is a provider identity failure"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			ProviderIdentityMismatch);
	FScriptedWitnessServices Integration(
		Fixture.SpatialResult);
	Integration.bUseIntegrationAuthority = true;
	TestFalse(
		TEXT("Synthetic R4 flow cannot claim Integration authority"),
		BuildWitness(
			DisplaySeed,
			Config,
			Fixture,
			Integration,
			Result,
			Failure));
	TestFalse(
		TEXT("Rejected synthetic Integration is never externally certified"),
		Result.bExternalInputsCertified);
	TestFalse(
		TEXT("Rejected synthetic Integration cannot promote the world"),
		Result.bMonthlyWorldAccepted);
	TestEqual(
		TEXT("Integration remains an explicit pending provider boundary"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			ProviderIdentityMismatch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyWitnessCatalogFailureTest,
	"ABTS.M3.Monthly.EncounterWitnessFailure.Catalog",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyWitnessCatalogFailureTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3MonthlyWitnessTests;
	FSourceFixture Fixture;
	FString Failure;
	if (!BuildSingleDisplayFixture(Fixture, Failure))
	{
		AddError(Failure);
		return false;
	}
	FScriptedWitnessServices Services(Fixture.SpatialResult);
	Services.bCatalogIdentityCorrupt = true;
	FABTSM3MonthlyWitnessResult Result;
	TestFalse(
		TEXT("Catalog identity corruption fails closed"),
		BuildWitness(
			DisplaySeed,
			MakeWitnessConfig(),
			Fixture,
			Services,
			Result,
			Failure));
	TestEqual(
		TEXT("Catalog failure reason"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			ProfileCatalogMismatch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyWitnessJoinFailureTest,
	"ABTS.M3.Monthly.EncounterWitnessFailure.ExactJoin",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyWitnessJoinFailureTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3MonthlyWitnessTests;
	FSourceFixture Fixture;
	FString Failure;
	if (!BuildSingleDisplayFixture(Fixture, Failure))
	{
		AddError(Failure);
		return false;
	}
	const FSourceFixture Clean = Fixture;
	FABTSM3MonthlyWitnessResult Result;
	Fixture.SpatialResult.RetainedCandidates[0].
		SpatialScore += 1;
	FScriptedWitnessServices SpatialPayloadServices(
		Fixture.SpatialResult);
	TestFalse(
		TEXT("Unhashed R3 payload tamper fails closed"),
		BuildWitness(
			DisplaySeed,
			MakeWitnessConfig(),
			Fixture,
			SpatialPayloadServices,
			Result,
			Failure));
	TestEqual(
		TEXT("R3 payload tamper is InvalidSource"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::InvalidSource);

	Fixture = Clean;
	Fixture.FieldResult.RetainedCandidates[0].
		Fields[0].SlotCellIds[0] += 1;
	FScriptedWitnessServices FieldPayloadServices(
		Fixture.SpatialResult);
	TestFalse(
		TEXT("Unhashed R3.1 field payload tamper fails closed"),
		BuildWitness(
			DisplaySeed,
			MakeWitnessConfig(),
			Fixture,
			FieldPayloadServices,
			Result,
			Failure));
	TestEqual(
		TEXT("R3.1 payload tamper is InvalidSource"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::InvalidSource);

	Fixture = Clean;
	Fixture.SpatialResult.SchemaVersion = 2;
	Fixture.SpatialResult.SpatialResultHash =
		static_cast<int64>(
			FABTSM3MonthlyEncounterBuilder::
				ComputeResultHash(
					Fixture.SpatialResult));
	Fixture.FieldResult.SourceSpatialResultHash =
		Fixture.SpatialResult.SpatialResultHash;
	Fixture.FieldResult.ResultHash = static_cast<int64>(
		FABTSM3MonthlySlingshotFieldBuilder::
			ComputeResultHash(Fixture.FieldResult));
	FScriptedWitnessServices FutureSpatialSchema(
		Fixture.SpatialResult);
	TestFalse(
		TEXT("Unknown R3 schema fails closed even when re-signed"),
		BuildWitness(
			DisplaySeed,
			MakeWitnessConfig(),
			Fixture,
			FutureSpatialSchema,
			Result,
			Failure));
	TestEqual(
		TEXT("Unknown R3 schema is InvalidSource"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::InvalidSource);

	Fixture = Clean;
	Fixture.FieldResult.SchemaVersion = 2;
	Fixture.FieldResult.ResultHash = static_cast<int64>(
		FABTSM3MonthlySlingshotFieldBuilder::
			ComputeResultHash(Fixture.FieldResult));
	FScriptedWitnessServices FutureFieldSchema(
		Fixture.SpatialResult);
	TestFalse(
		TEXT("Unknown R3.1 schema fails closed even when re-signed"),
		BuildWitness(
			DisplaySeed,
			MakeWitnessConfig(),
			Fixture,
			FutureFieldSchema,
			Result,
			Failure));
	TestEqual(
		TEXT("Unknown R3.1 schema is InvalidSource"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::InvalidSource);

	Fixture = Clean;
	FABTSM3MonthlySlingshotFieldCandidate& FieldCandidate =
		Fixture.FieldResult.RetainedCandidates[0];
	FieldCandidate.SourceSpatialCandidateHash ^= 1;
	FieldCandidate.CandidateHash = static_cast<int64>(
		FABTSM3MonthlySlingshotFieldBuilder::
			ComputeCandidateHash(FieldCandidate));
	Fixture.FieldResult.ResultHash = static_cast<int64>(
		FABTSM3MonthlySlingshotFieldBuilder::
			ComputeResultHash(Fixture.FieldResult));
	FScriptedWitnessServices JoinServices(
		Fixture.SpatialResult);
	TestFalse(
		TEXT("Hash-consistent but mismatched source join fails closed"),
		BuildWitness(
			DisplaySeed,
			MakeWitnessConfig(),
			Fixture,
			JoinServices,
			Result,
			Failure));
	TestEqual(
		TEXT("Join failure reason"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			CandidateJoinMismatch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyWitnessProfileFailureTest,
	"ABTS.M3.Monthly.EncounterWitnessFailure.ProfileFreeze",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyWitnessProfileFailureTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3MonthlyWitnessTests;
	FSourceFixture Fixture;
	FString Failure;
	if (!BuildSingleDisplayFixture(Fixture, Failure))
	{
		AddError(Failure);
		return false;
	}
	FScriptedWitnessServices Services(Fixture.SpatialResult);
	Services.bProfileBoundsMismatch = true;
	FABTSM3MonthlyWitnessResult Result;
	TestFalse(
		TEXT("R3 profile bounds mismatch fails closed"),
		BuildWitness(
			DisplaySeed,
			MakeWitnessConfig(),
			Fixture,
			Services,
			Result,
			Failure));
	TestEqual(
		TEXT("Profile freeze failure reason"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			ProfileMismatch);
	FScriptedWitnessServices LateSpeed(
		Fixture.SpatialResult);
	LateSpeed.bImpactSpeedOnlyAfterEntry = true;
	TestFalse(
		TEXT("Speed gained only after first sphere entry cannot sign a hit"),
		BuildWitness(
			DisplaySeed,
			MakeWitnessConfig(),
			Fixture,
			LateSpeed,
			Result,
			Failure));
	TestEqual(
		TEXT("First-contact speed gate rejects the positive witness"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			PositiveWitnessNotFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyWitnessBudgetFailureTest,
	"ABTS.M3.Monthly.EncounterWitnessFailure.Budget",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyWitnessBudgetFailureTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3MonthlyWitnessTests;
	FSourceFixture Fixture;
	FString Failure;
	if (!BuildSingleDisplayFixture(Fixture, Failure))
	{
		AddError(Failure);
		return false;
	}
	FABTSM3MonthlyWitnessConfig Config =
		MakeWitnessConfig();
	Config.MaxWitnessEvaluationsPerEncounter = 10;
	FScriptedWitnessServices Services(Fixture.SpatialResult);
	FABTSM3MonthlyWitnessResult Result;
	TestFalse(
		TEXT("Incomplete search budget fails closed"),
		BuildWitness(
			DisplaySeed,
			Config,
			Fixture,
			Services,
			Result,
			Failure));
	TestEqual(
		TEXT("Budget failure reason"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			SearchBudgetExceeded);
	TestTrue(
		TEXT("Budget check never performs evaluation 11"),
		Services.EvaluationCount <= 10);
	FScriptedWitnessServices InvalidPriorTermination(
		Fixture.SpatialResult);
	InvalidPriorTermination.bInvalidPriorTermination = true;
	TestFalse(
		TEXT("Invalid termination cannot sign a prior-tier certificate"),
		BuildWitness(
			DisplaySeed,
			MakeWitnessConfig(),
			Fixture,
			InvalidPriorTermination,
			Result,
			Failure));
	TestEqual(
		TEXT("Invalid prior trajectory is a provider identity failure"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			ProviderIdentityMismatch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyWitnessM9FailureTest,
	"ABTS.M3.Monthly.EncounterWitnessFailure.M9Evidence",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyWitnessM9FailureTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3MonthlyWitnessTests;
	FSourceFixture Fixture;
	FString Failure;
	if (!BuildSingleDisplayFixture(Fixture, Failure))
	{
		AddError(Failure);
		return false;
	}
	FScriptedWitnessServices Services(Fixture.SpatialResult);
	Services.bMissingM9Evidence = true;
	FABTSM3MonthlyWitnessResult Result;
	TestFalse(
		TEXT("M9 claim without causal evidence fails closed"),
		BuildWitness(
			DisplaySeed,
			MakeWitnessConfig(),
			Fixture,
			Services,
			Result,
			Failure));
	TestEqual(
		TEXT("M9 failure reason"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			M9EvidenceMissing);
	FScriptedWitnessServices MissingForbiddenVolume(
		Fixture.SpatialResult);
	MissingForbiddenVolume.bMissingM9ForbiddenVolume = true;
	TestFalse(
		TEXT("E5 without a satellite forbidden volume fails closed"),
		BuildWitness(
			DisplaySeed,
			MakeWitnessConfig(),
			Fixture,
			MissingForbiddenVolume,
			Result,
			Failure));
	TestEqual(
		TEXT("Missing E5 collider is a geometry failure"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			GeometryInvalid);
	FScriptedWitnessServices IgnoresDisable(
		Fixture.SpatialResult);
	IgnoresDisable.bIgnoreM9Disable = true;
	TestFalse(
		TEXT("Provider that ignores M9-off input fails closed"),
		BuildWitness(
			DisplaySeed,
			MakeWitnessConfig(),
			Fixture,
			IgnoresDisable,
			Result,
			Failure));
	TestEqual(
		TEXT("Ignored M9 disable is an identity failure"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			ProviderIdentityMismatch);
	FScriptedWitnessServices InvalidAblationTermination(
		Fixture.SpatialResult);
	InvalidAblationTermination.bInvalidAblationTermination = true;
	TestFalse(
		TEXT("Invalid M9-off termination fails closed"),
		BuildWitness(
			DisplaySeed,
			MakeWitnessConfig(),
			Fixture,
			InvalidAblationTermination,
			Result,
			Failure));
	TestEqual(
		TEXT("Invalid ablation is a provider identity failure"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			ProviderIdentityMismatch);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyWitnessProgressionFailureTest,
	"ABTS.M3.Monthly.EncounterWitnessFailure.Progression",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyWitnessProgressionFailureTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3MonthlyWitnessTests;
	FSourceFixture Fixture;
	FString Failure;
	if (!BuildSingleDisplayFixture(Fixture, Failure))
	{
		AddError(Failure);
		return false;
	}
	FScriptedWitnessServices Services(Fixture.SpatialResult);
	Services.bProgressionMissingRecipe = true;
	FABTSM3MonthlyWitnessResult Result;
	TestFalse(
		TEXT("Missing finale recipe fails closed"),
		BuildWitness(
			DisplaySeed,
			MakeWitnessConfig(),
			Fixture,
			Services,
			Result,
			Failure));
	TestEqual(
		TEXT("Progression failure reason"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			ProgressionInvalid);
	FScriptedWitnessServices InitialInventory(
		Fixture.SpatialResult);
	InitialInventory.bInitialInventoryNonEmpty = true;
	TestFalse(
		TEXT("Frozen closure rejects unearned initial inventory"),
		BuildWitness(
			DisplaySeed,
			MakeWitnessConfig(),
			Fixture,
			InitialInventory,
			Result,
			Failure));
	TestEqual(
		TEXT("Initial inventory rejection is progression-invalid"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			ProgressionInvalid);
	FScriptedWitnessServices FurnaceUnavailable(
		Fixture.SpatialResult);
	FurnaceUnavailable.bFurnaceUnavailable = true;
	TestFalse(
		TEXT("Finale recipes cannot close without a furnace"),
		BuildWitness(
			DisplaySeed,
			MakeWitnessConfig(),
			Fixture,
			FurnaceUnavailable,
			Result,
			Failure));
	TestEqual(
		TEXT("Missing furnace is progression-invalid"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			ProgressionInvalid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3MonthlyWitnessBridgeFailureTest,
	"ABTS.M3.Monthly.EncounterWitnessFailure.Bridge",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3MonthlyWitnessBridgeFailureTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM3MonthlyWitnessTests;
	FSourceFixture Fixture;
	FString Failure;
	if (!BuildSingleDisplayFixture(Fixture, Failure))
	{
		AddError(Failure);
		return false;
	}
	FScriptedWitnessServices Services(Fixture.SpatialResult);
	Services.bBridgeInvalid = true;
	FABTSM3MonthlyWitnessResult Result;
	TestFalse(
		TEXT("Bridge evidence without no-bypass proof fails closed"),
		BuildWitness(
			DisplaySeed,
			MakeWitnessConfig(),
			Fixture,
			Services,
			Result,
			Failure));
	TestEqual(
		TEXT("Bridge failure reason"),
		Result.RejectReason,
		EABTSM3MonthlyWitnessRejectReason::
			BridgeEvidenceInvalid);
	return true;
}

#endif
