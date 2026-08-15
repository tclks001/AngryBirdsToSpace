// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "ABTSM73BeamD1BrickCompiler.h"
#include "ABTSM7PenetrationValidator.h"
#include "ABTSRuntime.h"
#include "Building/ABTSM73BeamDemoManifest.h"
#include "Building/ABTSM73BuildingFreezeV3.h"
#include "Building/ABTSM73JuryDemoFixedSixRegistration.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Components/StaticMeshComponent.h"
#include "Contracts/ABTSWorldGenerationContracts.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Crc.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "Terrain/ABTSM3Planet.h"
#include "Tests/AutomationCommon.h"
#include "TestStage/ABTSM71TestStageActors.h"

namespace ABTSM73BeamStage5ChaosTests
{
	// The default production presentation cap is 60 FPS. This fixture uses the
	// same outer step and deliberately does not override project substepping.
	constexpr float ProductionOuterDeltaSeconds = 1.0f / 60.0f;
	constexpr float MinimumObservationSeconds = 1.25f;
	constexpr float StableHoldSeconds = 0.45f;
	constexpr float MaximumObservationSeconds = 6.0f;
	constexpr float MaximumLinearSpeedCMPerSec = 4.0f;
	constexpr float MaximumAngularSpeedDegreesPerSec = 1.5f;
	constexpr float MaximumPlanarDriftCM = 4.0f;
	constexpr float MaximumSettlementCM = 6.0f;
	constexpr float MaximumRotationDegrees = 2.0f;
	constexpr float GravityCMPerSec2 = 980.0f;
	constexpr float GroundContactToleranceCM = 0.1f;

	bool TryResolveProductionContractManifestEntryName(
		const FABTSM73BeamDemoManifestEntry& Entry,
		FName& OutContractManifestEntryName)
	{
		OutContractManifestEntryName = NAME_None;
		FString ContractManifestEntryId = Entry.StableId.ToString();
		if (!ContractManifestEntryId.RemoveFromStart(TEXT("Demo"))
			|| ContractManifestEntryId.IsEmpty())
		{
			return false;
		}
		OutContractManifestEntryName = FName(*ContractManifestEntryId);
		return true;
	}

	FABTSM73JuryDemoFixedSixStaticEntry MakePendingAtomicPhysicsEntry(
		const FABTSM73BeamDemoManifestEntry& ManifestEntry,
		const FABTSM73BuildingFreezeV3Descriptor& Descriptor,
		const FABTSJuryDemoFixedSixBuildingSite& Site,
		const int32 ContractVersion,
		const uint64 LayoutHash)
	{
		FABTSM73JuryDemoFixedSixStaticEntry Entry;
		Entry.ManifestEntryId = Site.ManifestEntryId;
		Entry.DemoBuilding = ManifestEntry.Id;
		Entry.EncounterIndex = Site.EncounterIndex;
		Entry.DifficultyTier = Descriptor.DifficultyTier;
		Entry.DeterministicSeed = Descriptor.BuildingSeed;
		Entry.SourceContractVersion = ContractVersion;
		Entry.WorldTransform = Site.WorldTransform;
		Entry.PadHalfExtentCM = Site.PadHalfExtentCM;
		Entry.LocalBounds = Descriptor.SiteLocalBounds;
		Entry.EffectBounds = Descriptor.EffectBounds;
		Entry.DescriptorHash = Descriptor.DescriptorHash;
		Entry.StaticGeometryHash = Descriptor.StaticGeometryHash;
		Entry.ProductionIdentityHash = Descriptor.ProductionHash;
		Entry.DeviceAssemblyHash = Descriptor.SourceDeviceAssemblyHash;
		Entry.SourceLayoutHash = LayoutHash;
		Entry.SourcePlacementHash = Site.V3Envelope.PlacementHash;
		Entry.SupportCenterWorldCM = Site.V3Envelope.SupportCenterWorldCM;
		Entry.SupportRadiusCM = Site.V3Envelope.SupportRadiusCM;
		Entry.GravityAuthorityId = Site.V3Envelope.GravityAuthorityId;
		Entry.GravityIdentityHash = Site.V3Envelope.GravityIdentityHash;
		Entry.PhysicsAssemblySchemaVersion =
			Descriptor.PhysicsAssemblySchemaVersion;
		Entry.PhysicsBodyCount = Descriptor.PhysicsBodyCount;
		Entry.PhysicsAssemblyHash = Descriptor.PhysicsAssemblyHash;
		Entry.Bricks = Descriptor.Bricks;
		Entry.Devices = Descriptor.Devices;
		Entry.Caps = Descriptor.Caps;
		Entry.PhysicsClusters = Descriptor.PhysicsClusters;
		return Entry;
	}

	class FStage5PhysicsWorld final : public FTestWorldWrapper
	{
	public:
		bool CreatePhysicsWorld()
		{
			if (TestWorld != nullptr)
			{
				ReportFailure(TEXT("Stage-5 physics world already exists"));
				return false;
			}
			if (GEngine == nullptr)
			{
				ReportFailure(TEXT("GEngine is unavailable"));
				return false;
			}

			UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
			UWorld::InitializationValues InitializationValues;
			InitializationValues
				.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(true)
				.ShouldSimulatePhysics(true)
				.EnableTraceCollision(true)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.CreateFXSystem(false)
				.SetDefaultGameMode(AGameModeBase::StaticClass());
			TestWorld = UWorld::CreateWorld(
				EWorldType::Game,
				false,
				TEXT("ABTSM73BeamStage5PhysicsTestWorld"),
				nullptr,
				true,
				ERHIFeatureLevel::Num,
				&InitializationValues);
			if (TestWorld == nullptr)
			{
				ReportFailure(TEXT("Failed to create Stage-5 physics world"));
				return false;
			}

			TestWorld->SetShouldTick(false);
			FWorldContext& WorldContext =
				GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext.OwningGameInstance = GameInstance;
			WorldContext.SetCurrentWorld(TestWorld);
			TestWorld->SetGameInstance(GameInstance);
			GameInstance->Init();
			return true;
		}
	};

	struct FObservationResult
	{
		bool bReachedQuietWindow = false;
		bool bEndedInQuietWindow = false;
		float FirstQuietWindowSeconds = 0.0f;
		float FinalMaximumPlanarDriftCM = 0.0f;
		float FinalMaximumSettlementCM = 0.0f;
		float FinalMaximumRotationDegrees = 0.0f;
		float FinalMaximumLinearSpeedCMPerSec = 0.0f;
		float FinalMaximumAngularSpeedDegreesPerSec = 0.0f;
		float PeakPlanarDriftCM = 0.0f;
		float PeakSettlementCM = 0.0f;
		float PeakRotationDegrees = 0.0f;
		int32 FinalAwakeBodyCount = 0;
		int32 FinalMaximumPlanarDriftBrickIndex = INDEX_NONE;
		int32 FinalMaximumSettlementBrickIndex = INDEX_NONE;
		int32 FinalMaximumRotationBrickIndex = INDEX_NONE;
	};

