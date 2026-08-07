// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSStylizedRenderingWorldSubsystem.h"

#include "ABTSRuntime.h"
#include "Camera/ABTSM101LandingPreviewCamera.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Party/ABTSBirdParty.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "Presentation/ABTSOpeningCinematicPreview.h"
#include "Rendering/ABTSSharedStylizedMaterialAdapter.h"
#include "Rendering/ABTSStylizedRenderingControl.h"
#include "Rendering/ABTSStylizedMaterialContract.h"
#include "Rendering/ABTSStylizedMaterialOverrideRegistry.h"
#include "Rendering/ABTSStylizedRenderingTypes.h"
#include "Rendering/ABTSStylizedSceneCaptureRegistry.h"
#include "Slingshot/ABTSM6SlingshotSystem.h"
#include "Terrain/ABTSM3Planet.h"
#include "Terrain/ABTSM3StylizedMaterialAdapter.h"
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
	MaterialRegistry = MakeUnique<FABTSStylizedMaterialOverrideRegistry>();
	PreloadedSharedMaterials.Reset();
	bSharedMaterialPreloadReady = false;
}

void UABTSStylizedRenderingWorldSubsystem::Deinitialize()
{
	if (MaterialRegistry)
	{
		MaterialRegistry->RestoreAll();
		MaterialRegistry.Reset();
	}
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
	PreloadedSharedMaterials.Reset();
	bSharedMaterialPreloadReady = false;
	Super::Deinitialize();
}

void UABTSStylizedRenderingWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	PreloadSharedMaterials();
	bWorldBeganPlay = true;
	RefreshNow();
}

void UABTSStylizedRenderingWorldSubsystem::PreloadSharedMaterials()
{
	const double StartSeconds = FPlatformTime::Seconds();
	PreloadedSharedMaterials.Reset();
	bSharedMaterialPreloadReady = false;

	TArray<UMaterialInterface*> LoadedMaterials;
	int32 FailureCount = 0;
	const int32 LoadedCount =
		FABTSSharedStylizedMaterialAdapter::PreloadCatalogMaterials(
			LoadedMaterials,
			FailureCount);
	int32 CompleteCount = 0;
	for (UMaterialInterface* Material : LoadedMaterials)
	{
		if (!IsValid(Material))
		{
			++FailureCount;
			continue;
		}

		// In Editor this blocks until any missing shader map is complete. That
		// deliberately moves the one-time fallback-material window from the first
		// slingshot click to world startup. Cooked builds retain the same strong
		// reference preload without requiring runtime shader compilation.
		Material->EnsureIsComplete();
		if (!Material->IsCompiling())
		{
			++CompleteCount;
		}
		PreloadedSharedMaterials.Add(Material);
	}

	const int32 CatalogCount =
		FABTSSharedStylizedMaterialAdapter::GetCatalogEntryCount();
	bSharedMaterialPreloadReady =
		FailureCount == 0
		&& LoadedCount == CatalogCount
		&& CompleteCount == CatalogCount
		&& PreloadedSharedMaterials.Num() == CatalogCount;
	const double ElapsedMilliseconds =
		(FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][Rendering][T3-A2][Preload] Catalog=%d Loaded=%d Complete=%d Failed=%d Ready=%d ElapsedMS=%.2f"),
		CatalogCount,
		LoadedCount,
		CompleteCount,
		FailureCount,
		bSharedMaterialPreloadReady ? 1 : 0,
		ElapsedMilliseconds);
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

int32 UABTSStylizedRenderingWorldSubsystem::GetRegisteredMaterialSlotCount() const
{
	return MaterialRegistry ? MaterialRegistry->Num() : 0;
}

