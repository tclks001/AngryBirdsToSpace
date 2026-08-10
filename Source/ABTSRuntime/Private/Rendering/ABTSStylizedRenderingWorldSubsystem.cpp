// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSStylizedRenderingWorldSubsystem.h"

#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

#include "ABTSRuntime.h"
#include "Camera/ABTSM101LandingPreviewCamera.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/World.h"
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
#include "World/ABTSM11StylizedMaterialAdapter.h"
#include "PCG/ABTSM3MonthlySatellitePracticeRuntime.h"
#include "World/ABTSM9Satellite.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/StaticMeshComponent.h"
#include "Misc/AutomationTest.h"
#endif

namespace ABTSStylizedRenderingWorldSubsystemPrivate
{
	constexpr float RefreshIntervalSeconds = 0.10f;
	constexpr float ContinuousAtmosphereTraceSampleCountScale = 2.0f;

	/**
	 * UE's sky defaults target an Earth-scale atmosphere. ABTS uses a five-
	 * kilometre planet with roughly three kilometres of atmosphere, so the
	 * variable ray-march count remains near its two-sample minimum across most
	 * gameplay paths. Conversely, forcing the full-resolution sky path exposes
	 * large camera-space integration tiles at UE's 0.1 km minimum planet radius.
	 * Use a higher-resolution, fixed-sample SkyView LUT for the background, keep
	 * full-precision supporting LUTs, and restore every process-wide CVar after
	 * the last stylized world. SDR sky quantization is handled by the final
	 * stylized Tone pass, where the actual background/depth identity is known;
	 * it must not be applied here as a process-wide mesh/PIP override.
	 */
	class FContinuousAtmosphereOverride
	{
	public:
		static bool Acquire(FString& OutFailure)
		{
			OutFailure.Reset();
			if (ReferenceCount > 0)
			{
				++ReferenceCount;
				return true;
			}

			Overrides = {
				{ TEXT("r.SkyAtmosphere.FastSkyLUT"), 1.0f },
				{ TEXT("r.SkyAtmosphere.FastSkyLUT.Width"), 384.0f },
				{ TEXT("r.SkyAtmosphere.FastSkyLUT.Height"), 208.0f },
				{ TEXT("r.SkyAtmosphere.FastSkyLUT.SampleCountMin"), 16.0f },
				{ TEXT("r.SkyAtmosphere.FastSkyLUT.SampleCountMax"), 32.0f },
				{ TEXT("r.SkyAtmosphere.FastSkyLUT.DistanceToSampleCountMax"), 0.01f },
				{ TEXT("r.SkyAtmosphere.AerialPerspectiveLUT.FastApplyOnOpaque"), 0.0f },
				{ TEXT("r.SkyAtmosphere.SampleCountMin"), 16.0f },
				{ TEXT("r.SkyAtmosphere.SampleCountMax"), 32.0f },
				{ TEXT("r.SkyAtmosphere.DistanceToSampleCountMax"), 0.01f },
				{ TEXT("r.SkyAtmosphere.LUT32"), 1.0f },
				{ TEXT("r.SkyAtmosphere.TransmittanceLUT.SampleCount"), 32.0f },
				{ TEXT("r.SkyAtmosphere.MultiScatteringLUT.HighQuality"), 1.0f }
			};
			FString BandingExperiment;
			FParse::Value(
				FCommandLine::Get(),
				TEXT("ABTSToonSkyBandingExperiment="),
				BandingExperiment);
			if (!BandingExperiment.IsEmpty()
				&& !BandingExperiment.Equals(
					TEXT("Control"),
					ESearchCase::IgnoreCase))
			{
				auto SetDesiredValue = [](const TCHAR* Name, const float Value)
				{
					FCVarOverride* Override = Overrides.FindByPredicate(
						[Name](const FCVarOverride& Candidate)
						{
							return FCString::Stricmp(Candidate.Name, Name) == 0;
						});
					check(Override != nullptr);
					Override->DesiredValue = Value;
				};

				if (BandingExperiment.Equals(
					TEXT("HighResolution"),
					ESearchCase::IgnoreCase))
				{
					SetDesiredValue(
						TEXT("r.SkyAtmosphere.FastSkyLUT.Width"),
						768.0f);
					SetDesiredValue(
						TEXT("r.SkyAtmosphere.FastSkyLUT.Height"),
						416.0f);
				}
				else if (BandingExperiment.Equals(
					TEXT("HighSamples"),
					ESearchCase::IgnoreCase))
				{
					SetDesiredValue(
						TEXT("r.SkyAtmosphere.FastSkyLUT.SampleCountMin"),
						64.0f);
					SetDesiredValue(
						TEXT("r.SkyAtmosphere.FastSkyLUT.SampleCountMax"),
						128.0f);
				}
				else if (BandingExperiment.Equals(
					TEXT("PerPixel"),
					ESearchCase::IgnoreCase))
				{
					SetDesiredValue(
						TEXT("r.SkyAtmosphere.FastSkyLUT"),
						0.0f);
					SetDesiredValue(
						TEXT("r.SkyAtmosphere.SampleCountMin"),
						64.0f);
					SetDesiredValue(
						TEXT("r.SkyAtmosphere.SampleCountMax"),
						128.0f);
				}
				else
				{
					Overrides.Reset();
					OutFailure = FString::Printf(
						TEXT("Unknown ABTSToonSkyBandingExperiment: %s"),
						*BandingExperiment);
					return false;
				}
			}
			for (FCVarOverride& Override : Overrides)
			{
				Override.Variable = IConsoleManager::Get().FindConsoleVariable(
					Override.Name);
				if (Override.Variable == nullptr)
				{
					RestoreAppliedOverrides();
					OutFailure = FString::Printf(
						TEXT("Required UE 5.8 SkyAtmosphere CVar is unavailable: %s"),
						Override.Name);
					return false;
				}
				Override.OriginalValue = Override.Variable->GetString();
				Override.bApplied = true;
				Override.Variable->SetWithCurrentPriority(Override.DesiredValue);
				if (!FMath::IsNearlyEqual(
					Override.Variable->GetFloat(),
					Override.DesiredValue,
					1.0e-4f))
				{
					const FString RejectedName(Override.Name);
					RestoreAppliedOverrides();
					OutFailure = FString::Printf(
						TEXT("Continuous SkyAtmosphere value was not retained: %s"),
						*RejectedName);
					return false;
				}
			}
			ReferenceCount = 1;
			return true;
		}

