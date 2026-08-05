// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSStylizedRenderingWorldSubsystem.h"

#include "ABTSRuntime.h"
#include "Camera/ABTSM101LandingPreviewCamera.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"
#include "Rendering/ABTSStylizedSceneCaptureRegistry.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "Terrain/ABTSM3Planet.h"
#include "Terrain/ABTSM3StylizedSemanticAdapter.h"
#include "World/ABTSM10ScoutMapSystem.h"
#include "World/ABTSM11FinaleActors.h"
#include "World/ABTSM11FinaleInteractionSystem.h"
#include "World/ABTSM11FinaleSystem.h"
#include "PCG/ABTSM3MonthlySatellitePracticeRuntime.h"
#include "World/ABTSM9Satellite.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/StaticMeshComponent.h"
#include "Misc/AutomationTest.h"
#endif

namespace ABTSStylizedRenderingWorldSubsystemPrivate
{
	constexpr float RefreshIntervalSeconds = 0.10f;

	struct FPrimitiveSavedState
	{
		bool bRenderCustomDepth = false;
		int32 StencilValue = 0;
		ERendererStencilMask StencilMask = ERendererStencilMask::ERSM_Default;
		uint8 AppliedStencilValue = 0;
	};

	void GatherActorPrimitives(
		const AActor& Actor,
		TArray<UPrimitiveComponent*>& OutPrimitives)
	{
		TInlineComponentArray<UPrimitiveComponent*> Components;
		Actor.GetComponents(Components);
		for (UPrimitiveComponent* Component : Components)
		{
			if (IsValid(Component) && Component->IsRegistered())
			{
				OutPrimitives.AddUnique(Component);
			}
		}
	}
}

class UABTSStylizedRenderingWorldSubsystem::FPrimitiveOverrideRegistry
{
public:
	using FPrimitiveSavedState =
		ABTSStylizedRenderingWorldSubsystemPrivate::FPrimitiveSavedState;

	void Apply(
		const TMap<TWeakObjectPtr<UPrimitiveComponent>,
			EABTSStylizedObjectClass>& Desired)
	{
		for (auto It = ConflictingComponents.CreateIterator(); It; ++It)
		{
			if (!It->IsValid() || !Desired.Contains(*It))
			{
				It.RemoveCurrent();
			}
		}

		for (auto It = SavedStates.CreateIterator(); It; ++It)
		{
			UPrimitiveComponent* Component = It.Key().Get();
			if (!IsValid(Component))
			{
				It.RemoveCurrent();
				continue;
			}
			if (!Desired.Contains(It.Key()))
			{
				RestoreIfStillOwned(*Component, It.Value());
				It.RemoveCurrent();
			}
		}

		for (const TPair<TWeakObjectPtr<UPrimitiveComponent>,
			EABTSStylizedObjectClass>& Pair : Desired)
		{
			UPrimitiveComponent* Component = Pair.Key.Get();
			const uint8 StencilValue =
				FABTSStylizedRenderingContract::ResolveStencilValueForRenderer(
					Pair.Value);
			if (!IsValid(Component) || StencilValue == 0)
			{
				continue;
			}

			FPrimitiveSavedState* Existing = SavedStates.Find(Pair.Key);
			if (Existing == nullptr)
			{
				// Do not steal an unrelated gameplay/debug stencil producer.
				if (Component->bRenderCustomDepth
					&& Component->CustomDepthStencilValue != StencilValue)
				{
					ConflictingComponents.Add(Pair.Key);
					continue;
				}
				ConflictingComponents.Remove(Pair.Key);
				FPrimitiveSavedState Saved;
				Saved.bRenderCustomDepth = Component->bRenderCustomDepth;
				Saved.StencilValue = Component->CustomDepthStencilValue;
				Saved.StencilMask = Component->CustomDepthStencilWriteMask;
				Saved.AppliedStencilValue = StencilValue;
				Existing = &SavedStates.Add(Pair.Key, Saved);
			}
			Existing->AppliedStencilValue = StencilValue;
			Component->SetCustomDepthStencilWriteMask(
				ERendererStencilMask::ERSM_Default);
			Component->SetCustomDepthStencilValue(StencilValue);
			Component->SetRenderCustomDepth(true);
		}
	}

	void RestoreAll()
	{
		for (TPair<TWeakObjectPtr<UPrimitiveComponent>, FPrimitiveSavedState>& Pair
			: SavedStates)
		{
			if (UPrimitiveComponent* Component = Pair.Key.Get())
			{
				RestoreIfStillOwned(*Component, Pair.Value);
			}
		}
		SavedStates.Reset();
		ConflictingComponents.Reset();
	}

