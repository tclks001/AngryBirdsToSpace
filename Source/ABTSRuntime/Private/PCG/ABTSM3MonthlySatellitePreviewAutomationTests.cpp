// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Calibration/ABTSCalibrationTargetProxy.h"
#include "Calibration/ABTSSlingshotSatelliteCalibrationTypes.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3MonthlySatellitePracticeRuntime.h"
#include "PCG/ABTSM3MonthlySatellitePreview.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "Terrain/ABTSM3Planet.h"
#include "Terrain/ABTSM3StylizedSemanticAdapter.h"
#include "World/ABTSM51WorldActors.h"
#include "World/ABTSM9GravityQuery.h"
#include "World/ABTSM9Satellite.h"

namespace ABTSM3R51SatellitePreviewTests
{
class FScopedTestWorld
{
public:
	FScopedTestWorld()
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
			TEXT("ABTSM3R51SatellitePreviewTestWorld"),
			nullptr,
			true,
			ERHIFeatureLevel::Num,
			&Values);
		if (World != nullptr && GEngine != nullptr)
		{
			WorldContext = &GEngine->CreateNewWorldContext(EWorldType::Game);
			WorldContext->SetCurrentWorld(World);
		}
	}

	~FScopedTestWorld()
	{
		if (World != nullptr)
		{
			if (GEngine != nullptr && WorldContext != nullptr)
			{
				GEngine->DestroyWorldContext(World);
				WorldContext = nullptr;
			}
			World->DestroyWorld(false);
			World->RemoveFromRoot();
		}
	}

	UWorld* Get() const { return World; }

private:
	UWorld* World = nullptr;
	FWorldContext* WorldContext = nullptr;
};

