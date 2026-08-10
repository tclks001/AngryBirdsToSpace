// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleBirdTrailComponent.h"

#include "ABTSRuntime.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Texture2D.h"
#include "MeshElementCollector.h"
#include "PrimitiveDrawInterface.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "SceneView.h"
#include "SceneTypes.h"
#include "TextureResource.h"

namespace
{
	TAutoConsoleVariable<int32> CVarABTSM11FinaleBirdTrailEnabled(
		TEXT("abts.M11.FinaleBirdTrail.Enabled"),
		1,
		TEXT("Draw the M11 finale bird presentation trail (0=off, 1=on)."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarABTSM11FinaleBirdTrailSampleSpacingCM(
		TEXT("abts.M11.FinaleBirdTrail.SampleSpacingCM"),
		64.0f,
		TEXT("Nominal world-arc spacing in cm between M11 trail particles."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarABTSM11FinaleBirdTrailBlueNoiseJitter(
		TEXT("abts.M11.FinaleBirdTrail.BlueNoiseJitterFraction"),
		0.20f,
		TEXT("Bounded one-dimensional stratified site jitter as a fraction of nominal spacing."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarABTSM11FinaleBirdTrailLifetimeSeconds(
		TEXT("abts.M11.FinaleBirdTrail.LifetimeSeconds"),
		0.40f,
		TEXT("Lifetime in presentation seconds for the slowest M11 trail particles."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarABTSM11FinaleBirdTrailLateralSpeedFraction(
		TEXT("abts.M11.FinaleBirdTrail.LateralSpeedFraction"),
		0.20f,
		TEXT("Three-sigma clamp for lateral particle speed as a fraction of bird speed."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarABTSM11FinaleBirdTrailOuterLifetimeScale(
		TEXT("abts.M11.FinaleBirdTrail.OuterLifetimeScale"),
		0.45f,
		TEXT("Lifetime scale applied at the maximum lateral particle speed."),
		ECVF_Default);
	TAutoConsoleVariable<int32> CVarABTSM11FinaleBirdTrailRenderMode(
		TEXT("abts.M11.FinaleBirdTrail.RenderMode"),
		1,
		TEXT("M11 finale trail renderer (0=legacy square points, 1=procedural soft sprites)."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarABTSM11FinaleBirdTrailHaloDiameterPixels(
		TEXT("abts.M11.FinaleBirdTrail.HaloDiameterPixels"),
		5.5f,
		TEXT("Screen-space diameter in pixels of the procedural trail halo."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarABTSM11FinaleBirdTrailContrastShellDiameterPixels(
		TEXT("abts.M11.FinaleBirdTrail.ContrastShellDiameterPixels"),
		4.4f,
		TEXT("Screen-space diameter in pixels of the dark contrast shell."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarABTSM11FinaleBirdTrailCoreDiameterPixels(
		TEXT("abts.M11.FinaleBirdTrail.CoreDiameterPixels"),
		3.0f,
		TEXT("Screen-space diameter in pixels of the procedural trail core."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarABTSM11FinaleBirdTrailHaloIntensity(
		TEXT("abts.M11.FinaleBirdTrail.HaloIntensity"),
		0.30f,
		TEXT("Additive RGB intensity of the procedural trail halo."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarABTSM11FinaleBirdTrailContrastShellOpacity(
		TEXT("abts.M11.FinaleBirdTrail.ContrastShellOpacity"),
		0.45f,
		TEXT("Alpha-composite opacity of the dark contrast shell."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarABTSM11FinaleBirdTrailCoreIntensity(
		TEXT("abts.M11.FinaleBirdTrail.CoreIntensity"),
		0.95f,
		TEXT("Premultiplied RGB radiance of the procedural trail core."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarABTSM11FinaleBirdTrailCoreCompositeOpacity(
		TEXT("abts.M11.FinaleBirdTrail.CoreCompositeOpacity"),
		0.80f,
		TEXT("Alpha-composite opacity of the procedural trail core."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarABTSM11FinaleBirdTrailSoftness(
		TEXT("abts.M11.FinaleBirdTrail.Softness"),
		0.68f,
		TEXT("Radial softness of the generated sprite texture (0=hard, 1=soft)."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarABTSM11FinaleBirdTrailExpansionFraction(
		TEXT("abts.M11.FinaleBirdTrail.ExpansionFraction"),
		0.15f,
		TEXT("Fractional sprite expansion over each particle lifetime."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarABTSM11FinaleBirdTrailCoreFadeExponent(
		TEXT("abts.M11.FinaleBirdTrail.CoreFadeExponent"),
		0.60f,
		TEXT("Power curve applied to remaining life for core intensity."),
		ECVF_Default);
	TAutoConsoleVariable<float> CVarABTSM11FinaleBirdTrailHaloFadeExponent(
		TEXT("abts.M11.FinaleBirdTrail.HaloFadeExponent"),
		1.50f,
		TEXT("Power curve applied to remaining life for halo intensity."),
		ECVF_Default);

	constexpr int32 TrailSpriteTextureSize = 64;
	constexpr float LegacyTrailParticleHaloDiameterPixels = 3.0f;
	constexpr float LegacyTrailParticleCoreDiameterPixels = 1.25f;
	constexpr FLinearColor LegacyTrailParticleHaloColor(
		0.72f, 0.16f, 0.015f, 1.0f);
	constexpr FLinearColor LegacyTrailParticleCoreColor(
		1.0f, 0.82f, 0.32f, 1.0f);
	constexpr FLinearColor TrailSpriteHaloColor(
		1.0f, 0.30f, 0.025f, 1.0f);
	constexpr FLinearColor TrailSpriteContrastShellColor(
		0.02f, 0.012f, 0.035f, 1.0f);
	constexpr FLinearColor TrailSpriteCoreColor(
		1.0f, 0.58f, 0.12f, 1.0f);

	bool ABTSM11IsFiniteTrailPosition(const FVector& Position)
	{
		return FMath::IsFinite(Position.X)
			&& FMath::IsFinite(Position.Y)
			&& FMath::IsFinite(Position.Z);
	}

	uint32 ABTSM11MixTrailParticleBits(uint32 Value)
	{
		Value ^= Value >> 16;
		Value *= 0x7FEB352Du;
		Value ^= Value >> 15;
		Value *= 0x846CA68Bu;
		Value ^= Value >> 16;
		return Value;
	}

	float ABTSM11TrailParticleUnitFloat(const uint32 Value)
	{
		return static_cast<float>(ABTSM11MixTrailParticleBits(Value) & 0xFFFFu)
			/ 65535.0f;
	}

	float ABTSM11ResolveTrailNominalSpacingCM()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailSampleSpacingCM.GetValueOnGameThread(),
			8.0f,
			1000.0f);
	}

	float ABTSM11ResolveTrailBlueNoiseJitterFraction()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailBlueNoiseJitter.GetValueOnGameThread(),
			0.0f,
			0.45f);
	}

	float ABTSM11ResolveTrailLifetimeSeconds()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailLifetimeSeconds.GetValueOnGameThread(),
			0.05f,
			2.0f);
	}

	float ABTSM11ResolveTrailMaximumLateralSpeedFraction()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailLateralSpeedFraction.GetValueOnGameThread(),
			0.0f,
			0.5f);
	}

	float ABTSM11ResolveTrailMinimumFastLifetimeScale()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailOuterLifetimeScale.GetValueOnGameThread(),
			0.1f,
			1.0f);
	}

	int32 ABTSM11ResolveTrailRenderModeGameThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailRenderMode.GetValueOnGameThread(),
			0,
			1);
	}

	int32 ABTSM11ResolveTrailRenderModeRenderThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailRenderMode.GetValueOnRenderThread(),
			0,
			1);
	}

	float ABTSM11ResolveTrailHaloDiameterPixelsGameThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailHaloDiameterPixels.GetValueOnGameThread(),
			0.25f,
			64.0f);
	}

	float ABTSM11ResolveTrailHaloDiameterPixelsRenderThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailHaloDiameterPixels.GetValueOnRenderThread(),
			0.25f,
			64.0f);
	}

	float ABTSM11ResolveTrailContrastShellDiameterPixelsGameThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailContrastShellDiameterPixels.GetValueOnGameThread(),
			0.25f,
			64.0f);
	}

	float ABTSM11ResolveTrailContrastShellDiameterPixelsRenderThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailContrastShellDiameterPixels.GetValueOnRenderThread(),
			0.25f,
			64.0f);
	}

	float ABTSM11ResolveTrailCoreDiameterPixelsGameThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailCoreDiameterPixels.GetValueOnGameThread(),
			0.25f,
			64.0f);
	}

	float ABTSM11ResolveTrailCoreDiameterPixelsRenderThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailCoreDiameterPixels.GetValueOnRenderThread(),
			0.25f,
			64.0f);
	}

	float ABTSM11ResolveTrailHaloIntensityGameThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailHaloIntensity.GetValueOnGameThread(),
			0.0f,
			8.0f);
	}

	float ABTSM11ResolveTrailHaloIntensityRenderThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailHaloIntensity.GetValueOnRenderThread(),
			0.0f,
			8.0f);
	}

