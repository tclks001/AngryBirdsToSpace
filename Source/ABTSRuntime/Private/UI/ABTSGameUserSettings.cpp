// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/ABTSGameUserSettings.h"

#include "ABTSRuntime.h"
#include "Audio/ABTSAudioWorldSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"

namespace
{
	UWorld* ResolveCurrentGameWorld()
	{
		return GEngine && GEngine->GameViewport ? GEngine->GameViewport->GetWorld() : nullptr;
	}
}

UABTSGameUserSettings* UABTSGameUserSettings::Get()
{
	return Cast<UABTSGameUserSettings>(UGameUserSettings::GetGameUserSettings());
}

void UABTSGameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	ABTSSettingsVersion = SettingsSchemaVersion;
	MasterVolume = 1.0f;
	MusicVolume = 1.0f;
	SFXVolume = 1.0f;
	UIVolume = 1.0f;
	AmbienceVolume = 1.0f;
	MenuScale = 1.0f;
	DisplayGamma = 2.2f;
	bSubtitlesEnabled = true;
	bMuteWhenUnfocused = true;
	bReduceMotion = false;
	bHighContrastMenu = false;
	SetOverallScalabilityLevel(3);
	SetVSyncEnabled(false);
	SetDynamicResolutionEnabled(false);
	SetFrameRateLimit(60.0f);
}

void UABTSGameUserSettings::ValidateSettings()
{
	Super::ValidateSettings();
	if (ABTSSettingsVersion != SettingsSchemaVersion)
	{
		SetToDefaults();
	}
	ABTSSettingsVersion = SettingsSchemaVersion;
	SetMasterVolume(MasterVolume);
	SetMusicVolume(MusicVolume);
	SetSFXVolume(SFXVolume);
	SetUIVolume(UIVolume);
	SetAmbienceVolume(AmbienceVolume);
	SetMenuScale(MenuScale);
	SetDisplayGamma(DisplayGamma);
	SetFrameRateLimit(FMath::Clamp(GetFrameRateLimit(), 0.0f, 360.0f));
}

void UABTSGameUserSettings::ApplyNonResolutionSettings()
{
	Super::ApplyNonResolutionSettings();
	ApplyABTSAccessibility();
	ApplyABTSAudio(ResolveCurrentGameWorld(), 0.08f);
}

void UABTSGameUserSettings::ApplyABTSAudio(UWorld* World, const float FadeSeconds) const
{
	if (World)
	{
		if (UABTSAudioWorldSubsystem* Audio = World->GetSubsystem<UABTSAudioWorldSubsystem>())
		{
			Audio->SetCategoryVolumes(
				MasterVolume * MusicVolume,
				MasterVolume * SFXVolume,
				MasterVolume * UIVolume,
				MasterVolume * AmbienceVolume,
				FadeSeconds);
		}
	}
}

void UABTSGameUserSettings::ApplyABTSAccessibility() const
{
	FApp::SetUnfocusedVolumeMultiplier(bMuteWhenUnfocused ? 0.0f : 1.0f);
	UGameplayStatics::SetSubtitlesEnabled(bSubtitlesEnabled);
	if (GEngine)
	{
		GEngine->DisplayGamma = DisplayGamma;
	}
	if (IConsoleVariable* MotionBlurQuality = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality")))
	{
		MotionBlurQuality->Set(bReduceMotion ? 0 : 4, ECVF_SetByGameSetting);
	}
}

void UABTSGameUserSettings::ApplyAndSave(
	UWorld* World,
	const bool bApplyResolution,
	const float AudioFadeSeconds)
{
	ValidateSettings();
	if (bApplyResolution)
	{
		ApplySettings(false);
	}
	else
	{
		ApplyNonResolutionSettings();
		SaveSettings();
	}
	ApplyABTSAccessibility();
	ApplyABTSAudio(World, AudioFadeSeconds);
	UE_LOG(LogABTSRuntime, Log, TEXT("[ABTS][Settings] Applied %s"), *BuildDiagnosticSummary());
}

FString UABTSGameUserSettings::BuildDiagnosticSummary() const
{
	const FIntPoint Resolution = GetScreenResolution();
	return FString::Printf(
		TEXT("Schema=%d Master=%.2f Music=%.2f SFX=%.2f UI=%.2f Ambience=%.2f Quality=%d Resolution=%dx%d Mode=%d VSync=%d DynamicResolution=%d FrameCap=%.0f MenuScale=%.2f Gamma=%.2f Subtitles=%d MuteUnfocused=%d ReduceMotion=%d HighContrast=%d"),
		ABTSSettingsVersion,
		MasterVolume,
		MusicVolume,
		SFXVolume,
		UIVolume,
		AmbienceVolume,
		GetOverallScalabilityLevel(),
		Resolution.X,
		Resolution.Y,
		static_cast<int32>(GetFullscreenMode()),
		IsVSyncEnabled() ? 1 : 0,
		IsDynamicResolutionEnabled() ? 1 : 0,
		GetFrameRateLimit(),
		MenuScale,
		DisplayGamma,
		bSubtitlesEnabled ? 1 : 0,
		bMuteWhenUnfocused ? 1 : 0,
		bReduceMotion ? 1 : 0,
		bHighContrastMenu ? 1 : 0);
}
