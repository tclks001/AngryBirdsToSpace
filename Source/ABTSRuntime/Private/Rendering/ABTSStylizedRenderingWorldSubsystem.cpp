// Copyright Epic Games, Inc. All Rights Reserved.

#include "Rendering/ABTSStylizedRenderingWorldSubsystem.h"

#include "Components/ExponentialHeightFogComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "ProceduralMeshComponent.h"

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
#include "Rendering/ABTST4LowPolyCloudPrototype.h"
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
		TArray<AVolumetricCloud*> CloudActors;
		for (TActorIterator<AVolumetricCloud> It(&World); It; ++It)
		{
			if (IsValid(*It)
				&& IsValid(It->FindComponentByClass<UVolumetricCloudComponent>()))
			{
				CloudActors.Add(*It);
			}
		}
		if (CloudActors.Num() != 1)
		{
			OutFailure = FString::Printf(
				TEXT("Expected exactly one VolumetricCloud, found %d."),
				CloudActors.Num());
			return false;
		}

		ASkyAtmosphere* AtmosphereActor = Atmospheres[0];
		USkyAtmosphereComponent* Atmosphere = AtmosphereActor->GetComponent();
		UVolumetricCloudComponent* Cloud =
			CloudActors[0]->FindComponentByClass<UVolumetricCloudComponent>();
		if (!bOriginalCaptured)
		{
			CaptureOriginal(World, *AtmosphereActor, *Atmosphere, *Cloud);
		}
		else if (SavedAtmosphereActor.Get() != AtmosphereActor
			|| SavedAtmosphereComponent.Get() != Atmosphere
			|| SavedCloudComponent.Get() != Cloud)
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

		// The stock Earth-scale volumetric material was proven to have no usable
		// interval at ABTS scale: it is transparent at authored density and a
		// uniform grey veil once optical depth is compensated. Hide it while the
		// reversible stylized presentation owns the world. T4-A2R0 supplies three
		// bounded, deterministic low-poly cloud islands instead of a global shell.
		Cloud->SetVisibility(false, true);
		bLowPolyCloudPrototypeEnabled = Parameters.bCloudsEnabled != 0u;
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
		if (UVolumetricCloudComponent* Cloud = SavedCloudComponent.Get())
		{
			Cloud->SetLayerBottomAltitude(OriginalCloudLayerBottomAltitude);
			Cloud->SetLayerHeight(OriginalCloudLayerHeight);
			Cloud->SetTracingStartMaxDistance(OriginalCloudTracingStartMaxDistance);
			Cloud->SetTracingStartDistanceFromCamera(
				OriginalCloudTracingStartDistanceFromCamera);
			Cloud->TracingMaxDistanceMode = OriginalCloudTracingMaxDistanceMode;
			Cloud->SetTracingMaxDistance(OriginalCloudTracingMaxDistance);
			Cloud->SetPlanetRadius(OriginalCloudPlanetRadius);
			Cloud->SetbUsePerSampleAtmosphericLightTransmittance(
				bOriginalCloudPerSampleTransmittance);
			Cloud->SetSkyLightCloudBottomOcclusion(
				OriginalCloudBottomOcclusion);
			Cloud->SetViewSampleCountScale(OriginalCloudViewSampleScale);
			Cloud->SetReflectionViewSampleCountScale(
				OriginalCloudReflectionSampleScale);
			Cloud->SetShadowViewSampleCountScale(OriginalCloudShadowSampleScale);
			Cloud->SetShadowReflectionViewSampleCountScale(
				OriginalCloudShadowReflectionSampleScale);
			Cloud->SetShadowTracingDistance(OriginalCloudShadowTracingDistance);
			Cloud->SetStopTracingTransmittanceThreshold(
				OriginalCloudStopTracingThreshold);
			Cloud->SetMaterial(OriginalCloudMaterial.Get());
			Cloud->SetVisibility(bOriginalCloudVisible, true);
		}
		bLowPolyCloudPrototypeEnabled = false;
		bApplied = false;
	}

	bool IsApplied() const { return bApplied; }
	int32 GetFogCount() const { return SavedFogs.Num(); }
	bool IsCloudApplied() const
	{
		return bApplied && bLowPolyCloudPrototypeEnabled;
	}

