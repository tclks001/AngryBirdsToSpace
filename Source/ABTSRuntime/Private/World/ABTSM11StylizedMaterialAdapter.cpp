// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11StylizedMaterialAdapter.h"

#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/ABTSStylizedMaterialContract.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"
#include "World/ABTSM11FinaleActors.h"
#include "World/ABTSM11FinaleSystem.h"

namespace ABTSM11StylizedMaterialAdapterPrivate
{
	const TSoftObjectPtr<UMaterialInterface> Assist1PlanetMaterial(
		FSoftObjectPath(TEXT(
			"/Game/M11/Toon/Planets/Mars/MI_Mars_FinalePlanet."
			"MI_Mars_FinalePlanet")));
	const TSoftObjectPtr<UMaterialInterface> Assist2PlanetMaterial(
		FSoftObjectPath(TEXT(
			"/Game/M11/Toon/Planets/Jupiter/MI_Jupiter_FinalePlanet."
			"MI_Jupiter_FinalePlanet")));
	const TSoftObjectPtr<UMaterialInterface> Assist3PlanetMaterial(
		FSoftObjectPath(TEXT(
			"/Game/M11/Toon/Planets/Saturn/MI_Saturn_FinalePlanet."
			"MI_Saturn_FinalePlanet")));
	const TSoftObjectPtr<UMaterialInterface> UFOMaterial(
		FSoftObjectPath(TEXT(
			"/Game/M11/Toon/UFO/MI_UFO_FinaleUFO."
			"MI_UFO_FinaleUFO")));

	struct FCandidate
	{
		UPrimitiveComponent* Component = nullptr;
		UMaterialInterface* Material = nullptr;
		EABTSStylizedMaterialFamily Family =
			EABTSStylizedMaterialFamily::None;
		int32 SemanticOrder = MAX_int32;
		int32 StableId = INDEX_NONE;
	};

	bool IsM11ReversibleFamily(
		const EABTSStylizedMaterialFamily Family)
	{
		return FABTSStylizedMaterialContract::ResolveOwner(Family)
				== EABTSStylizedMaterialOwner::M11
			&& FABTSStylizedMaterialContract::ResolveAdoptionMode(Family)
				== EABTSStylizedMaterialAdoptionMode::ReversibleSlotOverride;
	}

	bool TryBuildCandidate(
		const AABTSM11FinaleSystem& FinaleSystem,
		AActor* Actor,
		const FABTSM11StylizedMaterialSet& Materials,
		FCandidate& OutCandidate)
	{
		OutCandidate = FCandidate{};
		if (!IsValid(Actor))
		{
			return false;
		}

		EABTSStylizedObjectClass ObjectClass =
			EABTSStylizedObjectClass::None;
		if (!FinaleSystem.TryGetStylizedObjectClass(*Actor, ObjectClass))
		{
			return false;
		}

		if (ObjectClass == EABTSStylizedObjectClass::FinalePlanet)
		{
			AABTSM11GravityBodyActor* BodyActor =
				Cast<AABTSM11GravityBodyActor>(Actor);
			if (!IsValid(BodyActor))
			{
				return false;
			}

			int32 AssistIndex = INDEX_NONE;
			for (int32 CandidateIndex = 1;
				CandidateIndex <= FABTSM11GravityScenario::AssistCount;
				++CandidateIndex)
			{
				if (FinaleSystem.GetLayoutPreset()
						.CanonicalScenario.GetAssist(CandidateIndex).BodyId
					== BodyActor->GetStableBodyId())
				{
					AssistIndex = CandidateIndex;
					break;
				}
			}

			UMaterialInterface* Material =
				Materials.GetAssistPlanetMaterial(AssistIndex);
			UStaticMeshComponent* VisualMesh =
				BodyActor->GetVisualMeshComponent();
			if (!IsValid(Material)
				|| !IsValid(VisualMesh)
				|| VisualMesh->GetOwner() != BodyActor)
			{
				return false;
			}

			OutCandidate.Component = VisualMesh;
			OutCandidate.Material = Material;
			OutCandidate.Family =
				EABTSStylizedMaterialFamily::FinalePlanet;
			OutCandidate.SemanticOrder = 0;
			OutCandidate.StableId = BodyActor->GetStableBodyId();
			return IsM11ReversibleFamily(OutCandidate.Family);
		}

		if (ObjectClass != EABTSStylizedObjectClass::FinaleUFO)
		{
			return false;
		}

		AABTSM11UFOActor* UFOActor = Cast<AABTSM11UFOActor>(Actor);
		UStaticMeshComponent* VisualMesh = IsValid(UFOActor)
			? UFOActor->GetVisualMeshComponent()
			: nullptr;
		if (!IsValid(Materials.UFOMaterial)
			|| !IsValid(VisualMesh)
			|| VisualMesh->GetOwner() != UFOActor)
		{
			return false;
		}

		OutCandidate.Component = VisualMesh;
		OutCandidate.Material = Materials.UFOMaterial;
		OutCandidate.Family = EABTSStylizedMaterialFamily::FinaleUFO;
		OutCandidate.SemanticOrder = 1;
		OutCandidate.StableId = UFOActor->GetStableTargetId();
		return IsM11ReversibleFamily(OutCandidate.Family);
	}