	int32 Num() const { return SavedStates.Num(); }
	int32 GetConflictCount() const { return ConflictingComponents.Num(); }

private:
	static void RestoreIfStillOwned(
		UPrimitiveComponent& Component,
		const FPrimitiveSavedState& Saved)
	{
		if (!Component.bRenderCustomDepth
			|| Component.CustomDepthStencilValue
				!= Saved.AppliedStencilValue)
		{
			return;
		}
		Component.SetCustomDepthStencilWriteMask(Saved.StencilMask);
		Component.SetCustomDepthStencilValue(Saved.StencilValue);
		Component.SetRenderCustomDepth(Saved.bRenderCustomDepth);
	}

	TMap<TWeakObjectPtr<UPrimitiveComponent>, FPrimitiveSavedState> SavedStates;
	TSet<TWeakObjectPtr<UPrimitiveComponent>> ConflictingComponents;
};

UABTSStylizedRenderingWorldSubsystem::UABTSStylizedRenderingWorldSubsystem() = default;

UABTSStylizedRenderingWorldSubsystem::UABTSStylizedRenderingWorldSubsystem(
	FVTableHelper& Helper)
	: Super(Helper)
{
}

UABTSStylizedRenderingWorldSubsystem::~UABTSStylizedRenderingWorldSubsystem() = default;

bool UABTSStylizedRenderingWorldSubsystem::ShouldCreateSubsystem(
	UObject* Outer) const
{
	return Super::ShouldCreateSubsystem(Outer);
}

bool UABTSStylizedRenderingWorldSubsystem::DoesSupportWorldType(
	const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UABTSStylizedRenderingWorldSubsystem::Initialize(
	FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	PrimitiveRegistry = MakeUnique<FPrimitiveOverrideRegistry>();
}

void UABTSStylizedRenderingWorldSubsystem::Deinitialize()
{
	if (PrimitiveRegistry)
	{
		PrimitiveRegistry->RestoreAll();
		PrimitiveRegistry.Reset();
	}
	for (const TWeakObjectPtr<USceneCaptureComponent2D>& Capture
		: RegisteredCaptures)
	{
		if (Capture.IsValid())
		{
			FABTSStylizedSceneCaptureRegistry::Unregister(*Capture.Get());
		}
	}
	RegisteredCaptures.Reset();
	Super::Deinitialize();
}

void UABTSStylizedRenderingWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bWorldBeganPlay = true;
	RefreshNow();
}

void UABTSStylizedRenderingWorldSubsystem::Tick(const float DeltaTime)
{
	const bool bStyleEnabled = FABTSStylizedRenderingControl::IsEnabled();
	if (bStyleEnabled != bLastObservedStyleEnabled)
	{
		RefreshAccumulatorSeconds = 0.0f;
		RefreshNow();
		return;
	}
	RefreshAccumulatorSeconds += FMath::Max(0.0f, DeltaTime);
	if (RefreshAccumulatorSeconds >=
		ABTSStylizedRenderingWorldSubsystemPrivate::RefreshIntervalSeconds)
	{
		RefreshAccumulatorSeconds = 0.0f;
		RefreshNow();
	}
}

TStatId UABTSStylizedRenderingWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(
		UABTSStylizedRenderingWorldSubsystem,
		STATGROUP_Tickables);
}

bool UABTSStylizedRenderingWorldSubsystem::IsTickable() const
{
	return bWorldBeganPlay && GetWorld() != nullptr;
}

int32 UABTSStylizedRenderingWorldSubsystem::GetRegisteredPrimitiveCount() const
{
	return PrimitiveRegistry ? PrimitiveRegistry->Num() : 0;
}

