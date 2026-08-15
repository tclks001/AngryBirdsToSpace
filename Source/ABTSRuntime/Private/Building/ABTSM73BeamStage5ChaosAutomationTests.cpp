// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "ABTSM73BeamD1BrickCompiler.h"
#include "ABTSM7PenetrationValidator.h"
#include "ABTSRuntime.h"
#include "Building/ABTSM73BeamDemoManifest.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Building/ABTSM7MaterialProfileLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Crc.h"
#include "HAL/IConsoleManager.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Tests/AutomationCommon.h"
#include "TestStage/ABTSM71TestStageActors.h"

namespace ABTSM73BeamStage5ChaosTests
{
	constexpr float FixedDeltaSeconds = 1.0f / 30.0f;
	constexpr float MinimumObservationSeconds = 1.25f;
	constexpr float StableHoldSeconds = 0.45f;
	constexpr float MaximumObservationSeconds = 6.0f;
	constexpr float MaximumLinearSpeedCMPerSec = 4.0f;
	constexpr float MaximumAngularSpeedDegreesPerSec = 1.5f;
	constexpr float MaximumPlanarDriftCM = 4.0f;
	constexpr float MaximumSettlementCM = 6.0f;
	constexpr float MaximumRotationDegrees = 2.0f;
	constexpr int32 PositionSolverIterations = 80;
	constexpr int32 VelocitySolverIterations = 20;
	constexpr float GravityCMPerSec2 = 980.0f;
	constexpr float GroundContactToleranceCM = 0.1f;
	constexpr float LinearDamping = 2.0f;
	constexpr float AngularDamping = 4.0f;
	constexpr bool bUseNativeWorldGravity = true;
	constexpr float FrictionMultiplier = 1.0f;
	constexpr int32 PositionFrictionIterations = -1;
	constexpr int32 PositionShockPropagationIterations = -1;
	constexpr float MaximumSubstepDeltaSeconds = 1.0f / 120.0f;
	constexpr int32 MaximumSubsteps = 4;

	class FScopedPhysicsSubstepOverride final
	{
	public:
		FScopedPhysicsSubstepOverride()
		{
			Settings = UPhysicsSettings::Get();
			if (Settings != nullptr)
			{
				bSavedSubstepping = Settings->bSubstepping;
				SavedMaximumSubstepDeltaSeconds = Settings->MaxSubstepDeltaTime;
				SavedMaximumSubsteps = Settings->MaxSubsteps;
				Settings->bSubstepping = true;
				Settings->MaxSubstepDeltaTime = MaximumSubstepDeltaSeconds;
				Settings->MaxSubsteps = MaximumSubsteps;
			}
		}

		~FScopedPhysicsSubstepOverride()
		{
			if (Settings != nullptr)
			{
				Settings->bSubstepping = bSavedSubstepping;
				Settings->MaxSubstepDeltaTime = SavedMaximumSubstepDeltaSeconds;
				Settings->MaxSubsteps = SavedMaximumSubsteps;
			}
		}

	private:
		UPhysicsSettings* Settings = nullptr;
		bool bSavedSubstepping = false;
		float SavedMaximumSubstepDeltaSeconds = 0.0f;
		int32 SavedMaximumSubsteps = 1;
	};

	class FScopedChaosStackSolverOverride final
	{
	public:
		FScopedChaosStackSolverOverride()
		{
			PositionFrictionVariable = IConsoleManager::Get().FindConsoleVariable(
				TEXT("p.Chaos.Solver.Collision.PositionFrictionIterations"));
			PositionShockVariable = IConsoleManager::Get().FindConsoleVariable(
				TEXT("p.Chaos.Solver.Collision.PositionShockPropagationIterations"));
			if (PositionFrictionVariable != nullptr)
			{
				SavedPositionFriction = PositionFrictionVariable->GetInt();
				PositionFrictionVariable->Set(
					PositionFrictionIterations, ECVF_SetByCode);
			}
			if (PositionShockVariable != nullptr)
			{
				SavedPositionShock = PositionShockVariable->GetInt();
				PositionShockVariable->Set(
					PositionShockPropagationIterations, ECVF_SetByCode);
			}
		}

		~FScopedChaosStackSolverOverride()
		{
			if (PositionFrictionVariable != nullptr)
			{
				PositionFrictionVariable->Set(
					SavedPositionFriction, ECVF_SetByCode);
			}
			if (PositionShockVariable != nullptr)
			{
				PositionShockVariable->Set(
					SavedPositionShock, ECVF_SetByCode);
			}
		}