private:
	struct FFogSavedState
	{
		TWeakObjectPtr<UExponentialHeightFogComponent> Component;
		bool bVisible = true;
	};

	void CaptureOriginal(
		UWorld& World,
		ASkyAtmosphere& Actor,
		USkyAtmosphereComponent& Atmosphere,
		UVolumetricCloudComponent& Cloud)
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
		SavedCloudComponent = &Cloud;
		OriginalCloudLayerBottomAltitude = Cloud.LayerBottomAltitude;
		OriginalCloudLayerHeight = Cloud.LayerHeight;
		OriginalCloudTracingStartMaxDistance = Cloud.TracingStartMaxDistance;
		OriginalCloudTracingStartDistanceFromCamera =
			Cloud.TracingStartDistanceFromCamera;
		OriginalCloudTracingMaxDistanceMode = Cloud.TracingMaxDistanceMode;
		OriginalCloudTracingMaxDistance = Cloud.TracingMaxDistance;
		OriginalCloudPlanetRadius = Cloud.PlanetRadius;
		OriginalCloudMaterial = Cloud.GetMaterial();
		bOriginalCloudPerSampleTransmittance =
			Cloud.bUsePerSampleAtmosphericLightTransmittance != 0;
		OriginalCloudBottomOcclusion = Cloud.SkyLightCloudBottomOcclusion;
		OriginalCloudViewSampleScale = Cloud.ViewSampleCountScale;
		OriginalCloudReflectionSampleScale =
			Cloud.ReflectionViewSampleCountScaleValue;
		OriginalCloudShadowSampleScale = Cloud.ShadowViewSampleCountScale;
		OriginalCloudShadowReflectionSampleScale =
			Cloud.ShadowReflectionViewSampleCountScaleValue;
		OriginalCloudShadowTracingDistance = Cloud.ShadowTracingDistance;
		OriginalCloudStopTracingThreshold =
			Cloud.StopTracingTransmittanceThreshold;
		bOriginalCloudVisible = Cloud.IsVisible();

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
	TWeakObjectPtr<UVolumetricCloudComponent> SavedCloudComponent;
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
	float OriginalCloudLayerBottomAltitude = 0.0f;
	float OriginalCloudLayerHeight = 0.0f;
	float OriginalCloudTracingStartMaxDistance = 0.0f;
	float OriginalCloudTracingStartDistanceFromCamera = 0.0f;
	EVolumetricCloudTracingMaxDistanceMode OriginalCloudTracingMaxDistanceMode =
		EVolumetricCloudTracingMaxDistanceMode::DistanceFromCloudLayerEntryPoint;
	float OriginalCloudTracingMaxDistance = 0.0f;
	float OriginalCloudPlanetRadius = 0.0f;
	TWeakObjectPtr<UMaterialInterface> OriginalCloudMaterial;
	float OriginalCloudBottomOcclusion = 0.0f;
	float OriginalCloudViewSampleScale = 1.0f;
	float OriginalCloudReflectionSampleScale = 1.0f;
	float OriginalCloudShadowSampleScale = 1.0f;
	float OriginalCloudShadowReflectionSampleScale = 1.0f;
	float OriginalCloudShadowTracingDistance = 0.0f;
	float OriginalCloudStopTracingThreshold = 0.0f;
	bool bOriginalCloudPerSampleTransmittance = false;
	bool bOriginalCloudVisible = true;
	bool bOriginalCaptured = false;
	bool bApplied = false;
	bool bContinuousAtmosphereOverrideAcquired = false;
	bool bLowPolyCloudPrototypeEnabled = false;
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
	DestroyLowPolyCloudPrototype();
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
		DestroyLowPolyCloudPrototype();
		return;
	}

	if (!FABTSStylizedRenderingControl::IsEnabled()
		|| !bEnvironmentSnapshotReady)
	{
		FABTSStylizedRenderingControl::ClearEnvironmentParameters();
		DestroyLowPolyCloudPrototype();
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
		DestroyLowPolyCloudPrototype();
		EnvironmentPresentation->Restore();
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][Rendering][T4-A2][Environment] Applied=0 Reason=%s"),
			Failure.IsEmpty() ? TEXT("WorldUnavailable") : *Failure);
		return;
	}
	if (!RefreshLowPolyCloudPrototype(Parameters, Failure))
	{
		FABTSStylizedRenderingControl::ClearEnvironmentParameters();
		DestroyLowPolyCloudPrototype();
		EnvironmentPresentation->Restore();
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][Rendering][T4-A2R1A][CloudPrototype] Applied=0 Reason=%s"),
			Failure.IsEmpty() ? TEXT("Unknown") : *Failure);
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
	if (!bWasApplied && EnvironmentPresentation->IsCloudApplied())
	{
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][Rendering][T4-A2.2][CloudQuality] Route=InstancedCloudletsA2_2NightMegaCluster Islands=%d LogicalClouds=%d BackgroundClouds=%d TerminatorMegaClouds=%d WeatherSystems=%d MacroClusters=%d Cloudlets=%d Body=%d Crown=%d Edge=%d GlobalCoverage=1 BackgroundSunIndependentPlacement=1 TerminatorMegaSunRelative=1 TerminatorMegaConnected=1 SizeVariation=1 FusionDiagnostics=5 SphericalConformal=1 DetachedEdges=0 CustomDataFloats=%d Deterministic=1 Material=Unlit ViewInvariantIslandField=1 ViewInvariantVolumeGradient=1 CameraDependentLighting=0 ContinuousMacroNormal=1 GradientCoherenceGuard=1 GradientJunctionGate=1 PlanarCoreClosure=1 UndersideField=1 CriticalPointFallback=IslandUp ThreeBandColor=1 SunwardWhitening=1 ThinDensityWhitening=1 ViewIndependentWhitening=1 LocalSolarHeight=1 NightWhiteningGate=1 NightBrightness=%.2f DayBlend=[%.2f,%.2f] GenericObjectToneBypass=1 MacroNormalStrength=0.84 PixelLocalNormalWeight=0 PixelInstanceVariation=0 VertexNoiseWPO=1 CloudCompositeStencil=%d CloudToCloudOutlineSuppression=1 CloudToWorldOutlinePreserved=1 LogicalHash=%llu NativeActorHidden=1 Collision=0 Shadows=0 LayoutHash=%llu BaseCM=%.1f HeightCM=%.1f"),
			FABTST4LowPolyCloudPrototype::IslandCount,
			LowPolyLogicalCloudCount,
			FABTST4LowPolyCloudPrototype::GlobalIslandCount,
			FABTST4LowPolyCloudPrototype::TerminatorMegaClusterIslandCount,
			FABTST4LowPolyCloudPrototype::WeatherSystemCount,
			FABTST4LowPolyCloudPrototype::IslandCount
				* FABTST4LowPolyCloudPrototype::MacroClusterCountPerIsland,
			FABTST4LowPolyCloudPrototype::TotalCloudletCount,
			FABTST4LowPolyCloudPrototype::TotalBodyCloudletCount,
			FABTST4LowPolyCloudPrototype::TotalCrownCloudletCount,
			FABTST4LowPolyCloudPrototype::TotalEdgeCloudletCount,
			FABTST4LowPolyCloudPrototype::CloudletCustomDataFloatCount,
			FABTST4LowPolyCloudPrototype::NightBrightness,
			FABTST4LowPolyCloudPrototype::DaylightBlendMinSolarHeight,
			FABTST4LowPolyCloudPrototype::DaylightBlendMaxSolarHeight,
			static_cast<int32>(FABTSStylizedRenderingContract::
				ResolveCloudCompositeStencilValueForRenderer()),
			static_cast<unsigned long long>(LowPolyLogicalCloudLayoutHash),
			static_cast<unsigned long long>(LowPolyCloudLayoutHash),
			Parameters.CloudBaseAltitudeCM,
			Parameters.CloudLayerHeightCM);
	}
	UE_LOG(
		LogABTSRuntime,
		VeryVerbose,
		TEXT("[ABTS][Rendering][T4-A2][Environment] Applied=1 Profile=%d RadiusCM=%.2f AtmosphereHeightCM=%.2f FogHidden=%d Cloud=%d StarSeed=%u"),
		static_cast<int32>(Parameters.Profile),
		Parameters.PlanetRadiusCM,
		Parameters.AtmosphereHeightCM,
		EnvironmentPresentation->GetFogCount(),
		EnvironmentPresentation->IsCloudApplied() ? 1 : 0,
		Parameters.StarSeed);
}

