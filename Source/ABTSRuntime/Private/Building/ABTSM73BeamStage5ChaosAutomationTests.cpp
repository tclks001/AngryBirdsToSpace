// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "ABTSM73BeamD1BrickCompiler.h"
#include "ABTSM7PenetrationValidator.h"
#include "ABTSRuntime.h"
#include "Building/ABTSM73BeamDemoManifest.h"
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
		const FABTSJuryDemoFixedSixBuildingSite& Site,
		const FVector& PlanetCenter,
		const uint32 BodyProfileHash,
		const uint32 WorldProfileHash)
	{
		const FVector Location = Site.WorldTransform.GetLocation();
		const FQuat Rotation = Site.WorldTransform.GetRotation();
		const FString Canonical = FString::Printf(
			TEXT("BeamStage5ChaosProductionIdentity:v2:Entry=%s:Tier=%d:Seed=%d:Production=%llu")
			TEXT(":ContractEnvelopeProduction=%llu:Contract=%d:Layout=%llu:Site=%d:Location=%d,%d,%d")
			TEXT(":Rotation=%d,%d,%d,%d:PlanetCenter=%d,%d,%d")
			TEXT(":Bricks=%d:Contacts=%d:Ground=%d:ResultantAdvisories=%d")
			TEXT(":OuterDT=%d:Min=%d:Hold=%d:Max=%d:Lin=%d:Ang=%d")
			TEXT(":Drift=%d:Settle=%d:Rot=%d:BodyHash=%u:WorldHash=%u")
			TEXT(":GravityModel=RadialConstantAcceleration:Gravity=%d:SupportMaterial=ProductionTerrainDefault"),
			*Entry.StableId.ToString(),
			Entry.Settings.DifficultyTier,
			Entry.Settings.BuildingSeed,
			Result.ProductionIdentityHash,
			Site.V2Envelope.ProductionIdentityHash,
			FABTSJuryDemoFixedSixContract::SupportedV2ContractVersion,
			FABTSJuryDemoFixedSixContract::FrozenV2LayoutHash,
			Site.EncounterIndex,
			FMath::RoundToInt(Location.X * 1000.0),
			FMath::RoundToInt(Location.Y * 1000.0),
			FMath::RoundToInt(Location.Z * 1000.0),
			FMath::RoundToInt(Rotation.X * 1000000.0),
			FMath::RoundToInt(Rotation.Y * 1000000.0),
			FMath::RoundToInt(Rotation.Z * 1000000.0),
			FMath::RoundToInt(Rotation.W * 1000000.0),
			FMath::RoundToInt(PlanetCenter.X * 1000.0),
			FMath::RoundToInt(PlanetCenter.Y * 1000.0),
			FMath::RoundToInt(PlanetCenter.Z * 1000.0),
			Result.Bricks.Num(),
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
			FMath::RoundToInt(GravityCMPerSec2 * 1000.0f));
		return FCrc::StrCrc32(*Canonical);
	}

	bool ObserveUnderGravity(
		FAutomationTestBase& Test,
		FStage5PhysicsWorld& WorldWrapper,
		const TArray<AABTSM7BuildingModule*>& Modules,
		const TArray<FTransform>& Baselines,
		const FVector& PlanetCenter,
		FObservationResult& OutResult)
	{
		if (Modules.IsEmpty() || Baselines.Num() != Modules.Num())
		{
			Test.AddError(TEXT("Stage-5 entry has invalid Chaos observation inputs"));
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
				FVector LocalUp = (Baselines[ModuleIndex].GetLocation()
					- PlanetCenter).GetSafeNormal();
				if (LocalUp.IsNearlyZero())
				{
					LocalUp = Baselines[ModuleIndex].GetUnitAxis(EAxis::Z);
				}
				const float PlanarDriftCM =
					FVector::VectorPlaneProject(Delta, LocalUp).Size();
				const float SettlementCM =
					FMath::Abs(FVector::DotProduct(Delta, LocalUp));
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
					Body != nullptr && Body->IsInstanceAwake() ? 1 : 0;
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
		FABTSM73BeamD1Stage5Result Result;
		FString Error;
		if (!Test.TestTrue(TEXT("Frozen Stage-5 production entry generates"),
			FABTSM73BeamD1BrickCompiler().GenerateStage5(
				Entry.Settings, Result, Error)))
		{
			Test.AddError(Error);
			return false;
		}
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
		JuryPlanet->SurfaceSubdivision = 1;
		JuryPlanet->InstancesPerCell = 0;
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
		const int32 SiteIndex = static_cast<int32>(Entry.Id) - 1;
		if (!Test.TestTrue(TEXT("Frozen contract contains the requested site"),
			ProductionContract.JuryDemoFixedSix.Sites.IsValidIndex(SiteIndex)))
		{
			return false;
		}
		const FABTSJuryDemoFixedSixBuildingSite Site =
			ProductionContract.JuryDemoFixedSix.Sites[SiteIndex];
		const FVector PlanetCenter = JuryPlanet->GetPlanetCenterWorld();
		const bool bContractProductionEnvelopeMatches =
			Site.V2Envelope.ProductionIdentityHash
				== static_cast<uint64>(Result.ProductionIdentityHash);
		Test.AddInfo(FString::Printf(
			TEXT("PositionAuthority=M3FrozenV2 GeometryAuthority=M7CurrentProduction ContractProductionEnvelopeMatches=%d ContractProductionHash=%llu CurrentProductionHash=%llu"),
			bContractProductionEnvelopeMatches ? 1 : 0,
			Site.V2Envelope.ProductionIdentityHash,
			Result.ProductionIdentityHash));
		Test.TestEqual(TEXT("Frozen site keeps the manifest seed"),
			Site.DeterministicSeed, Entry.Settings.BuildingSeed);
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

		TArray<AABTSM7BuildingModule*> Modules;
		TArray<FTransform> InitialTransforms;
		Modules.Reserve(Result.Bricks.Num());
		InitialTransforms.Reserve(Result.Bricks.Num());
		FBox GroundSupportBounds(EForceInit::ForceInit);
		double TotalMassKG = 0.0;
		FVector MassMoment = FVector::ZeroVector;
		for (const FABTSM73BeamD1BrickBinding& Brick : Result.Bricks)
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
		Test.TestEqual(TEXT("Stage-5 dynamic body count matches production"),
			Modules.Num(), Result.Bricks.Num());
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
		Test.TestTrue(TEXT("Stage-5 static self-load matches spawned Chaos mass"),
			FMath::IsNearlyEqual(
				Result.LoadDAG.Summary.TotalSelfLoadKG,
				TotalMassKG,
				FMath::Max(1.0, TotalMassKG * 0.001)));

		const FABTSM7ChaosBodyProfile BodyProfile =
			FABTSM7ChaosBodyProfile::Production();
		const FABTSM7ChaosWorldProfile WorldProfile =
			FABTSM7ChaosWorldProfile::CaptureProduction();
		const uint32 BodyProfileHash = BodyProfile.ComputeCrc32();
		const uint32 WorldProfileHash = WorldProfile.ComputeCrc32();
		MaterialSystem->BeginLaunchPhysics(
			false, PlanetCenter, GravityCMPerSec2,
			MaximumObservationSeconds + 1.0f);
		Test.TestEqual(TEXT("Fixture and launch share the Chaos body identity"),
			MaterialSystem->GetLastLaunchChaosBodyProfileHash(), BodyProfileHash);
		Test.TestEqual(TEXT("Fixture and launch share the Chaos world identity"),
			MaterialSystem->GetLastLaunchChaosWorldProfileHash(), WorldProfileHash);
		const uint32 FixtureCrc32 = ComputeFixtureCrc32(
			Entry, Result, Site, PlanetCenter, BodyProfileHash, WorldProfileHash);
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][Identity] Entry=%s Tier=%d Seed=%d PositionAuthority=M3FrozenV2 GeometryAuthority=M7CurrentProduction ContractProductionHash=%llu ProductionHash=%llu ContractProductionEnvelopeMatches=%d FixtureCrc32=%u ContractVersion=%d LayoutHash=%llu Site=%d SiteTransform=%s PlanetCenter=%s Bricks=%d Contacts=%d Ground=%d ResultantAdvisories=%d StaticSelfLoadKG=%.3f MassKG=%.3f LocalCOM=%s GroundMin=%s GroundMax=%s COMSupported=%d OuterFPS=%.0f OuterDT=%.6f GravityModel=RadialConstantAcceleration Gravity=%.1f BodyHash=%u Solver=%d/%d Damping=%.2f/%.2f WorldHash=%u %s SupportMaterial=ProductionTerrainDefault Observation=%.1f"),
			*Entry.StableId.ToString(), Entry.Settings.DifficultyTier,
			Entry.Settings.BuildingSeed,
			Site.V2Envelope.ProductionIdentityHash,
			Result.ProductionIdentityHash,
			bContractProductionEnvelopeMatches ? 1 : 0,
			FixtureCrc32,
			ProductionContract.JuryDemoFixedSix.ContractVersion,
			ProductionContract.JuryDemoFixedSix.LayoutHash,
			Site.EncounterIndex,
			*Site.WorldTransform.ToHumanReadableString(),
			*PlanetCenter.ToString(),
			Result.Bricks.Num(),
			Result.CompactAssembly.BearingContacts.Num(),
			Result.LoadDAG.Summary.GroundNodeCount,
			Result.Summary.SupportResultantAdvisoryCount,
			Result.LoadDAG.Summary.TotalSelfLoadKG,
			TotalMassKG, *CenterOfMass.ToString(),
			*GroundSupportBounds.Min.ToString(),
			*GroundSupportBounds.Max.ToString(),
			bCenterOfMassInsideGroundEnvelope ? 1 : 0,
			1.0f / ProductionOuterDeltaSeconds,
			ProductionOuterDeltaSeconds,
			GravityCMPerSec2,
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
			PlanetCenter, Observation);
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
		WorldWrapper.ForwardErrorMessages(&Test);
		return bAccepted && !Test.HasAnyErrors();
	}
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