	void CollectFromActorSet(
		const AABTSM11FinaleSystem& FinaleSystem,
		TConstArrayView<AActor*> RuntimePresentationActors,
		const FABTSM11StylizedMaterialSet& Materials,
		TArray<FABTSStylizedMaterialSlotBinding>& OutBindings)
	{
		OutBindings.Reset();
		if (!FinaleSystem.IsLayoutReady())
		{
			return;
		}

		TArray<FCandidate> Candidates;
		Candidates.Reserve(RuntimePresentationActors.Num());
		for (AActor* Actor : RuntimePresentationActors)
		{
			FCandidate Candidate;
			if (TryBuildCandidate(
					FinaleSystem,
					Actor,
					Materials,
					Candidate))
			{
				Candidates.Add(Candidate);
			}
		}

		Candidates.Sort([](const FCandidate& A, const FCandidate& B)
		{
			if (A.SemanticOrder != B.SemanticOrder)
			{
				return A.SemanticOrder < B.SemanticOrder;
			}
			if (A.StableId != B.StableId)
			{
				return A.StableId < B.StableId;
			}
			return false;
		});

		TSet<UPrimitiveComponent*> PublishedComponents;
		for (const FCandidate& Candidate : Candidates)
		{
			if (!IsValid(Candidate.Component)
				|| PublishedComponents.Contains(Candidate.Component))
			{
				continue;
			}
			PublishedComponents.Add(Candidate.Component);

			const int32 MaterialSlotCount =
				Candidate.Component->GetNumMaterials();
			for (int32 SlotIndex = 0;
				SlotIndex < MaterialSlotCount;
				++SlotIndex)
			{
				FABTSStylizedMaterialSlotBinding Binding;
				Binding.Component = Candidate.Component;
				Binding.MaterialSlotIndex = SlotIndex;
				Binding.StylizedMaterial = Candidate.Material;
				Binding.Family = Candidate.Family;
				if (SlotIndex < Candidate.Component->GetNumMaterials()
					&& Binding.IsValid())
				{
					OutBindings.Add(Binding);
				}
			}
		}
	}
}

UMaterialInterface* FABTSM11StylizedMaterialSet::GetAssistPlanetMaterial(
	const int32 AssistIndex) const
{
	switch (AssistIndex)
	{
	case 1: return Assist1PlanetMaterial;
	case 2: return Assist2PlanetMaterial;
	case 3: return Assist3PlanetMaterial;
	default: return nullptr;
	}
}

