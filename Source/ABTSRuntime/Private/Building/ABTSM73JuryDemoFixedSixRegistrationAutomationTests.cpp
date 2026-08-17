// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Building/ABTSM73JuryDemoFixedSixRegistration.h"

#include "Building/ABTSM73BeamDemoManifest.h"
#include "Building/ABTSM73BuildingFreezeV3.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"

namespace ABTSM73JuryDemoFixedSixRegistrationTests
{
	class FFixedSixRegistrationTestWorld final : public FTestWorldWrapper
	{
	public:
		bool Create()
		{
			if (GEngine == nullptr)
			{
				ReportFailure(TEXT("GEngine unavailable"));
				return false;
			}
			UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(true)
				.ShouldSimulatePhysics(false)
				.EnableTraceCollision(true)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.CreateFXSystem(false)
				.SetDefaultGameMode(AGameModeBase::StaticClass());
			TestWorld = UWorld::CreateWorld(
				EWorldType::Game, false,
				TEXT("ABTSM73FixedSixStaticRegistrationWorld"),
				nullptr, true, ERHIFeatureLevel::Num, &Values);
			if (TestWorld == nullptr)
			{
				ReportFailure(TEXT("Failed to create registration test world"));
				return false;
			}
			FWorldContext& Context =
				GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.OwningGameInstance = GameInstance;
			Context.SetCurrentWorld(TestWorld);
			TestWorld->SetGameInstance(GameInstance);
			GameInstance->Init();
			return true;
		}
	};

	FABTSGeneratedBuildingSite MakeRegistrationTestGenericSite()
	{
		FABTSGeneratedBuildingSite Site;
		Site.SiteId = 7001;
		Site.TaskId = 71;
		Site.CellId = 701;
		Site.SourceTaskTypeValue = 4;
		Site.Purpose = EABTSGeneratedBuildingPurpose::DestructibleTarget;
		Site.EncounterIndex = 0;
		Site.DifficultyTier = 0;
		Site.DeterministicSeed = 710000;
		Site.WorldTransform = FTransform(
			FQuat::Identity, FVector(0.0, 0.0, 10000.0));
		Site.MaxSlopeDegrees = 0.0f;
		Site.AnchorDirection = FVector::UpVector;
		Site.TangentForward = FVector::ForwardVector;
		Site.TangentRight = FVector::RightVector;
		Site.PadHalfExtentCM = FVector2D(500.0, 500.0);
		Site.PadEdgeBlendWidthCM = 36.0f;
		Site.PadTargetRadiusCM = 10000.0f;
		Site.bTerrainPadApplied = true;
		return Site;
	}

