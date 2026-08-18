// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "PCG/ABTSM3MonthlySlingshotField.h"
#include "PCG/ABTSM3TaskGraphTypes.h"
#include "Planet/ABTSM2Planet.h"
#include "World/ABTSM51OrdinarySlingshotSlotPreview.h"
#include "World/ABTSM51WorldActors.h"
#include "World/ABTSM51WorldSystem.h"

namespace
{
class FScopedM51SlotPreviewWorld
{
public:
	FScopedM51SlotPreviewWorld()
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
			TEXT("ABTSM51SlotPreviewAutomationWorld"),
			nullptr,
			true,
			ERHIFeatureLevel::Num,
			&Values);
	}

	~FScopedM51SlotPreviewWorld()
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

FABTSM3MonthlySlingshotFieldCandidate MakeCandidate(
	const int32 CandidateId,
	const int32 FirstCellId,
	const int64 CandidateHash)
{
	FABTSM3MonthlySlingshotFieldCandidate Candidate;
	Candidate.SourceRouteCandidateId = CandidateId;
	Candidate.SourceSpatialCandidateHash = CandidateHash + 1;
	Candidate.CandidateHash = CandidateHash;
	for (int32 FieldIndex = 0; FieldIndex < 8; ++FieldIndex)
	{
		FABTSM3MonthlySlingshotField& Field =
			Candidate.Fields.AddDefaulted_GetRef();
		Field.FieldId = FieldIndex;
		Field.AnchorCellId = FirstCellId + FieldIndex * 7;
		for (int32 SlotIndex = 0; SlotIndex < 7; ++SlotIndex)
		{
			Field.SlotCellIds.Add(
				FirstCellId + FieldIndex * 7 + SlotIndex);
		}
		Candidate.TotalSlotCount += Field.SlotCellIds.Num();
	}
	return Candidate;
}

FABTSM3MonthlySlingshotFieldResult MakePreviewResult()
{
	FABTSM3MonthlySlingshotFieldResult Result;
	Result.WorldSeed = 312503;
	Result.ResultHash = 0x123456789ABCDEll;
	Result.MaxCordLengthCM = 1200;
	Result.FieldsPerCandidate = 8;
	Result.SlotsPerCandidate = 56;
	Result.bSlingshotFieldResultValid = true;
	Result.bMonthlyWorldAccepted = false;
	Result.RejectReason =
		EABTSM3MonthlySlingshotFieldRejectReason::None;
	// Candidate 9 intentionally precedes Candidate 4. The adapter must perform
	// an identity join and may not consume RetainedCandidates[0].
	Result.RetainedCandidates.Add(
		MakeCandidate(9, 900, 0x9009));
	Result.RetainedCandidates.Add(
		MakeCandidate(4, 100, 0x4004));
	return Result;
}

bool SnapshotsEqual(
	const FABTSM51OrdinarySlingshotSlotSnapshot& A,
	const FABTSM51OrdinarySlingshotSlotSnapshot& B)
{
	if (A.LayoutHash != B.LayoutHash
		|| A.CandidateHash != B.CandidateHash
		|| A.MaxCordLengthCM != B.MaxCordLengthCM
		|| A.SlotGroups.Num() != B.SlotGroups.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < A.SlotGroups.Num(); ++Index)
	{
		if (A.SlotGroups[Index].SlotCellIds
			!= B.SlotGroups[Index].SlotCellIds)
		{
			return false;
		}
	}
	return true;
}