void UABTSStylizedRenderingWorldSubsystem::RefreshNow()
{
	using namespace ABTSStylizedRenderingWorldSubsystemPrivate;
	UWorld* World = GetWorld();
	if (World == nullptr || PrimitiveRegistry == nullptr)
	{
		return;
	}
	bLastObservedStyleEnabled = FABTSStylizedRenderingControl::IsEnabled();

	TMap<TWeakObjectPtr<UPrimitiveComponent>, EABTSStylizedObjectClass> Desired;
	int32 M3SemanticCount = 0;
	int32 M11SemanticCount = 0;
	int32 PlayerSemanticCount = 0;
	int32 SlingshotSemanticCount = 0;

	auto AddPrimitive = [&Desired](
		UPrimitiveComponent* Component,
		const EABTSStylizedObjectClass ObjectClass)
	{
		if (IsValid(Component)
			&& FABTSStylizedRenderingContract::RequiresSelectiveStencil(
				ObjectClass))
		{
			Desired.Add(Component, ObjectClass);
		}
	};
	auto AddActor = [&AddPrimitive](
		const AActor& Actor,
		const EABTSStylizedObjectClass ObjectClass)
	{
		TArray<UPrimitiveComponent*> Primitives;
		GatherActorPrimitives(Actor, Primitives);
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			AddPrimitive(Primitive, ObjectClass);
		}
	};

	if (FABTSStylizedRenderingControl::IsEnabled())
	{
		for (TActorIterator<AABTSM3Planet> It(World); It; ++It)
		{
			TArray<FABTSM3StylizedSemanticBinding> Bindings;
			FABTSM3StylizedSemanticAdapter::GatherPrimaryPlanetSemantics(
				**It,
				Bindings);
			M3SemanticCount += Bindings.Num();
			for (const FABTSM3StylizedSemanticBinding& Binding : Bindings)
			{
				AddPrimitive(
					const_cast<UPrimitiveComponent*>(Binding.Component),
					Binding.ObjectClass);
			}
		}
		for (TActorIterator<AABTSM9Satellite> It(World); It; ++It)
		{
			TArray<FABTSM3StylizedSemanticBinding> Bindings;
			FABTSM3StylizedSemanticAdapter::GatherSatelliteSemantics(**It, Bindings);
			M3SemanticCount += Bindings.Num();
			for (const FABTSM3StylizedSemanticBinding& Binding : Bindings)
			{
				if (Binding.Actor)
				{
					AddActor(*Binding.Actor, Binding.ObjectClass);
				}
			}
		}
		for (TActorIterator<AABTSM3MonthlySatellitePracticeRuntime> It(World);
			It;
			++It)
		{
			TArray<FABTSM3StylizedSemanticBinding> Bindings;
			FABTSM3StylizedSemanticAdapter::GatherMonthlyPracticeSemantics(
				**It,
				Bindings);
			M3SemanticCount += Bindings.Num();
			for (const FABTSM3StylizedSemanticBinding& Binding : Bindings)
			{
				if (Binding.Actor)
				{
					AddActor(*Binding.Actor, Binding.ObjectClass);
				}
			}
		}

		for (TActorIterator<AABTSBirdParty> It(World); It; ++It)
		{
			for (AABTSM25BirdCharacter* Bird : It->GetPartyMembers())
			{
				if (IsValid(Bird))
				{
					AddActor(*Bird, EABTSStylizedObjectClass::PlayerBird);
					++PlayerSemanticCount;
				}
			}
		}

		for (TActorIterator<AABTSM6SlingshotSystem> It(World); It; ++It)
		{
			TArray<UPrimitiveComponent*> Primitives;
			It->GatherActiveSlingshotPrimitives(Primitives);
			for (UPrimitiveComponent* Primitive : Primitives)
			{
				AddPrimitive(Primitive, EABTSStylizedObjectClass::Slingshot);
			}
			SlingshotSemanticCount += Primitives.Num();
		}

		for (TActorIterator<AABTSM11FinaleSystem> It(World); It; ++It)
		{
			for (AABTSM11GravityBodyActor* Actor : It->GetGravityBodyActors())
			{
				if (!IsValid(Actor))
				{
					continue;
				}
				EABTSStylizedObjectClass ObjectClass =
					EABTSStylizedObjectClass::None;
				if (It->TryGetStylizedObjectClass(*Actor, ObjectClass))
				{
					AddActor(*Actor, ObjectClass);
					++M11SemanticCount;
				}
			}
			if (AABTSM11UFOActor* UFO = It->GetUFOActor())
			{
				EABTSStylizedObjectClass ObjectClass =
					EABTSStylizedObjectClass::None;
				if (It->TryGetStylizedObjectClass(*UFO, ObjectClass))
				{
					AddActor(*UFO, ObjectClass);
					++M11SemanticCount;
				}
			}
		}
	}
	PrimitiveRegistry->Apply(Desired);

	TMap<TWeakObjectPtr<USceneCaptureComponent2D>, EABTSStylizedViewClass>
		DesiredCaptures;
	for (TActorIterator<AABTSM10ScoutMapSystem> It(World); It; ++It)
	{
		AABTSM101LandingPreviewCamera* Camera = It->GetLandingPreviewCamera();
		if (!IsValid(Camera) || !Camera->IsPreviewActive())
		{
			continue;
		}
		USceneCaptureComponent2D* Capture = Camera->GetSceneCaptureComponent();
		if (!IsValid(Capture))
		{
			continue;
		}
		if (Camera->GetPreviewSubject()
			== EABTSM101PreviewSubject::PrimaryLanding)
		{
			DesiredCaptures.Add(
				Capture,
				EABTSStylizedViewClass::GroundLandingPreview);
		}
		else if (Camera->GetPreviewSubject()
			== EABTSM101PreviewSubject::SatelliteLanding)
		{
			DesiredCaptures.Add(
				Capture,
				EABTSStylizedViewClass::SatelliteLandingPreview);
		}
	}
	for (TActorIterator<AABTSM11FinaleInteractionSystem> It(World); It; ++It)
	{
		if (USceneCaptureComponent2D* Capture =
			It->GetFinaleRemotePreviewCaptureComponent())
		{
			DesiredCaptures.Add(
				Capture,
				It->GetFinaleRemotePreviewStylizedViewClass());
		}
	}

	for (auto It = RegisteredCaptures.CreateIterator(); It; ++It)
	{
		USceneCaptureComponent2D* Capture = It->Get();
		if (!IsValid(Capture) || !DesiredCaptures.Contains(*It))
		{
			if (IsValid(Capture))
			{
				FABTSStylizedSceneCaptureRegistry::Unregister(*Capture);
			}
			It.RemoveCurrent();
		}
	}
	for (const TPair<TWeakObjectPtr<USceneCaptureComponent2D>,
		EABTSStylizedViewClass>& Pair : DesiredCaptures)
	{
		if (USceneCaptureComponent2D* Capture = Pair.Key.Get())
		{
			if (FABTSStylizedSceneCaptureRegistry::Register(*Capture, Pair.Value))
			{
				RegisteredCaptures.Add(Capture);
			}
		}
	}

	uint64 DiagnosticSummaryHash = GetTypeHash(M3SemanticCount);
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(M11SemanticCount));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(PlayerSemanticCount));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(SlingshotSemanticCount));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(PrimitiveRegistry->Num()));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(RegisteredCaptures.Num()));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(PrimitiveRegistry->GetConflictCount()));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(FABTSStylizedRenderingControl::IsEnabled()));
	if (LastDiagnosticSummaryHash != DiagnosticSummaryHash)
	{
		LastDiagnosticSummaryHash = DiagnosticSummaryHash;
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Rendering][T2-B1] M3Semantics=%d M7AdapterReady=0 M11Semantics=%d Birds=%d SlingshotPrimitives=%d SelectiveProducers=%d PreviewViews=%d Conflicts=%d Style=%d"),
			M3SemanticCount,
			M11SemanticCount,
			PlayerSemanticCount,
			SlingshotSemanticCount,
			PrimitiveRegistry->Num(),
			RegisteredCaptures.Num(),
			PrimitiveRegistry->GetConflictCount(),
			FABTSStylizedRenderingControl::IsEnabled() ? 1 : 0);
	}
}

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT2B1PrimitiveRegistryTest,
	"ABTS.Rendering.Toon.T2B1.PrimitiveRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT2B1PrimitiveRegistryTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	UStaticMeshComponent* Component =
		NewObject<UStaticMeshComponent>(GetTransientPackage());
	TestNotNull(TEXT("Transient primitive is available"), Component);
	if (Component == nullptr)
	{
		return false;
	}
	Component->SetRenderCustomDepth(false);
	Component->SetCustomDepthStencilValue(19);

	UABTSStylizedRenderingWorldSubsystem::FPrimitiveOverrideRegistry Registry;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, EABTSStylizedObjectClass> Desired;
	Desired.Add(Component, EABTSStylizedObjectClass::PlayerBird);
	Registry.Apply(Desired);
	TestTrue(TEXT("Selective producer is enabled"), Component->bRenderCustomDepth != 0);
	TestEqual(
		TEXT("Semantic class resolves only through Integration allocation"),
		Component->CustomDepthStencilValue,
		static_cast<int32>(
			FABTSStylizedRenderingContract::ResolveStencilValueForRenderer(
				EABTSStylizedObjectClass::PlayerBird)));
	TestEqual(TEXT("One producer is tracked"), Registry.Num(), 1);

	Desired.Reset();
	Registry.Apply(Desired);
	TestFalse(
		TEXT("Style-off/absence restores the original producer switch"),
		Component->bRenderCustomDepth != 0);
	TestEqual(
		TEXT("Style-off/absence restores the original stencil value"),
		Component->CustomDepthStencilValue,
		19);
	TestEqual(TEXT("No producer remains tracked"), Registry.Num(), 0);

	Component->SetRenderCustomDepth(true);
	Component->SetCustomDepthStencilValue(99);
	Desired.Add(Component, EABTSStylizedObjectClass::FinaleUFO);
	Registry.Apply(Desired);
	TestEqual(
		TEXT("Foreign stencil producers are never stolen"),
		Component->CustomDepthStencilValue,
		99);
	TestEqual(TEXT("Foreign producer is not tracked"), Registry.Num(), 0);
	Registry.Apply(Desired);
	TestEqual(TEXT("Conflict fails closed once"), Registry.GetConflictCount(), 1);
	Registry.RestoreAll();
	return true;
}

#endif