	float ABTSM11ResolveTrailContrastShellOpacityGameThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailContrastShellOpacity.GetValueOnGameThread(),
			0.0f,
			1.0f);
	}

	float ABTSM11ResolveTrailContrastShellOpacityRenderThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailContrastShellOpacity.GetValueOnRenderThread(),
			0.0f,
			1.0f);
	}

	float ABTSM11ResolveTrailCoreIntensityGameThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailCoreIntensity.GetValueOnGameThread(),
			0.0f,
			8.0f);
	}

	float ABTSM11ResolveTrailCoreIntensityRenderThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailCoreIntensity.GetValueOnRenderThread(),
			0.0f,
			8.0f);
	}

	float ABTSM11ResolveTrailCoreCompositeOpacityGameThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailCoreCompositeOpacity.GetValueOnGameThread(),
			0.0f,
			1.0f);
	}

	float ABTSM11ResolveTrailCoreCompositeOpacityRenderThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailCoreCompositeOpacity.GetValueOnRenderThread(),
			0.0f,
			1.0f);
	}

	float ABTSM11ResolveTrailSpriteSoftness()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailSoftness.GetValueOnGameThread(),
			0.0f,
			1.0f);
	}

	float ABTSM11ResolveTrailExpansionFractionGameThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailExpansionFraction.GetValueOnGameThread(),
			0.0f,
			2.0f);
	}

	float ABTSM11ResolveTrailExpansionFractionRenderThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailExpansionFraction.GetValueOnRenderThread(),
			0.0f,
			2.0f);
	}

	float ABTSM11ResolveTrailCoreFadeExponentGameThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailCoreFadeExponent.GetValueOnGameThread(),
			0.05f,
			8.0f);
	}

	float ABTSM11ResolveTrailCoreFadeExponentRenderThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailCoreFadeExponent.GetValueOnRenderThread(),
			0.05f,
			8.0f);
	}

	float ABTSM11ResolveTrailHaloFadeExponentGameThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailHaloFadeExponent.GetValueOnGameThread(),
			0.05f,
			8.0f);
	}

	float ABTSM11ResolveTrailHaloFadeExponentRenderThread()
	{
		return FMath::Clamp(
			CVarABTSM11FinaleBirdTrailHaloFadeExponent.GetValueOnRenderThread(),
			0.05f,
			8.0f);
	}

	float ABTSM11ResolveTrailSpriteHalfWorldSize(
		const FSceneView& View,
		const FVector& WorldPosition,
		const float DiameterPixels)
	{
		const FVector4 ScreenPosition = View.WorldToScreen(WorldPosition);
		const FMatrix& ViewToClip = View.ViewMatrices.GetViewToClip();
		const float ProjectionScaleY = FMath::Abs(ViewToClip.M[1][1]);
		const float ViewHeightPixels = static_cast<float>(FMath::Max(
			1,
			View.UnscaledViewRect.Height()));
		const bool bPerspective = ViewToClip.M[3][3] < 1.0f;
		const float DepthScale = bPerspective ? ScreenPosition.W : 1.0f;
		if (DepthScale <= KINDA_SMALL_NUMBER
			|| ProjectionScaleY <= KINDA_SMALL_NUMBER)
		{
			return 0.0f;
		}
		return DiameterPixels * DepthScale
			/ (ProjectionScaleY * ViewHeightPixels);
	}

	float ABTSM11ResolveTrailSiteJitterCM(
		const uint32 ParticleSerial,
		const float NominalSpacingCM,
		const float JitterFraction)
	{
		const uint32 Key = ParticleSerial * 0x9E3779B9u + 0xB5297A4Du;
		return (ABTSM11TrailParticleUnitFloat(Key ^ 0x68E31DA4u) * 2.0f
			- 1.0f) * NominalSpacingCM * JitterFraction;
	}

	float ABTSM11ResolveTrailBlueNoiseSpacingCM(
		const uint32 ParticleSerial,
		const float NominalSpacingCM,
		const float JitterFraction)
	{
		// One deterministic sample occupies each nominal arc-length stratum.
		// Differencing adjacent bounded site jitters removes the periodic lattice
		// while retaining a hard minimum gap and stable long-run density.
		const float CurrentJitterCM = ABTSM11ResolveTrailSiteJitterCM(
			ParticleSerial,
			NominalSpacingCM,
			JitterFraction);
		const float NextJitterCM = ABTSM11ResolveTrailSiteJitterCM(
			ParticleSerial + 1u,
			NominalSpacingCM,
			JitterFraction);
		return FMath::Max(
			KINDA_SMALL_NUMBER,
			NominalSpacingCM + NextJitterCM - CurrentJitterCM);
	}

	void ABTSM11ResolveTrailParticleVariation(
		const uint32 ParticleSerial,
		const FVector& TravelDirection,
		const float BirdSpeedCMPerSecond,
		const float MaximumLateralSpeedFraction,
		const float BaseLifetimeSeconds,
		const float MinimumFastLifetimeScale,
		FVector& OutLateralVelocityCMPerSecond,
		float& OutLifetimeSeconds,
		float& OutSizeScale)
	{
		const uint32 Key = ParticleSerial * 0x9E3779B9u + 0xA341316Cu;
		FVector PlaneAxisA = FVector::UpVector;
		FVector PlaneAxisB = FVector::RightVector;
		TravelDirection.FindBestAxisVectors(PlaneAxisA, PlaneAxisB);

		const float UniformA = FMath::Clamp(
			ABTSM11TrailParticleUnitFloat(Key ^ 0xC8013EA4u),
			UE_SMALL_NUMBER,
			1.0f - UE_SMALL_NUMBER);
		const float UniformB = ABTSM11TrailParticleUnitFloat(
			Key ^ 0xAD90777Du);
		const float GaussianRadius = FMath::Sqrt(-2.0f * FMath::Loge(UniformA));
		const float GaussianAngle = 2.0f * UE_PI * UniformB;
		const float GaussianA = GaussianRadius * FMath::Cos(GaussianAngle);
		const float GaussianB = GaussianRadius * FMath::Sin(GaussianAngle);

		const float MaximumLateralSpeedCMPerSecond = FMath::Max(
			0.0f,
			BirdSpeedCMPerSecond * MaximumLateralSpeedFraction);
		const float LateralSigmaCMPerSecond =
			MaximumLateralSpeedCMPerSecond / 3.0f;
		OutLateralVelocityCMPerSecond =
			(PlaneAxisA * GaussianA + PlaneAxisB * GaussianB)
			* LateralSigmaCMPerSecond;
		OutLateralVelocityCMPerSecond =
			OutLateralVelocityCMPerSecond.GetClampedToMaxSize(
				MaximumLateralSpeedCMPerSecond);

		const float LateralSpeedRatio = MaximumLateralSpeedCMPerSecond
			> KINDA_SMALL_NUMBER
			? OutLateralVelocityCMPerSecond.Size()
				/ MaximumLateralSpeedCMPerSecond
			: 0.0f;
		OutLifetimeSeconds = BaseLifetimeSeconds * FMath::Lerp(
			1.0f,
			MinimumFastLifetimeScale,
			FMath::Clamp(LateralSpeedRatio, 0.0f, 1.0f));
		OutSizeScale = FMath::Lerp(
			0.78f,
			1.16f,
			ABTSM11TrailParticleUnitFloat(Key ^ 0x1B56C4E9u));
	}

	class FABTSM11FinaleBirdTrailSceneProxy final
		: public FPrimitiveSceneProxy
	{
	public:
		FABTSM11FinaleBirdTrailSceneProxy(
			const UABTSM11FinaleBirdTrailComponent* Component,
			TArray<FABTSM11FinaleBirdTrailParticle> InSamples,
			const FTexture* InSpriteTexture)
			: FPrimitiveSceneProxy(Component)
			, Samples(MoveTemp(InSamples))
			, SpriteTexture(InSpriteTexture)
		{
			bWillEverBeLit = false;
		}

		SIZE_T GetTypeHash() const override
		{
			static size_t UniquePointer;
			return reinterpret_cast<size_t>(&UniquePointer);
		}

		void GetDynamicMeshElements(
			const TArray<const FSceneView*>& Views,
			const FSceneViewFamily& ViewFamily,
			const uint32 VisibilityMap,
			FMeshElementCollector& Collector) const override
		{
			if (Samples.IsEmpty()
				|| CVarABTSM11FinaleBirdTrailEnabled.GetValueOnRenderThread()
					== 0)
			{
				return;
			}

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				if ((VisibilityMap & (1u << ViewIndex)) == 0)
				{
					continue;
				}
				const FSceneView* View = Views[ViewIndex];
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
				const int32 RenderMode =
					ABTSM11ResolveTrailRenderModeRenderThread();
				if (RenderMode == 1 && SpriteTexture == nullptr)
				{
					continue;
				}
				for (const FABTSM11FinaleBirdTrailParticle& Particle : Samples)
				{
					const float NormalizedAge = FMath::Clamp(
						Particle.AgeSeconds
							/ FMath::Max(
								KINDA_SMALL_NUMBER,
								Particle.LifetimeSeconds),
						0.0f,
						1.0f);
					const float Fade = 1.0f - NormalizedAge;
					if (Fade <= 0.04f)
					{
						continue;
					}

					const FVector WorldPosition = GetLocalToWorld().TransformPosition(
						Particle.LocalPosition);
					if (RenderMode == 0)
					{
						const float Taper = FMath::Lerp(0.42f, 1.0f, Fade)
							* Particle.SizeScale;
						FLinearColor Halo = LegacyTrailParticleHaloColor;
						Halo.R *= Fade;
						Halo.G *= Fade;
						Halo.B *= Fade;
						FLinearColor Core = LegacyTrailParticleCoreColor;
						Core.R *= Fade;
						Core.G *= Fade;
						Core.B *= Fade;
						PDI->DrawPoint(
							WorldPosition,
							Halo,
							LegacyTrailParticleHaloDiameterPixels * Taper,
							SDPG_World);
						PDI->DrawPoint(
							WorldPosition,
							Core,
							LegacyTrailParticleCoreDiameterPixels * Taper,
							SDPG_World);
						continue;
					}

					const float Expansion = 1.0f
						+ NormalizedAge
							* ABTSM11ResolveTrailExpansionFractionRenderThread();
					const float SizeScale = Particle.SizeScale * Expansion;
					const float HaloFade = FMath::Pow(
						Fade,
						ABTSM11ResolveTrailHaloFadeExponentRenderThread());
					const float CoreFade = FMath::Pow(
						Fade,
						ABTSM11ResolveTrailCoreFadeExponentRenderThread());
					const float HaloHalfWorldSize =
						ABTSM11ResolveTrailSpriteHalfWorldSize(
							*View,
							WorldPosition,
							ABTSM11ResolveTrailHaloDiameterPixelsRenderThread()
								* SizeScale);
					const float ContrastShellHalfWorldSize =
						ABTSM11ResolveTrailSpriteHalfWorldSize(
							*View,
							WorldPosition,
							ABTSM11ResolveTrailContrastShellDiameterPixelsRenderThread()
								* SizeScale);
					const float CoreHalfWorldSize =
						ABTSM11ResolveTrailSpriteHalfWorldSize(
							*View,
							WorldPosition,
							ABTSM11ResolveTrailCoreDiameterPixelsRenderThread()
								* SizeScale);
					if (HaloHalfWorldSize <= KINDA_SMALL_NUMBER
						|| ContrastShellHalfWorldSize <= KINDA_SMALL_NUMBER
						|| CoreHalfWorldSize <= KINDA_SMALL_NUMBER)
					{
						continue;
					}

					FLinearColor Halo = TrailSpriteHaloColor
						* (ABTSM11ResolveTrailHaloIntensityRenderThread()
							* HaloFade);
					const float CoreCompositeAlpha =
						ABTSM11ResolveTrailCoreCompositeOpacityRenderThread()
							* CoreFade;
					// SE_BLEND_AlphaComposite expects premultiplied RGB. The
					// generated texture already stores its radial coverage in both
					// RGB and alpha; premultiply the vertex radiance by the same
					// age/opacity factor so the core attenuates bright backgrounds
					// without turning into an opaque bead on dark backgrounds.
					FLinearColor Core = TrailSpriteCoreColor
						* (ABTSM11ResolveTrailCoreIntensityRenderThread()
							* CoreCompositeAlpha);
					const float ContrastShellAlpha =
						ABTSM11ResolveTrailContrastShellOpacityRenderThread()
							* CoreFade;
					FLinearColor ContrastShell = TrailSpriteContrastShellColor
						* ContrastShellAlpha;
					Halo.A = 1.0f;
					ContrastShell.A = ContrastShellAlpha;
					Core.A = CoreCompositeAlpha;
					PDI->DrawSprite(
						WorldPosition,
						HaloHalfWorldSize,
						HaloHalfWorldSize,
						SpriteTexture,
						Halo,
						SDPG_World,
						0.0f,
						0.0f,
						0.0f,
						0.0f,
						SE_BLEND_Additive);
					PDI->DrawSprite(
						WorldPosition,
						ContrastShellHalfWorldSize,
						ContrastShellHalfWorldSize,
						SpriteTexture,
						ContrastShell,
						SDPG_World,
						0.0f,
						0.0f,
						0.0f,
						0.0f,
						SE_BLEND_AlphaComposite);
					PDI->DrawSprite(
						WorldPosition,
						CoreHalfWorldSize,
						CoreHalfWorldSize,
						SpriteTexture,
						Core,
						SDPG_World,
						0.0f,
						0.0f,
						0.0f,
						0.0f,
						SE_BLEND_AlphaComposite);
				}
			}
		}

		FPrimitiveViewRelevance GetViewRelevance(
			const FSceneView* View) const override
		{
			FPrimitiveViewRelevance Relevance;
			Relevance.bDrawRelevance = IsShown(View);
			Relevance.bDynamicRelevance = true;
			Relevance.bOpaque = true;
			Relevance.bNormalTranslucency = true;
			Relevance.bSeparateTranslucency = true;
			Relevance.bRenderInMainPass = ShouldRenderInMainPass();
			return Relevance;
		}

		uint32 GetMemoryFootprint() const override
		{
			return sizeof(*this) + GetAllocatedSize();
		}

		uint32 GetAllocatedSize() const
		{
			return FPrimitiveSceneProxy::GetAllocatedSize()
				+ Samples.GetAllocatedSize();
		}

	private:
		TArray<FABTSM11FinaleBirdTrailParticle> Samples;
		const FTexture* SpriteTexture = nullptr;
	};
}