bool UABTSStylizedRenderingWorldSubsystem::RefreshLowPolyCloudPrototype(
	const FABTSStylizedEnvironmentParameters& Parameters,
	FString& OutFailure)
{
	OutFailure.Reset();
	if (Parameters.bCloudsEnabled == 0u)
	{
		DestroyLowPolyCloudPrototype();
		return true;
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		OutFailure = TEXT("Cloud prototype world is unavailable.");
		return false;
	}
	const TArray<FABTST4LowPolyCloudIslandDefinition> Definitions =
		FABTST4LowPolyCloudPrototype::BuildDefinitions(
			Parameters.PlanetCenterWorld,
			Parameters.PlanetRadiusCM,
			Parameters.StarSeed ^ 0xC10DF13Du,
			FVector(Parameters.SunDirectionToSunWorld),
			Parameters.CloudBaseAltitudeCM,
			Parameters.CloudLayerHeightCM);
	if (Definitions.Num() != FABTST4LowPolyCloudPrototype::IslandCount)
	{
		OutFailure = TEXT("Global cloud field did not produce its frozen island count.");
		return false;
	}
	const uint64 DesiredLogicalCloudHash =
		FABTST4LowPolyCloudPrototype::ComputeLogicalCloudLayoutHash(Definitions);
	if (DesiredLogicalCloudHash == 0)
	{
		OutFailure = TEXT("Logical cloud identity layout is invalid.");
		return false;
	}
	if (FABTST4LowPolyCloudPrototype::CountCloudFusionPairs(Definitions)
		< FABTST4LowPolyCloudPrototype::WeatherSystemCount)
	{
		OutFailure = TEXT("Global cloud field has insufficient neighbouring fusion pairs.");
		return false;
	}
	if (FABTST4LowPolyCloudPrototype::CountTerminatorMegaClusterClouds(
		Definitions)
		!= FABTST4LowPolyCloudPrototype::TerminatorMegaClusterIslandCount)
	{
		OutFailure = TEXT("Terminator mega-cluster member count is incomplete.");
		return false;
	}
	if (!FABTST4LowPolyCloudPrototype::
		IsTerminatorMegaClusterEnvelopeConnected(Definitions))
	{
		OutFailure = TEXT("Terminator mega-cluster envelope is disconnected.");
		return false;
	}
	const double TerminatorMegaSpanDegrees = FABTST4LowPolyCloudPrototype::
		ComputeTerminatorMegaClusterAngularSpanDegrees(Definitions);
	if (TerminatorMegaSpanDegrees < 27.0 || TerminatorMegaSpanDegrees > 33.0)
	{
		OutFailure = FString::Printf(
			TEXT("Terminator mega-cluster span %.2f degrees is outside [27, 33]."),
			TerminatorMegaSpanDegrees);
		return false;
	}
	const uint64 DesiredLayoutHash =
		FABTST4LowPolyCloudPrototype::ComputeLayoutHash(Definitions);
	uint64 DesiredCloudletHash = DesiredLayoutHash;
	DesiredCloudletHash ^= DesiredLogicalCloudHash + 0x9e3779b97f4a7c15ull
		+ (DesiredCloudletHash << 6) + (DesiredCloudletHash >> 2);
	TArray<TArray<FABTST4InstancedCloudletDefinition>> IslandCloudlets;
	IslandCloudlets.SetNum(Definitions.Num());
	int32 TotalCloudlets = 0;
	for (int32 DefinitionIndex = 0;
		DefinitionIndex < Definitions.Num();
		++DefinitionIndex)
	{
		FString CloudletFailure;
		if (!FABTST4LowPolyCloudPrototype::BuildInstancedCloudlets(
			Definitions[DefinitionIndex],
			IslandCloudlets[DefinitionIndex],
			&CloudletFailure))
		{
			OutFailure = FString::Printf(
				TEXT("Cloud island %d instance layout failed: %s"),
				Definitions[DefinitionIndex].IslandIndex,
				*CloudletFailure);
			return false;
		}
		const uint64 IslandHash =
			FABTST4LowPolyCloudPrototype::ComputeCloudletLayoutHash(
				IslandCloudlets[DefinitionIndex]);
		DesiredCloudletHash ^= IslandHash + 0x9e3779b97f4a7c15ull
			+ (DesiredCloudletHash << 6) + (DesiredCloudletHash >> 2);
		TotalCloudlets += IslandCloudlets[DefinitionIndex].Num();
	}
	if (TotalCloudlets != FABTST4LowPolyCloudPrototype::TotalCloudletCount)
	{
		OutFailure = FString::Printf(
			TEXT("Cloudlet budget mismatch: expected %d, produced %d."),
			FABTST4LowPolyCloudPrototype::TotalCloudletCount,
			TotalCloudlets);
		return false;
	}
	if (LowPolyCloudPrototypeActor.IsValid()
		&& LowPolyCloudLayoutHash == DesiredCloudletHash)
	{
		LowPolyLogicalCloudLayoutHash = DesiredLogicalCloudHash;
		LowPolyLogicalCloudCount = Definitions.Num();
		return true;
	}
	DestroyLowPolyCloudPrototype();

	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Game/Toon/Environment/Cloud/M_ABTS_Toon_Cloudlet.M_ABTS_Toon_Cloudlet"));
	if (!IsValid(BaseMaterial))
	{
		OutFailure = TEXT("T4-A2R1-B cloudlet material is unavailable.");
		return false;
	}
	float MaterialMacroLightingVersion = 0.0f;
	float MaterialNightBrightness = 0.0f;
	float MaterialDaylightBlendMin = 0.0f;
	float MaterialDaylightBlendMax = 0.0f;
	FLinearColor MaterialPlanetCenter = FLinearColor::Transparent;
	const bool bMaterialContractValid =
		BaseMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudMacroLightingVersion")),
			MaterialMacroLightingVersion)
		&& BaseMaterial->GetVectorParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudPlanetCenter")),
			MaterialPlanetCenter)
		&& BaseMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudNightBrightness")),
			MaterialNightBrightness)
		&& BaseMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(
				TEXT("ABTS_CloudDaylightBlendMinSolarHeight")),
			MaterialDaylightBlendMin)
		&& BaseMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(
				TEXT("ABTS_CloudDaylightBlendMaxSolarHeight")),
			MaterialDaylightBlendMax)
		&& FMath::IsNearlyEqual(MaterialMacroLightingVersion, 8.0f)
		&& FMath::IsNearlyEqual(
			MaterialNightBrightness,
			FABTST4LowPolyCloudPrototype::NightBrightness)
		&& FMath::IsNearlyEqual(
			MaterialDaylightBlendMin,
			FABTST4LowPolyCloudPrototype::DaylightBlendMinSolarHeight)
		&& FMath::IsNearlyEqual(
			MaterialDaylightBlendMax,
			FABTST4LowPolyCloudPrototype::DaylightBlendMaxSolarHeight);
	if (!bMaterialContractValid)
	{
		OutFailure = FString::Printf(
			TEXT("T4-A2.2 cloud material contract mismatch (Version=%.2f Night=%.2f Blend=[%.2f,%.2f])."),
			MaterialMacroLightingVersion,
			MaterialNightBrightness,
			MaterialDaylightBlendMin,
			MaterialDaylightBlendMax);
		return false;
	}
	UStaticMesh* CloudletMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Game/Toon/Environment/Cloud/SM_ABTS_Toon_Cloudlet.SM_ABTS_Toon_Cloudlet"));
	if (!IsValid(CloudletMesh))
	{
		OutFailure = TEXT("T4-A2R1-B cloudlet mesh is unavailable.");
		return false;
	}
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = MakeUniqueObjectName(
		World,
		AActor::StaticClass(),
		TEXT("ABTST4InstancedCloudPrototype"));
	SpawnParameters.ObjectFlags |= RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = World->SpawnActor<AActor>(
		AActor::StaticClass(),
		FTransform(FQuat::Identity, Parameters.PlanetCenterWorld),
		SpawnParameters);
	if (!IsValid(Actor))
	{
		OutFailure = TEXT("Unable to spawn the transient cloud prototype actor.");
		return false;
	}
	Actor->SetActorEnableCollision(false);
	USceneComponent* Root = NewObject<USceneComponent>(
		Actor,
		TEXT("CloudPrototypeRoot"),
		RF_Transient);
	if (!IsValid(Root))
	{
		Actor->Destroy();
		OutFailure = TEXT("Unable to allocate the cloud prototype root.");
		return false;
	}
	Actor->AddInstanceComponent(Root);
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	Actor->SetActorLocation(
		Parameters.PlanetCenterWorld,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);

	for (int32 DefinitionIndex = 0;
		DefinitionIndex < Definitions.Num();
		++DefinitionIndex)
	{
		const FABTST4LowPolyCloudIslandDefinition& Definition =
			Definitions[DefinitionIndex];
		UHierarchicalInstancedStaticMeshComponent* Component =
			NewObject<UHierarchicalInstancedStaticMeshComponent>(
				Actor,
				*FString::Printf(
					TEXT("CloudIslandInstances_%d"),
					Definition.IslandIndex),
				RF_Transient);
		if (!IsValid(Component))
		{
			Actor->Destroy();
			OutFailure = TEXT("Unable to allocate a cloud island component.");
			return false;
		}
		Actor->AddInstanceComponent(Component);
		Component->SetupAttachment(Root);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(CloudletMesh);
		Component->SetNumCustomDataFloats(
			FABTST4LowPolyCloudPrototype::CloudletCustomDataFloatCount);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCastShadow(false);
		Component->bCastDynamicShadow = false;
		Component->bCastStaticShadow = false;
		Component->SetCustomDepthStencilWriteMask(
			ERendererStencilMask::ERSM_Default);
		Component->SetCustomDepthStencilValue(
			FABTSStylizedRenderingContract::
				ResolveCloudCompositeStencilValueForRenderer());
		Component->SetRenderCustomDepth(true);
		Component->RegisterComponent();
		for (const FABTST4InstancedCloudletDefinition& Cloudlet
			: IslandCloudlets[DefinitionIndex])
		{
			const int32 InstanceIndex = Component->AddInstance(
				Cloudlet.TransformRelativeToPlanet, false);
			if (InstanceIndex == INDEX_NONE)
			{
				Actor->Destroy();
				OutFailure = TEXT("Unable to add a cloudlet instance.");
				return false;
			}
			Component->SetCustomDataValue(
				InstanceIndex, 0, Cloudlet.Seed01, false);
			Component->SetCustomDataValue(
				InstanceIndex, 1, Cloudlet.NormalizedHeight, false);
			Component->SetCustomDataValue(
				InstanceIndex, 2, Cloudlet.FakeOcclusion, false);
			Component->SetCustomDataValue(
				InstanceIndex, 3, Cloudlet.SizeTier, false);
			Component->SetCustomDataValue(
				InstanceIndex,
				4,
				static_cast<float>(Cloudlet.Layer) / 2.0f,
				false);
		}
		Component->MarkRenderStateDirty();
		UMaterialInstanceDynamic* Material =
			UMaterialInstanceDynamic::Create(BaseMaterial, Component);
		if (!IsValid(Material))
		{
			Actor->Destroy();
			OutFailure = TEXT("Unable to allocate the T4-A2R1-B cloudlet material instance.");
			return false;
		}
		const FVector SunDirection =
			FVector(Parameters.SunDirectionToSunWorld).GetSafeNormal();
		Material->SetVectorParameterValue(
			TEXT("ABTS_CloudSunDirection"),
			FLinearColor(
				SunDirection.X,
				SunDirection.Y,
				SunDirection.Z,
				0.0f));
		Material->SetVectorParameterValue(
			TEXT("ABTS_CloudPlanetCenter"),
			FLinearColor(
				Parameters.PlanetCenterWorld.X,
				Parameters.PlanetCenterWorld.Y,
				Parameters.PlanetCenterWorld.Z,
				0.0f));
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudNightBrightness"),
			FABTST4LowPolyCloudPrototype::NightBrightness);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudDaylightBlendMinSolarHeight"),
			FABTST4LowPolyCloudPrototype::DaylightBlendMinSolarHeight);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudDaylightBlendMaxSolarHeight"),
			FABTST4LowPolyCloudPrototype::DaylightBlendMaxSolarHeight);
		const FLinearColor LightColor = Definition.IslandIndex == 1
			? FLinearColor(0.86f, 0.89f, 0.94f, 1.0f)
			: FLinearColor(0.92f, 0.93f, 0.96f, 1.0f);
		const FLinearColor BodyColor = Definition.IslandIndex == 1
			? FLinearColor(0.38f, 0.46f, 0.58f, 1.0f)
			: FLinearColor(0.45f, 0.53f, 0.64f, 1.0f);
		const FLinearColor ShadowColor = Definition.IslandIndex == 1
			? FLinearColor(0.15f, 0.20f, 0.30f, 1.0f)
			: FLinearColor(0.18f, 0.24f, 0.34f, 1.0f);
		Material->SetVectorParameterValue(
			TEXT("ABTS_CloudLightColor"), LightColor);
		Material->SetVectorParameterValue(
			TEXT("ABTS_CloudBodyColor"), BodyColor);
		Material->SetVectorParameterValue(
			TEXT("ABTS_CloudShadowColor"), ShadowColor);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudSunWhiteStrength"), 0.70f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudThinWhiteStrength"), 0.58f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudThinDensityStart"), 0.30f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudThinDensityEnd"), 0.78f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudGradientConfidenceStart"), 0.10f);
		Material->SetScalarParameterValue(
			TEXT("ABTS_CloudGradientConfidenceEnd"), 0.34f);
		Material->SetVectorParameterValue(
			TEXT("ABTS_CloudIslandCenter"),
			FLinearColor(
				Definition.CenterWorld.X,
				Definition.CenterWorld.Y,
				Definition.CenterWorld.Z,
				0.0f));
		Material->SetVectorParameterValue(
			TEXT("ABTS_CloudIslandAxisX"),
			FLinearColor(
				Definition.TangentX.X,
				Definition.TangentX.Y,
				Definition.TangentX.Z,
				0.0f));
		Material->SetVectorParameterValue(
			TEXT("ABTS_CloudIslandAxisY"),
			FLinearColor(
				Definition.TangentY.X,
				Definition.TangentY.Y,
				Definition.TangentY.Z,
				0.0f));
		Material->SetVectorParameterValue(
			TEXT("ABTS_CloudIslandUp"),
			FLinearColor(
				Definition.RadialUp.X,
				Definition.RadialUp.Y,
				Definition.RadialUp.Z,
				0.0f));
		Material->SetVectorParameterValue(
			TEXT("ABTS_CloudIslandExtents"),
			FLinearColor(
				Definition.ExtentsCM.X,
				Definition.ExtentsCM.Y,
				Definition.ExtentsCM.Z,
				0.0f));
		const TArray<FABTST4CloudMacroClusterDefinition> MacroClusters =
			FABTST4LowPolyCloudPrototype::BuildMacroClusters(Definition);
		if (MacroClusters.Num()
			!= FABTST4LowPolyCloudPrototype::MacroClusterCountPerIsland)
		{
			Actor->Destroy();
			OutFailure = TEXT("R1-C2-B3-B1 gradient-confidence island parameters are incomplete.");
			return false;
		}
		for (const FABTST4CloudMacroClusterDefinition& Cluster : MacroClusters)
		{
			Material->SetVectorParameterValue(
				*FString::Printf(
					TEXT("ABTS_CloudMacroCluster%d"), Cluster.ClusterIndex),
				FLinearColor(
					Cluster.NormalizedCenter.X,
					Cluster.NormalizedCenter.Y,
					Cluster.NormalizedRadii.X,
					0.0f));
			Material->SetVectorParameterValue(
				*FString::Printf(
					TEXT("ABTS_CloudMacroShape%d"), Cluster.ClusterIndex),
				FLinearColor(
					Cluster.NormalizedRadii.Y,
					FMath::Cos(Cluster.OrientationRadians),
					FMath::Sin(Cluster.OrientationRadians),
					0.0f));
			Material->SetScalarParameterValue(
				*FString::Printf(
					TEXT("ABTS_CloudMacroHeight%d"), Cluster.ClusterIndex),
				Cluster.HeightBias);
		}
		Component->SetMaterial(0, Material);
	}
	LowPolyCloudPrototypeActor = Actor;
	LowPolyCloudLayoutHash = DesiredCloudletHash;
	LowPolyLogicalCloudLayoutHash = DesiredLogicalCloudHash;
	LowPolyLogicalCloudCount = Definitions.Num();
	return true;
}