		static void Release()
		{
			if (ReferenceCount <= 0)
			{
				return;
			}
			--ReferenceCount;
			if (ReferenceCount > 0)
			{
				return;
			}

			RestoreAppliedOverrides();
		}

		static FString DescribeEffectiveValues()
		{
			FString Result;
			for (const FCVarOverride& Override : Overrides)
			{
				if (Override.Variable != nullptr)
				{
					if (!Result.IsEmpty())
					{
						Result += TEXT(" ");
					}
					Result += FString::Printf(
						TEXT("%s=%s"),
						Override.Name,
						*Override.Variable->GetString());
				}
			}
			return Result;
		}

	private:
		struct FCVarOverride
		{
			const TCHAR* Name = nullptr;
			float DesiredValue = 0.0f;
			IConsoleVariable* Variable = nullptr;
			FString OriginalValue;
			bool bApplied = false;
		};

		static void RestoreAppliedOverrides()
		{
			for (int32 Index = Overrides.Num() - 1; Index >= 0; --Index)
			{
				FCVarOverride& Override = Overrides[Index];
				if (Override.bApplied && Override.Variable != nullptr)
				{
					Override.Variable->SetWithCurrentPriority(
						*Override.OriginalValue);
				}
			}
			Overrides.Reset();
		}

		inline static int32 ReferenceCount = 0;
		inline static TArray<FCVarOverride> Overrides;
	};

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

/**
 * Reversible, runtime-only presentation of the accepted spherical environment.
 * It deliberately edits the existing map actors instead of creating authored
 * assets, and restores every field when stylization is disabled.
 */
class FABTSToonEnvironmentPresentationState
{
public:
	bool Apply(
		UWorld& World,
		const FABTSToonEnvironmentSnapshot& Snapshot,
		const FABTSStylizedEnvironmentParameters& Parameters,
		FString& OutFailure)
	{
		OutFailure.Reset();
		if (!Snapshot.IsValid() || !Parameters.IsValid())
		{
			OutFailure = TEXT("The accepted environment snapshot is invalid.");
			return false;
		}

		TArray<ASkyAtmosphere*> Atmospheres;
		for (TActorIterator<ASkyAtmosphere> It(&World); It; ++It)
		{
			if (IsValid(*It) && IsValid(It->GetComponent()))
			{
				Atmospheres.Add(*It);
			}
		}
		if (Atmospheres.Num() != 1)
		{
			OutFailure = FString::Printf(
				TEXT("Expected exactly one SkyAtmosphere, found %d."),
				Atmospheres.Num());
			return false;
		}

		ASkyAtmosphere* AtmosphereActor = Atmospheres[0];
		USkyAtmosphereComponent* Atmosphere = AtmosphereActor->GetComponent();
		if (!bOriginalCaptured)
		{
			CaptureOriginal(World, *AtmosphereActor, *Atmosphere);
		}
		else if (SavedAtmosphereActor.Get() != AtmosphereActor
			|| SavedAtmosphereComponent.Get() != Atmosphere)
		{
			OutFailure = TEXT("The authoritative SkyAtmosphere changed during the run.");
			return false;
		}

		if (!bContinuousAtmosphereOverrideAcquired)
		{
			if (!ABTSStylizedRenderingWorldSubsystemPrivate::
				FContinuousAtmosphereOverride::Acquire(OutFailure))
			{
				return false;
			}
			bContinuousAtmosphereOverrideAcquired = true;
		}

		AtmosphereActor->SetActorLocation(
			Snapshot.PlanetCenterWorld,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		Atmosphere->TransformMode =
			ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
		Atmosphere->SetBottomRadius(FMath::Max(
			static_cast<float>(Snapshot.PlanetRadiusCM / 100000.0),
			0.1f));
		const float AtmosphereHeightKM = FMath::Max(
			Parameters.AtmosphereHeightCM / 100000.0f,
			0.1f);
		Atmosphere->SetAtmosphereHeight(AtmosphereHeightKM);
		Atmosphere->TraceSampleCountScale =
			ABTSStylizedRenderingWorldSubsystemPrivate::
				ContinuousAtmosphereTraceSampleCountScale;
		// The physically tiny ABTS planet produces much less optical path energy
		// than UE's Earth-scale defaults.  Compensate atmosphere radiance only;
		// object albedo, sun, clouds and HDR stars keep their independent exposure.
		const FLinearColor SkyLuminanceFactor =
			Parameters.Profile == EABTSStylizedRenderProfile::GroundDay
				? FLinearColor(22.0f, 24.0f, 24.0f, 1.0f)
				: FLinearColor::White;
		Atmosphere->SetSkyLuminanceFactor(SkyLuminanceFactor);
		Atmosphere->SetMultiScatteringFactor(0.75f);
		Atmosphere->SetRayleighScatteringScale(0.65f);
		Atmosphere->SetRayleighExponentialDistribution(
			FMath::Max(AtmosphereHeightKM * 0.22f, 0.001f));
		Atmosphere->SetMieScatteringScale(0.35f);
		Atmosphere->SetMieAnisotropy(0.72f);
		Atmosphere->SetMieExponentialDistribution(
			FMath::Max(AtmosphereHeightKM * 0.08f, 0.001f));
		Atmosphere->SetHeightFogContribution(0.0f);
		Atmosphere->SetAerialPerspectiveStartDepth(0.001f);
		Atmosphere->MarkRenderStateDirty();

		for (const FFogSavedState& Fog : SavedFogs)
		{
			if (UExponentialHeightFogComponent* Component = Fog.Component.Get())
			{
				Component->SetVisibility(false, true);
			}
		}
		bApplied = true;
		return true;
	}

	void Restore()
	{
		if (!bOriginalCaptured)
		{
			return;
		}
		if (bContinuousAtmosphereOverrideAcquired)
		{
			ABTSStylizedRenderingWorldSubsystemPrivate::
				FContinuousAtmosphereOverride::Release();
			bContinuousAtmosphereOverrideAcquired = false;
		}
		if (ASkyAtmosphere* Actor = SavedAtmosphereActor.Get())
		{
			Actor->SetActorLocation(
				OriginalActorLocation,
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
		}
		if (USkyAtmosphereComponent* Atmosphere =
			SavedAtmosphereComponent.Get())
		{
			Atmosphere->TransformMode = OriginalTransformMode;
			Atmosphere->SetBottomRadius(OriginalBottomRadius);
			Atmosphere->SetAtmosphereHeight(OriginalAtmosphereHeight);
			Atmosphere->TraceSampleCountScale = OriginalTraceSampleCountScale;
			Atmosphere->SetSkyLuminanceFactor(OriginalSkyLuminanceFactor);
			Atmosphere->SetMultiScatteringFactor(
				OriginalMultiScatteringFactor);
			Atmosphere->SetRayleighScatteringScale(
				OriginalRayleighScatteringScale);
			Atmosphere->SetRayleighExponentialDistribution(
				OriginalRayleighExponentialDistribution);
			Atmosphere->SetMieScatteringScale(OriginalMieScatteringScale);
			Atmosphere->SetMieAnisotropy(OriginalMieAnisotropy);
			Atmosphere->SetMieExponentialDistribution(
				OriginalMieExponentialDistribution);
			Atmosphere->SetHeightFogContribution(
				OriginalHeightFogContribution);
			Atmosphere->SetAerialPerspectiveStartDepth(
				OriginalAerialPerspectiveStartDepth);
			Atmosphere->MarkRenderStateDirty();
		}
		for (const FFogSavedState& Fog : SavedFogs)
		{
			if (UExponentialHeightFogComponent* Component = Fog.Component.Get())
			{
				Component->SetVisibility(Fog.bVisible, true);
			}
		}
		bApplied = false;
	}

	bool IsApplied() const { return bApplied; }
	int32 GetFogCount() const { return SavedFogs.Num(); }

private:
	struct FFogSavedState
	{
		TWeakObjectPtr<UExponentialHeightFogComponent> Component;
		bool bVisible = true;
	};

	void CaptureOriginal(
		UWorld& World,
		ASkyAtmosphere& Actor,
		USkyAtmosphereComponent& Atmosphere)
	{
		SavedAtmosphereActor = &Actor;
		SavedAtmosphereComponent = &Atmosphere;
		OriginalActorLocation = Actor.GetActorLocation();
		OriginalTransformMode = Atmosphere.TransformMode;
		OriginalBottomRadius = Atmosphere.BottomRadius;
		OriginalAtmosphereHeight = Atmosphere.AtmosphereHeight;
		OriginalTraceSampleCountScale = Atmosphere.TraceSampleCountScale;
		OriginalSkyLuminanceFactor = Atmosphere.SkyLuminanceFactor;
		OriginalMultiScatteringFactor = Atmosphere.MultiScatteringFactor;
		OriginalRayleighScatteringScale = Atmosphere.RayleighScatteringScale;
		OriginalRayleighExponentialDistribution =
			Atmosphere.RayleighExponentialDistribution;
		OriginalMieScatteringScale = Atmosphere.MieScatteringScale;
		OriginalMieAnisotropy = Atmosphere.MieAnisotropy;
		OriginalMieExponentialDistribution =
			Atmosphere.MieExponentialDistribution;
		OriginalHeightFogContribution = Atmosphere.HeightFogContribution;
		OriginalAerialPerspectiveStartDepth =
			Atmosphere.AerialPerspectiveStartDepth;

		SavedFogs.Reset();
		for (TActorIterator<AExponentialHeightFog> It(&World); It; ++It)
		{
			if (UExponentialHeightFogComponent* Fog =
				IsValid(*It) ? It->GetComponent() : nullptr)
			{
				FFogSavedState Saved;
				Saved.Component = Fog;
				Saved.bVisible = Fog->IsVisible();
				SavedFogs.Add(Saved);
			}
		}
		bOriginalCaptured = true;
	}

	TWeakObjectPtr<ASkyAtmosphere> SavedAtmosphereActor;
	TWeakObjectPtr<USkyAtmosphereComponent> SavedAtmosphereComponent;
	TArray<FFogSavedState> SavedFogs;
	FVector OriginalActorLocation = FVector::ZeroVector;
	ESkyAtmosphereTransformMode OriginalTransformMode =
		ESkyAtmosphereTransformMode::PlanetTopAtAbsoluteWorldOrigin;
	float OriginalBottomRadius = 0.0f;
	float OriginalAtmosphereHeight = 0.0f;
	float OriginalTraceSampleCountScale = 1.0f;
	FLinearColor OriginalSkyLuminanceFactor = FLinearColor::White;
	float OriginalMultiScatteringFactor = 0.0f;
	float OriginalRayleighScatteringScale = 0.0f;
	float OriginalRayleighExponentialDistribution = 0.0f;
	float OriginalMieScatteringScale = 0.0f;
	float OriginalMieAnisotropy = 0.0f;
	float OriginalMieExponentialDistribution = 0.0f;
	float OriginalHeightFogContribution = 0.0f;
	float OriginalAerialPerspectiveStartDepth = 0.0f;
	bool bOriginalCaptured = false;
	bool bApplied = false;
	bool bContinuousAtmosphereOverrideAcquired = false;
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
	EnvironmentPresentation =
		MakeUnique<FABTSToonEnvironmentPresentationState>();
	PreloadedSharedMaterials.Reset();
	bSharedMaterialPreloadReady = false;
	EnvironmentSnapshot = FABTSToonEnvironmentSnapshot();
	bEnvironmentSnapshotReady = false;
	LastEnvironmentDiagnosticHash = 0;
}

void UABTSStylizedRenderingWorldSubsystem::Deinitialize()
{
	FABTSStylizedRenderingControl::ClearEnvironmentParameters();
	if (EnvironmentPresentation)
	{
		EnvironmentPresentation->Restore();
		EnvironmentPresentation.Reset();
	}
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
	EnvironmentSnapshot = FABTSToonEnvironmentSnapshot();
	bEnvironmentSnapshotReady = false;
	LastEnvironmentDiagnosticHash = 0;
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

	FABTSToonEnvironmentSnapshot ResolvedEnvironment;
	FString EnvironmentFailure;
	bEnvironmentSnapshotReady =
		FABTSToonEnvironmentResolver::ResolveWorldSnapshot(
			*World,
			FABTSStylizedRenderingControl::GetProfile(),
			ResolvedEnvironment,
			&EnvironmentFailure);
	EnvironmentSnapshot = bEnvironmentSnapshotReady
		? ResolvedEnvironment
		: FABTSToonEnvironmentSnapshot();
	uint64 EnvironmentDiagnosticHash = bEnvironmentSnapshotReady
		? EnvironmentSnapshot.IdentityHash
		: GetTypeHash(EnvironmentFailure);
	EnvironmentDiagnosticHash = HashCombineFast(
		EnvironmentDiagnosticHash,
		GetTypeHash(bEnvironmentSnapshotReady));
	if (EnvironmentDiagnosticHash != LastEnvironmentDiagnosticHash)
	{
		LastEnvironmentDiagnosticHash = EnvironmentDiagnosticHash;
		if (bEnvironmentSnapshotReady)
		{
			UE_LOG(
				LogABTSRuntime,
				Log,
				TEXT("[ABTS][Rendering][T4-A0][Environment] Ready=1 Version=%d Profile=%d Seed=%d Generator=%d Attempt=%d Center=%s RadiusCM=%.2f SunToSun=%s SnapshotHash=0x%016llX"),
				EnvironmentSnapshot.Version,
				static_cast<int32>(EnvironmentSnapshot.Profile),
				EnvironmentSnapshot.WorldSeed,
				EnvironmentSnapshot.GeneratorVersion,
				EnvironmentSnapshot.GenerationAttempt,
				*EnvironmentSnapshot.PlanetCenterWorld.ToCompactString(),
				EnvironmentSnapshot.PlanetRadiusCM,
				*EnvironmentSnapshot.SunDirectionToSunWorld.ToCompactString(),
				static_cast<unsigned long long>(
					EnvironmentSnapshot.IdentityHash));
		}
		else
		{
			UE_LOG(
				LogABTSRuntime,
				Verbose,
				TEXT("[ABTS][Rendering][T4-A0][Environment] Ready=0 Reason=%s"),
				EnvironmentFailure.IsEmpty()
					? TEXT("Unknown")
					: *EnvironmentFailure);
		}
	}
	RefreshEnvironmentPresentation();

	TMap<TWeakObjectPtr<UPrimitiveComponent>, EABTSStylizedObjectClass> Desired;
	int32 M3SemanticCount = 0;
	int32 M11SemanticCount = 0;
	int32 PlayerSemanticCount = 0;
	int32 SlingshotSemanticCount = 0;
	int32 M3SurfaceStyleCount = 0;
	int32 M3BackgroundMaterialCount = 0;
	int32 M11FinaleMaterialCount = 0;
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

			if (It->IsLayoutReady())
			{
				TArray<FABTSStylizedMaterialSlotBinding> M11MaterialBindings;
				FABTSM11StylizedMaterialAdapter::CollectBindings(
					**It,
					M11MaterialBindings);
				M11FinaleMaterialCount += M11MaterialBindings.Num();
				DesiredMaterialBindings.Append(MoveTemp(M11MaterialBindings));
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
		GetTypeHash(M11FinaleMaterialCount));
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
			TEXT("[ABTS][Rendering][T3-A3] FinaleMaterialSlots=%d AppliedSlots=%d Conflicts=%d Rejected=%d Style=%d"),
			M11FinaleMaterialCount,
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

void UABTSStylizedRenderingWorldSubsystem::RefreshEnvironmentPresentation()
{
	if (!EnvironmentPresentation)
	{
		FABTSStylizedRenderingControl::ClearEnvironmentParameters();
		return;
	}

	if (!FABTSStylizedRenderingControl::IsEnabled()
		|| !bEnvironmentSnapshotReady)
	{
		FABTSStylizedRenderingControl::ClearEnvironmentParameters();
		EnvironmentPresentation->Restore();
		return;
	}

	const FABTSStylizedEnvironmentParameters Parameters =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			EnvironmentSnapshot.PlanetCenterWorld,
			EnvironmentSnapshot.PlanetRadiusCM,
			EnvironmentSnapshot.SunDirectionToSunWorld,
			EnvironmentSnapshot.Profile);
	FString Failure;
	UWorld* World = GetWorld();
	const bool bWasApplied = EnvironmentPresentation->IsApplied();
	if (World == nullptr
		|| !EnvironmentPresentation->Apply(
			*World,
			EnvironmentSnapshot,
			Parameters,
			Failure))
	{
		FABTSStylizedRenderingControl::ClearEnvironmentParameters();
		EnvironmentPresentation->Restore();
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][Rendering][T4-A1][Environment] Applied=0 Reason=%s"),
			Failure.IsEmpty() ? TEXT("WorldUnavailable") : *Failure);
		return;
	}