UABTSM11FinaleBirdTrailComponent::UABTSM11FinaleBirdTrailComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	SetMobility(EComponentMobility::Movable);
	CastShadow = false;
	bReceivesDecals = false;
	bUseEditorCompositing = false;
}

void UABTSM11FinaleBirdTrailComponent::BeginTrail(
	const FVector& WorldPosition)
{
	TrailSamples.Reset();
	NextParticleSerial = 0;
	DistanceUntilNextEmissionCM = 0.0f;
	bHasLatestEmitterPosition = false;
	bTrailActive = CVarABTSM11FinaleBirdTrailEnabled.GetValueOnGameThread()
		!= 0;
	if (bTrailActive
		&& ABTSM11ResolveTrailRenderModeGameThread() == 1
		&& !EnsureTrailSpriteTexture())
	{
		UE_LOG(
			LogABTSRuntime,
			Error,
			TEXT("[ABTS][M11-C][BirdTrail] SoftSpriteTextureCreationFailed"));
		bTrailActive = false;
	}
	if (bTrailActive && ABTSM11IsFiniteTrailPosition(WorldPosition))
	{
		LatestEmitterLocalPosition = GetComponentTransform()
			.InverseTransformPosition(WorldPosition);
		bHasLatestEmitterPosition = true;
		UE_LOG(
			LogABTSRuntime,
			Log,
			TEXT("[ABTS][M11-C][BirdTrail] Begin Mode=ArcLengthBlueNoise RenderMode=%d SpacingCM=%.1f Jitter=%.3f Lifetime=%.3f Lateral3SigmaFraction=%.3f OuterLifetimeScale=%.3f HaloPX=%.2f ContrastShellPX=%.2f CorePX=%.2f HaloIntensity=%.3f ContrastShellOpacity=%.3f CoreIntensity=%.3f CoreCompositeOpacity=%.3f Softness=%.3f Expansion=%.3f CoreFadeExp=%.3f HaloFadeExp=%.3f Texture=%dx%d MaxParticles=%d"),
			ABTSM11ResolveTrailRenderModeGameThread(),
			ABTSM11ResolveTrailNominalSpacingCM(),
			ABTSM11ResolveTrailBlueNoiseJitterFraction(),
			ABTSM11ResolveTrailLifetimeSeconds(),
			ABTSM11ResolveTrailMaximumLateralSpeedFraction(),
			ABTSM11ResolveTrailMinimumFastLifetimeScale(),
			ABTSM11ResolveTrailHaloDiameterPixelsGameThread(),
			ABTSM11ResolveTrailContrastShellDiameterPixelsGameThread(),
			ABTSM11ResolveTrailCoreDiameterPixelsGameThread(),
			ABTSM11ResolveTrailHaloIntensityGameThread(),
			ABTSM11ResolveTrailContrastShellOpacityGameThread(),
			ABTSM11ResolveTrailCoreIntensityGameThread(),
			ABTSM11ResolveTrailCoreCompositeOpacityGameThread(),
			ABTSM11ResolveTrailSpriteSoftness(),
			ABTSM11ResolveTrailExpansionFractionGameThread(),
			ABTSM11ResolveTrailCoreFadeExponentGameThread(),
			ABTSM11ResolveTrailHaloFadeExponentGameThread(),
			TrailSpriteTexture != nullptr ? TrailSpriteTexture->GetSizeX() : 0,
			TrailSpriteTexture != nullptr ? TrailSpriteTexture->GetSizeY() : 0,
			MaximumTrailSampleCount);
	}
	NotifyTrailChanged();
}