void FABTSM11StylizedMaterialAdapter::CollectBindings(
	const AABTSM11FinaleSystem& FinaleSystem,
	TArray<FABTSStylizedMaterialSlotBinding>& OutBindings)
{
	using namespace ABTSM11StylizedMaterialAdapterPrivate;
	FABTSM11StylizedMaterialSet Materials;
	Materials.Assist1PlanetMaterial = Assist1PlanetMaterial.LoadSynchronous();
	Materials.Assist2PlanetMaterial = Assist2PlanetMaterial.LoadSynchronous();
	Materials.Assist3PlanetMaterial = Assist3PlanetMaterial.LoadSynchronous();
	Materials.UFOMaterial = UFOMaterial.LoadSynchronous();
	CollectBindings(FinaleSystem, Materials, OutBindings);
}

void FABTSM11StylizedMaterialAdapter::CollectBindings(
	const AABTSM11FinaleSystem& FinaleSystem,
	const FABTSM11StylizedMaterialSet& Materials,
	TArray<FABTSStylizedMaterialSlotBinding>& OutBindings)
{
	TArray<AActor*> RuntimePresentationActors;
	RuntimePresentationActors.Reserve(
		AABTSM11FinaleSystem::ExpectedAssistPresentationCount
			+ AABTSM11FinaleSystem::ExpectedTargetPresentationCount);
	for (AABTSM11GravityBodyActor* Actor
		: FinaleSystem.GetGravityBodyActors())
	{
		RuntimePresentationActors.Add(Actor);
	}
	RuntimePresentationActors.Add(FinaleSystem.GetUFOActor());

	ABTSM11StylizedMaterialAdapterPrivate::CollectFromActorSet(
		FinaleSystem,
		RuntimePresentationActors,
		Materials,
		OutBindings);
}

#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Reverse.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "Rendering/ABTSToonVisualCaptureTypes.h"
#include "UObject/StrongObjectPtr.h"
#include "World/ABTSM11GravityAssistSolver.h"