	private:
		IConsoleVariable* PositionFrictionVariable = nullptr;
		IConsoleVariable* PositionShockVariable = nullptr;
		int32 SavedPositionFriction = -1;
		int32 SavedPositionShock = -1;
	};
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
		const FABTSM73BeamD1Stage5Result& Result)
	{
		const FString Canonical = FString::Printf(
			TEXT("BeamStage5Chaos:v1:Entry=%s:Tier=%d:Seed=%d:Production=%llu")
			TEXT(":Bricks=%d:Contacts=%d:Ground=%d:ResultantAdvisories=%d")
			TEXT(":DT=%d:Min=%d:Hold=%d:Max=%d:Lin=%d:Ang=%d")
			TEXT(":Drift=%d:Settle=%d:Rot=%d:Solver=%d,%d:PositionFriction=%d:PositionShock=%d:Substep=%d,%d:Gravity=%d:Damping=%d,%d:NativeGravity=%d:FrictionMultiplier=%d"),
			*Entry.StableId.ToString(),
			Entry.Settings.DifficultyTier,
			Entry.Settings.BuildingSeed,
			Result.ProductionIdentityHash,
			Result.Bricks.Num(),
			Result.CompactAssembly.BearingContacts.Num(),
			Result.LoadDAG.Summary.GroundNodeCount,
			Result.Summary.SupportResultantAdvisoryCount,
			FMath::RoundToInt(FixedDeltaSeconds * 1000.0f),
			FMath::RoundToInt(MinimumObservationSeconds * 1000.0f),
			FMath::RoundToInt(StableHoldSeconds * 1000.0f),
			FMath::RoundToInt(MaximumObservationSeconds * 1000.0f),
			FMath::RoundToInt(MaximumLinearSpeedCMPerSec * 1000.0f),
			FMath::RoundToInt(MaximumAngularSpeedDegreesPerSec * 1000.0f),
			FMath::RoundToInt(MaximumPlanarDriftCM * 1000.0f),
			FMath::RoundToInt(MaximumSettlementCM * 1000.0f),
			FMath::RoundToInt(MaximumRotationDegrees * 1000.0f),
			PositionSolverIterations,
			VelocitySolverIterations,
			PositionFrictionIterations,
			PositionShockPropagationIterations,
			FMath::RoundToInt(MaximumSubstepDeltaSeconds * 1000.0f),
			MaximumSubsteps,
			FMath::RoundToInt(GravityCMPerSec2 * 1000.0f),
			FMath::RoundToInt(LinearDamping * 1000.0f),
			FMath::RoundToInt(AngularDamping * 1000.0f),
			bUseNativeWorldGravity ? 1 : 0,
			FMath::RoundToInt(FrictionMultiplier * 1000.0f));
		return FCrc::StrCrc32(*Canonical);
	}

	bool ObserveUnderGravity(
		FAutomationTestBase& Test,
		FStage5PhysicsWorld& WorldWrapper,
		const TArray<AABTSM7BuildingModule*>& Modules,
		const TArray<FTransform>& Baselines,
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
			MaximumObservationSeconds / FixedDeltaSeconds);
		for (int32 TickIndex = 0; TickIndex < MaximumTicks; ++TickIndex)
		{
			if (!WorldWrapper.TickTestWorld(FixedDeltaSeconds))
			{
				WorldWrapper.ForwardErrorMessages(&Test);
				return false;
			}

			ElapsedSeconds += FixedDeltaSeconds;
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
				const float PlanarDriftCM = FVector(Delta.X, Delta.Y, 0.0f).Size();
				const float SettlementCM = FMath::Abs(Delta.Z);
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
				QuietSeconds += FixedDeltaSeconds;
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

		FScopedChaosStackSolverOverride StackSolverOverride;
		FScopedPhysicsSubstepOverride PhysicsSubstepOverride;
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

		AABTSM71PhysicsTestStage* Stage =
			World->SpawnActorDeferred<AABTSM71PhysicsTestStage>(
				AABTSM71PhysicsTestStage::StaticClass(),
				FTransform::Identity,
				nullptr,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Stage != nullptr)
		{
			UGameplayStatics::FinishSpawningActor(Stage, FTransform::Identity);
		}
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AABTSM7BuildingMaterialSystem* MaterialSystem =
			World->SpawnActor<AABTSM7BuildingMaterialSystem>(
				AABTSM7BuildingMaterialSystem::StaticClass(),
				FTransform::Identity,
				SpawnParameters);
		if (!Test.TestNotNull(TEXT("Stage-5 planar floor"), Stage)
			|| !Test.TestNotNull(TEXT("Stage-5 material system"), MaterialSystem))
		{
			return false;
		}
		if (!WorldWrapper.BeginPlayInTestWorld())
		{
			WorldWrapper.ForwardErrorMessages(&Test);
			return false;
		}

		TArray<FABTSM7MaterialProfile> Profiles;
		MaterialSystem->CopyMaterialProfiles(Profiles);
		const FABTSM7MaterialProfile* FloorProfile =
			FABTSM7MaterialProfileLibrary::FindProfile(
				Profiles, EABTSM7BuildingMaterial::Wood);
		if (!Test.TestNotNull(TEXT("Stage-5 floor material profile"), FloorProfile))
		{
			return false;
		}
		UPhysicalMaterial* FloorPhysicalMaterial = NewObject<UPhysicalMaterial>(
			Stage, TEXT("BeamStage5WoodFloorPhysicalMaterial"), RF_Transient);
		FloorPhysicalMaterial->Friction =
			FloorProfile->DynamicFriction * FrictionMultiplier;
		FloorPhysicalMaterial->StaticFriction =
			FloorProfile->StaticFriction * FrictionMultiplier;
		FloorPhysicalMaterial->Restitution = FloorProfile->Restitution;
		FloorPhysicalMaterial->Density = FloorProfile->DensityGPerCubicCM;
		FloorPhysicalMaterial->bOverrideFrictionCombineMode = true;
		FloorPhysicalMaterial->FrictionCombineMode =
			EFrictionCombineMode::Average;
		FloorPhysicalMaterial->bOverrideRestitutionCombineMode = true;
		FloorPhysicalMaterial->RestitutionCombineMode =
			EFrictionCombineMode::Average;
		Stage->GetFloorComponent()->SetPhysMaterialOverride(FloorPhysicalMaterial);

		TArray<AABTSM7BuildingModule*> Modules;
		TArray<FTransform> InitialTransforms;
		Modules.Reserve(Result.Bricks.Num());
		InitialTransforms.Reserve(Result.Bricks.Num());
		FBox GroundSupportBounds(EForceInit::ForceInit);
		double TotalMassKG = 0.0;
		FVector MassMoment = FVector::ZeroVector;
		for (const FABTSM73BeamD1BrickBinding& Brick : Result.Bricks)
		{
			AABTSM7BuildingModule* Module = MaterialSystem->SpawnBrickModule(
				Brick.BrickSpec, Brick.LocalTransform);
			if (!Test.TestNotNull(TEXT("Stage-5 Brick spawns as an independent body"),
				Module))
			{
				return false;
			}
			Module->ConfigureChaosSolverIterations(
				PositionSolverIterations, VelocitySolverIterations);
			Module->SetContactDamageGraceSeconds(MaximumObservationSeconds + 1.0f);
			UStaticMeshComponent* Mesh = Module->GetMeshComponent();
			if (!Test.TestNotNull(TEXT("Stage-5 Brick owns a collision mesh"), Mesh))
			{
				return false;
			}
			Mesh->SetLinearDamping(LinearDamping);
			Mesh->SetAngularDamping(AngularDamping);
			FBodyInstance* Body = Mesh->GetBodyInstance();
			if (!Test.TestNotNull(TEXT("Stage-5 Brick owns a Chaos body"), Body))
			{
				return false;
			}
			if (UPhysicalMaterial* BrickPhysicalMaterial =
				Body->GetSimplePhysicalMaterial())
			{
				BrickPhysicalMaterial->Friction *= FrictionMultiplier;
				BrickPhysicalMaterial->StaticFriction *= FrictionMultiplier;
			}
			const double MassKG = Body->GetBodyMass();
			TotalMassKG += MassKG;
			MassMoment += Module->GetActorLocation() * MassKG;
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

		for (AABTSM7BuildingModule* Module : Modules)
		{
			Module->ActivateDynamicPlanar(
				FVector::ZeroVector, FVector::UpVector, GravityCMPerSec2);
			if (bUseNativeWorldGravity)
			{
				Module->GetMeshComponent()->SetEnableGravity(true);
				Module->SetActorTickEnabled(false);
			}
		}
		const uint32 FixtureCrc32 = ComputeFixtureCrc32(Entry, Result);
		UE_LOG(LogABTSRuntime, Log,
			TEXT("[ABTS][M7.3-BeamC3V3][ChaosStability][Identity] Entry=%s Tier=%d Seed=%d ProductionHash=%llu FixtureCrc32=%u Bricks=%d Contacts=%d Ground=%d ResultantAdvisories=%d StaticSelfLoadKG=%.3f MassKG=%.3f COM=%s GroundMin=%s GroundMax=%s COMSupported=%d FPS=%.0f Solver=%d/%d PositionFriction=%d PositionShock=%d Substep=%.4f/%d Damping=%.2f/%.2f NativeGravity=%d FrictionMultiplier=%.2f Observation=%.1f"),
			*Entry.StableId.ToString(), Entry.Settings.DifficultyTier,
			Entry.Settings.BuildingSeed, Result.ProductionIdentityHash,
			FixtureCrc32, Result.Bricks.Num(),
			Result.CompactAssembly.BearingContacts.Num(),
			Result.LoadDAG.Summary.GroundNodeCount,
			Result.Summary.SupportResultantAdvisoryCount,
			Result.LoadDAG.Summary.TotalSelfLoadKG,
			TotalMassKG, *CenterOfMass.ToString(),
			*GroundSupportBounds.Min.ToString(),
			*GroundSupportBounds.Max.ToString(),
			bCenterOfMassInsideGroundEnvelope ? 1 : 0,
			1.0f / FixedDeltaSeconds,
			PositionSolverIterations, VelocitySolverIterations,
			PositionFrictionIterations,
			PositionShockPropagationIterations,
			MaximumSubstepDeltaSeconds, MaximumSubsteps,
			LinearDamping, AngularDamping,
			bUseNativeWorldGravity ? 1 : 0,
			FrictionMultiplier,
			MaximumObservationSeconds);

		FObservationResult Observation;
		const bool bAccepted = ObserveUnderGravity(
			Test, WorldWrapper, Modules, InitialTransforms, Observation);
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
