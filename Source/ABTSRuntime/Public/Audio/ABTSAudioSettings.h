// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "ABTSAudioSettings.generated.h"

/** Project-wide audio asset catalog. Defaults match the checked-in Content layout. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="ABTS Audio"))
class ABTSRUNTIME_API UABTSAudioSettings final : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UABTSAudioSettings();

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	UPROPERTY(Config, EditAnywhere, Category="Assets|Music")
	TSoftObjectPtr<USoundBase> BassStem;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Music")
	TSoftObjectPtr<USoundBase> HarmonyStem;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Music")
	TSoftObjectPtr<USoundBase> MelodyStem;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Music")
	TSoftObjectPtr<USoundBase> PercussionStem;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Slingshot")
	TSoftObjectPtr<USoundBase> PullLoop;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Slingshot")
	TSoftObjectPtr<USoundBase> ReleaseSnap;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Slingshot")
	TSoftObjectPtr<USoundBase> ReleaseResonance;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Impact")
	TArray<TSoftObjectPtr<USoundBase>> WoodImpacts;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Impact")
	TArray<TSoftObjectPtr<USoundBase>> StoneImpacts;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Impact")
	TArray<TSoftObjectPtr<USoundBase>> MetalImpacts;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Impact")
	TArray<TSoftObjectPtr<USoundBase>> GlassImpacts;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Explosion")
	TSoftObjectPtr<USoundBase> ExplosionBody;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Explosion")
	TSoftObjectPtr<USoundBase> ExplosionLowTail;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Bird")
	TSoftObjectPtr<USoundBase> RedBirdChirp;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Bird")
	TSoftObjectPtr<USoundBase> BlueBirdChirp;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Bird")
	TSoftObjectPtr<USoundBase> YellowBirdChirp;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Bird")
	TSoftObjectPtr<USoundBase> BlackBirdChirp;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Locomotion")
	TArray<TSoftObjectPtr<USoundBase>> GrassFootsteps;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Locomotion")
	TArray<TSoftObjectPtr<USoundBase>> WoodFootsteps;

	UPROPERTY(Config, EditAnywhere, Category="Assets|Pickup")
	TSoftObjectPtr<USoundBase> Pickup;

	UPROPERTY(Config, EditAnywhere, Category="Assets|UI")
	TSoftObjectPtr<USoundBase> UIOpen;

	UPROPERTY(Config, EditAnywhere, Category="Assets|UI")
	TSoftObjectPtr<USoundBase> UIClose;

	UPROPERTY(Config, EditAnywhere, Category="Assets|UI")
	TSoftObjectPtr<USoundBase> UISelect;

	UPROPERTY(Config, EditAnywhere, Category="Assets|UI")
	TSoftObjectPtr<USoundBase> UIConfirm;

	UPROPERTY(Config, EditAnywhere, Category="Assets|UI")
	TSoftObjectPtr<USoundBase> UIError;

	UPROPERTY(Config, EditAnywhere, Category="Assets|UI")
	TSoftObjectPtr<USoundBase> UITick;

	UPROPERTY(Config, EditAnywhere, Category="Infrastructure")
	TSoftObjectPtr<USoundClass> MusicSoundClass;

	UPROPERTY(Config, EditAnywhere, Category="Infrastructure")
	TSoftObjectPtr<USoundClass> SFXSoundClass;

	UPROPERTY(Config, EditAnywhere, Category="Infrastructure")
	TSoftObjectPtr<USoundClass> UISoundClass;

	UPROPERTY(Config, EditAnywhere, Category="Infrastructure")
	TSoftObjectPtr<USoundClass> AmbienceSoundClass;

	UPROPERTY(Config, EditAnywhere, Category="Infrastructure")
	TSoftObjectPtr<USoundMix> MasterSoundMix;

	UPROPERTY(Config, EditAnywhere, Category="Mix", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MusicVolume = 0.72f;

	UPROPERTY(Config, EditAnywhere, Category="Mix", meta=(ClampMin="0.0", ClampMax="1.0"))
	float SFXVolume = 0.90f;

	UPROPERTY(Config, EditAnywhere, Category="Mix", meta=(ClampMin="0.0", ClampMax="1.0"))
	float UIVolume = 0.82f;

	UPROPERTY(Config, EditAnywhere, Category="Mix", meta=(ClampMin="0.0", ClampMax="1.0"))
	float AmbienceVolume = 0.55f;

	UPROPERTY(Config, EditAnywhere, Category="Mix", meta=(ClampMin="0.0", ClampMax="0.5"))
	float MusicFadeSeconds = 0.35f;

	UPROPERTY(Config, EditAnywhere, Category="Impact", meta=(ClampMin="0.0"))
	float MinimumImpactSpeedCMPerSec = 180.0f;

	UPROPERTY(Config, EditAnywhere, Category="Impact", meta=(ClampMin="1.0"))
	float FullVolumeImpactSpeedCMPerSec = 1800.0f;

	UPROPERTY(Config, EditAnywhere, Category="Impact", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ImpactCooldownSeconds = 0.08f;

	UPROPERTY(Config, EditAnywhere, Category="Bird", meta=(ClampMin="0.0", ClampMax="2.0"))
	float BirdChirpCooldownSeconds = 0.18f;
};
