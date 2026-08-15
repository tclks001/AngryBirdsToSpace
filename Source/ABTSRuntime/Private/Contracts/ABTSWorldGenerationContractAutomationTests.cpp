// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Contracts/ABTSWorldGenerationContracts.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Terrain/ABTSM3Planet.h"

namespace
{
FABTSGeneratedWorldIdentity MakeAcceptedIdentity()
{
	FABTSGeneratedWorldIdentity Identity;
	Identity.WorldSeed = 312503;
	Identity.GeneratorVersion = 3;
	Identity.GenerationAttempt = 0;
	Identity.bSourceWorldAccepted = true;
	return Identity;
}

FABTSGeneratedBuildingSite MakeValidBuildingSite()
{
	FABTSGeneratedBuildingSite Site;
	Site.SiteId = 101;
	Site.TaskId = 3;
	Site.CellId = 4352;
	Site.SourceTaskTypeValue = 4;
	Site.Purpose = EABTSGeneratedBuildingPurpose::DestructibleTarget;
	Site.EncounterIndex = 1;
	Site.DifficultyTier = 1;
	Site.DeterministicSeed = 1034267999;
	Site.WorldTransform = FTransform(
		FQuat::Identity,
		FVector(0.0, 0.0, 10000.0));
	Site.MaxSlopeDegrees = 2.0f;
	Site.AnchorDirection = FVector::UpVector;
	Site.TangentForward = FVector::ForwardVector;
	Site.TangentRight = FVector::RightVector;
	Site.PadHalfExtentCM = FVector2D(650.0, 520.0);
	Site.PadEdgeBlendWidthCM = 180.0f;
	Site.PadTargetRadiusCM = 10000.0f;
	Site.bTerrainPadApplied = true;
	return Site;
}

FABTSJuryDemoFixedSixContract MakeValidJuryDemoFixedSixContract()
{
	FABTSJuryDemoFixedSixContract Contract;
	Contract.ContractVersion =
		FABTSJuryDemoFixedSixContract::CurrentContractVersion;
	Contract.PlacementSchemaVersion =
		FABTSJuryDemoFixedSixContract::FrozenPlacementSchemaVersion;
	Contract.DemoManifestVersion =
		FABTSJuryDemoFixedSixContract::FrozenDemoManifestVersion;
	Contract.DemoManifestHash =
		FABTSJuryDemoFixedSixContract::FrozenDemoManifestHash;
	Contract.PlacementCatalogHash =
		FABTSJuryDemoFixedSixContract::FrozenPlacementCatalogHash;
	Contract.WorldSeed = FABTSJuryDemoFixedSixContract::FrozenWorldSeed;
	Contract.CandidateId = FABTSJuryDemoFixedSixContract::FrozenCandidateId;
	Contract.LayoutHash = FABTSJuryDemoFixedSixContract::FrozenLayoutHash;

	const TConstArrayView<FABTSM3JuryBuildingPlacementFixture> Fixtures =
		FABTSM3JuryFixedSixLayoutBuilder::GetFrozenPlacementFixtures();
	Contract.Sites.Reserve(Fixtures.Num());
	for (int32 Index = 0; Index < Fixtures.Num(); ++Index)
	{
		const FABTSM3JuryBuildingPlacementFixture& Fixture = Fixtures[Index];
		FABTSJuryDemoFixedSixBuildingSite& Site =
			Contract.Sites.AddDefaulted_GetRef();
		Site.ManifestEntryId = Fixture.ManifestEntryId;
		Site.EncounterIndex = Index;
		Site.WorldTransform = FTransform(
			FQuat::Identity,
			FVector(0.0, 0.0, 10000.0 + Index * 2000.0));
		Site.PadHalfExtentCM = Fixture.RequiredPadHalfExtentCM;
		Site.LocalBounds = Fixture.LocalBounds;
		Site.DifficultyTier = Fixture.DifficultyTier;
		Site.DeterministicSeed = Fixture.BuildingSeed;
		Site.DescriptorHash =
			static_cast<uint64>(Fixture.SourceDescriptorHash);
	}
	return Contract;
}

FABTSJuryDemoFixedSixContract MakeValidJuryDemoFixedSixV2Contract()
{
	FABTSJuryDemoFixedSixContract Contract =
		MakeValidJuryDemoFixedSixContract();
	Contract.ContractVersion =
		FABTSJuryDemoFixedSixContract::SupportedV2ContractVersion;
	Contract.PlacementCatalogHash =
		FABTSJuryDemoFixedSixContract::FrozenV2PlacementCatalogHash;
	// M3 owns the final V2 value. This distinct non-zero identity exercises the
	// additive schema without pretending that Integration has frozen it early.
	Contract.LayoutHash = 0xA26413CF5E02179Bull;

	const uint64 DescriptorHashes[] = {
		10113758205408230493ull,
		1108134973396587699ull,
		17683520519518435068ull,
		11089610541129920709ull,
		7322844578368466709ull,
		3963542007450344969ull
	};
	const uint64 StaticGeometryHashes[] = {
		10276011350224018878ull,
		1243337162086650128ull,
		3075258440093988143ull,
		4328116049969586954ull,
		461929562625370845ull,
		6610608065286482828ull
	};
	const uint64 ProductionHashes[] = {
		6524532268529485689ull,
		3864694895529971157ull,
		15118401498293854757ull,
		3596567542130940914ull,
		12062404675177644267ull,
		10510335516369342439ull
	};
	const uint64 DeviceHashes[] = {
		12560907909080588493ull,
		1033929311817437759ull,
		6073774060920401162ull,
		3035395675580472088ull,
		9042370151666144586ull,
		1309116746468502251ull
	};
	const FBox EffectBounds[] = {
		FBox(FVector(-1102.0, -850.0, -670.0), FVector(418.0, 670.0, 850.0)),
		FBox(FVector(-1462.0, -1138.0, -670.0), FVector(58.0, 382.0, 850.0)),
		FBox(FVector(-1134.0, -522.0, -252.0), FVector(-774.0, -162.0, 468.0)),
		FBox(FVector(-378.0, -486.0, -144.0), FVector(342.0, -126.0, 216.0)),
		FBox(FVector(-1566.0, 126.0, -144.0), FVector(-846.0, 486.0, 216.0)),
		FBox(FVector(-1278.0, -594.0, -144.0), FVector(-558.0, -234.0, 216.0))
	};
	check(Contract.Sites.Num() == UE_ARRAY_COUNT(DescriptorHashes));
	for (int32 Index = 0; Index < Contract.Sites.Num(); ++Index)
	{
		FABTSJuryDemoFixedSixBuildingSite& Site = Contract.Sites[Index];
		Site.DescriptorHash = DescriptorHashes[Index];
		Site.V2Envelope.StaticGeometryHash = StaticGeometryHashes[Index];
		Site.V2Envelope.ProductionIdentityHash = ProductionHashes[Index];
		Site.V2Envelope.DeviceAssemblyHash = DeviceHashes[Index];
		Site.V2Envelope.PhysicalBounds = Site.LocalBounds;
		Site.V2Envelope.EffectBounds = EffectBounds[Index];
		Site.V2Envelope.bDynamicEnvelopeRequired = true;
	}
	return Contract;
}

FABTSM110FinaleLocalFrame MakeValidFinaleFrame()
{
	FABTSM110FinaleLocalFrame Frame;
	Frame.LayoutVersion = 1;
	Frame.LaunchTaskId = 6;
	Frame.AnchorCellId = 99;
	Frame.SlotPairId = 2468;
	Frame.WorldTransform = FTransform(
		FQuat::Identity,
		FVector(0.0, 0.0, 10000.0));
	Frame.LeftSlotWorldLocation =
		Frame.GetOrigin() - FVector::RightVector * 105.0f;
	Frame.RightSlotWorldLocation =
		Frame.GetOrigin() + FVector::RightVector * 105.0f;
	Frame.bValid = true;
	return Frame;
}

EABTSGeneratedBuildingPurpose GetExpectedBuildingPurpose(
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

int32 GetExpectedEncounterIndex(
	const EABTSGeneratedBuildingPurpose Purpose)
{
	switch (Purpose)
	{
	case EABTSGeneratedBuildingPurpose::Workshop:
		return 0;
	case EABTSGeneratedBuildingPurpose::DestructibleTarget:
		return 1;
	case EABTSGeneratedBuildingPurpose::FurnaceRuins:
		return 2;
	default:
		return INDEX_NONE;
	}
}

class FScopedWorldGenerationContractTestWorld
{
public:
	FScopedWorldGenerationContractTestWorld()
	{
		const UWorld::InitializationValues Values =
			UWorld::InitializationValues()
				.InitializeScenes(false)
				.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(false)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.ShouldSimulatePhysics(false)
				.EnableTraceCollision(false)
				.SetTransactional(false)
				.CreateFXSystem(false);
		World = UWorld::CreateWorld(
			EWorldType::Game,
			false,
			TEXT("ABTSWorldGenerationContractTestWorld"),
			nullptr,
			true,
			ERHIFeatureLevel::Num,
			&Values);
	}

	~FScopedWorldGenerationContractTestWorld()
	{
		if (World != nullptr)
		{
			World->DestroyWorld(false);
			World->RemoveFromRoot();
		}
	}

	UWorld* Get() const { return World; }

private:
	UWorld* World = nullptr;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSWorldGenerationContractValidationTest,
	"ABTS.Contracts.WorldGeneration.Validation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSWorldGenerationContractValidationTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;

	FABTSGeneratedWorldIdentity Identity = MakeAcceptedIdentity();
	TestTrue(TEXT("Accepted identity is usable"), Identity.IsUsable());
	Identity.ContractVersion++;
	TestFalse(
		TEXT("Unknown contract version fails closed"),
		Identity.IsUsable());

	const FABTSGeneratedBuildingSite Site = MakeValidBuildingSite();
	TestTrue(TEXT("Building site frame is usable"), Site.IsUsable());

	FABTSGeneratedBuildingSite UnsupportedSite = Site;
	UnsupportedSite.SiteId++;
	UnsupportedSite.TaskId++;
	UnsupportedSite.CellId++;
	UnsupportedSite.Purpose =
		EABTSGeneratedBuildingPurpose::Unsupported;
	TestTrue(
		TEXT("Unknown future site purposes remain skippable"),
		UnsupportedSite.IsUsable());

	FABTSGeneratedBuildingSite NegativeTaskSite = Site;
	NegativeTaskSite.TaskId = -2;
	TestFalse(
		TEXT("Any negative task identity fails closed"),
		NegativeTaskSite.IsUsable());

	FABTSGeneratedBuildingSite ScaledSite = Site;
	ScaledSite.WorldTransform.SetScale3D(FVector(2.0, 1.0, 1.0));
	TestFalse(
		TEXT("A scaled building frame fails closed"),
		ScaledSite.IsUsable());

	FABTSGeneratedBuildingSite NonUnitBasisSite = Site;
	NonUnitBasisSite.TangentForward *= 2.0;
	TestFalse(
		TEXT("A non-unit building basis fails closed"),
		NonUnitBasisSite.IsUsable());

	FABTSBuildingGenerationContract BuildingContract;
	BuildingContract.Identity = MakeAcceptedIdentity();
	BuildingContract.Sites.Add(Site);
	TestTrue(
		TEXT("One valid building site produces a usable snapshot"),
		BuildingContract.IsUsable());

	FABTSGeneratedBuildingSite DuplicateIdSite = UnsupportedSite;
	DuplicateIdSite.SiteId = Site.SiteId;
	BuildingContract.Sites.Add(DuplicateIdSite);
	TestFalse(
		TEXT("Duplicate site identity fails closed"),
		BuildingContract.IsUsable());

	BuildingContract.Sites.SetNum(1);
	FABTSGeneratedBuildingSite DuplicateTaskCellSite = Site;
	DuplicateTaskCellSite.SiteId++;
	BuildingContract.Sites.Add(DuplicateTaskCellSite);
	TestFalse(
		TEXT("Duplicate task/CellTopo identity fails closed"),
		BuildingContract.IsUsable());

	FABTSJuryDemoFixedSixContract FixedSixContract =
		MakeValidJuryDemoFixedSixContract();
	TestTrue(
		TEXT("Frozen JuryDemo fixed-six identity is usable"),
		FixedSixContract.IsUsable());
	FABTSBuildingGenerationContract FixedSixBuildingContract;
	FixedSixBuildingContract.Identity = MakeAcceptedIdentity();
	FixedSixBuildingContract.Sites.Add(Site);
	FixedSixBuildingContract.JuryDemoFixedSix = FixedSixContract;
	TestTrue(
		TEXT("The additive fixed-six snapshot preserves legacy contract usability"),
		FixedSixBuildingContract.IsUsable());

	FABTSJuryDemoFixedSixContract WrongManifest = FixedSixContract;
	WrongManifest.DemoManifestHash++;
	TestFalse(
		TEXT("A mismatched fixed-six Manifest hash fails closed"),
		WrongManifest.IsUsable());

	FABTSJuryDemoFixedSixContract MissingEntry = FixedSixContract;
	MissingEntry.Sites.Pop();
	TestFalse(
		TEXT("A missing fixed-six Manifest entry fails closed"),
		MissingEntry.IsUsable());

	FABTSJuryDemoFixedSixContract OutOfOrder = FixedSixContract;
	OutOfOrder.Sites.Swap(0, 1);
	TestFalse(
		TEXT("Out-of-order fixed-six Encounters fail closed"),
		OutOfOrder.IsUsable());

	FABTSJuryDemoFixedSixContract DuplicateEntry = FixedSixContract;
	DuplicateEntry.Sites[1].ManifestEntryId =
		DuplicateEntry.Sites[0].ManifestEntryId;
	TestFalse(
		TEXT("Duplicate fixed-six Manifest entries fail closed"),
		DuplicateEntry.IsUsable());

	FABTSJuryDemoFixedSixContract WrongLayout = FixedSixContract;
	WrongLayout.LayoutHash++;
	TestFalse(
		TEXT("A mismatched fixed-six Layout hash fails closed"),
		WrongLayout.IsUsable());

	FABTSJuryDemoFixedSixContract FixedSixV2Contract =
		MakeValidJuryDemoFixedSixV2Contract();
	TestTrue(
		TEXT("Fixed-Six V2 accepts the exact sealed M7 static and effect envelope"),
		FixedSixV2Contract.IsUsable());
	FABTSBuildingGenerationContract FixedSixV2BuildingContract;
	FixedSixV2BuildingContract.Identity = MakeAcceptedIdentity();
	FixedSixV2BuildingContract.Sites.Add(Site);
	FixedSixV2BuildingContract.JuryDemoFixedSix = FixedSixV2Contract;
	TestTrue(
		TEXT("The additive V2 snapshot remains a usable building contract"),
		FixedSixV2BuildingContract.IsUsable());

	FABTSJuryDemoFixedSixContract V1WithV2State = FixedSixContract;
	V1WithV2State.Sites[0].V2Envelope =
		FixedSixV2Contract.Sites[0].V2Envelope;
	TestFalse(
		TEXT("V1 rejects silently injected V2 envelope state"),
		V1WithV2State.IsUsable());

	FABTSJuryDemoFixedSixContract V2WithV1Catalog = FixedSixV2Contract;
	V2WithV1Catalog.PlacementCatalogHash =
		FABTSJuryDemoFixedSixContract::FrozenPlacementCatalogHash;
	TestFalse(
		TEXT("V2 rejects the stale V1 placement Catalog"),
		V2WithV1Catalog.IsUsable());

	FABTSJuryDemoFixedSixContract V2WithV1Layout = FixedSixV2Contract;
	V2WithV1Layout.LayoutHash =
		FABTSJuryDemoFixedSixContract::FrozenLayoutHash;
	TestFalse(
		TEXT("V2 cannot silently reuse the V1 Layout hash"),
		V2WithV1Layout.IsUsable());

	FABTSJuryDemoFixedSixContract WrongV2Descriptor = FixedSixV2Contract;
	WrongV2Descriptor.Sites[0].DescriptorHash++;
	TestFalse(
		TEXT("V2 rejects a Descriptor hash outside the sealed Catalog"),
		WrongV2Descriptor.IsUsable());

	FABTSJuryDemoFixedSixContract WrongV2Production = FixedSixV2Contract;
	WrongV2Production.Sites[1].V2Envelope.ProductionIdentityHash++;
	TestFalse(
		TEXT("V2 rejects a mismatched Stage 5 production identity"),
		WrongV2Production.IsUsable());

	FABTSJuryDemoFixedSixContract WrongV2Device = FixedSixV2Contract;
	WrongV2Device.Sites[2].V2Envelope.DeviceAssemblyHash++;
	TestFalse(
		TEXT("V2 rejects a mismatched Stage 5.5 device identity"),
		WrongV2Device.IsUsable());

	FABTSJuryDemoFixedSixContract WrongV2Physical = FixedSixV2Contract;
	WrongV2Physical.Sites[3].V2Envelope.PhysicalBounds.Max.X += 1.0;
	TestFalse(
		TEXT("V2 rejects physical Bounds that drift from frozen LocalBounds"),
		WrongV2Physical.IsUsable());

	FABTSJuryDemoFixedSixContract WrongV2Effect = FixedSixV2Contract;
	WrongV2Effect.Sites[4].V2Envelope.EffectBounds.Max.Y += 1.0;
	TestFalse(
		TEXT("V2 rejects a mismatched dynamic effect corridor"),
		WrongV2Effect.IsUsable());

	FABTSJuryDemoFixedSixContract MissingV2DynamicFlag = FixedSixV2Contract;
	MissingV2DynamicFlag.Sites[5].V2Envelope.bDynamicEnvelopeRequired = false;
	TestFalse(
		TEXT("V2 rejects an effect corridor that exceeds Pad without a dynamic flag"),
		MissingV2DynamicFlag.IsUsable());

	FABTSJuryDemoFixedSixContract UnknownFixedSixVersion = FixedSixV2Contract;
	UnknownFixedSixVersion.ContractVersion++;
	TestFalse(
		TEXT("Unknown Fixed-Six versions fail closed"),
		UnknownFixedSixVersion.IsUsable());

	FixedSixBuildingContract.Identity.WorldSeed++;
	TestFalse(
		TEXT("Fixed-six and accepted-world seeds must match"),
		FixedSixBuildingContract.IsUsable());

	FABTSFinaleWorldContract FinaleContract;
	FinaleContract.Identity = MakeAcceptedIdentity();
	FinaleContract.PrimaryRadiusCM = 10000.0;
	FinaleContract.LaunchFrame = MakeValidFinaleFrame();
	TestTrue(
		TEXT("Accepted world and usable local frame produce a finale snapshot"),
		FinaleContract.IsUsable());

	FinaleContract.LaunchFrame.bValid = false;
	TestFalse(
		TEXT("Invalid finale frame fails closed"),
		FinaleContract.IsUsable());

	FinaleContract.LaunchFrame = MakeValidFinaleFrame();
	FinaleContract.LaunchFrame.WorldTransform.SetScale3D(
		FVector(1.0, 2.0, 1.0));
	TestFalse(
		TEXT("A scaled finale frame fails closed"),
		FinaleContract.IsUsable());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3WorldGenerationContractAdapterTest,
	"ABTS.Contracts.WorldGeneration.M3Adapter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM3WorldGenerationContractAdapterTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;

	FScopedWorldGenerationContractTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Transient contract test World is created"), World);
	if (World == nullptr)
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM3Planet* Planet = World->SpawnActor<AABTSM3Planet>(
		AABTSM3Planet::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	TestNotNull(TEXT("Native M3 producer Actor spawns"), Planet);
	if (Planet == nullptr)
	{
		return false;
	}

	FABTSBuildingGenerationContract PrematureContract;
	TestFalse(
		TEXT("An unbuilt M3 world cannot export a consumer snapshot"),
		Planet->TryExportBuildingGenerationContract(PrematureContract));

	// Exercise the backward-compatible generic path on a non-jury seed while
	// minimizing presentation work; fixed-six export is verified separately.
	Planet->WorldSeed =
		FABTSJuryDemoFixedSixContract::FrozenWorldSeed + 1;
	Planet->SurfaceSubdivision = 1;
	Planet->InstancesPerCell = 0;
	TestTrue(TEXT("Generic M3 world rebuilds"), Planet->RebuildPlanet());

	FABTSBuildingGenerationContract BuildingContract;
	TestTrue(
		TEXT("Accepted M3 world exports its building snapshot"),
		Planet->TryExportBuildingGenerationContract(BuildingContract));
	TestEqual(
		TEXT("Snapshot preserves the final producer site count"),
		BuildingContract.Sites.Num(),
		Planet->GetBuildingSpawnSites().Num());
	TestEqual(
		TEXT("Snapshot preserves the accepted world seed"),
		BuildingContract.Identity.WorldSeed,
		Planet->WorldSeed);
	TestEqual(
		TEXT("Snapshot preserves the generator version"),
		BuildingContract.Identity.GeneratorVersion,
		Planet->PCGSummary.GeneratorVersion);
	TestEqual(
		TEXT("Snapshot preserves the accepted attempt"),
		BuildingContract.Identity.GenerationAttempt,
		Planet->PCGSummary.AttemptIndex);

	for (int32 Index = 0;
		Index < BuildingContract.Sites.Num()
			&& Index < Planet->GetBuildingSpawnSites().Num();
		++Index)
	{
		const FABTSGeneratedBuildingSite& Snapshot =
			BuildingContract.Sites[Index];
		const FABTSM3BuildingSpawnSite& Source =
			Planet->GetBuildingSpawnSites()[Index];
		const uint32 ExpectedSeedHash = HashCombineFast(
			GetTypeHash(Planet->WorldSeed),
			HashCombineFast(
				GetTypeHash(Source.TaskId),
				GetTypeHash(Source.CellId)));
		const uint64 ExpectedSiteId =
			(static_cast<uint64>(static_cast<uint32>(Source.TaskId)) << 32)
			| static_cast<uint32>(Source.CellId);

		TestEqual(
			*FString::Printf(TEXT("Site %d keeps its task"), Index),
			Snapshot.TaskId,
			Source.TaskId);
		TestEqual(
			*FString::Printf(TEXT("Site %d keeps its CellTopo anchor"), Index),
			Snapshot.CellId,
			Source.CellId);
		TestEqual(
			*FString::Printf(TEXT("Site %d keeps its source task diagnostic"), Index),
			Snapshot.SourceTaskTypeValue,
			static_cast<int32>(Source.TaskType));
		TestEqual(
			*FString::Printf(TEXT("Site %d maps to the stable purpose"), Index),
			static_cast<int32>(Snapshot.Purpose),
			static_cast<int32>(
				GetExpectedBuildingPurpose(Source.TaskType)));
		const int32 ExpectedEncounterIndex =
			GetExpectedEncounterIndex(Snapshot.Purpose);
		TestEqual(
			*FString::Printf(TEXT("Site %d keeps its encounter identity"), Index),
			Snapshot.EncounterIndex,
			ExpectedEncounterIndex);
		TestEqual(
			*FString::Printf(TEXT("Site %d keeps its difficulty tier"), Index),
			Snapshot.DifficultyTier,
			ExpectedEncounterIndex == INDEX_NONE
				? 0
				: ExpectedEncounterIndex);
		TestEqual(
			*FString::Printf(TEXT("Site %d has a collision-free stable ID"), Index),
			Snapshot.SiteId,
			ExpectedSiteId);
		TestEqual(
			*FString::Printf(TEXT("Site %d keeps the legacy deterministic seed"), Index),
			Snapshot.DeterministicSeed,
			static_cast<int32>(ExpectedSeedHash & MAX_int32));
		TestTrue(
			*FString::Printf(TEXT("Site %d keeps its final transform"), Index),
			Snapshot.WorldTransform.Equals(Source.WorldTransform, 1.0e-4));
		TestEqual(
			*FString::Printf(TEXT("Site %d keeps its terrain-pad state"), Index),
			Snapshot.bTerrainPadApplied,
			Source.bTerrainPadApplied);
		TestTrue(
			*FString::Printf(TEXT("Site %d keeps its maximum slope"), Index),
			FMath::IsNearlyEqual(
				Snapshot.MaxSlopeDegrees,
				Source.MaxSlopeDegrees,
				1.0e-4f));
		TestTrue(
			*FString::Printf(TEXT("Site %d keeps its radial axis"), Index),
			Snapshot.AnchorDirection.Equals(
				Source.AnchorDirection,
				1.0e-4));
		TestTrue(
			*FString::Printf(TEXT("Site %d keeps its forward axis"), Index),
			Snapshot.TangentForward.Equals(
				Source.TangentForward,
				1.0e-4));
		TestTrue(
			*FString::Printf(TEXT("Site %d keeps its right axis"), Index),
			Snapshot.TangentRight.Equals(
				Source.TangentRight,
				1.0e-4));
		TestTrue(
			*FString::Printf(TEXT("Site %d keeps its pad half extent"), Index),
			Snapshot.PadHalfExtentCM.Equals(
				Source.PadHalfExtentCM,
				1.0e-4));
		TestTrue(
			*FString::Printf(TEXT("Site %d keeps its pad blend width"), Index),
			FMath::IsNearlyEqual(
				Snapshot.PadEdgeBlendWidthCM,
				Source.PadEdgeBlendWidthCM,
				1.0e-4f));
		TestTrue(
			*FString::Printf(TEXT("Site %d keeps its pad target radius"), Index),
			FMath::IsNearlyEqual(
				Snapshot.PadTargetRadiusCM,
				Source.PadTargetRadiusCM,
				1.0e-4f));
	}

	FABTSBuildingGenerationContract RepeatBuildingContract;
	TestTrue(
		TEXT("The same accepted world exports again"),
		Planet->TryExportBuildingGenerationContract(
			RepeatBuildingContract));
	TestEqual(
		TEXT("Repeated export preserves site count"),
		RepeatBuildingContract.Sites.Num(),
		BuildingContract.Sites.Num());
	for (int32 Index = 0;
		Index < RepeatBuildingContract.Sites.Num()
			&& Index < BuildingContract.Sites.Num();
		++Index)
	{
		TestEqual(
			*FString::Printf(TEXT("Repeated site %d keeps stable identity"), Index),
			RepeatBuildingContract.Sites[Index].SiteId,
			BuildingContract.Sites[Index].SiteId);
		TestEqual(
			*FString::Printf(TEXT("Repeated site %d keeps deterministic seed"), Index),
			RepeatBuildingContract.Sites[Index].DeterministicSeed,
			BuildingContract.Sites[Index].DeterministicSeed);
	}

	FABTSFinaleWorldContract FinaleContract;
	TestTrue(
		TEXT("Accepted M3 world exports its finale snapshot"),
		Planet->TryExportFinaleWorldContract(FinaleContract));
	TestEqual(
		TEXT("Finale snapshot preserves the primary radius"),
		FinaleContract.PrimaryRadiusCM,
		static_cast<double>(Planet->GetPlanetRadiusCM()));
	TestEqual(
		TEXT("Finale snapshot preserves the slot-pair identity"),
		FinaleContract.LaunchFrame.SlotPairId,
		Planet->GetFinaleLaunchFrame().SlotPairId);
	TestTrue(
		TEXT("Finale snapshot preserves the local frame transform"),
		FinaleContract.LaunchFrame.WorldTransform.Equals(
			Planet->GetFinaleLaunchFrame().WorldTransform,
			1.0e-4));

	const int32 ExportedSeed = BuildingContract.Identity.WorldSeed;
	Planet->WorldSeed++;
	TestEqual(
		TEXT("Consumer snapshot remains an immutable value copy"),
		BuildingContract.Identity.WorldSeed,
		ExportedSeed);

	World->DestroyActor(Planet);
	AABTSM3Planet* JuryPlanet = World->SpawnActor<AABTSM3Planet>(
		AABTSM3Planet::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
	TestNotNull(TEXT("Fixed-six M3 producer Actor spawns"), JuryPlanet);
	if (JuryPlanet == nullptr)
	{
		return false;
	}
	JuryPlanet->WorldSeed = FABTSJuryDemoFixedSixContract::FrozenWorldSeed;
	JuryPlanet->SurfaceSubdivision = 1;
	JuryPlanet->InstancesPerCell = 0;
	TestTrue(
		TEXT("Frozen JuryDemo M3 world rebuilds"),
		JuryPlanet->RebuildPlanet());

	FABTSBuildingGenerationContract JuryContract;
	TestTrue(
		TEXT("Frozen JuryDemo world exports its exact fixed-six snapshot"),
		JuryPlanet->TryExportBuildingGenerationContract(JuryContract));
	TestTrue(
		TEXT("Exported fixed-six snapshot is usable"),
		JuryContract.JuryDemoFixedSix.IsUsable());
	TestEqual(
		TEXT("Exported fixed-six snapshot has exactly six ordered sites"),
		JuryContract.JuryDemoFixedSix.Sites.Num(),
		FABTSJuryDemoFixedSixContract::ExpectedSiteCount);
	TestEqual(
		TEXT("Exported fixed-six snapshot freezes the Layout hash"),
		JuryContract.JuryDemoFixedSix.LayoutHash,
		FABTSJuryDemoFixedSixContract::FrozenLayoutHash);

	FABTSBuildingGenerationContract RepeatedJuryContract;
	TestTrue(
		TEXT("The same JuryDemo world exports again"),
		JuryPlanet->TryExportBuildingGenerationContract(
			RepeatedJuryContract));
	TestEqual(
		TEXT("Repeated JuryDemo export preserves its Layout hash"),
		RepeatedJuryContract.JuryDemoFixedSix.LayoutHash,
		JuryContract.JuryDemoFixedSix.LayoutHash);
	for (int32 Index = 0;
		Index < JuryContract.JuryDemoFixedSix.Sites.Num();
		++Index)
	{
		const FABTSJuryDemoFixedSixBuildingSite& First =
			JuryContract.JuryDemoFixedSix.Sites[Index];
		const FABTSJuryDemoFixedSixBuildingSite& Second =
			RepeatedJuryContract.JuryDemoFixedSix.Sites[Index];
		TestEqual(
			*FString::Printf(
				TEXT("Repeated JuryDemo site %d keeps its Manifest entry"),
				Index),
			Second.ManifestEntryId,
			First.ManifestEntryId);
		TestTrue(
			*FString::Printf(
				TEXT("Repeated JuryDemo site %d keeps its transform"),
				Index),
			Second.WorldTransform.Equals(First.WorldTransform, 1.0e-4));
	}

	const FABTSM3JuryFixedSixLayoutResult AcceptedJuryResult =
		JuryPlanet->MonthlyJuryFixedSixLayoutResult;
	auto TestRejectedJuryExport =
		[this, JuryPlanet](const TCHAR* Label)
		{
			FABTSBuildingGenerationContract RejectedContract;
			TestFalse(
				Label,
				JuryPlanet->TryExportBuildingGenerationContract(
					RejectedContract));
			TestTrue(
				TEXT("Rejected JuryDemo export remains atomic"),
				RejectedContract.Sites.IsEmpty()
					&& RejectedContract.JuryDemoFixedSix.IsEmpty());
		};

	JuryPlanet->MonthlyJuryFixedSixLayoutResult.M7SourceManifestHash++;
	TestRejectedJuryExport(
		TEXT("Mismatched source Manifest hash rejects JuryDemo export"));
	JuryPlanet->MonthlyJuryFixedSixLayoutResult = AcceptedJuryResult;
	JuryPlanet->MonthlyJuryFixedSixLayoutResult.Placements.Pop();
	TestRejectedJuryExport(
		TEXT("Missing source Manifest entry rejects JuryDemo export"));
	JuryPlanet->MonthlyJuryFixedSixLayoutResult = AcceptedJuryResult;
	JuryPlanet->MonthlyJuryFixedSixLayoutResult.Placements.Swap(0, 1);
	TestRejectedJuryExport(
		TEXT("Out-of-order source Encounters reject JuryDemo export"));
	JuryPlanet->MonthlyJuryFixedSixLayoutResult = AcceptedJuryResult;
	JuryPlanet->MonthlyJuryFixedSixLayoutResult.Placements[1].ManifestEntryId =
		JuryPlanet->MonthlyJuryFixedSixLayoutResult.Placements[0].ManifestEntryId;
	TestRejectedJuryExport(
		TEXT("Duplicate source Manifest entries reject JuryDemo export"));
	JuryPlanet->MonthlyJuryFixedSixLayoutResult = AcceptedJuryResult;
	JuryPlanet->MonthlyJuryFixedSixLayoutResult.LayoutHash++;
	TestRejectedJuryExport(
		TEXT("Mismatched source Layout hash rejects JuryDemo export"));
	JuryPlanet->MonthlyJuryFixedSixLayoutResult = AcceptedJuryResult;
	return true;
}

#endif