	uint32 ComputeFixtureCrc32(
		const FABTSM73BeamDemoManifestEntry& Entry,
		const FABTSM73BeamD1Stage5Result& Result,
		const FABTSM73BuildingFreezeV3Descriptor& Descriptor,
		const FABTSM73JuryDemoFixedSixStaticEntry& StaticEntry,
		const FABTSJuryDemoFixedSixBuildingSite& Site,
		const FVector& SupportCenter,
		const uint32 BodyProfileHash,
		const uint32 WorldProfileHash,
		const FABTSM7SiteUniformGravityPolicy& SiteGravity)
	{
		const FVector Location = Site.WorldTransform.GetLocation();
		const FQuat Rotation = Site.WorldTransform.GetRotation();
		const FString Canonical = FString::Printf(
			TEXT("BeamStage5ChaosProductionIdentity:v6:Entry=%s:Tier=%d:Seed=%d:Stage5=%llu")
			TEXT(":Descriptor=%llu:Static=%llu:Production=%llu:Device=%llu")
			TEXT(":ContractEnvelopeProduction=%llu:Contract=%d:Layout=%llu:Placement=%llu")
			TEXT(":EncounterSlot=%d:Surface=%d:GravityAuthority=%s:GravityHash=%llu:Location=%d,%d,%d")
			TEXT(":Rotation=%d,%d,%d,%d:SupportCenter=%d,%d,%d:SupportRadius=%d")
			TEXT(":Bricks=%d:Devices=%d:Caps=%d:PhysicsBodies=%d:PhysicsAssembly=%llu:Contacts=%d:Ground=%d:ResultantAdvisories=%d")
			TEXT(":OuterDT=%d:Min=%d:Hold=%d:Max=%d:Lin=%d:Ang=%d")
			TEXT(":Drift=%d:Settle=%d:Rot=%d:BodyHash=%u:WorldHash=%u")
			TEXT(":GravityModel=SiteUniformTangentGravity:Gravity=%d")
			TEXT(":SiteGravitySchema=%d:SiteGravityHash=%u")
			TEXT(":SiteGravityDerivation=Normalize(SiteLocationWorldCM-SupportCenterWorldCM)")
			TEXT(":SiteUp=%d,%d,%d")
			TEXT(":GravityWakePolicy=NonInvalidatingForceSkipSleepingBodiesResumeOnExplicitWake")
			TEXT(":EmptyPhysicsHandle=FailClosed")
			TEXT(":MassExpectedPolicy=PerBodySetupCalculateMass")
			TEXT(":StaticExternalMass=CertificateOnly:SupportMaterial=ProductionTerrainDefault"),
			*Entry.StableId.ToString(),
			Entry.Settings.DifficultyTier,
			Entry.Settings.BuildingSeed,
			Result.ProductionIdentityHash,
			Descriptor.DescriptorHash,
			Descriptor.StaticGeometryHash,
			Descriptor.ProductionHash,
			Descriptor.SourceDeviceAssemblyHash,
			Site.V3Envelope.ProductionIdentityHash,
			FABTSJuryDemoFixedSixContract::SupportedV3ContractVersion,
			FABTSJuryDemoFixedSixContract::FrozenV3LayoutHash,
			Site.V3Envelope.PlacementHash,
			Site.EncounterIndex,
			static_cast<int32>(Site.V3Envelope.SurfaceKind),
			*Site.V3Envelope.GravityAuthorityId.ToString(),
			Site.V3Envelope.GravityIdentityHash,
			FMath::RoundToInt(Location.X * 1000.0),
			FMath::RoundToInt(Location.Y * 1000.0),
			FMath::RoundToInt(Location.Z * 1000.0),
			FMath::RoundToInt(Rotation.X * 1000000.0),
			FMath::RoundToInt(Rotation.Y * 1000000.0),
			FMath::RoundToInt(Rotation.Z * 1000000.0),
			FMath::RoundToInt(Rotation.W * 1000000.0),
			FMath::RoundToInt(SupportCenter.X * 1000.0),
			FMath::RoundToInt(SupportCenter.Y * 1000.0),
			FMath::RoundToInt(SupportCenter.Z * 1000.0),
			FMath::RoundToInt(Site.V3Envelope.SupportRadiusCM * 1000.0),
			StaticEntry.Bricks.Num(),
			StaticEntry.Devices.Num(),
			StaticEntry.Caps.Num(),
			StaticEntry.PhysicsBodyCount != 0
				? StaticEntry.PhysicsBodyCount
				: StaticEntry.Bricks.Num() + StaticEntry.Devices.Num()
					+ StaticEntry.Caps.Num(),
			StaticEntry.PhysicsAssemblyHash,
			Result.CompactAssembly.BearingContacts.Num(),
			Result.LoadDAG.Summary.GroundNodeCount,
			Result.Summary.SupportResultantAdvisoryCount,
			FMath::RoundToInt(ProductionOuterDeltaSeconds * 1000000.0f),
			FMath::RoundToInt(MinimumObservationSeconds * 1000.0f),
			FMath::RoundToInt(StableHoldSeconds * 1000.0f),
			FMath::RoundToInt(MaximumObservationSeconds * 1000.0f),
			FMath::RoundToInt(MaximumLinearSpeedCMPerSec * 1000.0f),
			FMath::RoundToInt(MaximumAngularSpeedDegreesPerSec * 1000.0f),
			FMath::RoundToInt(MaximumPlanarDriftCM * 1000.0f),
			FMath::RoundToInt(MaximumSettlementCM * 1000.0f),
			FMath::RoundToInt(MaximumRotationDegrees * 1000.0f),
			BodyProfileHash,
			WorldProfileHash,
			FMath::RoundToInt(GravityCMPerSec2 * 1000.0f),
			FABTSM7SiteUniformGravityPolicy::SchemaVersion,
			SiteGravity.ComputeCrc32(),
			FMath::RoundToInt(SiteGravity.SiteUp.X * 1000000.0),
			FMath::RoundToInt(SiteGravity.SiteUp.Y * 1000000.0),
			FMath::RoundToInt(SiteGravity.SiteUp.Z * 1000000.0));
		return FCrc::StrCrc32(*Canonical);
	}

	uint32 ComputeResultCrc32(
		const uint32 CandidateCrc32,
		const bool bAccepted,
		const FObservationResult& Observation)
	{
		const FString Canonical = FString::Printf(
			TEXT("BeamStage5ChaosResult:v1:Candidate=%u:Accepted=%d")
			TEXT(":ReachedQuiet=%d:EndedQuiet=%d:FirstQuietMS=%d")
			TEXT(":FinalDriftMilliCM=%d:FinalSettlementMilliCM=%d")
			TEXT(":FinalRotationMilliDegrees=%d:FinalLinearMilli=%d")
			TEXT(":FinalAngularMilli=%d:FinalAwake=%d")
			TEXT(":PeakDriftMilliCM=%d:PeakSettlementMilliCM=%d")
			TEXT(":PeakRotationMilliDegrees=%d"),
			CandidateCrc32, bAccepted ? 1 : 0,
			Observation.bReachedQuietWindow ? 1 : 0,
			Observation.bEndedInQuietWindow ? 1 : 0,
			FMath::RoundToInt(Observation.FirstQuietWindowSeconds * 1000.0f),
			FMath::RoundToInt(Observation.FinalMaximumPlanarDriftCM * 1000.0f),
			FMath::RoundToInt(Observation.FinalMaximumSettlementCM * 1000.0f),
			FMath::RoundToInt(Observation.FinalMaximumRotationDegrees * 1000.0f),
			FMath::RoundToInt(Observation.FinalMaximumLinearSpeedCMPerSec * 1000.0f),
			FMath::RoundToInt(Observation.FinalMaximumAngularSpeedDegreesPerSec * 1000.0f),
			Observation.FinalAwakeBodyCount,
			FMath::RoundToInt(Observation.PeakPlanarDriftCM * 1000.0f),
			FMath::RoundToInt(Observation.PeakSettlementCM * 1000.0f),
			FMath::RoundToInt(Observation.PeakRotationDegrees * 1000.0f));
		return FCrc::StrCrc32(*Canonical);
	}

	bool ObserveUnderGravity(
		FAutomationTestBase& Test,
		FStage5PhysicsWorld& WorldWrapper,
		const TArray<AABTSM7BuildingModule*>& Modules,
		const TArray<FTransform>& Baselines,
		const FVector& SiteUp,
		FObservationResult& OutResult)
	{
		if (Modules.IsEmpty() || Baselines.Num() != Modules.Num())
		{
			Test.AddError(TEXT("Stage-5 entry has invalid Chaos observation inputs"));
			return false;
		}
		if (SiteUp.ContainsNaN() || !SiteUp.IsNormalized())
		{
			Test.AddError(TEXT("Stage-5 entry has an invalid Site-uniform gravity frame"));
			return false;
		}

		float QuietSeconds = 0.0f;
		float ElapsedSeconds = 0.0f;
		const int32 MaximumTicks = FMath::CeilToInt(
			MaximumObservationSeconds / ProductionOuterDeltaSeconds);
		for (int32 TickIndex = 0; TickIndex < MaximumTicks; ++TickIndex)
		{
			if (!WorldWrapper.TickTestWorld(ProductionOuterDeltaSeconds))
			{
				WorldWrapper.ForwardErrorMessages(&Test);
				return false;
			}

			ElapsedSeconds += ProductionOuterDeltaSeconds;
			OutResult.FinalMaximumPlanarDriftCM = 0.0f;
			OutResult.FinalMaximumSettlementCM = 0.0f;
			OutResult.FinalMaximumRotationDegrees = 0.0f;
			OutResult.FinalMaximumLinearSpeedCMPerSec = 0.0f;
			OutResult.FinalMaximumAngularSpeedDegreesPerSec = 0.0f;
			OutResult.FinalAwakeBodyCount = 0;
			OutResult.FinalMaximumPlanarDriftBrickIndex = INDEX_NONE;
			OutResult.FinalMaximumSettlementBrickIndex = INDEX_NONE;
			OutResult.FinalMaximumRotationBrickIndex = INDEX_NONE;
			bool bEveryBodyQuiet = true;
			for (int32 ModuleIndex = 0; ModuleIndex < Modules.Num(); ++ModuleIndex)
			{
				const AABTSM7BuildingModule* Module = Modules[ModuleIndex];
				const UStaticMeshComponent* Mesh =
					Module != nullptr ? Module->GetMeshComponent() : nullptr;
				if (Module == nullptr || Mesh == nullptr)
				{
					Test.AddError(FString::Printf(
						TEXT("Stage-5 entry lost Brick %d during Chaos"), ModuleIndex));
					return false;
				}

				const FVector Delta = Module->GetActorLocation()
					- Baselines[ModuleIndex].GetLocation();
				const float PlanarDriftCM =
					FVector::VectorPlaneProject(Delta, SiteUp).Size();
				const float SettlementCM =
					FMath::Abs(FVector::DotProduct(Delta, SiteUp));
				const float RotationDegrees = FMath::RadiansToDegrees(
					Baselines[ModuleIndex].GetRotation().AngularDistance(
						Module->GetActorQuat()));
				const float LinearSpeedCMPerSec =
					Mesh->GetPhysicsLinearVelocity().Size();
				const float AngularSpeedDegreesPerSec =
					Mesh->GetPhysicsAngularVelocityInDegrees().Size();
				if (PlanarDriftCM > OutResult.FinalMaximumPlanarDriftCM)
				{
					OutResult.FinalMaximumPlanarDriftCM = PlanarDriftCM;
					OutResult.FinalMaximumPlanarDriftBrickIndex = ModuleIndex;
				}
				if (SettlementCM > OutResult.FinalMaximumSettlementCM)
				{
					OutResult.FinalMaximumSettlementCM = SettlementCM;
					OutResult.FinalMaximumSettlementBrickIndex = ModuleIndex;
				}
				if (RotationDegrees > OutResult.FinalMaximumRotationDegrees)
				{
					OutResult.FinalMaximumRotationDegrees = RotationDegrees;
					OutResult.FinalMaximumRotationBrickIndex = ModuleIndex;
				}
				OutResult.FinalMaximumLinearSpeedCMPerSec = FMath::Max(
					OutResult.FinalMaximumLinearSpeedCMPerSec, LinearSpeedCMPerSec);
				OutResult.FinalMaximumAngularSpeedDegreesPerSec = FMath::Max(
					OutResult.FinalMaximumAngularSpeedDegreesPerSec,
					AngularSpeedDegreesPerSec);
				const FBodyInstance* Body = Mesh->GetBodyInstance();
				OutResult.FinalAwakeBodyCount +=
					!Module->IsCompoundChild() && Body != nullptr
						&& Body->IsInstanceAwake() ? 1 : 0;
				bEveryBodyQuiet = bEveryBodyQuiet
					&& LinearSpeedCMPerSec <= MaximumLinearSpeedCMPerSec
					&& AngularSpeedDegreesPerSec
						<= MaximumAngularSpeedDegreesPerSec;
			}

			OutResult.PeakPlanarDriftCM = FMath::Max(
				OutResult.PeakPlanarDriftCM,
				OutResult.FinalMaximumPlanarDriftCM);
			OutResult.PeakSettlementCM = FMath::Max(
				OutResult.PeakSettlementCM,
				OutResult.FinalMaximumSettlementCM);
			OutResult.PeakRotationDegrees = FMath::Max(
				OutResult.PeakRotationDegrees,
				OutResult.FinalMaximumRotationDegrees);
			if (ElapsedSeconds >= MinimumObservationSeconds && bEveryBodyQuiet)
			{
				QuietSeconds += ProductionOuterDeltaSeconds;
			}
			else
			{
				QuietSeconds = 0.0f;
			}
			if (!OutResult.bReachedQuietWindow
				&& QuietSeconds >= StableHoldSeconds)
			{
				OutResult.bReachedQuietWindow = true;
				OutResult.FirstQuietWindowSeconds = ElapsedSeconds;
			}
		}
		OutResult.bEndedInQuietWindow = QuietSeconds >= StableHoldSeconds;

		Test.TestTrue(TEXT("Stage-5 entry reaches a real quiet window"),
			OutResult.bReachedQuietWindow);
		Test.TestTrue(TEXT("Stage-5 entry ends in a continuous quiet window without Freeze"),
			OutResult.bEndedInQuietWindow);
		Test.TestEqual(TEXT("Stage-5 entry ends with every Chaos body asleep"),
			OutResult.FinalAwakeBodyCount, 0);
		Test.TestTrue(TEXT("Stage-5 entry final planar drift remains bounded"),
			OutResult.FinalMaximumPlanarDriftCM <= MaximumPlanarDriftCM);
		Test.TestTrue(TEXT("Stage-5 entry final settlement remains bounded"),
			OutResult.FinalMaximumSettlementCM <= MaximumSettlementCM);
		Test.TestTrue(TEXT("Stage-5 entry final rotation remains bounded"),
			OutResult.FinalMaximumRotationDegrees <= MaximumRotationDegrees);
		Test.TestTrue(TEXT("Stage-5 entry peak planar drift remains bounded"),
			OutResult.PeakPlanarDriftCM <= MaximumPlanarDriftCM);
		Test.TestTrue(TEXT("Stage-5 entry peak settlement remains bounded"),
			OutResult.PeakSettlementCM <= MaximumSettlementCM);
		Test.TestTrue(TEXT("Stage-5 entry peak rotation remains bounded"),
			OutResult.PeakRotationDegrees <= MaximumRotationDegrees);
		return !Test.HasAnyErrors();
	}