void UABTSM11FinaleBirdTrailComponent::AdvanceTrail(
	const FVector& WorldPosition,
	const float DeltaSeconds)
{
	if (CVarABTSM11FinaleBirdTrailEnabled.GetValueOnGameThread() == 0)
	{
		ClearTrail();
		return;
	}
	if (!bTrailActive)
	{
		BeginTrail(WorldPosition);
		return;
	}
	if (!ABTSM11IsFiniteTrailPosition(WorldPosition)
		|| !FMath::IsFinite(DeltaSeconds))
	{
		return;
	}

	const float SafeDeltaSeconds = FMath::Max(0.0f, DeltaSeconds);
	for (FABTSM11FinaleBirdTrailParticle& Sample : TrailSamples)
	{
		Sample.AgeSeconds += SafeDeltaSeconds;
		Sample.LocalPosition += Sample.LocalVelocityCMPerSecond
			* SafeDeltaSeconds;
	}
	int32 RetainedCount = 0;
	for (int32 SampleIndex = 0;
		SampleIndex < TrailSamples.Num();
		++SampleIndex)
	{
		FABTSM11FinaleBirdTrailParticle& Sample = TrailSamples[SampleIndex];
		if (Sample.AgeSeconds > Sample.LifetimeSeconds)
		{
			continue;
		}
		if (RetainedCount != SampleIndex)
		{
			TrailSamples[RetainedCount] = MoveTemp(Sample);
		}
		++RetainedCount;
	}
	if (RetainedCount != TrailSamples.Num())
	{
		TrailSamples.SetNum(RetainedCount, EAllowShrinking::No);
	}

	const FTransform ComponentTransform = GetComponentTransform();
	const FVector PreviousWorldPosition = ComponentTransform.TransformPosition(
		LatestEmitterLocalPosition);
	const FVector Segment = WorldPosition - PreviousWorldPosition;
	const double SegmentLengthCM = Segment.Size();
	if (SafeDeltaSeconds > UE_SMALL_NUMBER
		&& FMath::IsFinite(SegmentLengthCM)
		&& SegmentLengthCM > UE_SMALL_NUMBER)
	{
		const FVector TravelDirection = Segment / SegmentLengthCM;
		const float BirdSpeedCMPerSecond = static_cast<float>(
			SegmentLengthCM / SafeDeltaSeconds);
		const float NominalSpacingCM = ABTSM11ResolveTrailNominalSpacingCM();
		const float JitterFraction =
			ABTSM11ResolveTrailBlueNoiseJitterFraction();
		double TraversedDistanceCM = 0.0;
		int32 EmissionCount = 0;
		while (DistanceUntilNextEmissionCM
				<= SegmentLengthCM - TraversedDistanceCM
			&& EmissionCount < MaximumEmissionsPerAdvance)
		{
			TraversedDistanceCM += DistanceUntilNextEmissionCM;
			const float SegmentAlpha = static_cast<float>(FMath::Clamp(
				TraversedDistanceCM / SegmentLengthCM,
				0.0,
				1.0));
			const float InitialAgeSeconds =
				(1.0f - SegmentAlpha) * SafeDeltaSeconds;
			const FVector EmissionWorldPosition = FMath::Lerp(
				PreviousWorldPosition,
				WorldPosition,
				SegmentAlpha);
			EmitParticle(
				EmissionWorldPosition,
				TravelDirection,
				BirdSpeedCMPerSecond,
				InitialAgeSeconds);
			DistanceUntilNextEmissionCM =
				ABTSM11ResolveTrailBlueNoiseSpacingCM(
					NextParticleSerial - 1u,
					NominalSpacingCM,
					JitterFraction);
			++EmissionCount;
		}

		const double UnconsumedSegmentLengthCM =
			SegmentLengthCM - TraversedDistanceCM;
		if (EmissionCount == MaximumEmissionsPerAdvance
			&& DistanceUntilNextEmissionCM <= UnconsumedSegmentLengthCM)
		{
			UE_LOG(
				LogABTSRuntime,
				Warning,
				TEXT("[ABTS][M11-C][BirdTrail] Emission guard reset. SegmentCM=%.1f Limit=%d"),
				SegmentLengthCM,
				MaximumEmissionsPerAdvance);
			TrailSamples.Reset();
			DistanceUntilNextEmissionCM = 0.0f;
		}
		else
		{
			DistanceUntilNextEmissionCM = FMath::Max(
				0.0f,
				DistanceUntilNextEmissionCM
					- static_cast<float>(UnconsumedSegmentLengthCM));
		}
	}

	LatestEmitterLocalPosition = ComponentTransform.InverseTransformPosition(
		WorldPosition);
	bHasLatestEmitterPosition = true;
	NotifyTrailChanged();
}