namespace ABTSM11StylizedMaterialAdapterAutomation
{
	class FScopedWorld
	{
	public:
		FScopedWorld()
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
				TEXT("ABTSM11StylizedMaterialsAutomationWorld"),
				nullptr,
				true,
				ERHIFeatureLevel::Num,
				&Values);
		}

		~FScopedWorld()
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

	FABTSM110FinaleLocalFrame MakeFrame()
	{
		FABTSM110FinaleLocalFrame Frame;
		Frame.LayoutVersion = 1;
		Frame.LaunchTaskId = 6;
		Frame.AnchorCellId = 99;
		Frame.SlotPairId = 11001;
		Frame.WorldTransform = FTransform(
			FQuat::Identity,
			FVector(1000.0, -2000.0, 3000.0));
		const FVector Right = Frame.GetRight();
		Frame.LeftSlotWorldLocation =
			Frame.WorldTransform.GetLocation() - Right * 105.0;
		Frame.RightSlotWorldLocation =
			Frame.WorldTransform.GetLocation() + Right * 105.0;
		Frame.bValid = true;
		return Frame;
	}

	AABTSM11FinaleSystem* SpawnReadySystem(
		UWorld& World,
		FAutomationTestBase& Test)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AABTSM11FinaleSystem* System =
			World.SpawnActor<AABTSM11FinaleSystem>(
				AABTSM11FinaleSystem::StaticClass(),
				FTransform::Identity,
				SpawnParameters);
		Test.TestNotNull(TEXT("M11 finale system spawns"), System);
		if (System == nullptr)
		{
			return nullptr;
		}

		const FABTSM11FinaleLayoutPreset Preset =
			FABTSM11FinaleLayoutPreset::MakeCertifiedV1();
		Test.TestTrue(
			TEXT("M11 certified presentation initializes"),
			System->InitializeFromRuntimeData(
				3,
				Preset.ReferencePrimaryRadiusCM,
				MakeFrame()));
		return System->IsLayoutReady() ? System : nullptr;
	}

	FABTSM11StylizedMaterialSet MakeMaterials(
		UMaterialInterface* Assist1,
		UMaterialInterface* Assist2,
		UMaterialInterface* Assist3,
		UMaterialInterface* UFO)
	{
		FABTSM11StylizedMaterialSet Materials;
		Materials.Assist1PlanetMaterial = Assist1;
		Materials.Assist2PlanetMaterial = Assist2;
		Materials.Assist3PlanetMaterial = Assist3;
		Materials.UFOMaterial = UFO;
		return Materials;
	}

	bool ResultsExactlyMatch(
		const FABTSM11TrajectoryResult& A,
		const FABTSM11TrajectoryResult& B)
	{
		if (A.ValidationHash != B.ValidationHash
			|| A.Termination != B.Termination
			|| A.CompletedAssistCount != B.CompletedAssistCount
			|| A.TargetContactCount != B.TargetContactCount
			|| A.Diagnostic != B.Diagnostic
			|| A.Points.Num() != B.Points.Num()
			|| A.Events.Num() != B.Events.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Points.Num(); ++Index)
		{
			const FABTSM11TrajectoryPoint& Left = A.Points[Index];
			const FABTSM11TrajectoryPoint& Right = B.Points[Index];
			if (Left.TimeSeconds != Right.TimeSeconds
				|| Left.PositionCM != Right.PositionCM
				|| Left.VelocityCMPerSec != Right.VelocityCMPerSec
				|| Left.PrimarySpecificEnergyCM2PerSec2
					!= Right.PrimarySpecificEnergyCM2PerSec2)
			{
				return false;
			}
		}
		for (int32 Index = 0; Index < A.Events.Num(); ++Index)
		{
			const FABTSM11TrajectoryEvent& Left = A.Events[Index];
			const FABTSM11TrajectoryEvent& Right = B.Events[Index];
			if (Left.Type != Right.Type
				|| Left.BodyId != Right.BodyId
				|| Left.AssistIndex != Right.AssistIndex
				|| Left.TimeSeconds != Right.TimeSeconds
				|| Left.PositionCM != Right.PositionCM
				|| Left.VelocityCMPerSec != Right.VelocityCMPerSec
				|| Left.EntrySpeedCMPerSec != Right.EntrySpeedCMPerSec
				|| Left.ExitSpeedCMPerSec != Right.ExitSpeedCMPerSec
				|| Left.ClosestDistanceCM != Right.ClosestDistanceCM
				|| Left.BPlaneTCM != Right.BPlaneTCM
				|| Left.BPlaneRCM != Right.BPlaneRCM
				|| Left.BPlaneChiSquared != Right.BPlaneChiSquared
				|| Left.CorridorQuality != Right.CorridorQuality
				|| Left.NaturalDeflectionRadians
					!= Right.NaturalDeflectionRadians
				|| Left.IdealDeflectionRadians
					!= Right.IdealDeflectionRadians
				|| Left.RawEnergyChangeCM2PerSec2
					!= Right.RawEnergyChangeCM2PerSec2
				|| Left.RequestedEnergyChangeCM2PerSec2
					!= Right.RequestedEnergyChangeCM2PerSec2
				|| Left.AppliedEnergyChangeCM2PerSec2
					!= Right.AppliedEnergyChangeCM2PerSec2)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11StylizedMaterialContractTest,
	"ABTS.M11.StylizedMaterials.Contract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11StylizedMaterialContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	for (const EABTSStylizedMaterialFamily Family : {
		EABTSStylizedMaterialFamily::FinalePlanet,
		EABTSStylizedMaterialFamily::FinaleUFO})
	{
		TestEqual(
			TEXT("Finale material family is owned by M11"),
			FABTSStylizedMaterialContract::ResolveOwner(Family),
			EABTSStylizedMaterialOwner::M11);
		TestEqual(
			TEXT("Finale material family uses reversible slot override"),
			FABTSStylizedMaterialContract::ResolveAdoptionMode(Family),
			EABTSStylizedMaterialAdoptionMode::ReversibleSlotOverride);
		TestTrue(
			TEXT("Finale material defaults are valid"),
			FABTSStylizedMaterialContract::ResolveDefaultParameters(Family)
				.IsValid());
	}
	TestTrue(
		TEXT("FinalePlanet preserves the high-roughness visual direction"),
		FABTSStylizedMaterialContract::ResolveDefaultParameters(
			EABTSStylizedMaterialFamily::FinalePlanet).RoughnessFloor >= 0.60f);
	TestTrue(
		TEXT("FinaleUFO remains glossier than FinalePlanet"),
		FABTSStylizedMaterialContract::ResolveDefaultParameters(
			EABTSStylizedMaterialFamily::FinaleUFO).RoughnessFloor
		< FABTSStylizedMaterialContract::ResolveDefaultParameters(
			EABTSStylizedMaterialFamily::FinalePlanet).RoughnessFloor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11StylizedMaterialBindingAdapterTest,
	"ABTS.M11.StylizedMaterials.BindingAdapter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11StylizedMaterialBindingAdapterTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	using namespace ABTSM11StylizedMaterialAdapterAutomation;
	FScopedWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Binding automation World is created"), World);
	if (World == nullptr)
	{
		return false;
	}
	AABTSM11FinaleSystem* System = SpawnReadySystem(*World, *this);
	if (System == nullptr)
	{
		return false;
	}

	TStrongObjectPtr<UMaterial> Assist1(NewObject<UMaterial>());
	TStrongObjectPtr<UMaterial> Assist2(NewObject<UMaterial>());
	TStrongObjectPtr<UMaterial> Assist3(NewObject<UMaterial>());
	TStrongObjectPtr<UMaterial> UFO(NewObject<UMaterial>());
	const FABTSM11StylizedMaterialSet Materials = MakeMaterials(
		Assist1.Get(), Assist2.Get(), Assist3.Get(), UFO.Get());

	TArray<AActor*> ForwardActors;
	for (AABTSM11GravityBodyActor* Actor : System->GetGravityBodyActors())
	{
		ForwardActors.Add(Actor);
	}
	ForwardActors.Add(System->GetUFOActor());
	TArray<AActor*> ReverseActors = ForwardActors;
	Algo::Reverse(ReverseActors);

	TArray<FABTSStylizedMaterialSlotBinding> ForwardBindings;
	TArray<FABTSStylizedMaterialSlotBinding> ReverseBindings;
	ABTSM11StylizedMaterialAdapterPrivate::CollectFromActorSet(
		*System, ForwardActors, Materials, ForwardBindings);
	ABTSM11StylizedMaterialAdapterPrivate::CollectFromActorSet(
		*System, ReverseActors, Materials, ReverseBindings);
	TestEqual(
		TEXT("Traversal order produces the same binding count"),
		ForwardBindings.Num(),
		ReverseBindings.Num());
	for (int32 Index = 0;
		Index < FMath::Min(ForwardBindings.Num(), ReverseBindings.Num());
		++Index)
	{
		const FABTSStylizedMaterialSlotBinding& A = ForwardBindings[Index];
		const FABTSStylizedMaterialSlotBinding& B = ReverseBindings[Index];
		TestTrue(
			*FString::Printf(TEXT("Binding %d has stable identity"), Index),
			A.Component == B.Component
				&& A.MaterialSlotIndex == B.MaterialSlotIndex
				&& A.StylizedMaterial == B.StylizedMaterial
				&& A.Family == B.Family);
	}

	int32 PlanetBindingCount = 0;
	int32 UFOBindingCount = 0;
	for (const FABTSStylizedMaterialSlotBinding& Binding : ForwardBindings)
	{
		TestTrue(TEXT("Every published binding is valid"), Binding.IsValid());
		TestTrue(
			TEXT("Every slot index is legal for its explicit component"),
			Binding.MaterialSlotIndex >= 0
				&& Binding.MaterialSlotIndex
					< Binding.Component->GetNumMaterials());
		if (Binding.Family == EABTSStylizedMaterialFamily::FinalePlanet)
		{
			++PlanetBindingCount;
		}
		else if (Binding.Family == EABTSStylizedMaterialFamily::FinaleUFO)
		{
			++UFOBindingCount;
		}
	}
	TestEqual(
		TEXT("All three explicit planet presentation meshes publish"),
		PlanetBindingCount,
		3);
	TestEqual(
		TEXT("The explicit UFO presentation mesh publishes"),
		UFOBindingCount,
		1);

	UStaticMeshComponent* HiddenHelper =
		NewObject<UStaticMeshComponent>(System);
	HiddenHelper->SetVisibility(false);
	USplineComponent* TrajectoryHelper = NewObject<USplineComponent>(System);
	UStaticMeshComponent* CollisionProxy =
		NewObject<UStaticMeshComponent>(System);
	CollisionProxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	for (const FABTSStylizedMaterialSlotBinding& Binding : ForwardBindings)
	{
		TestTrue(
			TEXT("No helper, trajectory or collision component is published"),
			Binding.Component != HiddenHelper
				&& Binding.Component != TrajectoryHelper
				&& Binding.Component != CollisionProxy);
	}

	FABTSM11StylizedMaterialSet MissingPlanetMaterial = Materials;
	MissingPlanetMaterial.Assist2PlanetMaterial = nullptr;
	TArray<FABTSStylizedMaterialSlotBinding> MissingBindings;
	FABTSM11StylizedMaterialAdapter::CollectBindings(
		*System, MissingPlanetMaterial, MissingBindings);
	TestEqual(
		TEXT("A missing stylized material fails soft for only that body"),
		MissingBindings.Num(),
		ForwardBindings.Num() - 1);

	TArray<FABTSStylizedMaterialSlotBinding> EmptyBindings;
	FABTSM11StylizedMaterialAdapter::CollectBindings(
		*System, FABTSM11StylizedMaterialSet{}, EmptyBindings);
	TestEqual(
		TEXT("All missing stylized materials preserve the original surfaces"),
		EmptyBindings.Num(),
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11StylizedMaterialAuthorityParityTest,
	"ABTS.M11.StylizedMaterials.AuthorityParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11StylizedMaterialAuthorityParityTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	using namespace ABTSM11StylizedMaterialAdapterAutomation;
	FScopedWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Authority automation World is created"), World);
	if (World == nullptr)
	{
		return false;
	}
	AABTSM11FinaleSystem* System = SpawnReadySystem(*World, *this);
	if (System == nullptr)
	{
		return false;
	}

	TStrongObjectPtr<UMaterial> PlanetMaterial(NewObject<UMaterial>());
	TStrongObjectPtr<UMaterial> UFOMaterial(NewObject<UMaterial>());
	const FABTSM11StylizedMaterialSet Materials = MakeMaterials(
		PlanetMaterial.Get(),
		PlanetMaterial.Get(),
		PlanetMaterial.Get(),
		UFOMaterial.Get());
	TArray<FABTSStylizedMaterialSlotBinding> Bindings;
	FABTSM11StylizedMaterialAdapter::CollectBindings(
		*System, Materials, Bindings);

	const FABTSM11FinaleLayoutPreset& LayoutBefore =
		System->GetLayoutPreset();
	const uint64 PresetHashBefore = LayoutBefore.PresetHash;
	const uint64 CertificationHashBefore = LayoutBefore.CertificationHash;
	const uint64 BundleHashBefore = LayoutBefore.CertifiedBundleHash;
	const uint64 FrameHashBefore =
		AABTSM11FinaleSystem::ComputeFinaleFrameDiagnosticHash(
			System->GetFinaleFrame());

	FABTSM11TrajectoryRequest RequestBefore;
	FABTSM11TrajectoryResult ResultBefore;
	FString Failure;
	TestTrue(
		TEXT("Authoritative request builds before material switching"),
		System->BuildRequest(
			LayoutBefore.NominalInput,
			0x7u,
			RequestBefore,
			&Failure));
	TestTrue(
		TEXT("Authoritative result solves before material switching"),
		FABTSM11GravityAssistSolver::Solve(
			RequestBefore,
			ResultBefore,
			&Failure));

	TArray<FTransform> PresentationTransformsBefore;
	for (const AABTSM11GravityBodyActor* Actor
		: System->GetGravityBodyActors())
	{
		PresentationTransformsBefore.Add(Actor->GetActorTransform());
	}
	PresentationTransformsBefore.Add(
		System->GetUFOActor()->GetActorTransform());
	const FTransform CameraTransform(
		System->GetFinaleFrame().WorldTransform.GetRotation(),
		System->GetFinaleFrame().TransformLocalPosition(
			FVector(-24000.0, 8000.0, 12000.0)));
	const uint64 CameraPoseHashBefore =
		FABTSToonVisualCaptureMath::ComputeCameraPoseHash(
			CameraTransform,
			System->GetUFOActor()->GetActorLocation(),
			50.0f);

	TArray<UMaterialInterface*> OriginalMaterials;
	OriginalMaterials.Reserve(Bindings.Num());
	for (const FABTSStylizedMaterialSlotBinding& Binding : Bindings)
	{
		OriginalMaterials.Add(
			Binding.Component->GetMaterial(Binding.MaterialSlotIndex));
	}

	FABTSStylizedMaterialOverrideRegistry Registry;
	Registry.Apply(Bindings, true);
	TestEqual(
		TEXT("Shared registry adopts every declared M11 slot"),
		Registry.Num(),
		Bindings.Num());
	for (const FABTSStylizedMaterialSlotBinding& Binding : Bindings)
	{
		TestTrue(
			TEXT("Style On applies the declared interface only"),
			Binding.Component->GetMaterial(Binding.MaterialSlotIndex)
				== Binding.StylizedMaterial);
	}

	FABTSM11TrajectoryRequest RequestAfter;
	FABTSM11TrajectoryResult ResultAfter;
	TestTrue(
		TEXT("Authoritative request builds after material switching"),
		System->BuildRequest(
			System->GetLayoutPreset().NominalInput,
			0x7u,
			RequestAfter,
			&Failure));
	TestTrue(
		TEXT("Authoritative result solves after material switching"),
		FABTSM11GravityAssistSolver::Solve(
			RequestAfter,
			ResultAfter,
			&Failure));
	TestTrue(
		TEXT("Style switching preserves every point and event exactly"),
		ResultsExactlyMatch(ResultBefore, ResultAfter));
	TestEqual(
		TEXT("Style switching preserves the scenario hash"),
		RequestAfter.Scenario.ScenarioHash,
		RequestBefore.Scenario.ScenarioHash);
	TestEqual(
		TEXT("Style switching preserves the preset hash"),
		System->GetLayoutPreset().PresetHash,
		PresetHashBefore);
	TestEqual(
		TEXT("Style switching preserves the certification hash"),
		System->GetLayoutPreset().CertificationHash,
		CertificationHashBefore);
	TestEqual(
		TEXT("Style switching preserves the certified bundle hash"),
		System->GetLayoutPreset().CertifiedBundleHash,
		BundleHashBefore);
	TestEqual(
		TEXT("Style switching preserves the finale frame identity"),
		AABTSM11FinaleSystem::ComputeFinaleFrameDiagnosticHash(
			System->GetFinaleFrame()),
		FrameHashBefore);
	TestEqual(
		TEXT("Style switching preserves the camera pose identity"),
		FABTSToonVisualCaptureMath::ComputeCameraPoseHash(
			CameraTransform,
			System->GetUFOActor()->GetActorLocation(),
			50.0f),
		CameraPoseHashBefore);

	int32 TransformIndex = 0;
	for (const AABTSM11GravityBodyActor* Actor
		: System->GetGravityBodyActors())
	{
		TestTrue(
			TEXT("Planet transform remains presentation-only and unchanged"),
			Actor->GetActorTransform().Equals(
				PresentationTransformsBefore[TransformIndex++]));
	}
	TestTrue(
		TEXT("UFO transform remains presentation-only and unchanged"),
		System->GetUFOActor()->GetActorTransform().Equals(
			PresentationTransformsBefore[TransformIndex]));

	Registry.RestoreAll();
	for (int32 Index = 0; Index < Bindings.Num(); ++Index)
	{
		TestTrue(
			TEXT("Style Off restores the exact pre-T3 material interface"),
			Bindings[Index].Component->GetMaterial(
				Bindings[Index].MaterialSlotIndex)
			== OriginalMaterials[Index]);
	}
	return true;
}

#endif