	bool RunEntryChaosStability(
		FAutomationTestBase& Test,
		const FABTSM73BeamDemoManifestEntry& Entry)
	{
		const double StartSeconds = FPlatformTime::Seconds();
		FString Error;
		FABTSM73BuildingFreezeV3Descriptor Descriptor;
		if (!Test.TestTrue(TEXT("Frozen V3 production descriptor resolves"),
			FABTSM73BuildingFreezeV3::DeriveAndValidate(
				Entry.Id, Descriptor, Error,
				Entry.Id == EABTSM73BeamDemoBuilding::E6TipOver)))
		{
			Test.AddError(Error);
			return false;
		}

		FABTSM73BeamD1MaterialPolicy MaterialPolicy;
		MaterialPolicy.bOverrideOrdinaryBody = true;
		MaterialPolicy.PrimaryMaterial = Descriptor.PrimaryMaterial;
		FABTSM73BeamD1Stage55Result Source;
		if (!Test.TestTrue(TEXT("Frozen Stage-5 production entry generates"),
			FABTSM73BeamD1BrickCompiler().
				GenerateStage55DeviceAssemblyWithMaterialPolicy(
					Entry.Settings, MaterialPolicy, Source, Error)))
		{
			Test.AddError(Error);
			return false;
		}
		const FABTSM73BeamD1Stage5Result& Result = Source.Stage5;
		Test.TestEqual(TEXT("Stage-5 entry has no support-resultant advisory"),
			Result.Summary.SupportResultantAdvisoryCount, 0);
		Test.TestFalse(TEXT("Stage 5 does not claim Chaos evidence"),
			Result.bPhysicalStabilityEvaluated);
		for (int32 NodeIndex = 0; NodeIndex < Result.LoadDAG.Nodes.Num(); ++NodeIndex)
		{
			const FABTSM73BeamCLoadNode& Node = Result.LoadDAG.Nodes[NodeIndex];
			if (Node.bSupportResultantValid)
			{
				continue;
			}
			int32 LowerContactCount = 0;
			for (const FABTSM73BeamCLoadEdge& Edge : Result.LoadDAG.Edges)
			{
				if (Edge.UpperMemberId != Node.MemberId)
				{
					continue;
				}
				++LowerContactCount;
				UE_LOG(LogABTSRuntime, Error,
					TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][InvalidResultantSupport] Entry=%s Upper=%d Lower=%d Position=%s Min=%s Max=%s Area=%.3f Share=%.6f ReactionKG=%.3f"),
					*Entry.StableId.ToString(), Node.MemberId,
					Edge.LowerMemberId, *Edge.ContactPosition.ToString(),
					*Edge.ContactMinXY.ToString(),
					*Edge.ContactMaxXY.ToString(),
					Edge.ContactAreaCM2, Edge.LoadShare,
					Edge.ReactionLoadKG);
			}
			const FABTSM73BeamD1BrickBinding* Brick = Result.Bricks.FindByPredicate(
				[&Node](const FABTSM73BeamD1BrickBinding& Candidate)
				{
					return Candidate.MemberId == Node.MemberId;
				});
			UE_LOG(LogABTSRuntime, Error,
				TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][InvalidResultant] Entry=%s Node=%d Member=%d Axis=%d Ground=%d Supports=%d LowerContacts=%d SelfKG=%.3f AccumKG=%.3f Resultant=%s Center=%s Dimensions=%s"),
				*Entry.StableId.ToString(), NodeIndex, Node.MemberId,
				static_cast<int32>(Node.Axis), Node.bGround ? 1 : 0,
				Node.SupportCount, LowerContactCount, Node.SelfLoadKG,
				Node.AccumulatedLoadKG, *Node.LoadResultant.ToString(),
				Brick != nullptr
					? *Brick->LocalTransform.GetLocation().ToString()
					: TEXT("Missing"),
				Brick != nullptr
					? *Brick->BrickSpec.DimensionsCM.ToString()
					: TEXT("Missing"));
		}

		FStage5PhysicsWorld WorldWrapper;
		if (!WorldWrapper.CreatePhysicsWorld())
		{
			WorldWrapper.ForwardErrorMessages(&Test);
			return false;
		}
		UWorld* World = WorldWrapper.GetTestWorld();
		if (!Test.TestNotNull(TEXT("Stage-5 isolated physics world"), World))
		{
			return false;
		}
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AABTSM3Planet* JuryPlanet = World->SpawnActor<AABTSM3Planet>(
			AABTSM3Planet::StaticClass(), FTransform::Identity, SpawnParameters);
		if (!Test.TestNotNull(TEXT("Frozen M3 Fixed-Six producer"), JuryPlanet))
		{
			return false;
		}
		JuryPlanet->WorldSeed = FABTSJuryDemoFixedSixContract::FrozenWorldSeed;
		if (!Test.TestTrue(TEXT("Frozen M3 Fixed-Six world rebuilds"),
			JuryPlanet->RebuildPlanet()))
		{
			return false;
		}
		FABTSBuildingGenerationContract ProductionContract;
		if (!Test.TestTrue(TEXT("M3 exports the production Fixed-Six contract"),
			JuryPlanet->TryExportBuildingGenerationContract(ProductionContract)))
		{
			return false;
		}
		Test.TestEqual(TEXT("Map Freeze exports the V3 production contract"),
			ProductionContract.JuryDemoFixedSix.ContractVersion,
			FABTSJuryDemoFixedSixContract::SupportedV3ContractVersion);
		Test.TestEqual(TEXT("Map Freeze exports the V3 production layout"),
			ProductionContract.JuryDemoFixedSix.LayoutHash,
			FABTSJuryDemoFixedSixContract::FrozenV3LayoutHash);

		Test.TestEqual(TEXT("V3 descriptor keeps the Stage-5 source identity"),
			Descriptor.SourceStage5ProductionHash,
			static_cast<uint64>(Result.ProductionIdentityHash));

		FName ContractManifestEntryName;
		if (!Test.TestTrue(TEXT("Demo manifest entry maps to the production contract name"),
			TryResolveProductionContractManifestEntryName(
				Entry, ContractManifestEntryName)))
		{
			return false;
		}
		const int32 SiteIndex =
			ProductionContract.JuryDemoFixedSix.Sites.IndexOfByPredicate(
				[ContractManifestEntryName](
					const FABTSJuryDemoFixedSixBuildingSite& Candidate)
				{
					return Candidate.ManifestEntryId == ContractManifestEntryName;
				});
		if (!Test.TestTrue(TEXT("V3 contract contains the requested complexity"),
			ProductionContract.JuryDemoFixedSix.Sites.IsValidIndex(SiteIndex)))
		{
			return false;
		}
		const FABTSJuryDemoFixedSixBuildingSite Site =
			ProductionContract.JuryDemoFixedSix.Sites[SiteIndex];
		FABTSM73JuryDemoFixedSixStaticEntry StaticEntry;
		FABTSM73JuryDemoFixedSixStaticPlan StaticPlan;
		const bool bPendingAtomicPhysicsSeal =
			Descriptor.PhysicsAssemblyHash != 0
			&& Site.DescriptorHash != Descriptor.DescriptorHash;
		if (bPendingAtomicPhysicsSeal)
		{
			StaticEntry = MakePendingAtomicPhysicsEntry(
				Entry, Descriptor, Site,
				ProductionContract.JuryDemoFixedSix.ContractVersion,
				ProductionContract.JuryDemoFixedSix.LayoutHash);
			UE_LOG(LogABTSRuntime, Display,
				TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][PendingIntegrationSeal]")
				TEXT(" Entry=%s Layout=%llu Placement=%llu OldDescriptor=%llu CandidateDescriptor=%llu")
				TEXT(" Static=%llu Production=%llu PhysicsBodies=%d PhysicsAssembly=%llu Accepted=1"),
				*Entry.StableId.ToString(),
				ProductionContract.JuryDemoFixedSix.LayoutHash,
				Site.V3Envelope.PlacementHash, Site.DescriptorHash,
				Descriptor.DescriptorHash, Descriptor.StaticGeometryHash,
				Descriptor.ProductionHash, Descriptor.PhysicsBodyCount,
				Descriptor.PhysicsAssemblyHash);
		}
		else
		{
			if (!Test.TestTrue(TEXT("Production V3 static plan resolves"),
				FABTSM73JuryDemoFixedSixRegistration::BuildStaticPlan(
					ProductionContract, StaticPlan, Error)))
			{
				Test.AddError(Error);
				return false;
			}
			Test.TestEqual(TEXT("Production V3 registration identity is frozen"),
				StaticPlan.RegistrationResultHash,
				FABTSM73JuryDemoFixedSixRegistration::
					FrozenV3RegistrationResultHash);
			const int32 StaticEntryIndex =
				StaticPlan.Entries.IndexOfByPredicate(
					[ContractManifestEntryName](
						const FABTSM73JuryDemoFixedSixStaticEntry& Candidate)
					{
						return Candidate.ManifestEntryId
							== ContractManifestEntryName;
					});
			if (!Test.TestTrue(TEXT("V3 static plan contains the requested complexity"),
				StaticPlan.Entries.IsValidIndex(StaticEntryIndex)))
			{
				return false;
			}
			StaticEntry = StaticPlan.Entries[StaticEntryIndex];
		}
		const FVector SupportCenter = Site.V3Envelope.SupportCenterWorldCM;
		FABTSM7SiteUniformGravityPolicy SiteGravity;
		if (!Test.TestTrue(TEXT("V3 Site-uniform gravity policy derives from frozen placement"),
			FABTSM7SiteUniformGravityPolicy::TryDerive(
				Site.WorldTransform.GetLocation(), SupportCenter,
				GravityCMPerSec2, SiteGravity)))
		{
			return false;
		}
		Test.TestTrue(TEXT("V3 frozen tangent frame agrees with derived SiteUp"),
			FVector::DotProduct(
				SiteGravity.SiteUp,
				Site.WorldTransform.GetUnitAxis(EAxis::Z)) >= 1.0 - 1.0e-6);
		FABTSM7SiteUniformGravityPolicy InvalidSiteGravity;
		Test.TestFalse(TEXT("Site-uniform gravity fails closed at the support center"),
			FABTSM7SiteUniformGravityPolicy::TryDerive(
				SupportCenter, SupportCenter,
				GravityCMPerSec2, InvalidSiteGravity));
		Test.TestTrue(TEXT("Production static plan retains the V3 support center"),
			StaticEntry.SupportCenterWorldCM.Equals(
				Site.V3Envelope.SupportCenterWorldCM, 1.0e-6));
		Test.TestEqual(TEXT("Production static plan retains the V3 support radius"),
			StaticEntry.SupportRadiusCM,
			Site.V3Envelope.SupportRadiusCM);
		Test.TestEqual(TEXT("Production static plan retains the V3 gravity authority"),
			StaticEntry.GravityAuthorityId,
			Site.V3Envelope.GravityAuthorityId);
		Test.TestEqual(TEXT("Production static plan retains the V3 gravity identity"),
			StaticEntry.GravityIdentityHash,
			Site.V3Envelope.GravityIdentityHash);
		FABTSM7SiteUniformGravityPolicy ProductionActorSiteGravity;
		Test.TestTrue(TEXT("Production Actor payload derives the same Site-uniform policy"),
			FABTSM7SiteUniformGravityPolicy::TryDerive(
				StaticEntry.WorldTransform.GetLocation(),
				StaticEntry.SupportCenterWorldCM,
				GravityCMPerSec2, ProductionActorSiteGravity));
		Test.TestEqual(TEXT("Fixture and production Actor payload share Site gravity hash"),
			ProductionActorSiteGravity.ComputeCrc32(),
			SiteGravity.ComputeCrc32());
		const bool bExpectedSatellite = Entry.Id
			== EABTSM73BeamDemoBuilding::E1ColumnBreak;
		const EABTSJuryDemoFixedSixSurfaceKind ExpectedSurface =
			bExpectedSatellite
				? EABTSJuryDemoFixedSixSurfaceKind::Satellite
				: EABTSJuryDemoFixedSixSurfaceKind::PrimaryPlanet;
		Test.TestEqual(TEXT("Complexity resolves its frozen V3 surface"),
			Site.V3Envelope.SurfaceKind, ExpectedSurface);
		Test.TestEqual(TEXT("Complexity resolves its independent encounter slot"),
			Site.EncounterIndex, Descriptor.EncounterSlot);
		Test.TestEqual(TEXT("V3 site keeps the manifest seed"),
			Site.DeterministicSeed, Entry.Settings.BuildingSeed);
		if (bPendingAtomicPhysicsSeal)
		{
			Test.TestNotEqual(TEXT("Atomic M7 physics candidate requires a new shared descriptor seal"),
				Site.DescriptorHash, StaticEntry.DescriptorHash);
		}
		else
		{
			Test.TestEqual(TEXT("V3 contract and production plan share descriptor identity"),
				Site.DescriptorHash, StaticEntry.DescriptorHash);
		}
		Test.TestEqual(TEXT("V3 contract and production plan share static geometry"),
			Site.V3Envelope.StaticGeometryHash,
			StaticEntry.StaticGeometryHash);
		if (bPendingAtomicPhysicsSeal)
		{
			Test.TestNotEqual(TEXT("Atomic M7 physics candidate requires a new shared production seal"),
				Site.V3Envelope.ProductionIdentityHash,
				StaticEntry.ProductionIdentityHash);
		}
		else
		{
			Test.TestEqual(TEXT("V3 contract and production plan share production identity"),
				Site.V3Envelope.ProductionIdentityHash,
				StaticEntry.ProductionIdentityHash);
		}
		Test.TestEqual(TEXT("V3 contract and production plan share device identity"),
			Site.V3Envelope.DeviceAssemblyHash,
			StaticEntry.DeviceAssemblyHash);
		Test.TestTrue(TEXT("V3 gravity authority is explicit"),
			!Site.V3Envelope.GravityAuthorityId.IsNone()
				&& Site.V3Envelope.GravityIdentityHash != 0
				&& !SupportCenter.ContainsNaN()
				&& Site.V3Envelope.SupportRadiusCM > 0.0);
		Test.AddInfo(FString::Printf(
			TEXT("PositionAuthority=M3MapFreezeV3 GeometryAuthority=M7BuildingFreezeV3")
			TEXT(" Complexity=%s EncounterSlot=%d Surface=%d GravityAuthority=%s")
			TEXT(" GravityHash=%llu PlacementHash=%llu Descriptor=%llu Static=%llu Production=%llu Device=%llu"),
			*Entry.StableId.ToString(), Site.EncounterIndex,
			static_cast<int32>(Site.V3Envelope.SurfaceKind),
			*Site.V3Envelope.GravityAuthorityId.ToString(),
			Site.V3Envelope.GravityIdentityHash,
			Site.V3Envelope.PlacementHash, StaticEntry.DescriptorHash,
			StaticEntry.StaticGeometryHash, StaticEntry.ProductionIdentityHash,
			StaticEntry.DeviceAssemblyHash));
		if (Test.HasAnyErrors())
		{
			return false;
		}
		// The contract and center are immutable value snapshots. Remove the
		// producer terrain so the fixture support remains the frozen tangent pad.
		World->DestroyActor(JuryPlanet);

		AABTSM71PhysicsTestStage* Stage =
			World->SpawnActorDeferred<AABTSM71PhysicsTestStage>(
				AABTSM71PhysicsTestStage::StaticClass(),
				Site.WorldTransform,
				nullptr,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Stage != nullptr)
		{
			UGameplayStatics::FinishSpawningActor(Stage, Site.WorldTransform);
		}
		AABTSM7BuildingMaterialSystem* MaterialSystem =
			World->SpawnActor<AABTSM7BuildingMaterialSystem>(
				AABTSM7BuildingMaterialSystem::StaticClass(),
				FTransform::Identity,
				SpawnParameters);
		if (!Test.TestNotNull(TEXT("Stage-5 frozen tangent support"), Stage)
			|| !Test.TestNotNull(TEXT("Stage-5 material system"), MaterialSystem))
		{
			return false;
		}
		if (!WorldWrapper.BeginPlayInTestWorld())
		{
			WorldWrapper.ForwardErrorMessages(&Test);
			return false;
		}

		AABTSM7BuildingModule* EmptyHandleProbe =
			World->SpawnActor<AABTSM7BuildingModule>(
				AABTSM7BuildingModule::StaticClass(),
				FTransform::Identity,
				SpawnParameters);
		if (!Test.TestNotNull(TEXT("Empty-handle gravity probe"), EmptyHandleProbe)
			|| !Test.TestNotNull(TEXT("Empty-handle gravity probe mesh"),
				EmptyHandleProbe != nullptr
					? EmptyHandleProbe->GetMeshComponent() : nullptr))
		{
			return false;
		}
		const bool bEmptyHandleAccepted =
			AABTSM7BuildingModule::TryApplyNonInvalidatingAcceleration(
				*EmptyHandleProbe->GetMeshComponent(), FVector::DownVector);
		Test.TestFalse(TEXT("Non-invalidating acceleration fails closed without a physics handle"),
			bEmptyHandleAccepted);
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][EmptyPhysicsHandle]")
			TEXT(" Entry=%s Policy=FailClosed Accepted=%d"),
			*Entry.StableId.ToString(), bEmptyHandleAccepted ? 0 : 1);
		EmptyHandleProbe->Destroy();

		// Production radial gravity is actor-driven because built-in gravity is
		// disabled. Prove that the sleep guard does not turn gravity into a
		// one-way latch: a sleeping body remains untouched, while an explicit
		// wake resumes the same radial acceleration on the next physics step.
		FABTSM7BrickSpec WakeProbeSpec;
		WakeProbeSpec.Material = Descriptor.PrimaryMaterial;
		WakeProbeSpec.DimensionsCM = FVector(20.0f);
		FVector ProbeUp = (Site.WorldTransform.GetLocation()
			- SupportCenter).GetSafeNormal();
		if (ProbeUp.IsNearlyZero())
		{
			ProbeUp = Site.WorldTransform.GetUnitAxis(EAxis::Z);
		}
		const FVector ProbeLocation = Site.WorldTransform.GetLocation()
			+ ProbeUp * 5000.0f
			+ Site.WorldTransform.GetUnitAxis(EAxis::X) * 5000.0f;
		AABTSM7BuildingModule* WakeProbe =
			MaterialSystem->SpawnStaticBrickModule(
				WakeProbeSpec, FTransform(ProbeLocation));
		if (!Test.TestNotNull(TEXT("Site-uniform-gravity wake probe"), WakeProbe)
			|| !Test.TestNotNull(TEXT("Site-uniform-gravity wake probe mesh"),
				WakeProbe != nullptr ? WakeProbe->GetMeshComponent() : nullptr))
		{
			return false;
		}
		UStaticMeshComponent* WakeProbeMesh = WakeProbe->GetMeshComponent();
		if (!Test.TestTrue(TEXT("Wake probe accepts the production Site-uniform policy"),
			WakeProbe->ActivateDynamicSiteUniform(
				FVector::ZeroVector, SiteGravity)))
		{
			return false;
		}
		WakeProbeMesh->SetPhysicsLinearVelocity(FVector::ZeroVector);
		WakeProbeMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		WakeProbeMesh->PutAllRigidBodiesToSleep();
		Test.TestFalse(TEXT("Sleeping radial-gravity body is asleep before guarded tick"),
			WakeProbeMesh->IsAnyRigidBodyAwake());
		if (!WorldWrapper.TickTestWorld(ProductionOuterDeltaSeconds))
		{
			WorldWrapper.ForwardErrorMessages(&Test);
			return false;
		}
		const float SleepingSpeed =
			WakeProbeMesh->GetPhysicsLinearVelocity().Size();
		const bool bStayedAsleep = !WakeProbeMesh->IsAnyRigidBodyAwake();
		Test.TestTrue(TEXT("Guarded radial-gravity tick does not wake sleeping body"),
			bStayedAsleep);
		Test.TestTrue(TEXT("Guarded radial-gravity tick leaves sleeping velocity at zero"),
			SleepingSpeed <= KINDA_SMALL_NUMBER);
		WakeProbeMesh->WakeAllRigidBodies();
		Test.TestTrue(TEXT("Explicit wake reactivates radial-gravity body"),
			WakeProbeMesh->IsAnyRigidBodyAwake());
		if (!WorldWrapper.TickTestWorld(ProductionOuterDeltaSeconds))
		{
			WorldWrapper.ForwardErrorMessages(&Test);
			return false;
		}
		const FVector ProbeGravityDirection = -SiteGravity.SiteUp;
		const float ResumedSiteGravitySpeed = FVector::DotProduct(
			WakeProbeMesh->GetPhysicsLinearVelocity(), ProbeGravityDirection);
		Test.TestTrue(TEXT("Explicit wake resumes Site-uniform gravity"),
			ResumedSiteGravitySpeed > 1.0f);
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][GravityWakePolicy]")
			TEXT(" Entry=%s GravityPolicy=SiteUniformTangentGravity")
			TEXT(" Derivation=Normalize(SiteLocationWorldCM-SupportCenterWorldCM)")
			TEXT(" SiteGravityHash=%u SiteUp=%s")
			TEXT(" WakePolicy=NonInvalidatingForceSkipSleepingBodiesResumeOnExplicitWake")
			TEXT(" SleepingSpeed=%.6f ResumedSiteGravitySpeed=%.6f Accepted=%d"),
			*Entry.StableId.ToString(), SiteGravity.ComputeCrc32(),
			*SiteGravity.SiteUp.ToString(), SleepingSpeed,
			ResumedSiteGravitySpeed,
			bStayedAsleep && SleepingSpeed <= KINDA_SMALL_NUMBER
				&& ResumedSiteGravitySpeed > 1.0f ? 1 : 0);
		WakeProbe->Destroy();
		if (!WorldWrapper.TickTestWorld(ProductionOuterDeltaSeconds))
		{
			WorldWrapper.ForwardErrorMessages(&Test);
			return false;
		}
		if (Test.HasAnyErrors())
		{
			return false;
		}

		TArray<AABTSM7BuildingModule*> Modules;
		TArray<AABTSM7BuildingModule*> PhysicsModules;
		TArray<FTransform> InitialTransforms;
		TArray<double> ExpectedBrickMassKG;
		const int32 ExpectedModuleCount = StaticEntry.Bricks.Num()
			+ StaticEntry.Devices.Num() + StaticEntry.Caps.Num();
		Modules.Reserve(ExpectedModuleCount);
		InitialTransforms.Reserve(ExpectedModuleCount);
		ExpectedBrickMassKG.Reserve(StaticEntry.Bricks.Num());
		FBox GroundSupportBounds(EForceInit::ForceInit);
		double TotalMassKG = 0.0;
		double BrickMassKG = 0.0;
		double CalculatedChaosMassKG = 0.0;
		FVector MassMoment = FVector::ZeroVector;
		int32 BrickMassIndex = 0;
		for (const FABTSM73BeamD1BrickBinding& Brick : StaticEntry.Bricks)
		{
			const FTransform BrickWorldTransform =
				Brick.LocalTransform * Site.WorldTransform;
			AABTSM7BuildingModule* Module = MaterialSystem->SpawnBrickModule(
				Brick.BrickSpec, BrickWorldTransform);
			if (!Test.TestNotNull(TEXT("Stage-5 Brick spawns as an independent body"),
				Module))
			{
				return false;
			}
			Module->SetContactDamageGraceSeconds(MaximumObservationSeconds + 1.0f);
			UStaticMeshComponent* Mesh = Module->GetMeshComponent();
			if (!Test.TestNotNull(TEXT("Stage-5 Brick owns a collision mesh"), Mesh))
			{
				return false;
			}
			FBodyInstance* Body = Mesh->GetBodyInstance();
			if (!Test.TestNotNull(TEXT("Stage-5 Brick owns a Chaos body"), Body))
			{
				return false;
			}
			const double MassKG = Body->GetBodyMass();
			TotalMassKG += MassKG;
			BrickMassKG += MassKG;
			UBodySetup* BodySetup = Body->GetBodySetup();
			if (!Test.TestNotNull(TEXT("Stage-5 Brick owns a BodySetup"), BodySetup))
			{
				return false;
			}
			const double ExpectedMassKG = BodySetup->CalculateMass(Mesh);
			ExpectedBrickMassKG.Add(ExpectedMassKG);
			const double MassToleranceKG = FMath::Max(0.01, ExpectedMassKG * 0.001);
			CalculatedChaosMassKG += ExpectedMassKG;
			Test.TestTrue(*FString::Printf(
				TEXT("BodySetup mass matches Brick Index=%d Member=%d Actual=%.6f Expected=%.6f Tolerance=%.6f"),
				BrickMassIndex, Brick.MemberId, MassKG, ExpectedMassKG,
				MassToleranceKG),
				FMath::IsNearlyEqual(MassKG, ExpectedMassKG, MassToleranceKG));
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][BodyMass]")
				TEXT(" Entry=%s Type=Brick Index=%d Member=%d ActualKG=%.6f ExpectedKG=%.6f ToleranceKG=%.6f Accepted=%d"),
				*Entry.StableId.ToString(), BrickMassIndex, Brick.MemberId,
				MassKG, ExpectedMassKG, MassToleranceKG,
				FMath::IsNearlyEqual(MassKG, ExpectedMassKG, MassToleranceKG) ? 1 : 0);
			++BrickMassIndex;
			MassMoment += Brick.LocalTransform.GetLocation() * MassKG;
			if (Brick.LocalBounds.Min.Z <= GroundContactToleranceCM)
			{
				GroundSupportBounds += Brick.LocalBounds.Min;
				GroundSupportBounds += Brick.LocalBounds.Max;
			}
			Modules.Add(Module);
			InitialTransforms.Add(Module->GetActorTransform());
			UE_LOG(LogABTSRuntime, VeryVerbose,
				TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][SpawnMap] Entry=%s Brick=%d Actor=%s Member=%d Center=%s Dimensions=%s"),
				*Entry.StableId.ToString(), Modules.Num() - 1,
				*Module->GetName(), Brick.MemberId,
				*Brick.LocalTransform.GetLocation().ToString(),
				*Brick.BrickSpec.DimensionsCM.ToString());
		}
		int32 DeviceMassIndex = 0;
		for (const FABTSM73BeamD1DeviceBinding& Device : StaticEntry.Devices)
		{
			AABTSM7BuildingModule* Module = MaterialSystem->SpawnVoxelDevice(
				Device.DeviceSpec, Device.LocalTransform * Site.WorldTransform);
			if (!Test.TestNotNull(TEXT("V3 device spawns through the production module path"),
				Module))
			{
				return false;
			}
			Module->SetContactDamageGraceSeconds(MaximumObservationSeconds + 1.0f);
			UStaticMeshComponent* Mesh = Module->GetMeshComponent();
			if (!Test.TestNotNull(TEXT("V3 device owns a collision mesh"), Mesh))
			{
				return false;
			}
			FBodyInstance* Body = Mesh->GetBodyInstance();
			if (!Test.TestNotNull(TEXT("V3 device owns a Chaos body"), Body))
			{
				return false;
			}
			UBodySetup* BodySetup = Body->GetBodySetup();
			if (!Test.TestNotNull(TEXT("V3 device owns a BodySetup"), BodySetup))
			{
				return false;
			}
			const double MassKG = Body->GetBodyMass();
			TotalMassKG += MassKG;
			const double ExpectedMassKG = BodySetup->CalculateMass(Mesh);
			const double MassToleranceKG = FMath::Max(0.01, ExpectedMassKG * 0.001);
			CalculatedChaosMassKG += ExpectedMassKG;
			Test.TestTrue(*FString::Printf(
				TEXT("BodySetup mass matches Device Index=%d Actual=%.6f Expected=%.6f Tolerance=%.6f"),
				DeviceMassIndex, MassKG, ExpectedMassKG, MassToleranceKG),
				FMath::IsNearlyEqual(MassKG, ExpectedMassKG, MassToleranceKG));
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][BodyMass]")
				TEXT(" Entry=%s Type=Device Index=%d ActualKG=%.6f ExpectedKG=%.6f ToleranceKG=%.6f Accepted=%d"),
				*Entry.StableId.ToString(), DeviceMassIndex, MassKG,
				ExpectedMassKG, MassToleranceKG,
				FMath::IsNearlyEqual(MassKG, ExpectedMassKG, MassToleranceKG) ? 1 : 0);
			++DeviceMassIndex;
			MassMoment += Device.LocalTransform.GetLocation() * MassKG;
			Modules.Add(Module);
			InitialTransforms.Add(Module->GetActorTransform());
		}
		int32 CapMassIndex = 0;
		for (const FABTSM73BuildingFreezeV3CapBinding& Cap : StaticEntry.Caps)
		{
			AABTSM7BuildingModule* Module = MaterialSystem->SpawnBrickModule(
				Cap.BrickSpec, Cap.SiteLocalTransform * Site.WorldTransform);
			if (!Test.TestNotNull(TEXT("V3 cap spawns through the production module path"),
				Module))
			{
				return false;
			}
			Module->SetContactDamageGraceSeconds(MaximumObservationSeconds + 1.0f);
			UStaticMeshComponent* Mesh = Module->GetMeshComponent();
			if (!Test.TestNotNull(TEXT("V3 cap owns a collision mesh"), Mesh))
			{
				return false;
			}
			FBodyInstance* Body = Mesh->GetBodyInstance();
			if (!Test.TestNotNull(TEXT("V3 cap owns a Chaos body"), Body))
			{
				return false;
			}
			UBodySetup* BodySetup = Body->GetBodySetup();
			if (!Test.TestNotNull(TEXT("V3 cap owns a BodySetup"), BodySetup))
			{
				return false;
			}
			const double MassKG = Body->GetBodyMass();
			TotalMassKG += MassKG;
			const double ExpectedMassKG = BodySetup->CalculateMass(Mesh);
			const double MassToleranceKG = FMath::Max(0.01, ExpectedMassKG * 0.001);
			CalculatedChaosMassKG += ExpectedMassKG;
			Test.TestTrue(*FString::Printf(
				TEXT("BodySetup mass matches Cap Index=%d Actual=%.6f Expected=%.6f Tolerance=%.6f"),
				CapMassIndex, MassKG, ExpectedMassKG, MassToleranceKG),
				FMath::IsNearlyEqual(MassKG, ExpectedMassKG, MassToleranceKG));
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][BodyMass]")
				TEXT(" Entry=%s Type=Cap Index=%d ActualKG=%.6f ExpectedKG=%.6f ToleranceKG=%.6f Accepted=%d"),
				*Entry.StableId.ToString(), CapMassIndex, MassKG,
				ExpectedMassKG, MassToleranceKG,
				FMath::IsNearlyEqual(MassKG, ExpectedMassKG, MassToleranceKG) ? 1 : 0);
			++CapMassIndex;
			MassMoment += Cap.SiteLocalTransform.GetLocation() * MassKG;
			Modules.Add(Module);
			InitialTransforms.Add(Module->GetActorTransform());
		}
		Test.TestEqual(TEXT("V3 visible module count matches production"),
			Modules.Num(), StaticEntry.Bricks.Num()
				+ StaticEntry.Devices.Num() + StaticEntry.Caps.Num());
		const FABTSM7PenetrationValidationStats Penetration =
			MaterialSystem->ValidateAndRepairPendingModules(Modules);
		Test.TestEqual(TEXT("Stage-5 entry starts without detected penetration"),
			Penetration.DetectedPairCount, 0);
		Test.TestEqual(TEXT("Stage-5 entry requires no penetration repair"),
			Penetration.RepairCount, 0);
		Test.TestEqual(TEXT("Stage-5 entry has no large penetration error"),
			Penetration.LargeErrorPairCount, 0);
		Test.TestEqual(TEXT("Stage-5 entry leaves no small penetration error"),
			Penetration.RemainingSmallPairCount, 0);
		if (Test.HasAnyErrors())
		{
			return false;
		}

		if (StaticEntry.PhysicsAssemblyHash != 0)
		{
			for (const FABTSM73BuildingFreezeV3PhysicsCluster& Cluster :
				StaticEntry.PhysicsClusters)
			{
				if (!Test.TestTrue(TEXT("Compound root Brick index is valid"),
					Modules.IsValidIndex(Cluster.RootBrickId)))
				{
					return false;
				}
				AABTSM7BuildingModule* Root = Modules[Cluster.RootBrickId];
				double ExpectedCompoundMassKG = 0.0;
				for (const int32 BrickId : Cluster.BrickIds)
				{
					if (!Test.TestTrue(TEXT("Compound member Brick index is valid"),
						Modules.IsValidIndex(BrickId)
							&& ExpectedBrickMassKG.IsValidIndex(BrickId)))
					{
						return false;
					}
					ExpectedCompoundMassKG += ExpectedBrickMassKG[BrickId];
					if (BrickId != Cluster.RootBrickId
						&& !Test.TestTrue(TEXT("Production module path welds the certified compound child"),
							Root->TryWeldStaticChild(*Modules[BrickId])))
					{
						return false;
					}
				}
				const double ActualCompoundMassKG =
					Root->GetMeshComponent()->GetMass();
				const double CompoundToleranceKG = FMath::Max(
					0.05, ExpectedCompoundMassKG * 0.001);
				Test.TestTrue(*FString::Printf(
					TEXT("Compound Body mass matches authoritative member mass Cluster=%d Root=%d Actual=%.6f Expected=%.6f Tolerance=%.6f"),
					Cluster.ClusterId, Cluster.RootBrickId,
					ActualCompoundMassKG, ExpectedCompoundMassKG,
					CompoundToleranceKG),
					FMath::IsNearlyEqual(ActualCompoundMassKG,
						ExpectedCompoundMassKG, CompoundToleranceKG));
				PhysicsModules.Add(Root);
			}
			for (int32 ModuleIndex = StaticEntry.Bricks.Num();
				ModuleIndex < Modules.Num(); ++ModuleIndex)
			{
				PhysicsModules.Add(Modules[ModuleIndex]);
			}
			Test.TestEqual(TEXT("Certified compound assembly produces the frozen Chaos body count"),
				PhysicsModules.Num(), StaticEntry.PhysicsBodyCount);
			UE_LOG(LogABTSRuntime, Display,
				TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][PhysicsAssembly]")
				TEXT(" Entry=%s Schema=%d VisibleModules=%d PhysicsBodies=%d Clusters=%d Hash=%llu MassPolicy=SumPerBodySetupCalculateMass Accepted=%d"),
				*Entry.StableId.ToString(),
				StaticEntry.PhysicsAssemblySchemaVersion, Modules.Num(),
				PhysicsModules.Num(), StaticEntry.PhysicsClusters.Num(),
				StaticEntry.PhysicsAssemblyHash,
				Test.HasAnyErrors() ? 0 : 1);
		}
		else
		{
			PhysicsModules = Modules;
		}
		if (Test.HasAnyErrors()) return false;

		const FVector CenterOfMass = TotalMassKG > UE_DOUBLE_SMALL_NUMBER
			? MassMoment / TotalMassKG
			: FVector::ZeroVector;
		const bool bCenterOfMassInsideGroundEnvelope = GroundSupportBounds.IsValid
			&& CenterOfMass.X >= GroundSupportBounds.Min.X
			&& CenterOfMass.X <= GroundSupportBounds.Max.X
			&& CenterOfMass.Y >= GroundSupportBounds.Min.Y
			&& CenterOfMass.Y <= GroundSupportBounds.Max.Y;
		Test.TestTrue(TEXT("Stage-5 aggregate center of mass projects into its ground-support envelope"),
			bCenterOfMassInsideGroundEnvelope);
		Test.TestTrue(TEXT("V3 certified Brick self-load matches spawned Chaos Brick mass"),
			FMath::IsNearlyEqual(
				Result.LoadDAG.Summary.TotalSelfLoadKG,
				BrickMassKG,
				FMath::Max(1.0, BrickMassKG * 0.001)));
		Test.TestTrue(TEXT("V3 spawned aggregate Chaos mass matches per-body UE BodySetup mass"),
			FMath::IsNearlyEqual(TotalMassKG, CalculatedChaosMassKG,
				FMath::Max(0.05, CalculatedChaosMassKG * 0.01)));

		const FABTSM7ChaosBodyProfile BodyProfile =
			FABTSM7ChaosBodyProfile::Production();
		const FABTSM7ChaosWorldProfile WorldProfile =
			FABTSM7ChaosWorldProfile::CaptureProduction();
		const uint32 BodyProfileHash = BodyProfile.ComputeCrc32();
		const uint32 WorldProfileHash = WorldProfile.ComputeCrc32();
		if (!Test.TestTrue(TEXT("Production module path accepts Site-uniform launch"),
			MaterialSystem->BeginSiteUniformLaunchPhysics(
				PhysicsModules,
				Site.WorldTransform.GetLocation(),
				SupportCenter,
				GravityCMPerSec2,
				MaximumObservationSeconds + 1.0f)))
		{
			return false;
		}
		Test.TestEqual(TEXT("Fixture and launch share the Chaos body identity"),
			MaterialSystem->GetLastLaunchChaosBodyProfileHash(), BodyProfileHash);
		Test.TestEqual(TEXT("Fixture and launch share the Chaos world identity"),
			MaterialSystem->GetLastLaunchChaosWorldProfileHash(), WorldProfileHash);
		Test.TestEqual(TEXT("Fixture and launch share the Site-uniform gravity identity"),
			MaterialSystem->GetLastSiteUniformGravityPolicyHash(),
			SiteGravity.ComputeCrc32());
		Test.TestTrue(TEXT("Fixture and launch share the exact SiteUp"),
			MaterialSystem->GetLastSiteUniformGravityUp().Equals(
				SiteGravity.SiteUp, 1.0e-6));
		const uint32 FixtureCrc32 = ComputeFixtureCrc32(
			Entry, Result, Descriptor, StaticEntry, Site, SupportCenter,
			BodyProfileHash, WorldProfileHash, SiteGravity);
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][Identity]")
			TEXT(" Complexity=%s Tier=%d Seed=%d EncounterSlot=%d Surface=%d")
			TEXT(" PositionAuthority=M3MapFreezeV3 GeometryAuthority=M7BuildingFreezeV3")
			TEXT(" ContractProductionHash=%llu DescriptorHash=%llu StaticHash=%llu")
			TEXT(" ProductionHash=%llu DeviceHash=%llu PlacementHash=%llu")
			TEXT(" GravityAuthority=%s GravityHash=%llu FixtureCrc32=%u")
			TEXT(" ContractVersion=%d LayoutHash=%llu SiteTransform=%s SupportCenter=%s SupportRadius=%.3f")
			TEXT(" Bricks=%d Devices=%d Caps=%d VisibleModules=%d PhysicsBodies=%d PhysicsAssembly=%llu Contacts=%d Ground=%d")
			TEXT(" ResultantAdvisories=%d StaticSelfLoadKG=%.3f ExternalLoadKG=%.3f")
			TEXT(" BrickMassKG=%.3f MassKG=%.3f CalculatedMassKG=%.3f")
			TEXT(" LocalCOM=%s GroundMin=%s GroundMax=%s COMSupported=%d")
			TEXT(" OuterFPS=%.0f OuterDT=%.6f GravityModel=SiteUniformTangentGravity Gravity=%.1f")
			TEXT(" SiteGravitySchema=%d SiteGravityHash=%u")
			TEXT(" SiteGravityDerivation=Normalize(SiteLocationWorldCM-SupportCenterWorldCM) SiteUp=%s")
			TEXT(" BodyHash=%u Solver=%d/%d Damping=%.2f/%.2f WorldHash=%u %s")
			TEXT(" SupportMaterial=ProductionTerrainDefault Observation=%.1f"),
			*Entry.StableId.ToString(), Entry.Settings.DifficultyTier,
			Entry.Settings.BuildingSeed,
			Site.EncounterIndex,
			static_cast<int32>(Site.V3Envelope.SurfaceKind),
			Site.V3Envelope.ProductionIdentityHash,
			StaticEntry.DescriptorHash,
			StaticEntry.StaticGeometryHash,
			StaticEntry.ProductionIdentityHash,
			StaticEntry.DeviceAssemblyHash,
			Site.V3Envelope.PlacementHash,
			*Site.V3Envelope.GravityAuthorityId.ToString(),
			Site.V3Envelope.GravityIdentityHash,
			FixtureCrc32,
			ProductionContract.JuryDemoFixedSix.ContractVersion,
			ProductionContract.JuryDemoFixedSix.LayoutHash,
			*Site.WorldTransform.ToHumanReadableString(),
			*SupportCenter.ToString(),
			Site.V3Envelope.SupportRadiusCM,
			StaticEntry.Bricks.Num(),
			StaticEntry.Devices.Num(),
			StaticEntry.Caps.Num(),
			Modules.Num(),
			PhysicsModules.Num(),
			StaticEntry.PhysicsAssemblyHash,
			Result.CompactAssembly.BearingContacts.Num(),
			Result.LoadDAG.Summary.GroundNodeCount,
			Result.Summary.SupportResultantAdvisoryCount,
			Result.LoadDAG.Summary.TotalSelfLoadKG,
			Descriptor.StaticExternalMassKG,
			BrickMassKG, TotalMassKG, CalculatedChaosMassKG,
			*CenterOfMass.ToString(),
			*GroundSupportBounds.Min.ToString(),
			*GroundSupportBounds.Max.ToString(),
			bCenterOfMassInsideGroundEnvelope ? 1 : 0,
			1.0f / ProductionOuterDeltaSeconds,
			ProductionOuterDeltaSeconds,
			GravityCMPerSec2,
			FABTSM7SiteUniformGravityPolicy::SchemaVersion,
			SiteGravity.ComputeCrc32(),
			*SiteGravity.SiteUp.ToString(),
			BodyProfileHash,
			BodyProfile.PositionSolverIterations,
			BodyProfile.VelocitySolverIterations,
			BodyProfile.LinearDamping,
			BodyProfile.AngularDamping,
			WorldProfileHash,
			*WorldProfile.ToLogString(),
			MaximumObservationSeconds);

		FObservationResult Observation;
		const bool bAccepted = ObserveUnderGravity(
			Test, WorldWrapper, Modules, InitialTransforms,
			SiteGravity.SiteUp, Observation);
		TSet<int32> DiagnosticBrickIndices;
		DiagnosticBrickIndices.Add(Observation.FinalMaximumPlanarDriftBrickIndex);
		DiagnosticBrickIndices.Add(Observation.FinalMaximumSettlementBrickIndex);
		DiagnosticBrickIndices.Add(Observation.FinalMaximumRotationBrickIndex);
		for (const int32 BrickIndex : DiagnosticBrickIndices)
		{
			if (!Result.Bricks.IsValidIndex(BrickIndex)
				|| !Result.LoadDAG.Nodes.IsValidIndex(BrickIndex)
				|| !Modules.IsValidIndex(BrickIndex))
			{
				continue;
			}
			const FABTSM73BeamD1BrickBinding& Brick = Result.Bricks[BrickIndex];
			const FABTSM73BeamCLoadNode& Node = Result.LoadDAG.Nodes[BrickIndex];
			int32 LowerContactCount = 0;
			int32 UpperContactCount = 0;
			for (const FABTSM73BeamCLoadEdge& Edge : Result.LoadDAG.Edges)
			{
				LowerContactCount += Edge.UpperMemberId == Brick.MemberId ? 1 : 0;
				UpperContactCount += Edge.LowerMemberId == Brick.MemberId ? 1 : 0;
			}
			const FVector Delta = Modules[BrickIndex]->GetActorLocation()
				- InitialTransforms[BrickIndex].GetLocation();
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][WorstBrick] Entry=%s Brick=%d Member=%d Axis=%d Role=%d Ground=%d Supports=%d LowerContacts=%d UpperContacts=%d SelfKG=%.3f AccumKG=%.3f Resultant=%s Valid=%d Center=%s Dimensions=%s Delta=%s Rotation=%.3f"),
				*Entry.StableId.ToString(), BrickIndex, Brick.MemberId,
				static_cast<int32>(Brick.Axis),
				static_cast<int32>(Brick.StructuralRole),
				Node.bGround ? 1 : 0, Node.SupportCount,
				LowerContactCount, UpperContactCount,
				Node.SelfLoadKG, Node.AccumulatedLoadKG,
				*Node.LoadResultant.ToString(),
				Node.bSupportResultantValid ? 1 : 0,
				*Brick.LocalTransform.GetLocation().ToString(),
				*Brick.BrickSpec.DimensionsCM.ToString(),
				*Delta.ToString(),
				FMath::RadiansToDegrees(
					InitialTransforms[BrickIndex].GetRotation().AngularDistance(
						Modules[BrickIndex]->GetActorQuat())));
			for (const FABTSM73BeamCLoadEdge& Edge : Result.LoadDAG.Edges)
			{
				if (Edge.UpperMemberId != Brick.MemberId
					&& Edge.LowerMemberId != Brick.MemberId)
				{
					continue;
				}
				const int32 OtherMemberId = Edge.UpperMemberId == Brick.MemberId
					? Edge.LowerMemberId : Edge.UpperMemberId;
				const int32 OtherBrickIndex = Result.Bricks.IndexOfByPredicate(
					[OtherMemberId](const FABTSM73BeamD1BrickBinding& Candidate)
					{
						return Candidate.MemberId == OtherMemberId;
					});
				const FVector OtherDelta = Modules.IsValidIndex(OtherBrickIndex)
					? Modules[OtherBrickIndex]->GetActorLocation()
						- InitialTransforms[OtherBrickIndex].GetLocation()
					: FVector::ZeroVector;
				const double OtherRotation = Modules.IsValidIndex(OtherBrickIndex)
					? FMath::RadiansToDegrees(
						InitialTransforms[OtherBrickIndex].GetRotation().AngularDistance(
							Modules[OtherBrickIndex]->GetActorQuat()))
					: 0.0;
				UE_LOG(LogABTSRuntime, Log,
					TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][WorstBrickContact] Entry=%s Brick=%d Direction=%s Upper=%d Lower=%d Position=%s Min=%s Max=%s Area=%.3f Share=%.6f ReactionKG=%.3f OtherDelta=%s OtherRotation=%.3f"),
					*Entry.StableId.ToString(), BrickIndex,
					Edge.UpperMemberId == Brick.MemberId
						? TEXT("Support") : TEXT("Load"),
					Edge.UpperMemberId, Edge.LowerMemberId,
					*Edge.ContactPosition.ToString(),
					*Edge.ContactMinXY.ToString(),
					*Edge.ContactMaxXY.ToString(),
					Edge.ContactAreaCM2, Edge.LoadShare,
					Edge.ReactionLoadKG, *OtherDelta.ToString(), OtherRotation);
			}
		}
		for (const int32 StartBrickIndex : DiagnosticBrickIndices)
		{
			if (!Result.Bricks.IsValidIndex(StartBrickIndex))
			{
				continue;
			}
			int32 CurrentMemberId = Result.Bricks[StartBrickIndex].MemberId;
			TSet<int32> VisitedMemberIds;
			for (int32 Depth = 0; Depth < 64; ++Depth)
			{
				if (VisitedMemberIds.Contains(CurrentMemberId))
				{
					break;
				}
				VisitedMemberIds.Add(CurrentMemberId);
				const int32 CurrentBrickIndex = Result.Bricks.IndexOfByPredicate(
					[CurrentMemberId](const FABTSM73BeamD1BrickBinding& Candidate)
					{
						return Candidate.MemberId == CurrentMemberId;
					});
				const FABTSM73BeamCLoadNode* CurrentNode =
					Result.LoadDAG.Nodes.FindByPredicate(
						[CurrentMemberId](const FABTSM73BeamCLoadNode& Candidate)
						{
							return Candidate.MemberId == CurrentMemberId;
						});
				if (!Result.Bricks.IsValidIndex(CurrentBrickIndex)
					|| !Modules.IsValidIndex(CurrentBrickIndex)
					|| CurrentNode == nullptr)
				{
					break;
				}
				const FVector CurrentDelta =
					Modules[CurrentBrickIndex]->GetActorLocation()
						- InitialTransforms[CurrentBrickIndex].GetLocation();
				UE_LOG(LogABTSRuntime, Log,
					TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][PrimarySupportPath] Entry=%s Start=%d Depth=%d Brick=%d Member=%d Ground=%d Supports=%d LoadKG=%.3f Delta=%s Rotation=%.3f"),
					*Entry.StableId.ToString(), StartBrickIndex, Depth,
					CurrentBrickIndex, CurrentMemberId,
					CurrentNode->bGround ? 1 : 0, CurrentNode->SupportCount,
					CurrentNode->AccumulatedLoadKG, *CurrentDelta.ToString(),
					FMath::RadiansToDegrees(
						InitialTransforms[CurrentBrickIndex].GetRotation().AngularDistance(
							Modules[CurrentBrickIndex]->GetActorQuat())));
				if (CurrentNode->bGround)
				{
					break;
				}
				const FABTSM73BeamCLoadEdge* PrimarySupport = nullptr;
				for (const FABTSM73BeamCLoadEdge& Edge : Result.LoadDAG.Edges)
				{
					if (Edge.UpperMemberId == CurrentMemberId
						&& (PrimarySupport == nullptr
							|| Edge.ReactionLoadKG > PrimarySupport->ReactionLoadKG))
					{
						PrimarySupport = &Edge;
					}
				}
				if (PrimarySupport == nullptr)
				{
					break;
				}
				CurrentMemberId = PrimarySupport->LowerMemberId;
			}
		}
		const uint32 ResultCrc32 = ComputeResultCrc32(
			FixtureCrc32, bAccepted, Observation);
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][Result] Entry=%s CandidateHash=%u Accepted=%d ReachedQuiet=%d EndedQuiet=%d FirstQuiet=%.2f FinalDrift=%.3f@%d FinalSettlement=%.3f@%d FinalRotation=%.3f@%d FinalLinear=%.3f FinalAngular=%.3f FinalAwake=%d PeakDrift=%.3f PeakSettlement=%.3f PeakRotation=%.3f Seconds=%.3f"),
			*Entry.StableId.ToString(), FixtureCrc32,
			bAccepted ? 1 : 0,
			Observation.bReachedQuietWindow ? 1 : 0,
			Observation.bEndedInQuietWindow ? 1 : 0,
			Observation.FirstQuietWindowSeconds,
			Observation.FinalMaximumPlanarDriftCM,
			Observation.FinalMaximumPlanarDriftBrickIndex,
			Observation.FinalMaximumSettlementCM,
			Observation.FinalMaximumSettlementBrickIndex,
			Observation.FinalMaximumRotationDegrees,
			Observation.FinalMaximumRotationBrickIndex,
			Observation.FinalMaximumLinearSpeedCMPerSec,
			Observation.FinalMaximumAngularSpeedDegreesPerSec,
			Observation.FinalAwakeBodyCount,
			Observation.PeakPlanarDriftCM,
			Observation.PeakSettlementCM,
			Observation.PeakRotationDegrees,
			FPlatformTime::Seconds() - StartSeconds);
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][ResultHash]")
			TEXT(" Entry=%s CandidateHash=%u ResultHash=%u Accepted=%d"),
			*Entry.StableId.ToString(), FixtureCrc32, ResultCrc32,
			bAccepted ? 1 : 0);
		WorldWrapper.ForwardErrorMessages(&Test);
		return bAccepted && !Test.HasAnyErrors();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamStage5ContractManifestLookupTest,
	"ABTS.M73DAG.BeamC3V3.ContractManifestLookup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamStage5ContractManifestLookupTest::RunTest(
	const FString& Parameters)
{
	static const TArray<FName> ExpectedContractNames = {
		TEXT("E1ColumnBreak"),
		TEXT("E2DropTrigger"),
		TEXT("E3SlideRelease"),
		TEXT("E4TipOver"),
		TEXT("E5SeamRelease"),
		TEXT("E6TipOver")};
	const TArray<FABTSM73BeamDemoManifestEntry>& Entries =
		FABTSM73BeamDemoManifest::GetEntries();
	TestEqual(TEXT("Every frozen demo entry has one production contract name"),
		Entries.Num(), ExpectedContractNames.Num());
	for (int32 Index = 0;
		Index < FMath::Min(Entries.Num(), ExpectedContractNames.Num()); ++Index)
	{
		FName ActualContractName;
		TestTrue(FString::Printf(TEXT("Entry %d maps to a contract name"), Index),
			ABTSM73BeamStage5ChaosTests::
				TryResolveProductionContractManifestEntryName(
					Entries[Index], ActualContractName));
		TestEqual(FString::Printf(TEXT("Entry %d maps to its V3 contract id"), Index),
			ActualContractName, ExpectedContractNames[Index]);
	}
	FABTSM73BeamDemoManifestEntry InvalidEntry;
	InvalidEntry.StableId = TEXT("E1ColumnBreak");
	FName InvalidContractName;
	TestFalse(TEXT("A non-demo id cannot silently enter the production lookup"),
		ABTSM73BeamStage5ChaosTests::
			TryResolveProductionContractManifestEntryName(
				InvalidEntry, InvalidContractName));
	TestTrue(TEXT("A rejected id leaves no contract name"),
		InvalidContractName.IsNone());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamStage5ChaosE1Test,
	"ABTS.M73DAG.BeamC3V3.Demo.ChaosStability.E1",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamStage5ChaosE1Test::RunTest(const FString& Parameters)
{
	return ABTSM73BeamStage5ChaosTests::RunEntryChaosStability(
		*this, FABTSM73BeamDemoManifest::GetEntries()[0]);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamStage5ChaosE2Test,
	"ABTS.M73DAG.BeamC3V3.Demo.ChaosStability.E2",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamStage5ChaosE2Test::RunTest(const FString& Parameters)
{
	return ABTSM73BeamStage5ChaosTests::RunEntryChaosStability(
		*this, FABTSM73BeamDemoManifest::GetEntries()[1]);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamStage5ChaosE3Test,
	"ABTS.M73DAG.BeamC3V3.Demo.ChaosStability.E3",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamStage5ChaosE3Test::RunTest(const FString& Parameters)
{
	return ABTSM73BeamStage5ChaosTests::RunEntryChaosStability(
		*this, FABTSM73BeamDemoManifest::GetEntries()[2]);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamStage5ChaosE4Test,
	"ABTS.M73DAG.BeamC3V3.Demo.ChaosStability.E4",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamStage5ChaosE4Test::RunTest(const FString& Parameters)
{
	return ABTSM73BeamStage5ChaosTests::RunEntryChaosStability(
		*this, FABTSM73BeamDemoManifest::GetEntries()[3]);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamStage5ChaosE5Test,
	"ABTS.M73DAG.BeamC3V3.Demo.ChaosStability.E5",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamStage5ChaosE5Test::RunTest(const FString& Parameters)
{
	return ABTSM73BeamStage5ChaosTests::RunEntryChaosStability(
		*this, FABTSM73BeamDemoManifest::GetEntries()[4]);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73BeamStage5ChaosE6Test,
	"ABTS.M73DAG.BeamC3V3.Demo.ChaosStability.E6",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM73BeamStage5ChaosE6Test::RunTest(const FString& Parameters)
{
	return ABTSM73BeamStage5ChaosTests::RunEntryChaosStability(
		*this, FABTSM73BeamDemoManifest::GetEntries()[5]);
}

#endif
