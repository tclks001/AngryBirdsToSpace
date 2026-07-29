// Copyright Epic Games, Inc. All Rights Reserved.

#include "ABTSRuntime.h"
#include "Building/ABTSM73StableBuildingActor.h"
#include "Building/ABTSM7BuildingMaterialSystem.h"
#include "Building/ABTSM7BuildingModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/ABTSM7GameMode.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "TestStage/ABTSM71TestStageActors.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr int32 DAG4WorkshopSeed = 1034266606;

	class FABTSM73DAG4RuntimeTestWorld final : public FTestWorldWrapper
	{
	public:
		bool CreatePhysicsWorld()
		{
			if (TestWorld != nullptr)
			{
				ReportFailure(TEXT("Physics test world already exists"));
				return false;
			}
			if (GEngine == nullptr)
			{
				ReportFailure(TEXT("GEngine is unavailable"));
				return false;
			}

			UGameInstance* GameInstance =
				NewObject<UGameInstance>(GEngine);
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
				TEXT("ABTSM73DAG4RuntimePatternMatrixWorld"),
				nullptr,
				true,
				ERHIFeatureLevel::Num,
				&InitializationValues);
			if (TestWorld == nullptr)
			{
				ReportFailure(
					TEXT("Failed to create a physics-enabled world"));
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

	struct FDAG4PatternExpectation
	{
		EABTSM73DAGFailurePattern Pattern =
			EABTSM73DAGFailurePattern::Auto;
		EABTSM73DAGFailureMotion Motion =
			EABTSM73DAGFailureMotion::None;
	};

	struct FDAG4FormalModuleSnapshot
	{
		TWeakObjectPtr<AABTSM7BuildingModule> Module;
		FTransform Transform = FTransform::Identity;
		float Damage = 0.0f;
	};

	struct FDAG4FixtureState
	{
		AABTSM73StableBuildingActor* Building = nullptr;
		FDAG4PatternExpectation Expected;
		FABTSM7TaskGraphBuildingProfile ResolvedProfile;
		TArray<int32> FormalNodeIds;
		TMap<int32, FDAG4FormalModuleSnapshot> FormalSnapshots;
		bool bDAG4StartSnapshotCaptured = false;
		bool bFailureReported = false;
	};

	const TCHAR* GetPatternName(
		const EABTSM73DAGFailurePattern Pattern)
	{
		switch (Pattern)
		{
		case EABTSM73DAGFailurePattern::InternalSingleSupport:
			return TEXT("Single");
		case EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport:
			return TEXT("Dual");
		case EABTSM73DAGFailurePattern::InternalOffsetSeam:
			return TEXT("Seam");
		default:
			return TEXT("Unknown");
		}
	}

	bool IsNearlyEqual(
		const float A,
		const float B)
	{
		return FMath::IsNearlyEqual(A, B, KINDA_SMALL_NUMBER);
	}

	bool UsesUnmodifiedFormalDAG4Defaults(
		const FABTSM73DAG4ValidationSettings& Settings)
	{
		const FABTSM73DAG4ValidationSettings Defaults;
		return Settings.bEnableSettledChaosValidation
			&& IsNearlyEqual(
				Settings.ContactGapToleranceCM,
				Defaults.ContactGapToleranceCM)
			&& IsNearlyEqual(
				Settings.ContactPenetrationToleranceCM,
				Defaults.ContactPenetrationToleranceCM)
			&& IsNearlyEqual(
				Settings.MinContactPatchAreaCM2,
				Defaults.MinContactPatchAreaCM2)
			&& IsNearlyEqual(
				Settings.MinRequiredContactAreaRetention,
				Defaults.MinRequiredContactAreaRetention)
			&& Settings.NonWeakProbeCount
				== Defaults.NonWeakProbeCount
			&& IsNearlyEqual(
				Settings.MaxOrdinaryPredictedAffectedMassRatio,
				Defaults.MaxOrdinaryPredictedAffectedMassRatio)
			&& IsNearlyEqual(
				Settings.TrialDurationSeconds,
				Defaults.TrialDurationSeconds)
			&& IsNearlyEqual(
				Settings.TrialWarmupSeconds,
				Defaults.TrialWarmupSeconds)
			&& IsNearlyEqual(
				Settings.SignificantDisplacementCM,
				Defaults.SignificantDisplacementCM)
			&& IsNearlyEqual(
				Settings.SignificantRotationDegrees,
				Defaults.SignificantRotationDegrees)
			&& IsNearlyEqual(
				Settings.MinWeakAffectedMassRatio,
				Defaults.MinWeakAffectedMassRatio)
			&& IsNearlyEqual(
				Settings.MaxWeakAffectedMassRatio,
				Defaults.MaxWeakAffectedMassRatio)
			&& IsNearlyEqual(
				Settings.MinPredictedAffectedRealizationRatio,
				Defaults.MinPredictedAffectedRealizationRatio)
			&& IsNearlyEqual(
				Settings.MaxOrdinaryAffectedMassRatio,
				Defaults.MaxOrdinaryAffectedMassRatio)
			&& IsNearlyEqual(
				Settings.MinWeakResponseAdvantage,
				Defaults.MinWeakResponseAdvantage)
			&& IsNearlyEqual(
				Settings.MinWeakAbsoluteAffectedMassAdvantage,
				Defaults.MinWeakAbsoluteAffectedMassAdvantage)
			&& IsNearlyEqual(
				Settings.MinFailureDirectionAlignment,
				Defaults.MinFailureDirectionAlignment)
			&& IsNearlyEqual(
				Settings.MinWeakResponseScore,
				Defaults.MinWeakResponseScore)
			&& IsNearlyEqual(
				Settings.MinSecondaryContactSpeedCMPerSec,
				Defaults.MinSecondaryContactSpeedCMPerSec)
			&& IsNearlyEqual(
				Settings.SecondaryContactDebounceSeconds,
				Defaults.SecondaryContactDebounceSeconds)
			&& Settings.MaxSettledBodyCount
				== Defaults.MaxSettledBodyCount
			&& Settings.MaxContactPairQueryCount
				== Defaults.MaxContactPairQueryCount
			&& Settings.MaxTrialCount
				== Defaults.MaxTrialCount
			&& Settings.MaxTrialTickCount
				== Defaults.MaxTrialTickCount
			&& IsNearlyEqual(
				Settings.MaxTotalValidationSeconds,
				Defaults.MaxTotalValidationSeconds)
			&& Settings.MaxContactEventCount
				== Defaults.MaxContactEventCount;
	}

	AABTSM73StableBuildingActor* SpawnDAG4Workshop(
		UWorld& World,
		const FTransform& Transform,
		const EABTSM73DAGFailurePattern Pattern,
		FABTSM7TaskGraphBuildingProfile& OutResolvedProfile,
		FString& OutError)
	{
		OutResolvedProfile = FABTSM7TaskGraphBuildingProfile();
		OutError.Reset();
		FABTSM7TaskGraphBuildingProfile AuthoredProfile =
			FABTSM7TaskGraphDAG23ProfileResolver::MakeDefaultProfile(
				EABTSM3TaskType::Workshop,
				EABTSM7BuildingMaterial::Wood);
		AuthoredProfile.GenerationSettings.BuildingSeed =
			DAG4WorkshopSeed;
		AuthoredProfile.DAGGenerationSettings.BuildingSeed =
			DAG4WorkshopSeed;
		AuthoredProfile.DAGFailureFrontierSettings.bEnableAnalysis =
			true;
		AuthoredProfile.DAGFailureFrontierSettings
			.bEnableGeneralizedSmallCutSearch = true;
		AuthoredProfile.DAGFailurePatternSettings
			.bEnableGeometryRewrite = true;
		AuthoredProfile.DAGFailurePatternSettings.Pattern = Pattern;
		AuthoredProfile.DAGFailurePlayabilitySettings
			.bEnablePlayabilityRouting = true;
		AuthoredProfile.DAG4ValidationSettings =
			FABTSM73DAG4ValidationSettings();
		AuthoredProfile.DAG4ValidationSettings
			.bEnableSettledChaosValidation = true;

		bool bMigratedLegacy = true;
		if (!FABTSM7TaskGraphDAG23ProfileResolver::
			ResolveRuntimeProfile(
				EABTSM3TaskType::Workshop,
				AuthoredProfile,
				OutResolvedProfile,
				bMigratedLegacy)
			|| bMigratedLegacy)
		{
			OutError = TEXT("DAG4ExplicitProfileResolutionFailed");
			return nullptr;
		}

		AABTSM73StableBuildingActor* Building =
			World.SpawnActorDeferred<AABTSM73StableBuildingActor>(
				AABTSM73StableBuildingActor::StaticClass(),
				Transform,
				nullptr,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Building == nullptr)
		{
			OutError = TEXT("DAG4WorkshopSpawnFailed");
			return nullptr;
		}
		Building->ConfigureTaskGraphGeneration(
			OutResolvedProfile.GenerationSettings,
			OutResolvedProfile.DAGGenerationSettings,
			OutResolvedProfile.DAGLayoutSettings,
			OutResolvedProfile.DAGFailureFrontierSettings,
			OutResolvedProfile.DAGFailurePatternSettings,
			OutResolvedProfile.DAGFailurePlayabilitySettings,
			OutResolvedProfile.DAG4ValidationSettings,
			OutResolvedProfile.DifficultySettings);
		UGameplayStatics::FinishSpawningActor(Building, Transform);
		return Building;
	}

	int32 CountModulesOwnedBy(
		UWorld& World,
		const AActor* Owner)
	{
		int32 Count = 0;
		for (TActorIterator<AABTSM7BuildingModule> It(&World);
			It;
			++It)
		{
			if (It->GetOwner() == Owner)
			{
				++Count;
			}
		}
		return Count;
	}

	int32 CountShadowFoundationsOwnedBy(
		UWorld& World,
		const AActor* Building)
	{
		int32 Count = 0;
		for (TActorIterator<AStaticMeshActor> It(&World); It; ++It)
		{
			if (It->GetOwner() == Building)
			{
				++Count;
			}
		}
		return Count;
	}

	FString FindEarliestFailureStage(
		const AABTSM73StableBuildingActor& Building)
	{
		const FABTSM73GenerationSummary& Summary =
			Building.GetGenerationSummary();
		const FABTSM73DAG4ValidationResult& DAG4 =
			Building.GetDAG4ValidationResultForValidation();
		if (!Summary.bDAGFailureFrontierAnalysisEnabled
			|| !Summary.bDAGFailureFrontierAccepted)
		{
			return TEXT("DAG3-A/GeneralizedCut");
		}
		if (!Summary.bDAGFailurePatternEnabled
			|| !Summary.bDAGFailurePatternApplied)
		{
			return TEXT("DAG3-B");
		}
		if (!Summary.bDAGFailurePlayabilityEnabled
			|| !Summary.bDAGFailurePlayable)
		{
			return TEXT("DAG3-C");
		}
		const FString& Reason = !DAG4.RejectReason.IsEmpty()
			? DAG4.RejectReason
			: Summary.RejectReason;
		if (Reason.StartsWith(TEXT("IdleChaos")))
		{
			return TEXT("IdleSettling");
		}
		if (!DAG4.bSettledContactAccepted)
		{
			return TEXT("DAG4.SettledContact");
		}
		if (Reason.StartsWith(TEXT("DAG4Planner"))
			|| Reason.StartsWith(TEXT("DAG4TrialPlanning")))
		{
			return TEXT("DAG4.TrialPlanner");
		}
		if (DAG4.Trials.Num() < 4)
		{
			return TEXT("DAG4.ChaosRollout");
		}
		if (!DAG4.bChaosComparisonAccepted)
		{
			return TEXT("DAG4.ResponseComparison");
		}
		return TEXT("TerminalPublication");
	}

	FString FindFailureReason(
		const AABTSM73StableBuildingActor& Building)
	{
		const FABTSM73DAG4ValidationResult& DAG4 =
			Building.GetDAG4ValidationResultForValidation();
		return !DAG4.RejectReason.IsEmpty()
			? DAG4.RejectReason
			: Building.GetGenerationSummary().RejectReason;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM73DAG4RuntimePatternMatrixWeakVsNonWeakTest,
	"ABTS.M73DAG4.Runtime.PatternMatrixWeakVsNonWeak",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM73DAG4RuntimePatternMatrixWeakVsNonWeakTest::RunTest(
	const FString& Parameters)
{
	const FDAG4PatternExpectation Expectations[] = {
		{
			EABTSM73DAGFailurePattern::InternalSingleSupport,
			EABTSM73DAGFailureMotion::Drop
		},
		{
			EABTSM73DAGFailurePattern::InternalAsymmetricDualSupport,
			EABTSM73DAGFailureMotion::Tip
		},
		{
			EABTSM73DAGFailurePattern::InternalOffsetSeam,
			EABTSM73DAGFailureMotion::SlideThenTip
		}
	};
	constexpr float FixedDeltaSeconds = 1.0f / 30.0f;
	constexpr int32 ProbeTickCount = 8;
	constexpr int32 MaximumTickCount = 900;
	constexpr int32 CleanupTickCount = 2;

	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M7.3-DAG4][RuntimeTest][Begin] Buildings=%d FPS=30 Seed=%d Fixture=Workshop Thresholds=FormalDefaults"),
		static_cast<int32>(UE_ARRAY_COUNT(Expectations)),
		DAG4WorkshopSeed);

	FABTSM73DAG4RuntimeTestWorld WorldWrapper;
	if (!WorldWrapper.CreatePhysicsWorld())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = WorldWrapper.GetTestWorld();
	if (!TestNotNull(TEXT("DAG-4 physics test world"), World))
	{
		return false;
	}
	if (!TestNotNull(
		TEXT("DAG-4 physics scene"),
		World->GetPhysicsScene()))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM71PhysicsTestStage* Stage =
		World->SpawnActorDeferred<AABTSM71PhysicsTestStage>(
			AABTSM71PhysicsTestStage::StaticClass(),
			FTransform::Identity,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Stage != nullptr)
	{
		UGameplayStatics::FinishSpawningActor(
			Stage,
			FTransform::Identity);
	}
	AABTSM7BuildingMaterialSystem* MaterialSystem =
		World->SpawnActor<AABTSM7BuildingMaterialSystem>(
			AABTSM7BuildingMaterialSystem::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	if (!TestNotNull(TEXT("DAG-4 planar physics stage"), Stage)
		|| !TestNotNull(
			TEXT("DAG-4 material system"),
			MaterialSystem))
	{
		return false;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	AStaticMeshActor* GravityProbe =
		World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(),
			FTransform(FVector(4800.0f, 4800.0f, 1500.0f)),
			SpawnParameters);
	if (!TestNotNull(TEXT("DAG-4 gravity probe"), GravityProbe)
		|| !TestNotNull(
			TEXT("DAG-4 gravity probe mesh asset"),
			CubeMesh))
	{
		return false;
	}
	UStaticMeshComponent* ProbeMesh =
		GravityProbe->GetStaticMeshComponent();
	if (!TestNotNull(
		TEXT("DAG-4 gravity probe component"),
		ProbeMesh))
	{
		return false;
	}
	ProbeMesh->SetMobility(EComponentMobility::Movable);
	ProbeMesh->SetStaticMesh(CubeMesh);
	ProbeMesh->SetCollisionEnabled(
		ECollisionEnabled::QueryAndPhysics);
	ProbeMesh->SetSimulatePhysics(true);
	ProbeMesh->SetEnableGravity(true);

	TArray<FDAG4FixtureState> Fixtures;
	for (int32 Index = 0;
		Index < UE_ARRAY_COUNT(Expectations);
		++Index)
	{
		FDAG4FixtureState& Fixture =
			Fixtures.AddDefaulted_GetRef();
		Fixture.Expected = Expectations[Index];
		FString SpawnError;
		const FTransform Transform(FVector(
			0.0f,
			(static_cast<float>(Index) - 1.0f) * 1800.0f,
			0.0f));
		Fixture.Building = SpawnDAG4Workshop(
			*World,
			Transform,
			Fixture.Expected.Pattern,
			Fixture.ResolvedProfile,
			SpawnError);
		if (Fixture.Building == nullptr)
		{
			AddError(FString::Printf(
				TEXT("%s DAG-4 Workshop spawn failed: %s"),
				GetPatternName(Fixture.Expected.Pattern),
				*SpawnError));
			return false;
		}
	}

	if (!WorldWrapper.BeginPlayInTestWorld())
	{
		WorldWrapper.ForwardErrorMessages(this);
		return false;
	}
	for (FDAG4FixtureState& Fixture : Fixtures)
	{
		Fixture.Building->InitializeRuntimeBuilding(MaterialSystem);
	}

	int32 ExpectedFormalModuleCount = 0;
	for (FDAG4FixtureState& Fixture : Fixtures)
	{
		const TCHAR* PatternName =
			GetPatternName(Fixture.Expected.Pattern);
		const FABTSM73GenerationSummary& Summary =
			Fixture.Building->GetGenerationSummary();
		const FABTSM73DAGFailurePatternResult& PatternResult =
			Fixture.Building
				->GetDAGFailurePatternResultForValidation();
		const FABTSM73DAGFailurePlayabilityResult& Playability =
			Fixture.Building
				->GetDAGFailurePlayabilityResultForValidation();
		const FABTSM73DAG4ValidationResult& InitialDAG4 =
			Fixture.Building
				->GetDAG4ValidationResultForValidation();

		TestTrue(
			FString::Printf(
				TEXT("%s profile explicitly enables DAG3-A"),
				PatternName),
			Fixture.ResolvedProfile.DAGFailureFrontierSettings
				.bEnableAnalysis);
		TestTrue(
			FString::Printf(
				TEXT("%s profile explicitly enables generalized cuts"),
				PatternName),
			Fixture.ResolvedProfile.DAGFailureFrontierSettings
				.bEnableGeneralizedSmallCutSearch);
		TestTrue(
			FString::Printf(
				TEXT("%s profile explicitly enables DAG3-B"),
				PatternName),
			Fixture.ResolvedProfile.DAGFailurePatternSettings
				.bEnableGeometryRewrite);
		TestTrue(
			FString::Printf(
				TEXT("%s profile explicitly enables DAG3-C"),
				PatternName),
			Fixture.ResolvedProfile.DAGFailurePlayabilitySettings
				.bEnablePlayabilityRouting);
		TestTrue(
			FString::Printf(
				TEXT("%s uses enabled, unmodified formal DAG-4 defaults"),
				PatternName),
			UsesUnmodifiedFormalDAG4Defaults(
				Fixture.ResolvedProfile.DAG4ValidationSettings));
		TestEqual(
			FString::Printf(
				TEXT("%s profile preserves its explicit pattern"),
				PatternName),
			static_cast<int32>(
				Fixture.ResolvedProfile
					.DAGFailurePatternSettings.Pattern),
			static_cast<int32>(Fixture.Expected.Pattern));

		TestTrue(
			FString::Printf(
				TEXT("%s generation reaches runtime: %s"),
				PatternName,
				*Summary.RejectReason),
			Summary.bAccepted);
		TestTrue(
			FString::Printf(
				TEXT("%s accepts DAG3-A"),
				PatternName),
			Summary.bDAGFailureFrontierAccepted);
		TestTrue(
			FString::Printf(
				TEXT("%s applies DAG3-B"),
				PatternName),
			Summary.bDAGFailurePatternApplied);
		TestTrue(
			FString::Printf(
				TEXT("%s accepts DAG3-C: %s"),
				PatternName,
				*Playability.RejectReason),
			Summary.bDAGFailurePlayable
				&& Playability.bPlayable);
		TestEqual(
			FString::Printf(
				TEXT("%s realizes the requested pattern"),
				PatternName),
			static_cast<int32>(PatternResult.Pattern),
			static_cast<int32>(Fixture.Expected.Pattern));
		TestEqual(
			FString::Printf(
				TEXT("%s publishes the requested motion"),
				PatternName),
			static_cast<int32>(PatternResult.ExpectedMotion),
			static_cast<int32>(Fixture.Expected.Motion));
		TestTrue(
			FString::Printf(
				TEXT("%s initializes DAG-4 as enabled"),
				PatternName),
			InitialDAG4.bEnabled);
		TestEqual(
			FString::Printf(
				TEXT("%s starts the combined Idle/DAG-4 gate"),
				PatternName),
			Fixture.Building->GetIdleValidationState(),
			EABTSM73IdleValidationState::Running);

		ExpectedFormalModuleCount += Summary.BrickCount;
		const int32 NodeSearchLimit =
			FMath::Max(512, Summary.BrickCount * 16);
		for (int32 NodeId = 0;
			NodeId < NodeSearchLimit;
			++NodeId)
		{
			if (Fixture.Building
				->FindRuntimeModuleForNodeForValidation(NodeId)
				!= nullptr)
			{
				Fixture.FormalNodeIds.Add(NodeId);
			}
		}
		TestEqual(
			FString::Printf(
				TEXT("%s discovers every formal NodeId-to-Module binding"),
				PatternName),
			Fixture.FormalNodeIds.Num(),
			Summary.BrickCount);

		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M7.3-DAG4][RuntimeTest][Fixture] Actor=%s Pattern=%s Motion=%d Bricks=%d Weak=%d Affected=%d PlayabilityHash=%u"),
			*Fixture.Building->GetName(),
			PatternName,
			static_cast<int32>(PatternResult.ExpectedMotion),
			Summary.BrickCount,
			PatternResult.WeakNodeIds.Num(),
			PatternResult.AffectedMainBodyNodeIds.Num(),
			Playability.PlayabilityHash);
	}

	const int32 InitialFormalModuleCount =
		CountModulesOwnedBy(*World, MaterialSystem);
	TestEqual(
		TEXT("MaterialSystem initially owns every formal module"),
		InitialFormalModuleCount,
		ExpectedFormalModuleCount);

	const float InitialProbeZ =
		GravityProbe->GetActorLocation().Z;
	for (int32 TickIndex = 0;
		TickIndex < ProbeTickCount;
		++TickIndex)
	{
		if (!WorldWrapper.TickTestWorld(FixedDeltaSeconds))
		{
			WorldWrapper.ForwardErrorMessages(this);
			return false;
		}
	}
	const float ProbeDropCM =
		InitialProbeZ - GravityProbe->GetActorLocation().Z;
	TestTrue(
		FString::Printf(
			TEXT("DAG-4 fresh Physics Scene advances at 30 Hz; probe drop %.2f cm"),
			ProbeDropCM),
		ProbeDropCM > 10.0f);
	TestTrue(
		TEXT("DAG-4 gravity probe gains downward velocity"),
		ProbeMesh->GetPhysicsLinearVelocity().Z < -50.0f);

	auto CaptureFormalSnapshotAtDAG4Start =
		[this](FDAG4FixtureState& Fixture)
	{
		if (Fixture.bDAG4StartSnapshotCaptured)
		{
			return;
		}
		Fixture.bDAG4StartSnapshotCaptured = true;
		const TCHAR* PatternName =
			GetPatternName(Fixture.Expected.Pattern);
		for (const int32 NodeId : Fixture.FormalNodeIds)
		{
			AABTSM7BuildingModule* Module =
				Fixture.Building
					->FindRuntimeModuleForNodeForValidation(
						NodeId);
			if (Module == nullptr)
			{
				AddError(FString::Printf(
					TEXT("%s formal NodeId %d is missing at DAG-4 start"),
					PatternName,
					NodeId));
				continue;
			}
			FDAG4FormalModuleSnapshot& Snapshot =
				Fixture.FormalSnapshots.Add(NodeId);
			Snapshot.Module = Module;
			Snapshot.Transform = Module->GetActorTransform();
			Snapshot.Damage = Module->GetCurrentDamage();
			UStaticMeshComponent* Mesh =
				Module->GetMeshComponent();
			TestNotNull(
				FString::Printf(
					TEXT("%s formal NodeId %d has a mesh at DAG-4 start"),
					PatternName,
					NodeId),
				Mesh);
			if (Mesh != nullptr)
			{
				TestFalse(
					FString::Printf(
						TEXT("%s formal NodeId %d is frozen during DAG-4"),
						PatternName,
						NodeId),
					Mesh->IsSimulatingPhysics());
			}
		}
		TestEqual(
			FString::Printf(
				TEXT("%s snapshots every formal module at DAG-4 start"),
				PatternName),
			Fixture.FormalSnapshots.Num(),
			Fixture.FormalNodeIds.Num());
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M7.3-DAG4][RuntimeTest][DAG4Start] Actor=%s Pattern=%s Nodes=%d"),
			*Fixture.Building->GetName(),
			PatternName,
			Fixture.FormalSnapshots.Num());
	};

	int32 TerminalCount = 0;
	int32 LastTickIndex = ProbeTickCount;
	for (int32 TickIndex = ProbeTickCount;
		TickIndex < MaximumTickCount;
		++TickIndex)
	{
		LastTickIndex = TickIndex;
		if (!WorldWrapper.TickTestWorld(FixedDeltaSeconds))
		{
			WorldWrapper.ForwardErrorMessages(this);
			return false;
		}

		TerminalCount = 0;
		for (FDAG4FixtureState& Fixture : Fixtures)
		{
			const FABTSM73DAG4ValidationResult& DAG4 =
				Fixture.Building
					->GetDAG4ValidationResultForValidation();
			if (DAG4.bSettledContactAccepted
				&& !Fixture.bDAG4StartSnapshotCaptured)
			{
				CaptureFormalSnapshotAtDAG4Start(Fixture);
			}
			if (!Fixture.Building->IsIdleValidationTerminal())
			{
				continue;
			}
			++TerminalCount;
			if (Fixture.Building->GetIdleValidationState()
					== EABTSM73IdleValidationState::Rejected
				&& !Fixture.bFailureReported)
			{
				Fixture.bFailureReported = true;
				const FString StageName =
					FindEarliestFailureStage(*Fixture.Building);
				const FString Reason =
					FindFailureReason(*Fixture.Building);
				AddError(FString::Printf(
					TEXT("%s earliest terminal failure Stage=%s Reason=%s"),
					GetPatternName(Fixture.Expected.Pattern),
					*StageName,
					*Reason));
				UE_LOG(
					LogABTSRuntime,
					Error,
					TEXT("[ABTS][M7.3-DAG4][RuntimeTest][EarliestFailure] Actor=%s Pattern=%s Stage=%s Reason=%s Trials=%d Settled=%d"),
					*Fixture.Building->GetName(),
					GetPatternName(Fixture.Expected.Pattern),
					*StageName,
					*Reason,
					DAG4.Trials.Num(),
					DAG4.bSettledContactAccepted ? 1 : 0);
			}
		}
		if (TerminalCount == Fixtures.Num())
		{
			break;
		}
	}
	for (FDAG4FixtureState& Fixture : Fixtures)
	{
		if (Fixture.Building->IsIdleValidationTerminal()
			|| Fixture.bFailureReported)
		{
			continue;
		}
		Fixture.bFailureReported = true;
		AddError(FString::Printf(
			TEXT("%s earliest terminal failure Stage=RuntimeTimeout Reason=NotTerminalAfter%dTicks State=%d Settled=%d Trials=%d"),
			GetPatternName(Fixture.Expected.Pattern),
			MaximumTickCount,
			static_cast<int32>(
				Fixture.Building->GetIdleValidationState()),
			Fixture.Building
				->GetDAG4ValidationResultForValidation()
				.bSettledContactAccepted
					? 1
					: 0,
			Fixture.Building
				->GetDAG4ValidationResultForValidation()
				.Trials.Num()));
	}
	TestEqual(
		TEXT("Every DAG-4 fixture reaches a terminal state"),
		TerminalCount,
		Fixtures.Num());

	for (int32 CleanupTick = 0;
		CleanupTick < CleanupTickCount;
		++CleanupTick)
	{
		if (!WorldWrapper.TickTestWorld(FixedDeltaSeconds))
		{
			WorldWrapper.ForwardErrorMessages(this);
			return false;
		}
	}

	for (FDAG4FixtureState& Fixture : Fixtures)
	{
		const TCHAR* PatternName =
			GetPatternName(Fixture.Expected.Pattern);
		const FABTSM73GenerationSummary& Summary =
			Fixture.Building->GetGenerationSummary();
		const FABTSM73DAG4ValidationResult& DAG4 =
			Fixture.Building
				->GetDAG4ValidationResultForValidation();
		TestEqual(
			FString::Printf(
				TEXT("%s terminal state is Accepted"),
				PatternName),
			Fixture.Building->GetIdleValidationState(),
			EABTSM73IdleValidationState::Accepted);
		TestTrue(
			FString::Printf(
				TEXT("%s keeps the formal building accepted: %s"),
				PatternName,
				*Summary.RejectReason),
			Summary.bAccepted);
		TestTrue(
			FString::Printf(
				TEXT("%s accepts settled-contact certification: %s"),
				PatternName,
				*DAG4.RejectReason),
			DAG4.bSettledContactAccepted);
		TestTrue(
			FString::Printf(
				TEXT("%s accepts weak-vs-ordinary Chaos comparison: %s"),
				PatternName,
				*DAG4.RejectReason),
			DAG4.bChaosComparisonAccepted);
		TestTrue(
			FString::Printf(
				TEXT("%s publishes accepted DAG-4 result: %s"),
				PatternName,
				*DAG4.RejectReason),
			DAG4.bAccepted);
		TestTrue(
			FString::Printf(
				TEXT("%s publishes non-zero validation hash"),
				PatternName),
			DAG4.ValidationHash != 0);
		TestEqual(
			FString::Printf(
				TEXT("%s summary and result validation hashes agree"),
				PatternName),
			Summary.DAG4ValidationHash,
			DAG4.ValidationHash);
		TestTrue(
			FString::Printf(
				TEXT("%s summary publishes settled acceptance"),
				PatternName),
			Summary.bDAG4SettledContactAccepted);
		TestTrue(
			FString::Printf(
				TEXT("%s summary publishes comparison acceptance"),
				PatternName),
			Summary.bDAG4ChaosComparisonAccepted);

		const int32 ExpectedTrialCount =
			Fixture.ResolvedProfile.DAG4ValidationSettings
				.NonWeakProbeCount + 1;
		TestEqual(
			FString::Printf(
				TEXT("%s retains one weak plus three ordinary trials"),
				PatternName),
			DAG4.Trials.Num(),
			ExpectedTrialCount);
		int32 WeakTrialCount = 0;
		int32 OrdinaryTrialCount = 0;
		const FABTSM73DAG4TrialMetrics* WeakTrial = nullptr;
		for (const FABTSM73DAG4TrialMetrics& Trial : DAG4.Trials)
		{
			if (Trial.Kind
				== EABTSM73DAG4TrialKind::WeakPoint)
			{
				++WeakTrialCount;
				WeakTrial = &Trial;
			}
			else
			{
				++OrdinaryTrialCount;
			}
			TestTrue(
				FString::Printf(
					TEXT("%s trial Probe=%d completes without reject: %s"),
					PatternName,
					Trial.ProbeIndex,
					*Trial.RejectReason),
				Trial.bCompleted
					&& Trial.RejectReason.IsEmpty());
		}
		TestEqual(
			FString::Printf(
				TEXT("%s has exactly one weak trial"),
				PatternName),
			WeakTrialCount,
			1);
		TestEqual(
			FString::Printf(
				TEXT("%s has exactly three ordinary trials"),
				PatternName),
			OrdinaryTrialCount,
			Fixture.ResolvedProfile.DAG4ValidationSettings
				.NonWeakProbeCount);
		TestTrue(
			FString::Printf(
				TEXT("%s weak trial index is valid"),
				PatternName),
			DAG4.Trials.IsValidIndex(DAG4.WeakTrialIndex));
		if (DAG4.Trials.IsValidIndex(DAG4.WeakTrialIndex))
		{
			TestEqual(
				FString::Printf(
					TEXT("%s weak trial index points to WeakPoint"),
					PatternName),
				static_cast<int32>(
					DAG4.Trials[DAG4.WeakTrialIndex].Kind),
				static_cast<int32>(
					EABTSM73DAG4TrialKind::WeakPoint));
		}
		if (WeakTrial != nullptr)
		{
			const FABTSM73DAG4ValidationSettings& Settings =
				Fixture.ResolvedProfile.DAG4ValidationSettings;
			for (const FABTSM73DAG4TrialMetrics& Trial
				: DAG4.Trials)
			{
				if (Trial.Kind
					!= EABTSM73DAG4TrialKind::Ordinary)
				{
					continue;
				}
				TestTrue(
					FString::Printf(
						TEXT("%s weak score %.3f beats ordinary Probe=%d score %.3f"),
						PatternName,
						WeakTrial->ResponseScore,
						Trial.ProbeIndex,
						Trial.ResponseScore),
					WeakTrial->ResponseScore
						> Trial.ResponseScore);
				TestTrue(
					FString::Printf(
						TEXT("%s weak score meets formal advantage over Probe=%d"),
						PatternName,
						Trial.ProbeIndex),
					WeakTrial->ResponseScore
							+ KINDA_SMALL_NUMBER
						>= Trial.ResponseScore
							* Settings.MinWeakResponseAdvantage);
				TestTrue(
					FString::Printf(
						TEXT("%s weak affected mass beats ordinary Probe=%d"),
						PatternName,
						Trial.ProbeIndex),
					WeakTrial->AffectedMainBodyMassRatio
						> Trial.AffectedMainBodyMassRatio);
				TestTrue(
					FString::Printf(
						TEXT("%s weak affected mass meets formal absolute gap over Probe=%d"),
						PatternName,
						Trial.ProbeIndex),
					WeakTrial->AffectedMainBodyMassRatio
							- Trial.AffectedMainBodyMassRatio
							+ KINDA_SMALL_NUMBER
						>= Settings
							.MinWeakAbsoluteAffectedMassAdvantage);
			}
		}

		TestTrue(
			FString::Printf(
				TEXT("%s captured formal state at DAG-4 start"),
				PatternName),
			Fixture.bDAG4StartSnapshotCaptured);
		TestEqual(
			FString::Printf(
				TEXT("%s retained a snapshot for every formal NodeId"),
				PatternName),
			Fixture.FormalSnapshots.Num(),
			Fixture.FormalNodeIds.Num());
		for (const TPair<int32, FDAG4FormalModuleSnapshot>& Pair
			: Fixture.FormalSnapshots)
		{
			const int32 NodeId = Pair.Key;
			const FDAG4FormalModuleSnapshot& Snapshot =
				Pair.Value;
			AABTSM7BuildingModule* Current =
				Fixture.Building
					->FindRuntimeModuleForNodeForValidation(
						NodeId);
			TestNotNull(
				FString::Printf(
					TEXT("%s formal NodeId %d remains mapped after DAG-4"),
					PatternName,
					NodeId),
				Current);
			if (Current == nullptr)
			{
				continue;
			}
			TestTrue(
				FString::Printf(
					TEXT("%s formal NodeId %d keeps the same Module actor"),
					PatternName,
					NodeId),
				Current == Snapshot.Module.Get());
			const FTransform CurrentTransform =
				Current->GetActorTransform();
			TestTrue(
				FString::Printf(
					TEXT("%s formal NodeId %d transform is unchanged (Move=%.6f Rot=%.6f)"),
					PatternName,
					NodeId,
					FVector::Distance(
						CurrentTransform.GetLocation(),
						Snapshot.Transform.GetLocation()),
					FMath::RadiansToDegrees(
						CurrentTransform.GetRotation()
							.AngularDistance(
								Snapshot.Transform
									.GetRotation()))),
				CurrentTransform.Equals(
					Snapshot.Transform,
					KINDA_SMALL_NUMBER));
			TestEqual(
				FString::Printf(
					TEXT("%s formal NodeId %d damage is unchanged"),
					PatternName,
					NodeId),
				Current->GetCurrentDamage(),
				Snapshot.Damage);
		}

		const int32 RemainingShadowModuleCount =
			CountModulesOwnedBy(*World, Fixture.Building);
		const int32 RemainingShadowFoundationCount =
			CountShadowFoundationsOwnedBy(
				*World,
				Fixture.Building);
		TestEqual(
			FString::Printf(
				TEXT("%s leaves no Building-owned shadow module"),
				PatternName),
			RemainingShadowModuleCount,
			0);
		TestEqual(
			FString::Printf(
				TEXT("%s leaves no Building-owned shadow foundation"),
				PatternName),
			RemainingShadowFoundationCount,
			0);

		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M7.3-DAG4][RuntimeTest][FixtureComplete] Actor=%s Pattern=%s State=%d Settled=%d Comparison=%d Accepted=%d Trials=%d Weak=%.3f Ordinary=%.3f Advantage=%.3f Hash=%lld Reason=%s"),
			*Fixture.Building->GetName(),
			PatternName,
			static_cast<int32>(
				Fixture.Building->GetIdleValidationState()),
			DAG4.bSettledContactAccepted ? 1 : 0,
			DAG4.bChaosComparisonAccepted ? 1 : 0,
			DAG4.bAccepted ? 1 : 0,
			DAG4.Trials.Num(),
			DAG4.WeakResponseScore,
			DAG4.MaxOrdinaryResponseScore,
			DAG4.WeakResponseAdvantage,
			DAG4.ValidationHash,
			*DAG4.RejectReason);
	}

	const int32 FinalFormalModuleCount =
		CountModulesOwnedBy(*World, MaterialSystem);
	TestEqual(
		TEXT("MaterialSystem formal module count is unchanged by DAG-4"),
		FinalFormalModuleCount,
		InitialFormalModuleCount);
	TestEqual(
		TEXT("MaterialSystem still owns every expected formal module"),
		FinalFormalModuleCount,
		ExpectedFormalModuleCount);

	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M7.3-DAG4][RuntimeTest][Complete] Terminal=%d/%d Tick=%d FormalBefore=%d FormalAfter=%d ProbeDrop=%.2f Errors=%d"),
		TerminalCount,
		Fixtures.Num(),
		LastTickIndex,
		InitialFormalModuleCount,
		FinalFormalModuleCount,
		ProbeDropCM,
		HasAnyErrors() ? 1 : 0);
	WorldWrapper.ForwardErrorMessages(this);
	return !HasAnyErrors();
}

#endif
