// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "ABTSGameUserSettings.generated.h"

class UWorld;

/** Persistent, asset-free player settings shared by every ABTS map. */
UCLASS(Config=GameUserSettings)
class ABTSRUNTIME_API UABTSGameUserSettings final : public UGameUserSettings
{
	GENERATED_BODY()

public:
	static constexpr int32 SettingsSchemaVersion = 1;

	virtual void SetToDefaults() override;
	virtual void ValidateSettings() override;
	virtual void ApplyNonResolutionSettings() override;

	static UABTSGameUserSettings* Get();

	void ApplyABTSAudio(UWorld* World, float FadeSeconds = 0.08f) const;
	void ApplyABTSAccessibility() const;
	void ApplyAndSave(UWorld* World, bool bApplyResolution, float AudioFadeSeconds = 0.08f);

	void SetMasterVolume(float Value) { MasterVolume = FMath::Clamp(Value, 0.0f, 1.0f); }
	void SetMusicVolume(float Value) { MusicVolume = FMath::Clamp(Value, 0.0f, 1.0f); }
	void SetSFXVolume(float Value) { SFXVolume = FMath::Clamp(Value, 0.0f, 1.0f); }
	void SetUIVolume(float Value) { UIVolume = FMath::Clamp(Value, 0.0f, 1.0f); }
	void SetAmbienceVolume(float Value) { AmbienceVolume = FMath::Clamp(Value, 0.0f, 1.0f); }
	void SetMenuScale(float Value) { MenuScale = FMath::Clamp(Value, 0.80f, 1.25f); }
	void SetDisplayGamma(float Value) { DisplayGamma = FMath::Clamp(Value, 1.8f, 2.6f); }
	void SetSubtitlesEnabled(bool bValue) { bSubtitlesEnabled = bValue; }
	void SetMuteWhenUnfocused(bool bValue) { bMuteWhenUnfocused = bValue; }
	void SetReduceMotion(bool bValue) { bReduceMotion = bValue; }
	void SetHighContrastMenu(bool bValue) { bHighContrastMenu = bValue; }

	float GetMasterVolume() const { return MasterVolume; }
	float GetMusicVolume() const { return MusicVolume; }
	float GetSFXVolume() const { return SFXVolume; }
	float GetUIVolume() const { return UIVolume; }
	float GetAmbienceVolume() const { return AmbienceVolume; }
	float GetMenuScale() const { return MenuScale; }
	float GetABTSDisplayGamma() const { return DisplayGamma; }
	bool GetSubtitlesEnabled() const { return bSubtitlesEnabled; }
	bool GetMuteWhenUnfocused() const { return bMuteWhenUnfocused; }
	bool GetReduceMotion() const { return bReduceMotion; }
	bool GetHighContrastMenu() const { return bHighContrastMenu; }

	FString BuildDiagnosticSummary() const;

private:
	UPROPERTY(Config)
	int32 ABTSSettingsVersion = SettingsSchemaVersion;

	UPROPERTY(Config)
	float MasterVolume = 1.0f;

	UPROPERTY(Config)
	float MusicVolume = 1.0f;

	UPROPERTY(Config)
	float SFXVolume = 1.0f;

	UPROPERTY(Config)
	float UIVolume = 1.0f;

	UPROPERTY(Config)
	float AmbienceVolume = 1.0f;

	UPROPERTY(Config)
	float MenuScale = 1.0f;

	UPROPERTY(Config)
	float DisplayGamma = 2.2f;

	UPROPERTY(Config)
	bool bSubtitlesEnabled = true;

	UPROPERTY(Config)
	bool bMuteWhenUnfocused = true;

	UPROPERTY(Config)
	bool bReduceMotion = false;

	UPROPERTY(Config)
	bool bHighContrastMenu = false;
};