void UABTSM11FinaleBirdTrailComponent::ClearTrail()
{
	if (!bTrailActive && TrailSamples.IsEmpty())
	{
		return;
	}
	bTrailActive = false;
	TrailSamples.Reset();
	LatestEmitterLocalPosition = FVector::ZeroVector;
	NextParticleSerial = 0;
	DistanceUntilNextEmissionCM = 0.0f;
	bHasLatestEmitterPosition = false;
	NotifyTrailChanged();
}

void UABTSM11FinaleBirdTrailComponent::EmitParticle(
	const FVector& EmissionWorldPosition,
	const FVector& TravelDirection,
	const float BirdSpeedCMPerSecond,
	const float InitialAgeSeconds)
{
	FVector LateralWorldVelocityCMPerSecond = FVector::ZeroVector;
	float LifetimeSeconds = ABTSM11ResolveTrailLifetimeSeconds();
	float SizeScale = 1.0f;
	ABTSM11ResolveTrailParticleVariation(
		NextParticleSerial++,
		TravelDirection,
		BirdSpeedCMPerSecond,
		ABTSM11ResolveTrailMaximumLateralSpeedFraction(),
		LifetimeSeconds,
		ABTSM11ResolveTrailMinimumFastLifetimeScale(),
		LateralWorldVelocityCMPerSecond,
		LifetimeSeconds,
		SizeScale);
	if (InitialAgeSeconds > LifetimeSeconds)
	{
		return;
	}

	const FTransform ComponentTransform = GetComponentTransform();
	FABTSM11FinaleBirdTrailParticle& Particle =
		TrailSamples.AddDefaulted_GetRef();
	Particle.LocalVelocityCMPerSecond =
		ComponentTransform.InverseTransformVectorNoScale(
			LateralWorldVelocityCMPerSecond);
	Particle.AgeSeconds = FMath::Max(0.0f, InitialAgeSeconds);
	Particle.LifetimeSeconds = LifetimeSeconds;
	Particle.SizeScale = SizeScale;
	Particle.LocalPosition = ComponentTransform.InverseTransformPosition(
		EmissionWorldPosition)
		+ Particle.LocalVelocityCMPerSecond * Particle.AgeSeconds;

	if (TrailSamples.Num() > MaximumTrailSampleCount)
	{
		TrailSamples.RemoveAt(
			0,
			TrailSamples.Num() - MaximumTrailSampleCount,
			EAllowShrinking::No);
	}
}