void UABTSStylizedRenderingWorldSubsystem::DestroyLowPolyCloudPrototype()
{
	if (AActor* Actor = LowPolyCloudPrototypeActor.Get())
	{
		Actor->Destroy();
	}
	LowPolyCloudPrototypeActor.Reset();
	LowPolyCloudLayoutHash = 0;
	LowPolyLogicalCloudLayoutHash = 0;
	LowPolyLogicalCloudCount = 0;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A2R0LowPolyCloudContractTest,
	"ABTS.Rendering.Toon.T4A2R0.LowPolyCloudContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A2R0LowPolyCloudContractTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FABTSStylizedEnvironmentParameters First =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			FVector::ZeroVector,
			10000.0,
			FVector::UpVector,
			EABTSStylizedRenderProfile::GroundDay);
	const FABTSStylizedEnvironmentParameters Second =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			FVector::ZeroVector,
			10000.0,
			FVector::UpVector,
			EABTSStylizedRenderProfile::GroundDay);
	TestTrue(TEXT("Low-poly cloud envelope validates"), First.IsValid());
	TestEqual(TEXT("Cloud islands are enabled only by the GroundDay profile"),
		First.bCloudsEnabled, 1u);
	TestTrue(TEXT("Cloud base is radial and above the planet"),
		First.CloudBaseAltitudeCM > 0.0f);
	TestTrue(TEXT("Cloud island envelope has finite radial separation"),
		First.CloudLayerHeightCM > 0.0f);
	TestTrue(TEXT("Cloud opacity is normalized"),
		First.CloudDensity > 0.0f && First.CloudDensity <= 1.0f);
	TestTrue(TEXT("Cloud coverage is normalized"),
		First.CloudCoverage >= 0.0f && First.CloudCoverage <= 1.0f);
	TestTrue(TEXT("Cloud profile is deterministic"),
		FMath::IsNearlyEqual(
			First.CloudBaseAltitudeCM, Second.CloudBaseAltitudeCM)
		&& FMath::IsNearlyEqual(
			First.CloudLayerHeightCM, Second.CloudLayerHeightCM)
		&& FMath::IsNearlyEqual(
			First.CloudGlobalScaleKM, Second.CloudGlobalScaleKM)
		&& FMath::IsNearlyEqual(First.CloudCoverage, Second.CloudCoverage)
		&& FMath::IsNearlyEqual(First.CloudDensity, Second.CloudDensity));

	const TArray<FABTST4LowPolyCloudIslandDefinition> FirstLayout =
		FABTST4LowPolyCloudPrototype::BuildDefinitions(
			First.PlanetCenterWorld,
			First.PlanetRadiusCM,
			First.StarSeed ^ 0xC10DF13Du,
			FVector(First.SunDirectionToSunWorld),
			First.CloudBaseAltitudeCM,
			First.CloudLayerHeightCM);
	const TArray<FABTST4LowPolyCloudIslandDefinition> SecondLayout =
		FABTST4LowPolyCloudPrototype::BuildDefinitions(
			Second.PlanetCenterWorld,
			Second.PlanetRadiusCM,
			Second.StarSeed ^ 0xC10DF13Du,
			FVector(Second.SunDirectionToSunWorld),
			Second.CloudBaseAltitudeCM,
			Second.CloudLayerHeightCM);
	TestEqual(TEXT("A2.2 freezes the deterministic global cloud count"),
		FirstLayout.Num(), FABTST4LowPolyCloudPrototype::IslandCount);
	const uint64 FirstHash =
		FABTST4LowPolyCloudPrototype::ComputeLayoutHash(FirstLayout);
	TestTrue(TEXT("Cloud layout hash is non-zero"), FirstHash != 0);
	TestEqual(TEXT("Cloud layout is deterministic"), FirstHash,
		FABTST4LowPolyCloudPrototype::ComputeLayoutHash(SecondLayout));
	for (const FABTST4LowPolyCloudIslandDefinition& Definition : FirstLayout)
	{
		FABTST4LowPolyCloudMeshData Mesh;
		FString Failure;
		TestTrue(TEXT("Every cloud island produces a closed mesh"),
			FABTST4LowPolyCloudPrototype::BuildClosedMesh(
				Definition, Mesh, &Failure));
		TestTrue(TEXT("Every cloud mesh validates"), Mesh.IsValid());
		TestTrue(TEXT("Every cloud mesh has a deterministic geometry identity"),
			Mesh.GeometryHash != 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A2R1AInstancedCloudletContractTest,
	"ABTS.Rendering.Toon.T4A2R1A.InstancedCloudletContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A2R1AInstancedCloudletContractTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FABTSStylizedEnvironmentParameters Environment =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			FVector(120.0, -340.0, 560.0),
			10000.0,
			FVector(0.3, -0.6, 0.7).GetSafeNormal(),
			EABTSStylizedRenderProfile::GroundDay);
	const TArray<FABTST4LowPolyCloudIslandDefinition> Layout =
		FABTST4LowPolyCloudPrototype::BuildDefinitions(
			Environment.PlanetCenterWorld,
			Environment.PlanetRadiusCM,
			Environment.StarSeed ^ 0xC10DF13Du,
			FVector(Environment.SunDirectionToSunWorld),
			Environment.CloudBaseAltitudeCM,
			Environment.CloudLayerHeightCM);
	TestEqual(TEXT("R1-A produces the frozen global cloud field"),
		Layout.Num(), FABTST4LowPolyCloudPrototype::IslandCount);

	int32 TotalCloudlets = 0;
	uint64 CombinedHashA = 0xA2C1A11Aull;
	uint64 CombinedHashB = 0xA2C1A11Aull;
	for (const FABTST4LowPolyCloudIslandDefinition& Island : Layout)
	{
		TArray<FABTST4InstancedCloudletDefinition> First;
		TArray<FABTST4InstancedCloudletDefinition> Second;
		FString Failure;
		TestTrue(TEXT("Cloud island builds an instanced cloudlet population"),
			FABTST4LowPolyCloudPrototype::BuildInstancedCloudlets(
				Island, First, &Failure));
		TestTrue(TEXT("Repeated cloudlet generation succeeds"),
			FABTST4LowPolyCloudPrototype::BuildInstancedCloudlets(
				Island, Second, &Failure));
		TestTrue(TEXT("Every cloud island has more than one shared-mesh instance"),
			First.Num() > 1);
		const uint64 FirstHash =
			FABTST4LowPolyCloudPrototype::ComputeCloudletLayoutHash(First);
		const uint64 SecondHash =
			FABTST4LowPolyCloudPrototype::ComputeCloudletLayoutHash(Second);
		TestTrue(TEXT("Cloudlet population hash is non-zero"), FirstHash != 0);
		TestEqual(TEXT("Cloudlet transforms and custom data are deterministic"),
			FirstHash, SecondHash);
		CombinedHashA ^= FirstHash + 0x9e3779b97f4a7c15ull
			+ (CombinedHashA << 6) + (CombinedHashA >> 2);
		CombinedHashB ^= SecondHash + 0x9e3779b97f4a7c15ull
			+ (CombinedHashB << 6) + (CombinedHashB >> 2);
		TotalCloudlets += First.Num();
		for (const FABTST4InstancedCloudletDefinition& Cloudlet : First)
		{
			TestTrue(TEXT("Every cloudlet validates"), Cloudlet.IsValid());
		}
	}
	TestEqual(TEXT("R1-A freezes the total instance budget"),
		TotalCloudlets, FABTST4LowPolyCloudPrototype::TotalCloudletCount);
	TestEqual(TEXT("Combined cloudlet identity is repeatable"),
		CombinedHashA, CombinedHashB);
	TestEqual(TEXT("R1-C2-B3-B1 retains five geometry custom-data channels"),
		FABTST4LowPolyCloudPrototype::CloudletCustomDataFloatCount, 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A2R1BCloudAssetContractTest,
	"ABTS.Rendering.Toon.T4A2R1B.CloudAssetContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A2R1BCloudAssetContractTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	UMaterial* CloudMaterial = LoadObject<UMaterial>(
		nullptr,
		TEXT("/Game/Toon/Environment/Cloud/M_ABTS_Toon_Cloudlet.M_ABTS_Toon_Cloudlet"));
	UStaticMesh* CloudMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Game/Toon/Environment/Cloud/SM_ABTS_Toon_Cloudlet.SM_ABTS_Toon_Cloudlet"));
	TestNotNull(TEXT("R1-B cloudlet material is loadable"), CloudMaterial);
	TestNotNull(TEXT("R1-B cloudlet mesh is loadable"), CloudMesh);
	if (!IsValid(CloudMaterial) || !IsValid(CloudMesh))
	{
		return false;
	}
	TestTrue(
		TEXT("R1-B cloudlet material is exclusively Unlit"),
		CloudMaterial->GetShadingModels().HasOnlyShadingModel(MSM_Unlit));
	TestTrue(
		TEXT("R1-B cloudlet material is compiled for static meshes"),
		CloudMaterial->GetUsageByFlag(MATUSAGE_StaticMesh));
	TestTrue(
		TEXT("R1-B cloudlet material is compiled for instanced static meshes"),
		CloudMaterial->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
	float MacroLightingVersion = 0.0f;
	TestTrue(
		TEXT("A2.2 material exposes its local-solar-height night-cloud version"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudMacroLightingVersion")),
			MacroLightingVersion));
	TestEqual(
		TEXT("A2.2 material version is current"),
		MacroLightingVersion,
		8.0f);
	FLinearColor PlanetCenterParameter = FLinearColor::Transparent;
	TestTrue(
		TEXT("A2.2 material consumes the accepted planet centre per pixel"),
		CloudMaterial->GetVectorParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudPlanetCenter")),
			PlanetCenterParameter));
	float NightBrightness = 0.0f;
	TestTrue(
		TEXT("A2.2 material exposes a bounded night-cloud brightness"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudNightBrightness")),
			NightBrightness));
	TestTrue(TEXT("Night clouds remain readable without retaining daytime white"),
		NightBrightness >= 0.35f && NightBrightness <= 0.65f);
	float DaylightBlendMinSolarHeight = 0.0f;
	float DaylightBlendMaxSolarHeight = 0.0f;
	TestTrue(
		TEXT("A2.2 material exposes the night-to-day blend start"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(
				TEXT("ABTS_CloudDaylightBlendMinSolarHeight")),
			DaylightBlendMinSolarHeight));
	TestTrue(
		TEXT("A2.2 material exposes the night-to-day blend end"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(
				TEXT("ABTS_CloudDaylightBlendMaxSolarHeight")),
			DaylightBlendMaxSolarHeight));
	TestEqual(TEXT("The material and CPU oracle share the blend start"),
		DaylightBlendMinSolarHeight,
		FABTST4LowPolyCloudPrototype::DaylightBlendMinSolarHeight);
	TestEqual(TEXT("The material and CPU oracle share the blend end"),
		DaylightBlendMaxSolarHeight,
		FABTST4LowPolyCloudPrototype::DaylightBlendMaxSolarHeight);
	FLinearColor CloudLightColor = FLinearColor::Transparent;
	TestTrue(
		TEXT("R1-C2-B3-B material exposes a neutral white light band"),
		CloudMaterial->GetVectorParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudLightColor")),
			CloudLightColor));
	TestTrue(
		TEXT("R1-C2-B3-B light band is bright enough to read as sunlit white"),
		CloudLightColor.GetLuminance() > 0.80f);
	FLinearColor CloudBodyColor = FLinearColor::Transparent;
	TestTrue(
		TEXT("R1-C2-B3-B material exposes a distinct body colour band"),
		CloudMaterial->GetVectorParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudBodyColor")),
			CloudBodyColor));
	TestTrue(
		TEXT("R1-C2-B3-B body colour remains below the cloud-top band"),
		CloudBodyColor.GetLuminance() < CloudLightColor.GetLuminance());
	float SunWhiteStrength = 0.0f;
	float ThinWhiteStrength = 0.0f;
	TestTrue(
		TEXT("R1-C2-B3-B exposes sunward whitening"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudSunWhiteStrength")),
			SunWhiteStrength));
	TestTrue(
		TEXT("R1-C2-B3-B exposes thin-density whitening"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudThinWhiteStrength")),
			ThinWhiteStrength));
	TestTrue(
		TEXT("R1-C2-B3-B sunward whitening is visually material"),
		SunWhiteStrength >= 0.60f);
	TestTrue(
		TEXT("R1-C2-B3-B thin-density whitening is visually material"),
		ThinWhiteStrength >= 0.50f);
	float GradientConfidenceStart = 0.0f;
	float GradientConfidenceEnd = 0.0f;
	TestTrue(
		TEXT("R1-C2-B3-B1 exposes a gradient-confidence start"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudGradientConfidenceStart")),
			GradientConfidenceStart));
	TestTrue(
		TEXT("R1-C2-B3-B1 exposes a gradient-confidence end"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudGradientConfidenceEnd")),
			GradientConfidenceEnd));
	TestTrue(
		TEXT("R1-C2-B3-B1 keeps a non-zero critical-point fallback band"),
		GradientConfidenceStart >= 0.05f
			&& GradientConfidenceEnd >= GradientConfidenceStart + 0.15f);
	float RetiredPerInstancePixelParameter = 0.0f;
	TestFalse(
		TEXT("R1-C2-B3-B keeps local-normal detail out of pixel lighting"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudBodyDetailWeight")),
			RetiredPerInstancePixelParameter));
	TestFalse(
		TEXT("R1-C2-B3-B keeps instance variation out of pixel lighting"),
		CloudMaterial->GetScalarParameterValue(
			FMaterialParameterInfo(TEXT("ABTS_CloudInstanceVariationStrength")),
			RetiredPerInstancePixelParameter));
	const FVector PositiveBounds = CloudMesh->GetPositiveBoundsExtension();
	const FVector NegativeBounds = CloudMesh->GetNegativeBoundsExtension();
	TestTrue(
		TEXT("R1-B cloudlet mesh reserves positive WPO bounds"),
		PositiveBounds.GetMin() >= 18.0);
	TestTrue(
		TEXT("R1-B cloudlet mesh reserves negative WPO bounds"),
		NegativeBounds.GetMin() >= 18.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSToonT4A2R1C2A4SeededAmorphousFootprintContractTest,
	"ABTS.Rendering.Toon.T4A2R1C2A4.SeededAmorphousFootprintContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSToonT4A2R1C2A4SeededAmorphousFootprintContractTest::RunTest(
	const FString& Parameters)
{
	(void)Parameters;
	const FABTSStylizedEnvironmentParameters Environment =
		FABTSStylizedRenderingControl::BuildEnvironmentParameters(
			FVector(120.0, -340.0, 560.0),
			10000.0,
			FVector(0.3, -0.6, 0.7).GetSafeNormal(),
			EABTSStylizedRenderProfile::GroundDay);
	const TArray<FABTST4LowPolyCloudIslandDefinition> Layout =
		FABTST4LowPolyCloudPrototype::BuildDefinitions(
			Environment.PlanetCenterWorld,
			Environment.PlanetRadiusCM,
			Environment.StarSeed ^ 0xC10DF13Du,
			FVector(Environment.SunDirectionToSunWorld),
			Environment.CloudBaseAltitudeCM,
			Environment.CloudLayerHeightCM);
	TestEqual(TEXT("R1-C2-A4 retains the global cloud population"),
		Layout.Num(), FABTST4LowPolyCloudPrototype::IslandCount);

	int32 TotalBody = 0;
	int32 TotalCrown = 0;
	int32 TotalEdge = 0;
	int32 TotalMacroClusters = 0;
	for (const FABTST4LowPolyCloudIslandDefinition& Island : Layout)
	{
		const double HorizontalEnvelopeAspect = FMath::Max(
			Island.ExtentsCM.X, Island.ExtentsCM.Y) / FMath::Min(
				Island.ExtentsCM.X, Island.ExtentsCM.Y);
		TestTrue(TEXT("Cloud island horizontal envelope has no dominant axis"),
			HorizontalEnvelopeAspect <= 1.08);
		const TArray<FABTST4CloudMacroClusterDefinition> MacroClusters =
			FABTST4LowPolyCloudPrototype::BuildMacroClusters(Island);
		const TArray<FABTST4CloudMacroClusterDefinition> RepeatedClusters =
			FABTST4LowPolyCloudPrototype::BuildMacroClusters(Island);
		TestEqual(TEXT("Every island owns six deterministic macro clusters"),
			MacroClusters.Num(),
			FABTST4LowPolyCloudPrototype::MacroClusterCountPerIsland);
		TestEqual(TEXT("Repeated macro-cluster generation keeps its count"),
			RepeatedClusters.Num(), MacroClusters.Num());
		for (int32 ClusterIndex = 0;
			ClusterIndex < MacroClusters.Num(); ++ClusterIndex)
		{
			TestTrue(TEXT("Every macro cluster validates"),
				MacroClusters[ClusterIndex].IsValid());
			TestEqual(TEXT("Macro cluster identity is deterministic"),
				MacroClusters[ClusterIndex].IdentityHash,
				RepeatedClusters[ClusterIndex].IdentityHash);
		}
		if (MacroClusters.Num()
			== FABTST4LowPolyCloudPrototype::MacroClusterCountPerIsland)
		{
			TestTrue(TEXT("Seeded core remains close to the island centre"),
				MacroClusters[0].NormalizedCenter.Size() <= 0.10);
			TArray<double> OuterAngles;
			OuterAngles.Reserve(MacroClusters.Num() - 1);
			double MinimumOuterDistance = TNumericLimits<double>::Max();
			double MaximumOuterDistance = 0.0;
			double MinimumEquivalentRadius = TNumericLimits<double>::Max();
			double MaximumEquivalentRadius = 0.0;
			for (int32 ClusterIndex = 1;
				ClusterIndex < MacroClusters.Num(); ++ClusterIndex)
			{
				const FABTST4CloudMacroClusterDefinition& Cluster =
					MacroClusters[ClusterIndex];
				double Angle = FMath::Atan2(
					Cluster.NormalizedCenter.Y,
					Cluster.NormalizedCenter.X);
				if (Angle < 0.0)
				{
					Angle += UE_TWO_PI;
				}
				OuterAngles.Add(Angle);
				const double Distance = Cluster.NormalizedCenter.Size();
				MinimumOuterDistance = FMath::Min(
					MinimumOuterDistance, Distance);
				MaximumOuterDistance = FMath::Max(
					MaximumOuterDistance, Distance);
				const double EquivalentRadius = FMath::Sqrt(
					Cluster.NormalizedRadii.X * Cluster.NormalizedRadii.Y);
				MinimumEquivalentRadius = FMath::Min(
					MinimumEquivalentRadius, EquivalentRadius);
				MaximumEquivalentRadius = FMath::Max(
					MaximumEquivalentRadius, EquivalentRadius);
			}
			OuterAngles.Sort();
			double MinimumAngularGap = TNumericLimits<double>::Max();
			double MaximumAngularGap = 0.0;
			for (int32 AngleIndex = 0;
				AngleIndex < OuterAngles.Num(); ++AngleIndex)
			{
				const double NextAngle = AngleIndex + 1 < OuterAngles.Num()
					? OuterAngles[AngleIndex + 1]
					: OuterAngles[0] + UE_TWO_PI;
				const double Gap = NextAngle - OuterAngles[AngleIndex];
				MinimumAngularGap = FMath::Min(MinimumAngularGap, Gap);
				MaximumAngularGap = FMath::Max(MaximumAngularGap, Gap);
			}
			TestTrue(TEXT("Outer lobe distances vary enough to avoid a regular ring"),
				MaximumOuterDistance - MinimumOuterDistance >= 0.045);
			TestTrue(TEXT("Outer lobe angular gaps vary enough to avoid a regular polygon"),
				MaximumAngularGap - MinimumAngularGap >= 0.10);
			TestTrue(TEXT("Outer lobe sizes vary enough to avoid repeated corner puffs"),
				MaximumEquivalentRadius - MinimumEquivalentRadius >= 0.025);
		}
		TotalMacroClusters += MacroClusters.Num();

		TArray<FABTST4InstancedCloudletDefinition> Cloudlets;
		TArray<FABTST4InstancedCloudletDefinition> RepeatedCloudlets;
		FString Failure;
		TestTrue(TEXT("Curved clustered cloudlet population builds"),
			FABTST4LowPolyCloudPrototype::BuildInstancedCloudlets(
				Island, Cloudlets, &Failure));
		TestTrue(TEXT("Repeated curved cloudlet population builds"),
			FABTST4LowPolyCloudPrototype::BuildInstancedCloudlets(
				Island, RepeatedCloudlets, &Failure));
		TestEqual(TEXT("R1-C2-A4 layout identity is deterministic"),
			FABTST4LowPolyCloudPrototype::ComputeCloudletLayoutHash(Cloudlets),
			FABTST4LowPolyCloudPrototype::ComputeCloudletLayoutHash(
				RepeatedCloudlets));
		int32 BodyCount = 0;
		int32 CrownCount = 0;
		int32 EdgeCount = 0;
		double BodyHeightSum = 0.0;
		double CrownHeightSum = 0.0;
		for (const FABTST4InstancedCloudletDefinition& Cloudlet : Cloudlets)
		{
			TestTrue(TEXT("Cloudlet keeps a valid macro-cluster membership"),
				MacroClusters.IsValidIndex(Cloudlet.MacroClusterIndex));
			const FVector TranslationUp =
				Cloudlet.TransformRelativeToPlanet.GetLocation().GetSafeNormal();
			const FVector RotationUp =
				Cloudlet.TransformRelativeToPlanet.GetRotation().GetAxisZ();
			TestTrue(TEXT("Cloudlet radial up follows curved shell position"),
				FVector::DotProduct(TranslationUp, Cloudlet.RadialUp) > 0.9999);
			TestTrue(TEXT("Cloudlet local Z follows curved radial up"),
				FVector::DotProduct(RotationUp, Cloudlet.RadialUp) > 0.9999);
			switch (Cloudlet.Layer)
			{
			case EABTST4CloudletLayer::Body:
				++BodyCount;
				BodyHeightSum += Cloudlet.NormalizedHeight;
				TestTrue(TEXT("Body cloudlets form a flattened coverage layer"),
					Cloudlet.TransformRelativeToPlanet.GetScale3D().Z
					< FMath::Max(
						Cloudlet.TransformRelativeToPlanet.GetScale3D().X,
						Cloudlet.TransformRelativeToPlanet.GetScale3D().Y));
				break;
			case EABTST4CloudletLayer::Crown:
				++CrownCount;
				CrownHeightSum += Cloudlet.NormalizedHeight;
				break;
			case EABTST4CloudletLayer::Edge:
				++EdgeCount;
				{
					bool bAttachedToBody = false;
					for (const FABTST4InstancedCloudletDefinition& Body : Cloudlets)
					{
						if (Body.Layer != EABTST4CloudletLayer::Body
							|| Body.MacroClusterIndex
								!= Cloudlet.MacroClusterIndex)
						{
							continue;
						}
						const FVector2D Delta =
							Cloudlet.NormalizedPlanarCenter
							- Body.NormalizedPlanarCenter;
						const double CosYaw = FMath::Cos(
							Body.PlanarOrientationRadians);
						const double SinYaw = FMath::Sin(
							Body.PlanarOrientationRadians);
						const double LocalX =
							Delta.X * CosYaw + Delta.Y * SinYaw;
						const double LocalY =
							-Delta.X * SinYaw + Delta.Y * CosYaw;
						const double ExpandedDistance = FMath::Square(
							LocalX / (Body.NormalizedPlanarRadii.X * 1.08))
							+ FMath::Square(
								LocalY / (Body.NormalizedPlanarRadii.Y * 1.08));
						if (ExpandedDistance <= 1.0)
						{
							bAttachedToBody = true;
							break;
						}
					}
					TestTrue(TEXT("Edge cloudlet remains attached to its body cluster"),
						bAttachedToBody);
				}
				break;
			default:
				AddError(TEXT("Unknown cloudlet layer."));
				break;
			}
		}
		TestEqual(TEXT("Body budget is frozen per island"), BodyCount,
			FABTST4LowPolyCloudPrototype::GetCloudletLayerCount(
				Island.IslandIndex, EABTST4CloudletLayer::Body));
		TestEqual(TEXT("Crown budget is frozen per island"), CrownCount,
			FABTST4LowPolyCloudPrototype::GetCloudletLayerCount(
				Island.IslandIndex, EABTST4CloudletLayer::Crown));
		TestEqual(TEXT("Edge budget is frozen per island"), EdgeCount,
			FABTST4LowPolyCloudPrototype::GetCloudletLayerCount(
				Island.IslandIndex, EABTST4CloudletLayer::Edge));
		TestTrue(TEXT("Crown layer is higher than body coverage"),
			CrownCount > 0 && BodyCount > 0
			&& CrownHeightSum / CrownCount > BodyHeightSum / BodyCount);

		// Measure the actual cloudlet union rather than only the authored island
		// extents. A support-width sweep catches a chain of clusters or locally
		// stretched edge cloudlets even when the outer envelope looks square.
		double MinimumProjectedWidthCM = TNumericLimits<double>::Max();
		double MaximumProjectedWidthCM = 0.0;
		constexpr int32 AzimuthSampleCount = 24;
		for (int32 AzimuthIndex = 0;
			AzimuthIndex < AzimuthSampleCount; ++AzimuthIndex)
		{
			const double Angle = UE_TWO_PI
				* static_cast<double>(AzimuthIndex)
				/ static_cast<double>(AzimuthSampleCount);
			const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
			double MinimumSupportCM = TNumericLimits<double>::Max();
			double MaximumSupportCM = -TNumericLimits<double>::Max();
			for (const FABTST4InstancedCloudletDefinition& Cloudlet : Cloudlets)
			{
				const FVector2D CenterCM(
					Cloudlet.NormalizedPlanarCenter.X * Island.ExtentsCM.X,
					Cloudlet.NormalizedPlanarCenter.Y * Island.ExtentsCM.Y);
				const FVector2D RadiiCM(
					Cloudlet.NormalizedPlanarRadii.X * Island.ExtentsCM.X,
					Cloudlet.NormalizedPlanarRadii.Y * Island.ExtentsCM.Y);
				const double Orientation = Cloudlet.PlanarOrientationRadians;
				const FVector2D AxisX(FMath::Cos(Orientation), FMath::Sin(Orientation));
				const FVector2D AxisY(-FMath::Sin(Orientation), FMath::Cos(Orientation));
				const double RadiusSupportCM = FMath::Sqrt(
					FMath::Square(RadiiCM.X * FVector2D::DotProduct(Direction, AxisX))
					+ FMath::Square(RadiiCM.Y * FVector2D::DotProduct(Direction, AxisY)));
				const double CenterSupportCM = FVector2D::DotProduct(
					Direction, CenterCM);
				MinimumSupportCM = FMath::Min(
					MinimumSupportCM, CenterSupportCM - RadiusSupportCM);
				MaximumSupportCM = FMath::Max(
					MaximumSupportCM, CenterSupportCM + RadiusSupportCM);
			}
			const double ProjectedWidthCM = MaximumSupportCM - MinimumSupportCM;
			MinimumProjectedWidthCM = FMath::Min(
				MinimumProjectedWidthCM, ProjectedWidthCM);
			MaximumProjectedWidthCM = FMath::Max(
				MaximumProjectedWidthCM, ProjectedWidthCM);
		}
		const double AzimuthalFootprintIsotropy = MaximumProjectedWidthCM > 0.0
			? MinimumProjectedWidthCM / MaximumProjectedWidthCM
			: 0.0;
		TestTrue(
			FString::Printf(
				TEXT("Cloudlet union remains broad from every sampled azimuth (Island=%d Isotropy=%.4f MinWidthCM=%.2f MaxWidthCM=%.2f)"),
				Island.IslandIndex,
				AzimuthalFootprintIsotropy,
				MinimumProjectedWidthCM,
				MaximumProjectedWidthCM),
			AzimuthalFootprintIsotropy >= 0.80);

		constexpr int32 CoverageGrid = 65;
		int32 EnclosingSamples = 0;
		int32 MaskSamples = 0;
		int32 CoveredSamples = 0;
		for (int32 YIndex = 0; YIndex < CoverageGrid; ++YIndex)
		{
			const double Y = FMath::Lerp(
				-1.0, 1.0,
				static_cast<double>(YIndex) / (CoverageGrid - 1));
			for (int32 XIndex = 0; XIndex < CoverageGrid; ++XIndex)
			{
				const double X = FMath::Lerp(
					-1.0, 1.0,
					static_cast<double>(XIndex) / (CoverageGrid - 1));
				if (X * X + Y * Y > 1.0)
				{
					continue;
				}
				++EnclosingSamples;
				bool bInsideMacroMaskCore = false;
				for (const FABTST4CloudMacroClusterDefinition& Cluster
					: MacroClusters)
				{
					const FVector2D Delta = FVector2D(X, Y)
						- Cluster.NormalizedCenter;
					const double CosYaw = FMath::Cos(Cluster.OrientationRadians);
					const double SinYaw = FMath::Sin(Cluster.OrientationRadians);
					const double LocalX = Delta.X * CosYaw + Delta.Y * SinYaw;
					const double LocalY = -Delta.X * SinYaw + Delta.Y * CosYaw;
					const double ClusterDistance = FMath::Square(
						LocalX / Cluster.NormalizedRadii.X)
						+ FMath::Square(LocalY / Cluster.NormalizedRadii.Y);
					if (ClusterDistance <= FMath::Square(0.76))
					{
						bInsideMacroMaskCore = true;
						break;
					}
				}
				if (!bInsideMacroMaskCore)
				{
					continue;
				}
				++MaskSamples;
				bool bCovered = false;
				for (const FABTST4InstancedCloudletDefinition& Cloudlet : Cloudlets)
				{
					if (Cloudlet.Layer != EABTST4CloudletLayer::Body)
					{
						continue;
					}
					const FVector2D Delta = FVector2D(X, Y)
						- Cloudlet.NormalizedPlanarCenter;
					const double CosYaw = FMath::Cos(
						Cloudlet.PlanarOrientationRadians);
					const double SinYaw = FMath::Sin(
						Cloudlet.PlanarOrientationRadians);
					const double LocalX = Delta.X * CosYaw + Delta.Y * SinYaw;
					const double LocalY = -Delta.X * SinYaw + Delta.Y * CosYaw;
					const double EllipseDistance = FMath::Square(
						LocalX / Cloudlet.NormalizedPlanarRadii.X)
						+ FMath::Square(
							LocalY / Cloudlet.NormalizedPlanarRadii.Y);
					if (EllipseDistance <= 1.0)
					{
						bCovered = true;
						break;
					}
				}
				CoveredSamples += bCovered ? 1 : 0;
			}
		}
		const double Coverage = MaskSamples > 0
			? static_cast<double>(CoveredSamples) / MaskSamples
			: 0.0;
		const double MaskOccupancy = EnclosingSamples > 0
			? static_cast<double>(MaskSamples) / EnclosingSamples
			: 0.0;
		TestTrue(TEXT("Body covers at least 98 percent of intended macro mask"),
			Coverage >= 0.98);
		TestTrue(TEXT("Macro mask preserves deliberate non-convex negative space"),
			MaskOccupancy >= 0.12 && MaskOccupancy <= 0.62);
		TotalBody += BodyCount;
		TotalCrown += CrownCount;
		TotalEdge += EdgeCount;
	}
	TestEqual(TEXT("R1-C2-A4 total macro-cluster budget"),
		TotalMacroClusters,
		FABTST4LowPolyCloudPrototype::IslandCount
			* FABTST4LowPolyCloudPrototype::MacroClusterCountPerIsland);
	TestEqual(TEXT("Global field total body budget"),
		TotalBody,
		FABTST4LowPolyCloudPrototype::TotalBodyCloudletCount);
	TestEqual(TEXT("Global field total crown budget"),
		TotalCrown,
		FABTST4LowPolyCloudPrototype::TotalCrownCloudletCount);
	TestEqual(TEXT("Global field total edge budget"),
		TotalEdge,
		FABTST4LowPolyCloudPrototype::TotalEdgeCloudletCount);
	TestEqual(TEXT("A2.2 freezes the total instanced-cloudlet GPU budget"),
		TotalBody + TotalCrown + TotalEdge,
		FABTST4LowPolyCloudPrototype::TotalCloudletCount);
	return true;
}

#endif