	bool MakeRegistrationTestV2Contract(
		FABTSBuildingGenerationContract& OutContract,
		FString& OutError)
	{
		OutContract = FABTSBuildingGenerationContract();
		OutError.Reset();
		OutContract.Identity.WorldSeed =
			FABTSJuryDemoFixedSixContract::FrozenWorldSeed;
		OutContract.Identity.GeneratorVersion = 3;
		OutContract.Identity.GenerationAttempt = 0;
		OutContract.Identity.bSourceWorldAccepted = true;
		OutContract.Sites.Add(MakeRegistrationTestGenericSite());

		FABTSJuryDemoFixedSixContract& Snapshot =
			OutContract.JuryDemoFixedSix;
		Snapshot.ContractVersion =
			FABTSJuryDemoFixedSixContract::SupportedV3ContractVersion;
		Snapshot.PlacementSchemaVersion =
			FABTSJuryDemoFixedSixContract::FrozenV3PlacementSchemaVersion;
		Snapshot.DemoManifestVersion =
			FABTSJuryDemoFixedSixContract::FrozenDemoManifestVersion;
		Snapshot.DemoManifestHash =
			FABTSJuryDemoFixedSixContract::FrozenDemoManifestHash;
		Snapshot.PlacementCatalogHash =
			FABTSJuryDemoFixedSixContract::FrozenV3PlacementCatalogHash;
		Snapshot.WorldSeed = FABTSJuryDemoFixedSixContract::FrozenWorldSeed;
		Snapshot.CandidateId = FABTSJuryDemoFixedSixContract::FrozenCandidateId;
		Snapshot.LayoutHash = FABTSJuryDemoFixedSixContract::FrozenV3LayoutHash;

		const TArray<FABTSM73BuildingFreezeV3FrozenIdentity>& Identities =
			FABTSM73BuildingFreezeV3::GetFrozenIdentities();
		if (Identities.Num()
				!= FABTSJuryDemoFixedSixContract::ExpectedSiteCount)
		{
			OutError = TEXT("RegistrationTestV3IdentityCount");
			return false;
		}

		const FVector PrimaryCenter = FVector::ZeroVector;
		constexpr double PrimaryRadiusCM = 10000.0;
		const FVector PrimaryLocations[] = {
			FVector(10000.0, 0.0, 0.0),
			FVector(0.0, 10000.0, 0.0),
			FVector(-10000.0, 0.0, 0.0),
			FVector(0.0, -10000.0, 0.0),
			FVector(7071.067811865, 7071.067811865, 0.0)};
		const FVector SatelliteCenter(30000.0, 0.0, 0.0);
		constexpr double SatelliteRadiusCM = 2000.0;
		Snapshot.Sites.Reserve(Identities.Num());
		for (int32 Index = 0; Index < Identities.Num(); ++Index)
		{
			const FABTSM73BuildingFreezeV3FrozenIdentity& Identity =
				Identities[Index];
			if (Identity.EncounterSlot != Index)
			{
				OutError = FString::Printf(
					TEXT("RegistrationTestV3Encounter:%d"), Index);
				return false;
			}
			FABTSM73BeamDemoManifestEntry Entry;
			if (!FABTSM73BeamDemoManifest::Resolve(
				Identity.ManifestEntryId, Entry, OutError))
			{
				return false;
			}

			FString ContractEntryId = Entry.StableId.ToString();
			if (!ContractEntryId.RemoveFromStart(TEXT("Demo")))
			{
				OutError = TEXT("RegistrationTestManifestPrefixMissing");
				return false;
			}
			FABTSJuryDemoFixedSixBuildingSite& Site =
				Snapshot.Sites.AddDefaulted_GetRef();
			Site.ManifestEntryId = FName(*ContractEntryId);
			Site.EncounterIndex = Index;
			Site.DifficultyTier = Identity.DifficultyTier;
			Site.DeterministicSeed = Identity.BuildingSeed;
			Site.DescriptorHash = Identity.DescriptorHash;
			Site.LocalBounds = Identity.SiteLocalBounds;
			Site.PadHalfExtentCM = FVector2D(
				FMath::Max(FMath::Abs(Identity.PadBounds.Min.X),
					FMath::Abs(Identity.PadBounds.Max.X)),
				FMath::Max(FMath::Abs(Identity.PadBounds.Min.Y),
					FMath::Abs(Identity.PadBounds.Max.Y)));

			const bool bSatellite = Index == 4;
			const FVector SupportCenter = bSatellite
				? SatelliteCenter : PrimaryCenter;
			const double SupportRadius = bSatellite
				? SatelliteRadiusCM : PrimaryRadiusCM;
			const FVector Location = bSatellite
				? SatelliteCenter + FVector(SatelliteRadiusCM, 0.0, 0.0)
				: PrimaryLocations[Index < 4 ? Index : 4];
			const FVector RadialUp = (Location - SupportCenter).GetSafeNormal();
			Site.WorldTransform = FTransform(
				FQuat::FindBetweenNormals(FVector::UpVector, RadialUp), Location);

			FABTSJuryDemoFixedSixV3Envelope& Envelope = Site.V3Envelope;
			Envelope.StaticGeometryHash = Identity.StaticGeometryHash;
			Envelope.ProductionIdentityHash = Identity.ProductionHash;
			Envelope.DeviceAssemblyHash = Identity.SourceDeviceAssemblyHash;
			Envelope.SiteLocalBounds = Identity.SiteLocalBounds;
			Envelope.PadBounds = Identity.PadBounds;
			Envelope.EffectBounds = Identity.EffectBounds;
			Envelope.SurfaceKind = bSatellite
				? EABTSJuryDemoFixedSixSurfaceKind::Satellite
				: EABTSJuryDemoFixedSixSurfaceKind::PrimaryPlanet;
			Envelope.SupportCenterWorldCM = SupportCenter;
			Envelope.SupportRadiusCM = SupportRadius;
			Envelope.GravityAuthorityId = bSatellite
				? FName(TEXT("RegistrationTestSatelliteGravity"))
				: FName(TEXT("RegistrationTestPrimaryGravity"));
			Envelope.GravityIdentityHash = bSatellite ? 0x2202ull : 0x1101ull;
			Envelope.PlacementHash =
				FABTSJuryDemoFixedSixContract::FrozenV3PlacementHashes[Index];
		}
		if (!OutContract.IsUsable())
		{
			OutError = TEXT("RegistrationTestContractRejected");
			return false;
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73JuryDemoFixedSixV2StaticPlanTest,
	"ABTS.M73DAG.BeamC3V3.Demo.J4V2Consumer.StaticPlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73JuryDemoFixedSixV2StaticPlanTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73JuryDemoFixedSixRegistrationTests;
	FABTSBuildingGenerationContract Contract;
	FString Error;
	if (!MakeRegistrationTestV2Contract(Contract, Error))
	{
		AddError(Error);
		return false;
	}
	FABTSM73JuryDemoFixedSixStaticPlan Plan;
	if (!FABTSM73JuryDemoFixedSixRegistration::BuildStaticPlan(
		Contract, Plan, Error))
	{
		AddError(Error);
		return false;
	}
	TestTrue(TEXT("Static plan is usable"), Plan.IsUsable());
	TestEqual(TEXT("Static plan contains exactly six entries"),
		Plan.Entries.Num(), FABTSJuryDemoFixedSixContract::ExpectedSiteCount);
	TestTrue(TEXT("Registration result hash is published"),
		Plan.RegistrationResultHash != 0);
	for (int32 Index = 0; Index < Plan.Entries.Num(); ++Index)
	{
		const FABTSM73JuryDemoFixedSixStaticEntry& Entry = Plan.Entries[Index];
		TestEqual(*FString::Printf(TEXT("Entry %d preserves contract order"), Index),
			Entry.EncounterIndex, Index);
		TestEqual(*FString::Printf(TEXT("Entry %d preserves result hash"), Index),
			Entry.RegistrationResultHash, Plan.RegistrationResultHash);
		TestTrue(*FString::Printf(TEXT("Entry %d has bricks"), Index),
			!Entry.Bricks.IsEmpty());
		TestEqual(*FString::Printf(TEXT("Entry %d has one device"), Index),
			Entry.Devices.Num(), 1);
	}

	FABTSBuildingGenerationContract RejectedContract = Contract;
	RejectedContract.JuryDemoFixedSix.Sites[2]
		.V2Envelope.ProductionIdentityHash ^= 1ull;
	FABTSM73JuryDemoFixedSixStaticPlan RejectedPlan;
	TestFalse(TEXT("Production identity drift fails closed"),
		FABTSM73JuryDemoFixedSixRegistration::BuildStaticPlan(
			RejectedContract, RejectedPlan, Error));
	TestTrue(TEXT("Rejected plan remains empty"),
		RejectedPlan.Entries.IsEmpty());

	RejectedContract = Contract;
	Swap(RejectedContract.JuryDemoFixedSix.Sites[1],
		RejectedContract.JuryDemoFixedSix.Sites[2]);
	TestFalse(TEXT("Contract order drift fails closed"),
		FABTSM73JuryDemoFixedSixRegistration::BuildStaticPlan(
			RejectedContract, RejectedPlan, Error));
	TestTrue(TEXT("Order rejection cannot retain a partial plan"),
		RejectedPlan.Entries.IsEmpty());
	AddInfo(FString::Printf(
		TEXT("J4V2StaticPlan Buildings=%d Layout=%llu ResultHash=%llu")
		TEXT(" Authority=StaticPlan Chaos=NotEvaluated"),
		Plan.Entries.Num(), Plan.LayoutHash, Plan.RegistrationResultHash));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73JuryDemoFixedSixV2AtomicStaticRegistrationTest,
	"ABTS.M73DAG.BeamC3V3.Demo.J4V2Consumer.AtomicStaticRegistration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73JuryDemoFixedSixV2AtomicStaticRegistrationTest::RunTest(
	const FString& Parameters)
{
	using namespace ABTSM73JuryDemoFixedSixRegistrationTests;
	FABTSBuildingGenerationContract Contract;
	FString Error;
	if (!MakeRegistrationTestV2Contract(Contract, Error))
	{
		AddError(Error);
		return false;
	}
	FABTSM73JuryDemoFixedSixStaticPlan Plan;
	if (!FABTSM73JuryDemoFixedSixRegistration::BuildStaticPlan(
		Contract, Plan, Error))
	{
		AddError(Error);
		return false;
	}
	const uint64 ExpectedResultHash = Plan.RegistrationResultHash;
	int32 ExpectedModuleCount = 0;
	int32 ExpectedStaticDeviceCount = 0;
	for (const FABTSM73JuryDemoFixedSixStaticEntry& Entry : Plan.Entries)
	{
		ExpectedModuleCount += Entry.Bricks.Num()
			+ Entry.Devices.Num() + Entry.Caps.Num();
		ExpectedStaticDeviceCount += Entry.Devices.Num() + Entry.Caps.Num();
	}

	FFixedSixRegistrationTestWorld WorldWrapper;
	if (!WorldWrapper.Create())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!TestNotNull(TEXT("Automation world"), World))
	{
		return false;
	}
	AABTSM7BuildingMaterialSystem* MaterialSystem =
		World->SpawnActor<AABTSM7BuildingMaterialSystem>();
	if (!TestNotNull(TEXT("Material system"), MaterialSystem))
	{
		return false;
	}

	FABTSM73JuryDemoFixedSixStaticPlan InvalidPlan = Plan;
	InvalidPlan.Entries[3].Bricks.Reset();
	TArray<TWeakObjectPtr<AABTSM73StableBuildingActor>> Actors;
	TestFalse(TEXT("Invalid batch fails before spawning any Actor"),
		FABTSM73JuryDemoFixedSixRegistration::SpawnStaticActors(
			*World, *MaterialSystem,
			AABTSM73StableBuildingActor::StaticClass(),
			MoveTemp(InvalidPlan), Actors, Error));
	TestTrue(TEXT("Invalid batch returns no Actors"), Actors.IsEmpty());

	if (!FABTSM73JuryDemoFixedSixRegistration::SpawnStaticActors(
		*World, *MaterialSystem,
		AABTSM73StableBuildingActor::StaticClass(),
		MoveTemp(Plan), Actors, Error))
	{
		AddError(Error);
		return false;
	}
	TestEqual(TEXT("Exactly six Actors register atomically"),
		Actors.Num(), FABTSJuryDemoFixedSixContract::ExpectedSiteCount);
	int32 ActualModuleCount = 0;
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		AABTSM73StableBuildingActor* Actor = Actors[Index].Get();
		if (!TestNotNull(*FString::Printf(TEXT("Actor %d"), Index), Actor))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("Actor %d accepted"), Index),
			Actor->IsJuryDemoFixedSixStaticRegistrationAccepted());
		TestEqual(*FString::Printf(TEXT("Actor %d closes the M6 startup gate while static"), Index),
			Actor->GetIdleValidationState(),
			EABTSM73IdleValidationState::Accepted);
		TestTrue(*FString::Printf(TEXT("Actor %d is static-ready but has not started Chaos"), Index),
			Actor->IsJuryDemoFixedSixStaticReadyDeferredForValidation());
		TestFalse(*FString::Printf(TEXT("Actor %d cannot claim first-hit arming before Prepare"), Index),
			Actor->IsJuryDemoFixedSixChaosDeferredUntilFirstHitForValidation());
		TestEqual(*FString::Printf(TEXT("Actor %d order"), Index),
			Actor->GetJuryDemoFixedSixEncounterIndex(), Index);
		TestEqual(*FString::Printf(TEXT("Actor %d result hash"), Index),
			Actor->GetJuryDemoFixedSixRegistrationResultHash(),
			ExpectedResultHash);
		FVector Anchor;
		int32 LiveModules = 0;
		TestTrue(*FString::Printf(TEXT("Actor %d presentation anchor"), Index),
			Actor->QueryLivePresentationAnchor(Anchor, LiveModules));
		TestTrue(*FString::Printf(TEXT("Actor %d live modules"), Index),
			LiveModules > 0);
		FBox PresentationBounds(EForceInit::ForceInit);
		int32 BoundedModules = 0;
		TestTrue(*FString::Printf(TEXT("Actor %d physical presentation bounds"), Index),
			Actor->QueryLivePresentationBounds(
				PresentationBounds,
				BoundedModules));
		TestEqual(*FString::Printf(TEXT("Actor %d bounded module identity"), Index),
			BoundedModules,
			LiveModules);
		TestTrue(*FString::Printf(TEXT("Actor %d bounds have framing volume"), Index),
			PresentationBounds.IsValid
				&& PresentationBounds.GetExtent().GetMin() > 0.0);
		ActualModuleCount +=
			Actor->GetJuryDemoFixedSixStaticModuleCount();
	}
	TestEqual(TEXT("All static Brick instances and devices registered"),
		ActualModuleCount, ExpectedModuleCount);
	int32 StaticDeviceCount = 0;
	for (TActorIterator<AABTSM7BuildingModule> It(World); It; ++It)
	{
		++StaticDeviceCount;
		TestFalse(TEXT("Fixed-Six device starts static"), It->IsDynamic());
	}
	TestEqual(TEXT("All frozen static devices and caps are registered"),
		StaticDeviceCount, ExpectedStaticDeviceCount);
	MaterialSystem->BeginLaunchPhysics(
		false, FVector::ZeroVector, 0.0f, 0.0f);
	int32 DynamicDeviceCountAfterGlobalLaunch = 0;
	for (TActorIterator<AABTSM7BuildingModule> It(World); It; ++It)
	{
		DynamicDeviceCountAfterGlobalLaunch += It->IsDynamic() ? 1 : 0;
	}
	TestEqual(TEXT("Global launch cannot activate Fixed-Six static devices"),
		DynamicDeviceCountAfterGlobalLaunch, 0);
	AddInfo(FString::Printf(
		TEXT("J4V2AtomicStaticRegistration Buildings=%d Modules=%d")
		TEXT(" ResultHash=%llu Authority=StaticRegistration")
		TEXT(" Chaos=NotEvaluated"),
		Actors.Num(), ActualModuleCount, ExpectedResultHash));

	for (const TWeakObjectPtr<AABTSM73StableBuildingActor>& WeakActor : Actors)
	{
		if (AABTSM73StableBuildingActor* Actor = WeakActor.Get())
		{
			Actor->RollbackJuryDemoFixedSixStaticRegistration(
				TEXT("AutomationCleanup"));
		}
	}
	MaterialSystem->Destroy();
	return true;
}

#endif