void UABTSStylizedRenderingWorldSubsystem::RefreshNow()
{
	using namespace ABTSStylizedRenderingWorldSubsystemPrivate;
	UWorld* World = GetWorld();
	if (World == nullptr || PrimitiveRegistry == nullptr || MaterialRegistry == nullptr)
	{
		return;
	}
	bLastObservedStyleEnabled = FABTSStylizedRenderingControl::IsEnabled();

	TMap<TWeakObjectPtr<UPrimitiveComponent>, EABTSStylizedObjectClass> Desired;
	int32 M3SemanticCount = 0;
	int32 M11SemanticCount = 0;
	int32 PlayerSemanticCount = 0;
	int32 SlingshotSemanticCount = 0;
	int32 M3SurfaceStyleCount = 0;
	int32 M3BackgroundMaterialCount = 0;
	int32 SharedBirdMaterialCount = 0;
	int32 SharedSlingshotMaterialCount = 0;
	TArray<FABTSStylizedMaterialSlotBinding> DesiredMaterialBindings;

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

	// M3 surface parameters must be refreshed outside the Style-On-only
	// semantic pass so a 1 -> 0 transition restores the same TerrainMID. Tree
	// and rock slots remain read-only publications consumed by the Integration
	// registry, which owns exact source-material restoration.
	for (TActorIterator<AABTSM3Planet> It(World); It; ++It)
	{
		if (It->ApplyStylizedSurfaceStyle(bLastObservedStyleEnabled))
		{
			++M3SurfaceStyleCount;
		}
		TArray<FABTSStylizedMaterialSlotBinding> M3MaterialBindings;
		FABTSM3StylizedMaterialAdapter::GatherBackgroundPropMaterialBindings(
			**It,
			M3MaterialBindings);
		M3BackgroundMaterialCount += M3MaterialBindings.Num();
		DesiredMaterialBindings.Append(MoveTemp(M3MaterialBindings));
	}

	if (bLastObservedStyleEnabled)
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
					if (bSharedMaterialPreloadReady)
					{
						SharedBirdMaterialCount +=
							FABTSSharedStylizedMaterialAdapter::GatherActorBindings(
								*Bird,
								DesiredMaterialBindings);
					}
					++PlayerSemanticCount;
				}
			}
		}
		for (TActorIterator<AABTSOpeningCinematicPreview> It(World); It; ++It)
		{
			if (bSharedMaterialPreloadReady)
			{
				SharedBirdMaterialCount +=
					FABTSSharedStylizedMaterialAdapter::GatherActorBindings(
						**It,
						DesiredMaterialBindings);
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
			if (bSharedMaterialPreloadReady)
			{
				SharedSlingshotMaterialCount +=
					FABTSSharedStylizedMaterialAdapter::GatherPrimitiveBindings(
						Primitives,
						DesiredMaterialBindings);
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

	MaterialRegistry->Apply(
		DesiredMaterialBindings,
		FABTSStylizedRenderingControl::IsEnabled());

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
		GetTypeHash(MaterialRegistry->Num()));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(MaterialRegistry->GetConflictCount()));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(MaterialRegistry->GetRejectedBindingCount()));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(M3SurfaceStyleCount));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(M3BackgroundMaterialCount));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(SharedBirdMaterialCount));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(SharedSlingshotMaterialCount));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(bSharedMaterialPreloadReady));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(PreloadedSharedMaterials.Num()));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(FABTSSharedStylizedMaterialAdapter::GetCatalogHash()));
	DiagnosticSummaryHash = HashCombineFast(
		DiagnosticSummaryHash,
		GetTypeHash(FABTSStylizedMaterialContract::GetContractHash()));
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
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Rendering][T3-A0] MaterialSlots=%d MaterialConflicts=%d MaterialRejected=%d MaterialContractVersion=%d MaterialContractHash=%u Style=%d"),
			MaterialRegistry->Num(),
			MaterialRegistry->GetConflictCount(),
			MaterialRegistry->GetRejectedBindingCount(),
			FABTSStylizedMaterialContract::GetVersion(),
			FABTSStylizedMaterialContract::GetContractHash(),
			FABTSStylizedRenderingControl::IsEnabled() ? 1 : 0);
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Rendering][T3-A1] SurfaceStyles=%d BackgroundMaterialSlots=%d AppliedSlots=%d Conflicts=%d Rejected=%d Style=%d"),
			M3SurfaceStyleCount,
			M3BackgroundMaterialCount,
			MaterialRegistry->Num(),
			MaterialRegistry->GetConflictCount(),
			MaterialRegistry->GetRejectedBindingCount(),
			bLastObservedStyleEnabled ? 1 : 0);
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Rendering][T3-A2] BirdMaterialSlots=%d SlingshotMaterialSlots=%d SharedCatalogEntries=%d SharedCatalogHash=%u Preloaded=%d PreloadReady=%d AppliedSlots=%d Conflicts=%d Rejected=%d Style=%d"),
			SharedBirdMaterialCount,
			SharedSlingshotMaterialCount,
			FABTSSharedStylizedMaterialAdapter::GetCatalogEntryCount(),
			FABTSSharedStylizedMaterialAdapter::GetCatalogHash(),
			PreloadedSharedMaterials.Num(),
			bSharedMaterialPreloadReady ? 1 : 0,
			MaterialRegistry->Num(),
			MaterialRegistry->GetConflictCount(),
			MaterialRegistry->GetRejectedBindingCount(),
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
