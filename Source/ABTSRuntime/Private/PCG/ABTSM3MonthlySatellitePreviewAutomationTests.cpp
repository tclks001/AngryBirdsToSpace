// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Calibration/ABTSSlingshotSatelliteCalibrationTypes.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3MonthlySatellitePreview.h"
#include "Terrain/ABTSM3Planet.h"

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
	}

	~FScopedTestWorld()
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

#endif