float UABTSM11FinaleBirdTrailComponent::GetNominalSampleSpacingCM() const
{
	return ABTSM11ResolveTrailNominalSpacingCM();
}

float UABTSM11FinaleBirdTrailComponent::GetBlueNoiseSiteJitterFraction() const
{
	return ABTSM11ResolveTrailBlueNoiseJitterFraction();
}

float UABTSM11FinaleBirdTrailComponent::GetBaseTrailLifetimeSeconds() const
{
	return ABTSM11ResolveTrailLifetimeSeconds();
}

float UABTSM11FinaleBirdTrailComponent::GetMaximumLateralSpeedFraction() const
{
	return ABTSM11ResolveTrailMaximumLateralSpeedFraction();
}

float UABTSM11FinaleBirdTrailComponent::GetMinimumFastParticleLifetimeScale()
	const
{
	return ABTSM11ResolveTrailMinimumFastLifetimeScale();
}

int32 UABTSM11FinaleBirdTrailComponent::GetTrailRenderMode() const
{
	return ABTSM11ResolveTrailRenderModeGameThread();
}

float UABTSM11FinaleBirdTrailComponent::GetTrailHaloDiameterPixels() const
{
	return ABTSM11ResolveTrailHaloDiameterPixelsGameThread();
}

float UABTSM11FinaleBirdTrailComponent::GetTrailContrastShellDiameterPixels() const
{
	return ABTSM11ResolveTrailContrastShellDiameterPixelsGameThread();
}