	FABTSStylizedRenderingControl::SetEnvironmentParameters(Parameters);
	if (!bWasApplied)
	{
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Rendering][T4-A1][AtmosphereQuality] %s TraceScale=%.2f"),
			*ABTSStylizedRenderingWorldSubsystemPrivate::
				FContinuousAtmosphereOverride::DescribeEffectiveValues(),
			ABTSStylizedRenderingWorldSubsystemPrivate::
				ContinuousAtmosphereTraceSampleCountScale);
	}
	UE_LOG(
		LogABTSRuntime,
		VeryVerbose,
		TEXT("[ABTS][Rendering][T4-A1][Environment] Applied=1 Profile=%d RadiusCM=%.2f AtmosphereHeightCM=%.2f FogHidden=%d StarSeed=%u"),
		static_cast<int32>(Parameters.Profile),
		Parameters.PlanetRadiusCM,
		Parameters.AtmosphereHeightCM,
		EnvironmentPresentation->GetFogCount(),
		Parameters.StarSeed);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A1ContinuousAtmosphereOverrideTest,
	"ABTS.Rendering.Toon.T4A1.ContinuousAtmosphereOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A1ContinuousAtmosphereOverrideTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	IConsoleVariable* FastSky = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.FastSkyLUT"));
	IConsoleVariable* FastSkyWidth = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.FastSkyLUT.Width"));
	IConsoleVariable* FastSkyHeight = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.FastSkyLUT.Height"));
	IConsoleVariable* FastSkySampleMin = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.FastSkyLUT.SampleCountMin"));
	IConsoleVariable* FastSkySampleMax = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.FastSkyLUT.SampleCountMax"));
	IConsoleVariable* FastSkySampleDistance =
		IConsoleManager::Get().FindConsoleVariable(
			TEXT("r.SkyAtmosphere.FastSkyLUT.DistanceToSampleCountMax"));
	IConsoleVariable* FastAerial = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.AerialPerspectiveLUT.FastApplyOnOpaque"));
	IConsoleVariable* SampleMin = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.SampleCountMin"));
	IConsoleVariable* SampleMax = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.SampleCountMax"));
	IConsoleVariable* SampleDistance = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.DistanceToSampleCountMax"));
	IConsoleVariable* LUT32 = IConsoleManager::Get().FindConsoleVariable(
		TEXT("r.SkyAtmosphere.LUT32"));
	TestNotNull(TEXT("Fast sky CVar exists in UE 5.8"), FastSky);
	TestNotNull(TEXT("Fast sky width CVar exists in UE 5.8"), FastSkyWidth);
	TestNotNull(TEXT("Fast sky height CVar exists in UE 5.8"), FastSkyHeight);
	TestNotNull(TEXT("Fast sky minimum sample CVar exists in UE 5.8"),
		FastSkySampleMin);
	TestNotNull(TEXT("Fast sky maximum sample CVar exists in UE 5.8"),
		FastSkySampleMax);
	TestNotNull(TEXT("Fast sky sample distance CVar exists in UE 5.8"),
		FastSkySampleDistance);
	TestNotNull(TEXT("Fast aerial CVar exists in UE 5.8"), FastAerial);
	TestNotNull(TEXT("Full sky minimum sample CVar exists in UE 5.8"), SampleMin);
	TestNotNull(TEXT("Full sky maximum sample CVar exists in UE 5.8"), SampleMax);
	TestNotNull(TEXT("Full sky sample distance CVar exists in UE 5.8"), SampleDistance);
	TestNotNull(TEXT("Full precision LUT CVar exists in UE 5.8"), LUT32);
	if (FastSky == nullptr || FastSkyWidth == nullptr || FastSkyHeight == nullptr
		|| FastSkySampleMin == nullptr || FastSkySampleMax == nullptr
		|| FastSkySampleDistance == nullptr || FastAerial == nullptr
		|| SampleMin == nullptr
		|| SampleMax == nullptr || SampleDistance == nullptr || LUT32 == nullptr)
	{
		return false;
	}

	const int32 OriginalFastSky = FastSky->GetInt();
	const float OriginalFastSkyWidth = FastSkyWidth->GetFloat();
	const float OriginalFastSkyHeight = FastSkyHeight->GetFloat();
	const float OriginalFastSkySampleMin = FastSkySampleMin->GetFloat();
	const float OriginalFastSkySampleMax = FastSkySampleMax->GetFloat();
	const float OriginalFastSkySampleDistance = FastSkySampleDistance->GetFloat();
	const int32 OriginalFastAerial = FastAerial->GetInt();
	const float OriginalSampleMin = SampleMin->GetFloat();
	const float OriginalSampleMax = SampleMax->GetFloat();
	const float OriginalSampleDistance = SampleDistance->GetFloat();
	const int32 OriginalLUT32 = LUT32->GetInt();
	FString Failure;
	TestTrue(
		TEXT("First stylized world acquires the continuous atmosphere override"),
		ABTSStylizedRenderingWorldSubsystemPrivate::
			FContinuousAtmosphereOverride::Acquire(Failure));
	TestTrue(TEXT("Acquire failure remains empty"), Failure.IsEmpty());
	TestEqual(TEXT("Fast sky LUT is enabled while owned"), FastSky->GetInt(), 1);
	TestTrue(TEXT("SkyView LUT width is doubled"),
		FMath::IsNearlyEqual(FastSkyWidth->GetFloat(), 384.0f));
	TestTrue(TEXT("SkyView LUT height is doubled"),
		FMath::IsNearlyEqual(FastSkyHeight->GetFloat(), 208.0f));
	TestTrue(TEXT("SkyView LUT has a 16-sample floor"),
		FMath::IsNearlyEqual(FastSkySampleMin->GetFloat(), 16.0f));
	TestTrue(TEXT("SkyView LUT is capped at 32 samples"),
		FMath::IsNearlyEqual(FastSkySampleMax->GetFloat(), 32.0f));
	TestTrue(TEXT("Tiny-planet SkyView rays reach the cap after ten metres"),
		FMath::IsNearlyEqual(FastSkySampleDistance->GetFloat(), 0.01f));
	TestEqual(TEXT("Fast aerial LUT is disabled while owned"), FastAerial->GetInt(), 0);
	TestTrue(TEXT("Full ray march has a 16-sample floor"),
		FMath::IsNearlyEqual(SampleMin->GetFloat(), 16.0f));
	TestTrue(TEXT("Full ray march is capped at 32 samples"),
		FMath::IsNearlyEqual(SampleMax->GetFloat(), 32.0f));
	TestTrue(TEXT("Tiny-planet rays reach the cap after ten metres"),
		FMath::IsNearlyEqual(SampleDistance->GetFloat(), 0.01f));
	TestEqual(TEXT("Atmosphere LUTs use full precision while owned"),
		LUT32->GetInt(), 1);

	Failure.Reset();
	TestTrue(
		TEXT("A second game world shares the same process override"),
		ABTSStylizedRenderingWorldSubsystemPrivate::
			FContinuousAtmosphereOverride::Acquire(Failure));
	ABTSStylizedRenderingWorldSubsystemPrivate::
		FContinuousAtmosphereOverride::Release();
	TestEqual(TEXT("One remaining owner keeps fast sky enabled"), FastSky->GetInt(), 1);
	TestEqual(TEXT("One remaining owner keeps fast aerial disabled"), FastAerial->GetInt(), 0);

	ABTSStylizedRenderingWorldSubsystemPrivate::
		FContinuousAtmosphereOverride::Release();
	TestEqual(TEXT("Final release restores the original fast sky value"),
		FastSky->GetInt(), OriginalFastSky);
	TestTrue(TEXT("Final release restores the original SkyView LUT width"),
		FMath::IsNearlyEqual(FastSkyWidth->GetFloat(), OriginalFastSkyWidth));
	TestTrue(TEXT("Final release restores the original SkyView LUT height"),
		FMath::IsNearlyEqual(FastSkyHeight->GetFloat(), OriginalFastSkyHeight));
	TestTrue(TEXT("Final release restores the original SkyView minimum samples"),
		FMath::IsNearlyEqual(
			FastSkySampleMin->GetFloat(), OriginalFastSkySampleMin));
	TestTrue(TEXT("Final release restores the original SkyView maximum samples"),
		FMath::IsNearlyEqual(
			FastSkySampleMax->GetFloat(), OriginalFastSkySampleMax));
	TestTrue(TEXT("Final release restores the original SkyView sample distance"),
		FMath::IsNearlyEqual(
			FastSkySampleDistance->GetFloat(), OriginalFastSkySampleDistance));
	TestEqual(TEXT("Final release restores the original fast aerial value"),
		FastAerial->GetInt(), OriginalFastAerial);
	TestTrue(TEXT("Final release restores the original minimum samples"),
		FMath::IsNearlyEqual(SampleMin->GetFloat(), OriginalSampleMin));
	TestTrue(TEXT("Final release restores the original maximum samples"),
		FMath::IsNearlyEqual(SampleMax->GetFloat(), OriginalSampleMax));
	TestTrue(TEXT("Final release restores the original sample distance"),
		FMath::IsNearlyEqual(
			SampleDistance->GetFloat(), OriginalSampleDistance));
	TestEqual(TEXT("Final release restores the original LUT precision"),
		LUT32->GetInt(), OriginalLUT32);
	return true;
}

#endif
