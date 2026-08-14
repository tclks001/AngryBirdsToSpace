// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Party/ABTSBirdTypes.h"
#include "Slingshot/ABTSM6Types.h"
#include "Subsystems/WorldSubsystem.h"
#include "ABTSAudioWorldSubsystem.generated.h"

class UAudioComponent;
class USoundBase;
class USoundClass;
class USoundMix;

UENUM(BlueprintType)
enum class EABTSMusicState : uint8
{
	Explore,
	Approach,
	Aim,
	Destruction,
	Satellite,
	Finale
};

UENUM(BlueprintType)
enum class EABTSUIAudioEvent : uint8
{
	Open,
	Close,
	Select,
	Confirm,
	Error,
	Tick
};

UENUM(BlueprintType)
enum class EABTSFootstepSurface : uint8
{
	Grass,
	Wood
};

/** Automatically owns synchronized music stems and shared gameplay/UI one-shots for each game world. */
UCLASS()
class ABTSRUNTIME_API UABTSAudioWorldSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="ABTS|Audio")
	void SetMusicState(EABTSMusicState NewState, float FadeSeconds = -1.0f);

	UFUNCTION(BlueprintCallable, Category="ABTS|Audio")
	void UpdateSlingshotPull(const FVector& WorldLocation, float RestCordLengthCM, float PullPowerAlpha);

	UFUNCTION(BlueprintCallable, Category="ABTS|Audio")
	void StopSlingshotPull(float FadeSeconds = 0.15f);

	UFUNCTION(BlueprintCallable, Category="ABTS|Audio")
	void PlaySlingshotRelease(const FVector& WorldLocation, float RestCordLengthCM, float PullPowerAlpha);

	UFUNCTION(BlueprintCallable, Category="ABTS|Audio")
	void PlayImpact(const FVector& WorldLocation, EABTSM6ImpactMaterial Material, float NormalSpeedCMPerSec);

	UFUNCTION(BlueprintCallable, Category="ABTS|Audio")
	void PlayExplosion(const FVector& WorldLocation, bool bLarge = true);

	UFUNCTION(BlueprintCallable, Category="ABTS|Audio")
	void PlayBirdChirp(const FVector& WorldLocation, EABTSBirdId BirdId, float VolumeMultiplier = 1.0f);

	UFUNCTION(BlueprintCallable, Category="ABTS|Audio")
	void PlayFootstep(const FVector& WorldLocation, EABTSFootstepSurface Surface, float TangentialSpeedCMPerSec);

	UFUNCTION(BlueprintCallable, Category="ABTS|Audio")
	void PlayLanding(const FVector& WorldLocation, EABTSFootstepSurface Surface, float DownwardSpeedCMPerSec);

	UFUNCTION(BlueprintCallable, Category="ABTS|Audio")
	void PlayPickup(const FVector& WorldLocation, int32 Quantity = 1);

	UFUNCTION(BlueprintCallable, Category="ABTS|Audio")
	void PlayUIEvent(EABTSUIAudioEvent Event);

	UFUNCTION(BlueprintCallable, Category="ABTS|Audio")
	void SetCategoryVolumes(float Music, float SFX, float UI, float Ambience, float FadeSeconds = 0.1f);

	static float ComputeCordPitchMultiplier(float RestCordLengthCM);
	static float ComputePullLoopVolumeMultiplier(float PullPowerAlpha);
	static float ComputeFootstepSpacingCM(float TangentialSpeedCMPerSec);
	static float ComputeFootstepVolumeMultiplier(float TangentialSpeedCMPerSec);
	static float ComputeLandingVolumeMultiplier(float DownwardSpeedCMPerSec);
	static EABTSFootstepSurface ResolveFootstepSurfaceFromSemanticName(const FString& SemanticName);
	static void GetMusicStemTargets(EABTSMusicState State, float& OutBass, float& OutHarmony, float& OutMelody, float& OutPercussion);

private:
	void LoadCatalog();
	UAudioComponent* CreatePersistentComponent(USoundBase* Sound, USoundClass* SoundClass, bool bSpatialized);
	void PlayOneShot(USoundBase* Sound, USoundClass* SoundClass, const FVector& WorldLocation, bool bSpatialized, float Volume, float Pitch = 1.0f);
	USoundBase* SelectImpact(EABTSM6ImpactMaterial Material);
	USoundBase* SelectFootstep(EABTSFootstepSurface Surface);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudioComponent>> MusicComponents;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> PullComponent;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ReleaseSnapComponent;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> ReleaseResonanceComponent;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> MusicSounds;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> PullSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ReleaseSnapSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ReleaseResonanceSound;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> WoodImpactSounds;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> StoneImpactSounds;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> MetalImpactSounds;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> GlassImpactSounds;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ExplosionBodySound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> ExplosionTailSound;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> BirdChirpSounds;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> GrassFootstepSounds;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> WoodFootstepSounds;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> PickupSound;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USoundBase>> UISounds;

	UPROPERTY(Transient)
	TObjectPtr<USoundClass> MusicClass;

	UPROPERTY(Transient)
	TObjectPtr<USoundClass> SFXClass;

	UPROPERTY(Transient)
	TObjectPtr<USoundClass> UIClass;

	UPROPERTY(Transient)
	TObjectPtr<USoundClass> AmbienceClass;

	UPROPERTY(Transient)
	TObjectPtr<USoundMix> MasterMix;

	TMap<EABTSM6ImpactMaterial, double> LastImpactTimeByMaterial;
	TMap<EABTSBirdId, double> LastBirdChirpTimeByBird;
	uint32 ImpactVariantCounter = 0;
	uint32 GrassFootstepVariantCounter = 0;
	uint32 WoodFootstepVariantCounter = 0;
	EABTSMusicState MusicState = EABTSMusicState::Explore;
	float RuntimeMusicVolume = 1.0f;
	float RuntimeSFXVolume = 1.0f;
	float RuntimeUIVolume = 1.0f;
	float RuntimeAmbienceVolume = 1.0f;
	int32 SlingshotPrimeRequestCount = 0;
	bool bAudioReady = false;
};