float UABTSM11FinaleBirdTrailComponent::GetTrailCoreDiameterPixels() const
{
	return ABTSM11ResolveTrailCoreDiameterPixelsGameThread();
}

float UABTSM11FinaleBirdTrailComponent::GetTrailHaloIntensity() const
{
	return ABTSM11ResolveTrailHaloIntensityGameThread();
}

float UABTSM11FinaleBirdTrailComponent::GetTrailContrastShellOpacity() const
{
	return ABTSM11ResolveTrailContrastShellOpacityGameThread();
}

float UABTSM11FinaleBirdTrailComponent::GetTrailCoreIntensity() const
{
	return ABTSM11ResolveTrailCoreIntensityGameThread();
}

float UABTSM11FinaleBirdTrailComponent::GetTrailCoreCompositeOpacity() const
{
	return ABTSM11ResolveTrailCoreCompositeOpacityGameThread();
}

float UABTSM11FinaleBirdTrailComponent::GetTrailSpriteSoftness() const
{
	return ABTSM11ResolveTrailSpriteSoftness();
}

float UABTSM11FinaleBirdTrailComponent::GetTrailExpansionFraction() const
{
	return ABTSM11ResolveTrailExpansionFractionGameThread();
}

float UABTSM11FinaleBirdTrailComponent::GetTrailCoreFadeExponent() const
{
	return ABTSM11ResolveTrailCoreFadeExponentGameThread();
}

float UABTSM11FinaleBirdTrailComponent::GetTrailHaloFadeExponent() const
{
	return ABTSM11ResolveTrailHaloFadeExponentGameThread();
}

bool UABTSM11FinaleBirdTrailComponent::HasGeneratedSpriteTexture() const
{
	return TrailSpriteTexture != nullptr;
}

int32 UABTSM11FinaleBirdTrailComponent::GetGeneratedSpriteTextureSize() const
{
	return TrailSpriteTexture != nullptr ? TrailSpriteTexture->GetSizeX() : 0;
}

float UABTSM11FinaleBirdTrailComponent::GetOldestSampleAgeSeconds() const
{
	return TrailSamples.IsEmpty() ? 0.0f : TrailSamples[0].AgeSeconds;
}

float UABTSM11FinaleBirdTrailComponent::GetTrailSampleAgeSeconds(
	const int32 SampleIndex) const
{
	return TrailSamples.IsValidIndex(SampleIndex)
		? TrailSamples[SampleIndex].AgeSeconds
		: 0.0f;
}

float UABTSM11FinaleBirdTrailComponent::GetTrailSampleLifetimeSeconds(
	const int32 SampleIndex) const
{
	return TrailSamples.IsValidIndex(SampleIndex)
		? TrailSamples[SampleIndex].LifetimeSeconds
		: 0.0f;
}

FVector UABTSM11FinaleBirdTrailComponent::GetTrailSampleWorldPosition(
	const int32 SampleIndex) const
{
	return TrailSamples.IsValidIndex(SampleIndex)
		? GetComponentTransform().TransformPosition(
			TrailSamples[SampleIndex].LocalPosition)
		: FVector::ZeroVector;
}

