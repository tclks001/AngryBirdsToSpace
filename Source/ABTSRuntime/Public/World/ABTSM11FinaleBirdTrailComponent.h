// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/PrimitiveComponent.h"
#include "CoreMinimal.h"
#include "ABTSM11FinaleBirdTrailComponent.generated.h"

class UTexture2D;

struct FABTSM11FinaleBirdTrailParticle
{
	FVector LocalPosition = FVector::ZeroVector;
	FVector LocalVelocityCMPerSecond = FVector::ZeroVector;
	float AgeSeconds = 0.0f;
	float LifetimeSeconds = 0.0f;
	float SizeScale = 1.0f;
};

/**
 * Project-asset-free, camera-independent presentation trail for M11 playback.
 *
 * The component receives only the already-resolved bird world position. It
 * never feeds trajectory, collision, candidate identity or camera state back
 * into gameplay. The render proxy draws short-lived, depth-tested procedural
 * soft sprites in both the player view and ordinary scene captures.
 */
UCLASS(ClassGroup = "ABTS")
class ABTSRUNTIME_API UABTSM11FinaleBirdTrailComponent final
	: public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UABTSM11FinaleBirdTrailComponent();

	void BeginTrail(const FVector& WorldPosition);
	void AdvanceTrail(const FVector& WorldPosition, float DeltaSeconds);
	void ClearTrail();

	bool IsTrailActive() const { return bTrailActive; }
	int32 GetTrailSampleCount() const { return TrailSamples.Num(); }
	float GetTrailLifetimeSeconds() const
	{
		return GetBaseTrailLifetimeSeconds();
	}
	int32 GetMaximumTrailSampleCount() const
	{
		return MaximumTrailSampleCount;
	}
	float GetNominalSampleSpacingCM() const;
	float GetBlueNoiseSiteJitterFraction() const;
	float GetBaseTrailLifetimeSeconds() const;
	float GetMaximumLateralSpeedFraction() const;
	float GetMinimumFastParticleLifetimeScale() const;
	int32 GetTrailRenderMode() const;
	float GetTrailHaloDiameterPixels() const;
	float GetTrailContrastShellDiameterPixels() const;
	float GetTrailCoreDiameterPixels() const;
	float GetTrailHaloIntensity() const;
	float GetTrailContrastShellOpacity() const;
	float GetTrailCoreIntensity() const;
	float GetTrailCoreCompositeOpacity() const;
	float GetTrailSpriteSoftness() const;
	float GetTrailExpansionFraction() const;
	float GetTrailCoreFadeExponent() const;
	float GetTrailHaloFadeExponent() const;
	bool HasGeneratedSpriteTexture() const;
	int32 GetGeneratedSpriteTextureSize() const;
	float GetDistanceUntilNextEmissionCM() const
	{
		return DistanceUntilNextEmissionCM;
	}
	float GetOldestSampleAgeSeconds() const;
	float GetTrailSampleAgeSeconds(int32 SampleIndex) const;
	float GetTrailSampleLifetimeSeconds(int32 SampleIndex) const;
	FVector GetTrailSampleWorldPosition(int32 SampleIndex) const;
	FVector GetTrailSampleEmissionWorldPosition(int32 SampleIndex) const;
	FVector GetTrailSampleWorldVelocity(int32 SampleIndex) const;
	bool AreTrailSampleAgesOrdered() const;
	FVector GetNewestSampleWorldPosition() const;

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(
		const FTransform& LocalToWorld) const override;

private:
	void EmitParticle(
		const FVector& EmissionWorldPosition,
		const FVector& TravelDirection,
		float BirdSpeedCMPerSecond,
		float InitialAgeSeconds);
	bool EnsureTrailSpriteTexture();
	void NotifyTrailChanged();

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> TrailSpriteTexture;

	TArray<FABTSM11FinaleBirdTrailParticle> TrailSamples;
	FVector LatestEmitterLocalPosition = FVector::ZeroVector;
	uint32 NextParticleSerial = 0;
	float DistanceUntilNextEmissionCM = 0.0f;
	float GeneratedSpriteSoftness = -1.0f;
	bool bTrailActive = false;
	bool bHasLatestEmitterPosition = false;

	static constexpr int32 MaximumTrailSampleCount = 384;
	static constexpr int32 MaximumEmissionsPerAdvance = 96;
};