void BuildReleaseTraversalFixture(
	TArray<FABTSM2Cell>& OutCells,
	TArray<FABTSM3CellState>& OutStates,
	FABTSM51OrdinarySlingshotSlotSnapshot& OutSnapshot)
{
	constexpr int32 CellCount = 60;
	OutCells.SetNum(CellCount);
	OutStates.SetNum(CellCount);
	for (int32 CellId = 0; CellId < CellCount; ++CellId)
	{
		if (CellId > 0)
		{
			OutCells[CellId].NeighborCellIds.Add(CellId - 1);
		}
		if (CellId + 1 < CellCount)
		{
			OutCells[CellId].NeighborCellIds.Add(CellId + 1);
		}
		OutStates[CellId].bWater = true;
	}
	for (int32 CellId = 7; CellId <= 18; ++CellId)
	{
		OutStates[CellId].bWater = false;
		OutStates[CellId].bBuildable = true;
	}
	for (int32 CellId = 41; CellId <= 52; ++CellId)
	{
		OutStates[CellId].bWater = false;
		OutStates[CellId].bBuildable = true;
	}

	OutSnapshot.LayoutHash = 0x51A0ull;
	OutSnapshot.CandidateHash = 0x51B0ull;
	OutSnapshot.MaxCordLengthCM = 1200;
	OutSnapshot.SlotGroups.AddDefaulted_GetRef().SlotCellIds =
		{0, 1, 2, 3, 4, 5, 6};
	OutSnapshot.SlotGroups.AddDefaulted_GetRef().SlotCellIds =
		{59, 58, 57, 56, 55, 54, 53};
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM51OrdinarySlingshotSlotPreviewAdapterTest,
	"ABTS.M51.OrdinarySlots.PreviewAdapter",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM51OrdinarySlingshotSlotPreviewAdapterTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FABTSM3MonthlySlingshotFieldResult Source =
		MakePreviewResult();
	const FABTSM3MonthlySlingshotFieldResult SourceBefore = Source;
	FABTSM51OrdinarySlingshotSlotSnapshot First;
	FABTSM51OrdinarySlingshotSlotSnapshot Second;
	FString Failure;
	TestTrue(
		TEXT("An explicit non-zero candidate can be adapted for Preview/Test"),
		FABTSM51OrdinarySlingshotSlotPreviewAdapter::
			BuildFromExplicitCandidate(Source, 4, First, Failure));
	TestTrue(
		TEXT("Repeated adaptation succeeds"),
		FABTSM51OrdinarySlingshotSlotPreviewAdapter::
			BuildFromExplicitCandidate(Source, 4, Second, Failure));
	TestTrue(
		TEXT("Repeated adaptation is deterministic"),
		SnapshotsEqual(First, Second));
	TestEqual(
		TEXT("The explicit Candidate 4 identity is used instead of array element zero"),
		First.CandidateHash,
		static_cast<uint64>(0x4004));
	TestEqual(
		TEXT("All eight current ordinary slot fields are retained"),
		First.SlotGroups.Num(),
		8);
	TArray<int32> FlattenedCells;
	TestTrue(
		TEXT("The Preview/Test snapshot flattens against CellTopo"),
		First.TryBuildCellList(1000, FlattenedCells));
	TestEqual(
		TEXT("The current candidate contributes exactly 56 ordinary slots"),
		FlattenedCells.Num(),
		56);
	TestEqual(
		TEXT("The M3 maximum cord length crosses the snapshot boundary exactly"),
		First.MaxCordLengthCM,
		1200);
	TestFalse(
		TEXT("Adapting a Preview/Test candidate never accepts the monthly world"),
		Source.bMonthlyWorldAccepted);
	TestTrue(
		TEXT("The Preview/Test adapter does not mutate or promote its M3 source"),
		FABTSM3MonthlySlingshotFieldResult::StaticStruct()
			->CompareScriptStruct(&Source, &SourceBefore, 0));

	TArray<FABTSM2Cell> ReleaseCells;
	TArray<FABTSM3CellState> ReleaseStates;
	FABTSM51OrdinarySlingshotSlotSnapshot ReleaseSnapshot;
	BuildReleaseTraversalFixture(
		ReleaseCells,
		ReleaseStates,
		ReleaseSnapshot);
	const FABTSM51OrdinarySlingshotSlotSnapshot ReleaseBefore =
		ReleaseSnapshot;
	int32 RemovedInvalidSlots = 0;
	int32 AddedSlots = 0;
	TestTrue(
		TEXT("Production adaptation traverses beyond wholly invalid group roots"),
		FABTSM51OrdinarySlingshotSlotReleaseAdapter::
			AdaptToProductionSurface(
				ReleaseCells,
				ReleaseStates,
				INDEX_NONE,
				ReleaseSnapshot,
				Failure,
				&RemovedInvalidSlots,
				&AddedSlots));
	TestEqual(
		TEXT("Both release groups remain present"),
		ReleaseSnapshot.SlotGroups.Num(),
		2);
	for (int32 GroupIndex = 0;
		GroupIndex < ReleaseSnapshot.SlotGroups.Num();
		++GroupIndex)
	{
		TestEqual(
			*FString::Printf(
				TEXT("Release group %d reaches the required capacity"),
				GroupIndex),
			ReleaseSnapshot.SlotGroups[GroupIndex].SlotCellIds.Num(),
			FABTSM51OrdinarySlingshotSlotReleaseAdapter::
				RequiredSlotsPerGroup);
	}
	TestEqual(
		TEXT("All invalid authored roots are removed"),
		RemovedInvalidSlots,
		14);
	TestEqual(
		TEXT("The topology walk contributes every release slot"),
		AddedSlots,
		24);
	TestEqual(
		TEXT("Production adaptation preserves the 1200 cm cord authority"),
		ReleaseSnapshot.MaxCordLengthCM,
		1200);
	TestTrue(
		TEXT("Adapted release snapshot remains structurally usable"),
		ReleaseSnapshot.IsStructurallyUsable());

	TArray<FABTSM2Cell> ImpossibleCells = ReleaseCells;
	TArray<FABTSM3CellState> ImpossibleStates = ReleaseStates;
	for (FABTSM3CellState& State : ImpossibleStates)
	{
		State.bWater = true;
		State.bBuildable = false;
	}
	FABTSM51OrdinarySlingshotSlotSnapshot Impossible =
		ReleaseBefore;
	TestFalse(
		TEXT("Insufficient production terrain still fails closed"),
		FABTSM51OrdinarySlingshotSlotReleaseAdapter::
			AdaptToProductionSurface(
				ImpossibleCells,
				ImpossibleStates,
				INDEX_NONE,
				Impossible,
				Failure));
	TestTrue(
		TEXT("Failed production adaptation is atomic"),
		SnapshotsEqual(Impossible, ReleaseBefore));

	FABTSM51OrdinarySlingshotSlotSnapshot Missing;
	TestFalse(
		TEXT("Array index zero is not an implicit candidate request"),
		FABTSM51OrdinarySlingshotSlotPreviewAdapter::
			BuildFromExplicitCandidate(Source, INDEX_NONE, Missing, Failure));
	TestEqual(
		TEXT("Missing explicit identity has a stable rejection"),
		Failure,
		FString(TEXT("ExplicitCandidateRequired")));

	FScopedM51SlotPreviewWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Preview/Test automation World is created"), World);
	if (World == nullptr)
	{
		return false;
	}
	AABTSM51WorldSystem* System =
		World->SpawnActorDeferred<AABTSM51WorldSystem>(
			AABTSM51WorldSystem::StaticClass(),
			FTransform::Identity,
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	TestNotNull(
		TEXT("M5.1 WorldSystem supports a deferred pre-BeginPlay phase"),
		System);
	if (System != nullptr)
	{
		TestTrue(
			TEXT("Preview/Test snapshot configures before BeginPlay"),
			System->ConfigurePreviewOrdinarySlingshotSlotSnapshot(First));
		TestEqual(
			TEXT("WorldSystem records Preview/Test rather than AcceptedMonthly authority"),
			static_cast<int32>(System->GetOrdinarySlotSnapshotAuthority()),
			static_cast<int32>(
				EABTSM51OrdinarySlingshotSlotSnapshotAuthority::PreviewTest));
		TestEqual(
			TEXT("M6 receives the exact Preview/Test maximum cord length"),
			System->GetActiveOrdinaryMaxCordLengthCM(),
			1200);
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AABTSM51SlingshotDirtHole* Standard =
		World->SpawnActor<AABTSM51SlingshotDirtHole>(
			AABTSM51SlingshotDirtHole::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	AABTSM51SlingshotDirtHole* Finale =
		World->SpawnActor<AABTSM51SlingshotDirtHole>(
			AABTSM51SlingshotDirtHole::StaticClass(),
			FTransform::Identity,
			SpawnParameters);
	TestNotNull(TEXT("Standard slot Actor spawns"), Standard);
	TestNotNull(TEXT("Finale slot Actor spawns"), Finale);
	if (Standard != nullptr && Finale != nullptr)
	{
		Standard->InitializeHole(10);
		Finale->InitializeFinaleSpaceSlot(
			20,
			51,
			EABTSSlingshotSlotSide::Left);
		UStaticMeshComponent* StandardVisual =
			Standard->FindComponentByClass<UStaticMeshComponent>();
		UStaticMeshComponent* FinaleVisual =
			Finale->FindComponentByClass<UStaticMeshComponent>();
		TestNotNull(TEXT("Standard slot exposes a visual"), StandardVisual);
		TestNotNull(TEXT("Finale slot exposes a visual"), FinaleVisual);
		if (StandardVisual != nullptr && FinaleVisual != nullptr)
		{
			UStaticMesh* StandardMesh =
				StandardVisual->GetStaticMesh().Get();
			UStaticMesh* FinaleMesh =
				FinaleVisual->GetStaticMesh().Get();
			TestNotNull(
				TEXT("Standard slot keeps the dirt mesh"),
				StandardMesh);
			TestNotNull(
				TEXT("Finale slot loads the steel mesh"),
				FinaleMesh);
			if (StandardMesh == nullptr || FinaleMesh == nullptr)
			{
				return false;
			}
			TestEqual(
				TEXT("Standard slot mesh path stays dirt"),
				StandardMesh->GetPathName(),
				FString(TEXT("/Game/StaticMesh/SlingshotDirtHole/SM_SlingshotDitHole.SM_SlingshotDitHole")));
			TestEqual(
				TEXT("FinaleSpace selects the authored steel mesh"),
				FinaleMesh->GetPathName(),
				FString(TEXT("/Game/StaticMesh/SlingshotSteelHole/SM_SlingshotSteelHole.SM_SlingshotSteelHole")));
			TestNotEqual(
				TEXT("Ordinary and Finale slots never share a mesh"),
				StandardMesh,
				FinaleMesh);
			UMaterialInterface* StandardMaterial =
				StandardVisual->GetMaterial(0);
			UMaterialInterface* FinaleMaterial =
				FinaleVisual->GetMaterial(0);
			TestNotNull(
				TEXT("Standard slot keeps the dirt material"),
				StandardMaterial);
			TestNotNull(
				TEXT("Finale slot loads the steel material"),
				FinaleMaterial);
			if (StandardMaterial != nullptr && FinaleMaterial != nullptr)
			{
				TestEqual(
					TEXT("FinaleSpace selects the authored steel material"),
					FinaleMaterial->GetPathName(),
					FString(TEXT("/Game/StaticMesh/SlingshotSteelHole/M_SlingshotSteelHole.M_SlingshotSteelHole")));
				TestNotEqual(
					TEXT("Ordinary and Finale slots never share a material"),
					StandardMaterial,
					FinaleMaterial);
			}
		}
	}
	return true;
}

#endif