FVector UABTSM11FinaleBirdTrailComponent::GetTrailSampleEmissionWorldPosition(
	const int32 SampleIndex) const
{
	if (!TrailSamples.IsValidIndex(SampleIndex))
	{
		return FVector::ZeroVector;
	}
	const FABTSM11FinaleBirdTrailParticle& Particle = TrailSamples[SampleIndex];
	return GetComponentTransform().TransformPosition(
		Particle.LocalPosition
			- Particle.LocalVelocityCMPerSecond * Particle.AgeSeconds);
}

FVector UABTSM11FinaleBirdTrailComponent::GetTrailSampleWorldVelocity(
	const int32 SampleIndex) const
{
	return TrailSamples.IsValidIndex(SampleIndex)
		? GetComponentTransform().TransformVectorNoScale(
			TrailSamples[SampleIndex].LocalVelocityCMPerSecond)
		: FVector::ZeroVector;
}

bool UABTSM11FinaleBirdTrailComponent::AreTrailSampleAgesOrdered() const
{
	for (int32 SampleIndex = 1;
		SampleIndex < TrailSamples.Num();
		++SampleIndex)
	{
		if (TrailSamples[SampleIndex - 1].AgeSeconds
			+ KINDA_SMALL_NUMBER
			< TrailSamples[SampleIndex].AgeSeconds)
		{
			return false;
		}
	}
	return true;
}

FVector UABTSM11FinaleBirdTrailComponent::GetNewestSampleWorldPosition() const
{
	return !bHasLatestEmitterPosition
		? FVector::ZeroVector
		: GetComponentTransform().TransformPosition(
			LatestEmitterLocalPosition);
}

FPrimitiveSceneProxy*
UABTSM11FinaleBirdTrailComponent::CreateSceneProxy()
{
	return new FABTSM11FinaleBirdTrailSceneProxy(
		this,
		TrailSamples,
		TrailSpriteTexture != nullptr
			? TrailSpriteTexture->GetResource()
			: nullptr);
}

FBoxSphereBounds UABTSM11FinaleBirdTrailComponent::CalcBounds(
	const FTransform& LocalToWorld) const
{
	FBox LocalBox(ForceInit);
	for (const FABTSM11FinaleBirdTrailParticle& Sample : TrailSamples)
	{
		LocalBox += Sample.LocalPosition;
	}
	if (!LocalBox.IsValid)
	{
		LocalBox = FBox(FVector::ZeroVector, FVector::ZeroVector);
	}
	return FBoxSphereBounds(LocalBox.ExpandBy(100.0f)).TransformBy(
		LocalToWorld);
}

void UABTSM11FinaleBirdTrailComponent::NotifyTrailChanged()
{
	MarkRenderStateDirty();
	UpdateComponentToWorld();
}

bool UABTSM11FinaleBirdTrailComponent::EnsureTrailSpriteTexture()
{
	const float Softness = ABTSM11ResolveTrailSpriteSoftness();
	if (TrailSpriteTexture != nullptr
		&& FMath::IsNearlyEqual(GeneratedSpriteSoftness, Softness))
	{
		return true;
	}

	TArray64<uint8> ImageData;
	ImageData.SetNumUninitialized(
		static_cast<int64>(TrailSpriteTextureSize)
			* TrailSpriteTextureSize
			* 4);
	const float FalloffExponent = FMath::Lerp(5.0f, 0.85f, Softness);
	for (int32 PixelY = 0; PixelY < TrailSpriteTextureSize; ++PixelY)
	{
		for (int32 PixelX = 0; PixelX < TrailSpriteTextureSize; ++PixelX)
		{
			const float NormalizedX =
				(static_cast<float>(PixelX) + 0.5f)
					/ static_cast<float>(TrailSpriteTextureSize)
					* 2.0f
				- 1.0f;
			const float NormalizedY =
				(static_cast<float>(PixelY) + 0.5f)
					/ static_cast<float>(TrailSpriteTextureSize)
					* 2.0f
				- 1.0f;
			const float RadialBase = FMath::Max(
				0.0f,
				1.0f - NormalizedX * NormalizedX
					- NormalizedY * NormalizedY);
			const uint8 RadialValue = static_cast<uint8>(FMath::RoundToInt(
				FMath::Pow(RadialBase, FalloffExponent) * 255.0f));
			const int64 PixelOffset =
				(static_cast<int64>(PixelY) * TrailSpriteTextureSize + PixelX)
				* 4;
			// PF_B8G8R8A8. RGB must carry the radial falloff because additive
			// simple-element blending uses One/One rather than source alpha.
			ImageData[PixelOffset + 0] = RadialValue;
			ImageData[PixelOffset + 1] = RadialValue;
			ImageData[PixelOffset + 2] = RadialValue;
			ImageData[PixelOffset + 3] = RadialValue;
		}
	}

	UTexture2D* GeneratedTexture = UTexture2D::CreateTransient(
		TrailSpriteTextureSize,
		TrailSpriteTextureSize,
		PF_B8G8R8A8,
		NAME_None,
		ImageData);
	if (GeneratedTexture == nullptr)
	{
		return false;
	}
	GeneratedTexture->SRGB = false;
	GeneratedTexture->NeverStream = true;
	GeneratedTexture->Filter = TF_Bilinear;
	GeneratedTexture->AddressX = TA_Clamp;
	GeneratedTexture->AddressY = TA_Clamp;
	GeneratedTexture->LODGroup = TEXTUREGROUP_Effects;
	GeneratedTexture->UpdateResource();
	TrailSpriteTexture = GeneratedTexture;
	GeneratedSpriteSoftness = Softness;
	return true;
}