AABTSM3Planet* SpawnPreviewPlanet(
	FAutomationTestBase& Test,
	UWorld& World)
{
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM3Planet* Planet = World.SpawnActor<AABTSM3Planet>(
		AABTSM3Planet::StaticClass(),
		FTransform::Identity,
		Parameters);
	Test.TestNotNull(TEXT("M3 preview planet spawns"), Planet);
	if (Planet != nullptr)
	{
		Planet->SurfaceSubdivision = 1;
		Planet->InstancesPerCell = 0;
	}
	return Planet;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R51SatellitePreviewCoreTest,
	"ABTS.M3.Monthly.SatellitePreview.01CandidateBoundCore",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R51SatellitePreviewCoreTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	using namespace ABTSM3R51SatellitePreviewTests;
	FScopedTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Transient test World is created"), World);
	if (World == nullptr)
	{
		return false;
	}
	AABTSM3Planet* Planet = SpawnPreviewPlanet(*this, *World);
	if (Planet == nullptr)
	{
		return false;
	}
	TestTrue(TEXT("M3 world rebuilds with R-5.1 preview"), Planet->RebuildPlanet());

	const FABTSM3MonthlySatellitePreviewResult First =
		Planet->GetMonthlySatellitePreviewResult();
	TestTrue(TEXT("Satellite preview result is valid"), First.bPreviewResultValid);
	TestFalse(TEXT("Satellite preview never accepts the monthly world"),
		First.bMonthlyWorldAccepted);
	TestEqual(TEXT("Every R-3 candidate gets one preview"),
		First.RetainedCandidates.Num(),
		Planet->GetMonthlySpatialResult().RetainedCandidates.Num());
	TestEqual(TEXT("Result hash is canonical"),
		static_cast<uint64>(First.ResultHash),
		FABTSM3MonthlySatellitePreviewBuilder::ComputeResultHash(First));

	const FABTSSatellitePracticePreset FrozenPreset =
		FABTSSlingshotSatelliteCalibrationModel::MakeFrozenSatellitePracticePresetV0();
	const int64 FrozenPresetHash = static_cast<int64>(
		FABTSSlingshotSatelliteCalibrationModel::ComputeSatellitePracticePresetHash(FrozenPreset));
	for (const FABTSM3MonthlySatellitePreviewCandidate& Candidate :
		First.RetainedCandidates)
	{
		TestTrue(TEXT("Reference pair contains two distinct slots"),
			Candidate.ReferenceSlotACellId != INDEX_NONE
				&& Candidate.ReferenceSlotBCellId != INDEX_NONE
				&& Candidate.ReferenceSlotACellId != Candidate.ReferenceSlotBCellId);
		TestEqual(TEXT("Frozen preset version is preserved"),
			Candidate.SatellitePracticePresetVersion,
			FrozenPreset.Version);
		TestEqual(TEXT("Frozen preset hash is preserved"),
			Candidate.SatellitePracticePresetHash,
			FrozenPresetHash);
		TestEqual(TEXT("Candidate hash is canonical"),
			static_cast<uint64>(Candidate.CandidateHash),
			FABTSM3MonthlySatellitePreviewBuilder::ComputeCandidateHash(Candidate));
		TestTrue(TEXT("Satellite has a positive generated radius"),
			Candidate.SatelliteRadiusCM > 0.0f);
		TestTrue(TEXT("E5 proxy is on the satellite back side"),
			Candidate.bE5OnSatelliteBackside);
		const float TargetCenterRadius = FVector::Distance(
			Candidate.E5TargetWorldTransform.GetLocation(),
			Candidate.SatelliteCenterWorld);
		const float ExpectedTargetCenterRadius =
			Candidate.SatelliteRadiusCM
			+ FrozenPreset.TargetProxyRadiusCM
			+ FrozenPreset.TargetSatelliteClearanceCM;
		TestTrue(TEXT("E5 proxy rests on the satellite surface"),
			FMath::IsNearlyEqual(
				TargetCenterRadius,
				ExpectedTargetCenterRadius,
				0.25f));
	}

	FString ValidationFailure;
	TestTrue(TEXT("Planet whole-struct validation succeeds"),
		Planet->ValidateMonthlySatellitePreviewResult(ValidationFailure));
	TestTrue(TEXT("Repeated rebuild succeeds"), Planet->RebuildPlanet());
	const FABTSM3MonthlySatellitePreviewResult& Second =
		Planet->GetMonthlySatellitePreviewResult();
	TestTrue(TEXT("Repeated rebuild is whole-struct deterministic"),
		FABTSM3MonthlySatellitePreviewResult::StaticStruct()
			->CompareScriptStruct(&First, &Second, PPF_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R51SatellitePreviewFailureClosureTest,
	"ABTS.M3.Monthly.SatellitePreview.02FailureClosure",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R51SatellitePreviewFailureClosureTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	using namespace ABTSM3R51SatellitePreviewTests;
	FScopedTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	if (World == nullptr)
	{
		AddError(TEXT("Transient test World was not created"));
		return false;
	}
	AABTSM3Planet* Planet = SpawnPreviewPlanet(*this, *World);
	if (Planet == nullptr || !Planet->RebuildPlanet())
	{
		AddError(TEXT("Preview fixture failed to rebuild"));
		return false;
	}

	const FABTSM3MonthlySatellitePreviewResult& Result =
		Planet->GetMonthlySatellitePreviewResult();
	TestTrue(TEXT("Fixture preview is valid"), Result.bPreviewResultValid);
	FABTSM3MonthlySatellitePreviewResult Tampered = Result;
	if (Tampered.RetainedCandidates.IsEmpty())
	{
		AddError(TEXT("Fixture preview has no candidates"));
		return false;
	}
	Tampered.RetainedCandidates[0].E5TargetWorldTransform.AddToTranslation(
		FVector(1.0f, 0.0f, 0.0f));
	TestNotEqual(TEXT("Transform tamper changes candidate hash oracle"),
		static_cast<uint64>(Tampered.RetainedCandidates[0].CandidateHash),
		FABTSM3MonthlySatellitePreviewBuilder::ComputeCandidateHash(
			Tampered.RetainedCandidates[0]));
	TestFalse(TEXT("Tampered result cannot masquerade as a canonical result"),
		FABTSM3MonthlySatellitePreviewResult::StaticStruct()
			->CompareScriptStruct(&Result, &Tampered, PPF_None));
	TestFalse(TEXT("Observation remains non-authoritative after tamper"),
		Tampered.bMonthlyWorldAccepted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R51SatelliteRuntimePracticeTest,
	"ABTS.M3.Monthly.SatellitePreview.03RuntimePracticeSnapshot",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R51SatelliteRuntimePracticeTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	using namespace ABTSM3R51SatellitePreviewTests;
	FScopedTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	if (World == nullptr)
	{
		AddError(TEXT("Transient test World was not created"));
		return false;
	}
	AABTSM3Planet* Planet = SpawnPreviewPlanet(*this, *World);
	if (Planet == nullptr || !Planet->RebuildPlanet())
	{
		AddError(TEXT("Runtime practice fixture failed to rebuild"));
		return false;
	}
	const FABTSM3MonthlySatellitePreviewResult& Preview =
		Planet->GetMonthlySatellitePreviewResult();
	if (!Preview.bPreviewResultValid
		|| Preview.RetainedCandidates.IsEmpty())
	{
		AddError(TEXT("Runtime practice fixture has no preview candidate"));
		return false;
	}
	const FABTSM3MonthlySatellitePreviewCandidate Candidate =
		Preview.RetainedCandidates[0];
	const int64 PreviewResultHashBeforeSemanticQuery = Preview.ResultHash;
	const int64 CandidateHashBeforeSemanticQuery = Candidate.CandidateHash;
	const bool bPreviewAcceptedBeforeSemanticQuery =
		Preview.bMonthlyWorldAccepted;
	EABTSStylizedObjectClass PreviewSatelliteClass =
		EABTSStylizedObjectClass::None;
	EABTSStylizedObjectClass PreviewE5Class =
		EABTSStylizedObjectClass::None;
	TestTrue(TEXT("Preview result publishes the satellite semantic"),
		FABTSM3StylizedSemanticAdapter::
			TryResolveMonthlySatellitePreviewElement(
				Preview,
				Candidate,
				EABTSM3StylizedSatellitePreviewElement::SatelliteSurface,
				PreviewSatelliteClass));
	TestTrue(TEXT("Preview result publishes the backside E5 semantic"),
		FABTSM3StylizedSemanticAdapter::
			TryResolveMonthlySatellitePreviewElement(
				Preview,
				Candidate,
				EABTSM3StylizedSatellitePreviewElement::BacksideE5Target,
				PreviewE5Class));
	TestEqual(TEXT("Preview satellite maps to SatelliteTarget"),
		PreviewSatelliteClass,
		EABTSStylizedObjectClass::SatelliteTarget);
	TestEqual(TEXT("Preview E5 maps to SatelliteTarget"),
		PreviewE5Class,
		EABTSStylizedObjectClass::SatelliteTarget);
	EABTSStylizedObjectClass UnknownPreviewClass =
		EABTSStylizedObjectClass::SatelliteTarget;
	TestFalse(TEXT("Unknown preview element fails closed"),
		FABTSM3StylizedSemanticAdapter::
			TryResolveMonthlySatellitePreviewElement(
				Preview,
				Candidate,
				static_cast<EABTSM3StylizedSatellitePreviewElement>(255),
				UnknownPreviewClass));
	TestEqual(TEXT("Unknown preview element publishes None"),
		UnknownPreviewClass,
		EABTSStylizedObjectClass::None);
	TestEqual(TEXT("Preview semantic query preserves result hash"),
		Preview.ResultHash,
		PreviewResultHashBeforeSemanticQuery);
	TestEqual(TEXT("Preview semantic query preserves candidate hash"),
		Candidate.CandidateHash,
		CandidateHashBeforeSemanticQuery);
	TestEqual(TEXT("Preview semantic query preserves preview authority"),
		Preview.bMonthlyWorldAccepted,
		bPreviewAcceptedBeforeSemanticQuery);

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM6SlingshotSystem* SlingshotSystem =
		World->SpawnActor<AABTSM6SlingshotSystem>(
			AABTSM6SlingshotSystem::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	TestNotNull(TEXT("M6 target consumer spawns"), SlingshotSystem);
	if (SlingshotSystem == nullptr)
	{
		return false;
	}
	const FABTSM6LaunchProfileCatalog ProductionCatalog =
		FABTSSlingshotSatelliteCalibrationModel::
			MakeFrozenLaunchProfileCatalogV0();
	TestTrue(TEXT("Transient M6 installs the production frozen launch catalog"),
		SlingshotSystem->ConfigureLaunchProfiles(ProductionCatalog));

	IConsoleVariable* GravityCVar = IConsoleManager::Get().FindConsoleVariable(
		TEXT("abts.Calibration.SatelliteGravity"));
	TestNotNull(TEXT("Shared calibration gravity cvar is registered"), GravityCVar);
	if (GravityCVar == nullptr)
	{
		return false;
	}
	const int32 OriginalGravityOverride = GravityCVar->GetInt();
	GravityCVar->Set(0, ECVF_SetByCode);

	AABTSM3MonthlySatellitePracticeRuntime* Runtime =
		World->SpawnActorDeferred<AABTSM3MonthlySatellitePracticeRuntime>(
			AABTSM3MonthlySatellitePracticeRuntime::StaticClass(),
			FTransform::Identity,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	const bool bConfigured = Runtime != nullptr
		&& Runtime->Configure(*Planet, Candidate, Preview.ResultHash);
	TestTrue(TEXT("Runtime accepts the canonical frozen candidate"), bConfigured);
	if (!bConfigured)
	{
		GravityCVar->Set(OriginalGravityOverride, ECVF_SetByCode);
		return false;
	}
	UGameplayStatics::FinishSpawningActor(Runtime, FTransform::Identity);
	TestTrue(TEXT("Runtime activates satellite, E5 collision and M6 target"),
		Runtime->ActivateSnapshot());
	TestTrue(TEXT("Runtime readiness closes all gameplay dependencies"),
		Runtime->IsRuntimeReady());
	TestTrue(TEXT("Satellite collision blocks the bird"),
		Runtime->IsSatelliteCollisionEnabled());
	TestTrue(TEXT("E5 cube collision blocks the bird"),
		Runtime->IsE5CollisionEnabled());
	TestTrue(TEXT("M6 consumes the exact E5 snapshot"),
		Runtime->IsM6TargetBound());
	TestTrue(TEXT("Runtime layout passes the gravity-dependent trajectory gate"),
		Runtime->IsTrajectoryCertified());
	TestTrue(TEXT("A real reinforced slingshot is grounded from the candidate cells"),
		Runtime->IsPracticeSlingshotReady());
	TestTrue(TEXT("The assembled practice slingshot exposes only its pouch to cursor interaction"),
		Runtime->IsPracticePouchInteractionReady());
	const FABTSM3MonthlySatelliteRuntimeSnapshot
		RuntimeSnapshotBeforeSemanticQuery = Runtime->GetRuntimeSnapshot();
	const bool bGravityBeforeSemanticQuery =
		Runtime->IsSatelliteGravityEnabled();
	TArray<FABTSM3StylizedSemanticBinding> RuntimeSemanticsFirst;
	TArray<FABTSM3StylizedSemanticBinding> RuntimeSemanticsSecond;
	FABTSM3StylizedSemanticAdapter::GatherMonthlyPracticeSemantics(
		*Runtime,
		RuntimeSemanticsFirst);
	FABTSM3StylizedSemanticAdapter::GatherMonthlyPracticeSemantics(
		*Runtime,
		RuntimeSemanticsSecond);
	TestEqual(TEXT("Production practice publishes satellite and E5 actors"),
		RuntimeSemanticsFirst.Num(),
		2);
	TestEqual(TEXT("Repeated production semantic query is deterministic"),
		RuntimeSemanticsSecond.Num(),
		RuntimeSemanticsFirst.Num());
	for (int32 SemanticIndex = 0;
		SemanticIndex < RuntimeSemanticsFirst.Num()
			&& SemanticIndex < RuntimeSemanticsSecond.Num();
		++SemanticIndex)
	{
		TestTrue(TEXT("Production semantic binding is valid"),
			RuntimeSemanticsFirst[SemanticIndex].IsValid());
		TestEqual(TEXT("Production semantic is SatelliteTarget"),
			RuntimeSemanticsFirst[SemanticIndex].ObjectClass,
			EABTSStylizedObjectClass::SatelliteTarget);
		TestTrue(TEXT("Repeated production query keeps the exact Actor"),
			RuntimeSemanticsFirst[SemanticIndex].Actor
				== RuntimeSemanticsSecond[SemanticIndex].Actor);
		TestEqual(TEXT("Repeated production query keeps the exact source"),
			RuntimeSemanticsFirst[SemanticIndex].Source,
			RuntimeSemanticsSecond[SemanticIndex].Source);
	}
	if (RuntimeSemanticsFirst.Num() == 2)
	{
		TestTrue(TEXT("Production satellite semantic uses the runtime Actor"),
			RuntimeSemanticsFirst[0].Actor
				== Runtime->GetRuntimeSatellite());
		TestTrue(TEXT("Production E5 semantic uses the runtime Actor"),
			RuntimeSemanticsFirst[1].Actor
				== Runtime->GetRuntimeE5Target());
	}
	FABTSM3StylizedSemanticBinding UnrelatedRuntimeBinding;
	TestFalse(TEXT("Unrelated M6 actor under runtime authority fails closed"),
		FABTSM3StylizedSemanticAdapter::TryResolveActor(
			*Runtime,
			*SlingshotSystem,
			UnrelatedRuntimeBinding));
	TestEqual(TEXT("Production semantic query preserves runtime layout hash"),
		Runtime->GetRuntimeSnapshot().RuntimeLayoutSnapshotHash,
		RuntimeSnapshotBeforeSemanticQuery.RuntimeLayoutSnapshotHash);
	TestEqual(TEXT("Production semantic query preserves source result hash"),
		Runtime->GetRuntimeSnapshot().SourcePreviewResultHash,
		RuntimeSnapshotBeforeSemanticQuery.SourcePreviewResultHash);
	TestEqual(TEXT("Production semantic query preserves gravity authority"),
		Runtime->IsSatelliteGravityEnabled(),
		bGravityBeforeSemanticQuery);
	AABTSM51SlingshotStake* PracticeStakeA =
		Runtime->GetRuntimePracticeStakeA();
	AABTSM51SlingshotStake* PracticeStakeB =
		Runtime->GetRuntimePracticeStakeB();
	TestNotNull(TEXT("First reinforced stake exists"), PracticeStakeA);
	TestNotNull(TEXT("Second reinforced stake exists"), PracticeStakeB);
	AABTSM51SlingshotCord* PracticeCord = Runtime->GetRuntimePracticeCord();
	TestNotNull(TEXT("Cell-grounded practice slingshot exposes its runtime cord"),
		PracticeCord);
	if (PracticeCord != nullptr)
	{
		TestEqual(TEXT("Cell-grounded practice cord is reinforced"),
			PracticeCord->GetSlingshotTier(),
			EABTSSlingshotTier::Reinforced);
	}
	for (const AABTSM51SlingshotStake* PracticeStake :
		{ PracticeStakeA, PracticeStakeB })
	{
		if (PracticeStake == nullptr)
		{
			continue;
		}
		TInlineComponentArray<UPrimitiveComponent*> StakeComponents;
		PracticeStake->GetComponents(StakeComponents);
		for (const UPrimitiveComponent* Component : StakeComponents)
		{
			if (Component != nullptr)
			{
				TestTrue(TEXT("Preassembled practice stakes cannot occlude the pouch click"),
					Component->GetCollisionResponseToChannel(ECC_Visibility)
						!= ECR_Block);
			}
		}
	}

	const FABTSM3MonthlySatelliteRuntimeSnapshot Snapshot =
		Runtime->GetRuntimeSnapshot();
	TestTrue(TEXT("Session layout snapshot is valid"), Snapshot.bValid);
	TestEqual(TEXT("Session snapshot retains the candidate identity"),
		Snapshot.SourceCandidateHash,
		Candidate.CandidateHash);
	TestEqual(TEXT("Live production M6 hash matches the preview witness"),
		Snapshot.ProductionLaunchProfileHash,
		Candidate.LaunchProfileHash);
	TestTrue(TEXT("Trajectory certification is persisted"),
		Snapshot.bTrajectoryCertified);
	TestTrue(TEXT("Certification finds gravity-on hits"),
		Snapshot.GravityOnHits > 0);
	TestTrue(TEXT("Certification finds gravity-dependent hits"),
		Snapshot.GravityDependentHits > 0);
	TestTrue(TEXT("Certification retains a connected success island"),
		Snapshot.LargestSuccessIslandSamples >= 3);
	TestTrue(TEXT("Certified gravity-off witness misses by the frozen margin"),
		Snapshot.MinimumGravityOffMissCM >= 60.0f);
	TestNotEqual(TEXT("Trajectory certification hash is persisted"),
		Snapshot.TrajectoryCertificationHash,
		static_cast<int64>(0));
	TestTrue(TEXT("Runtime satellite stays joined to the preview candidate"),
		Snapshot.SatellitePreviewRuntimeDeltaCM <= 250.0f);
	TestEqual(TEXT("Runtime keeps the first selected terrain cell"),
		Snapshot.PracticeStakeACellId,
		Candidate.ReferenceSlotACellId);
	TestEqual(TEXT("Runtime keeps the second selected terrain cell"),
		Snapshot.PracticeStakeBCellId,
		Candidate.ReferenceSlotBCellId);
	if (PracticeStakeA != nullptr)
	{
		TestTrue(TEXT("First reinforced stake bottom is on its queried surface"),
			PracticeStakeA->GetVisualBottomWorldLocation().Equals(
				Snapshot.PracticeStakeASurfaceWorld,
				1.0f));
	}
	if (PracticeStakeB != nullptr)
	{
		TestTrue(TEXT("Second reinforced stake bottom is on its queried surface"),
			PracticeStakeB->GetVisualBottomWorldLocation().Equals(
				Snapshot.PracticeStakeBSurfaceWorld,
				1.0f));
	}
	if (PracticeCord != nullptr)
	{
		TestTrue(TEXT("Runtime snapshot stores the physical rest pouch frame"),
			Snapshot.PracticeLaunchWorldTransform.Equals(
				PracticeCord->GetRestPouchTransform(),
				0.1f));
	}
	TestNotNull(TEXT("Cell-grounded layout spawns the corresponding satellite"),
		Runtime->GetRuntimeSatellite());
	TestNotNull(TEXT("Cell-grounded layout spawns the corresponding E5 target"),
		Runtime->GetRuntimeE5Target());
	if (Runtime->GetRuntimeSatellite() != nullptr)
	{
		TestTrue(TEXT("Snapshot center matches the corresponding runtime satellite"),
			Snapshot.SatelliteWorldTransform.Equals(
				Runtime->GetRuntimeSatellite()->GetActorTransform(),
				0.1f));
		TestTrue(TEXT("Physical reinforced slingshot faces the satellite within five degrees"),
			Snapshot.SatelliteFacingErrorDegrees <= 5.0f);
		TestTrue(TEXT("Runtime facing stays joined to the candidate terrain compensation"),
			FMath::IsNearlyEqual(
				Snapshot.SatelliteFacingCorrectionAzimuthDegrees,
				Candidate.SatelliteFacingCorrectionAzimuthDegrees,
				0.001f));
	}
	if (Runtime->GetRuntimeE5Target() != nullptr)
	{
		TestTrue(TEXT("Snapshot transform matches the corresponding runtime E5 target"),
			Snapshot.E5WorldTransform.Equals(
				Runtime->GetRuntimeE5Target()->GetActorTransform(),
				0.1f));
	}
	TestNotEqual(TEXT("Baseline gravity snapshot hash is persisted"),
		Snapshot.BaselineGravitySnapshotHash,
		static_cast<int64>(0));
	TestNotEqual(TEXT("Joined runtime layout hash is persisted"),
		Snapshot.RuntimeLayoutSnapshotHash,
		static_cast<int64>(0));
	TestFalse(TEXT("Override 0 disables the real satellite gravity source"),
		Runtime->IsSatelliteGravityEnabled());
	const FVector Probe = Snapshot.SatelliteWorldTransform.GetLocation()
		+ FVector::UpVector * (Snapshot.SatelliteRadiusCM * 2.0f);
	TestTrue(TEXT("Gravity query is zero while the shared cvar is off"),
		ABTSM9Gravity::GetSatelliteAcceleration(World, Probe).IsNearlyZero());

	GravityCVar->Set(1, ECVF_SetByCode);
	Runtime->Tick(0.1f);
	TestTrue(TEXT("Override 1 enables the real satellite gravity source"),
		Runtime->IsSatelliteGravityEnabled());
	TestFalse(TEXT("Gravity query uses the spawned frozen satellite when enabled"),
		ABTSM9Gravity::GetSatelliteAcceleration(World, Probe).IsNearlyZero());
	TestEqual(TEXT("Gravity toggle never mutates the layout snapshot"),
		Runtime->GetRuntimeSnapshot().RuntimeLayoutSnapshotHash,
		Snapshot.RuntimeLayoutSnapshotHash);

	AABTSM9Satellite* BoundSatellite = nullptr;
	AActor* BoundTarget = nullptr;
	FVector BoundHalfExtent = FVector::ZeroVector;
	TestTrue(TEXT("M6 exposes the configured satellite practice target"),
		SlingshotSystem->CopySatellitePracticeTarget(
			BoundSatellite,
			BoundTarget,
			BoundHalfExtent));
	TestTrue(TEXT("M6 satellite is the runtime snapshot actor"),
		BoundSatellite == Runtime->GetRuntimeSatellite());
	TestTrue(TEXT("M6 E5 target is the runtime snapshot actor"),
		BoundTarget == Runtime->GetRuntimeE5Target());
	TestTrue(TEXT("M6 target extent is the frozen E5 extent"),
		BoundHalfExtent.Equals(Candidate.E5TargetHalfExtentCM, 0.1f));

	GravityCVar->Set(OriginalGravityOverride, ECVF_SetByCode);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM3R51ScoutMapPresentationAuthorityTest,
	"ABTS.M3.Monthly.SatellitePreview.04ScoutMapPresentationAuthority",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM3R51ScoutMapPresentationAuthorityTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	using namespace ABTSM3R51SatellitePreviewTests;
	FScopedTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Transient test World is created"), World);
	if (World == nullptr)
	{
		return false;
	}

	AABTSM3Planet* Planet = SpawnPreviewPlanet(*this, *World);
	if (Planet == nullptr || !Planet->RebuildPlanet())
	{
		AddError(TEXT("Compatibility fixture failed to rebuild"));
		return false;
	}

	const TArray<FABTSM3CellState>& CompatibilityStates =
		Planet->GetGeneratedCellStates();
	const FABTSM3MonthlyCandidatePresentation* SelectedCandidate =
		nullptr;
	for (const FABTSM3MonthlyCandidatePresentation& Candidate :
		Planet->GetMonthlyPresentationResult().CandidatePresentations)
	{
		const bool bHasDiscriminatingLandCell =
			Candidate.Cells.ContainsByPredicate(
				[&CompatibilityStates](
					const FABTSM3MonthlyPresentationCell& Cell)
				{
					return CompatibilityStates.IsValidIndex(Cell.CellId)
						&& Cell.ActiveRoleMask == 0
						&& !Cell.bWater
						&& !CompatibilityStates[Cell.CellId].bWater
						&& Cell.VisualTerrainType
							!= EABTSM3TerrainType::Water
						&& CompatibilityStates[Cell.CellId].TerrainType
							!= EABTSM3TerrainType::Water
						&& Cell.VisualTerrainType
							!= CompatibilityStates[Cell.CellId].TerrainType;
				});
		if (bHasDiscriminatingLandCell)
		{
			SelectedCandidate = &Candidate;
			break;
		}
	}
	TestNotNull(
		TEXT("Fixture contains a candidate whose presented land differs from compatibility terrain"),
		SelectedCandidate);
	if (SelectedCandidate == nullptr)
	{
		return false;
	}

	const int32 SelectedCandidateId =
		SelectedCandidate->SourceRouteCandidateId;
	Planet->bEnableMonthlyPresentationPreview = true;
	Planet->MonthlyPresentationPreviewCandidateId =
		SelectedCandidateId;
	TestTrue(TEXT("Candidate preview rebuild succeeds"),
		Planet->RebuildPlanet());
	TestTrue(TEXT("Candidate preview becomes the presentation authority"),
		Planet->IsMonthlyPresentationPreviewActive());
	TestEqual(TEXT("Preview keeps the selected candidate identity"),
		Planet->GetMonthlyPresentationPreviewCandidateId(),
		SelectedCandidateId);

	const FABTSM3MonthlyCandidatePresentation* ActiveCandidate =
		FABTSM3MonthlyPresentationBuilder::FindCandidatePresentation(
			Planet->GetMonthlyPresentationResult(),
			SelectedCandidateId);
	TestNotNull(TEXT("Active candidate remains available after rebuild"),
		ActiveCandidate);
	if (ActiveCandidate == nullptr)
	{
		return false;
	}

	int32 DiscriminatingSamples = 0;
	int32 PresentedPaletteMatches = 0;
	for (const FABTSM3MonthlyPresentationCell& Cell :
		ActiveCandidate->Cells)
	{
		if (!Planet->LogicalCells.IsValidIndex(Cell.CellId)
			|| !CompatibilityStates.IsValidIndex(Cell.CellId)
			|| Cell.ActiveRoleMask != 0
			|| Cell.bWater
			|| CompatibilityStates[Cell.CellId].bWater
			|| Cell.VisualTerrainType == EABTSM3TerrainType::Water
			|| CompatibilityStates[Cell.CellId].TerrainType
				== EABTSM3TerrainType::Water
			|| Cell.VisualTerrainType
				== CompatibilityStates[Cell.CellId].TerrainType)
		{
			continue;
		}
		++DiscriminatingSamples;
		FLinearColor ScoutColor = FLinearColor::Black;
		int32 ResolvedCellId = INDEX_NONE;
		if (!Planet->QueryScoutMapTerrainColor(
				Planet->LogicalCells[Cell.CellId].UnitCenter,
				ScoutColor,
				Cell.CellId,
				&ResolvedCellId)
			|| ResolvedCellId != Cell.CellId)
		{
			continue;
		}
		const FLinearColor PresentedColor =
			FABTSM3TerrainVisualField::GetTerrainBaseColor(
				Cell.VisualTerrainType);
		const FLinearColor CompatibilityColor =
			FABTSM3TerrainVisualField::GetTerrainBaseColor(
				CompatibilityStates[Cell.CellId].TerrainType);
		if (ScoutColor.Equals(PresentedColor, 0.0001f)
			&& !ScoutColor.Equals(CompatibilityColor, 0.0001f))
		{
			++PresentedPaletteMatches;
		}
	}
	TestTrue(TEXT("Preview exposes discriminating scout-map samples"),
		DiscriminatingSamples > 0);
	TestTrue(
		TEXT("Scout map samples the selected presentation palette instead of the compatibility candidate"),
		PresentedPaletteMatches > 0);
	AddInfo(FString::Printf(
		TEXT("ScoutMapPresentationAuthority Candidate=%d Samples=%d PresentedMatches=%d"),
		SelectedCandidateId,
		DiscriminatingSamples,
		PresentedPaletteMatches));
	return true;
}

#endif
