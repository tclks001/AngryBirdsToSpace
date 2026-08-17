// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "UI/ABTSGameUserSettings.h"
#include "UI/ABTSGameViewportClient.h"

#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSGameUserSettingsContractTest,
	"ABTS.UI.SystemMenu.SettingsContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSGameUserSettingsContractTest::RunTest(const FString& Parameters)
{
	UABTSGameUserSettings* Settings = NewObject<UABTSGameUserSettings>();
	TestNotNull(TEXT("Settings object can be created without assets"), Settings);
	if (!Settings) return false;

	Settings->SetToDefaults();
	TestEqual(TEXT("Settings schema is frozen at v1"), UABTSGameUserSettings::SettingsSchemaVersion, 1);
	TestEqual(TEXT("Master defaults to unity"), Settings->GetMasterVolume(), 1.0f);
	TestEqual(TEXT("Music defaults to unity"), Settings->GetMusicVolume(), 1.0f);
	TestEqual(TEXT("Default quality is Epic"), Settings->GetOverallScalabilityLevel(), 3);
	TestEqual(TEXT("Default frame cap is 60"), Settings->GetFrameRateLimit(), 60.0f);
	TestTrue(TEXT("Subtitles default on"), Settings->GetSubtitlesEnabled());

	Settings->SetMasterVolume(-1.0f);
	Settings->SetMusicVolume(2.0f);
	Settings->SetMenuScale(4.0f);
	Settings->SetDisplayGamma(0.5f);
	TestEqual(TEXT("Master clamps low"), Settings->GetMasterVolume(), 0.0f);
	TestEqual(TEXT("Music clamps high"), Settings->GetMusicVolume(), 1.0f);
	TestEqual(TEXT("Menu scale clamps high"), Settings->GetMenuScale(), 1.25f);
	TestEqual(TEXT("Gamma clamps low"), Settings->GetABTSDisplayGamma(), 1.8f);

	Settings->SetReduceMotion(true);
	Settings->SetHighContrastMenu(true);
	Settings->SetMuteWhenUnfocused(false);
	const FString Summary = Settings->BuildDiagnosticSummary();
	TestTrue(TEXT("Summary includes schema identity"), Summary.Contains(TEXT("Schema=1")));
	TestTrue(TEXT("Summary includes reduced motion"), Summary.Contains(TEXT("ReduceMotion=1")));
	TestTrue(TEXT("Summary includes high contrast"), Summary.Contains(TEXT("HighContrast=1")));

	const TArray<FIntPoint> Resolutions = UABTSGameViewportClient::BuildFallbackResolutionOptions(FIntPoint(2560, 1440));
	TestTrue(TEXT("Fallback resolutions include 720p"), Resolutions.Contains(FIntPoint(1280, 720)));
	TestTrue(TEXT("Fallback resolutions include the desktop"), Resolutions.Contains(FIntPoint(2560, 1440)));
	TestFalse(TEXT("Fallback resolutions do not exceed the desktop"), Resolutions.Contains(FIntPoint(3840, 2160)));
	TestEqual(TEXT("Unlimited frame cap has a readable label"), UABTSGameViewportClient::FormatFrameRateLimit(0.0f), FString(TEXT("UNLIMITED")));
	TestEqual(TEXT("Finite frame cap has a readable label"), UABTSGameViewportClient::FormatFrameRateLimit(120.0f), FString(TEXT("120 FPS")));
	TestEqual(TEXT("Confirmation countdown rounds up"), UABTSGameViewportClient::ComputeConfirmationSecondsRemaining(12.0, 0.1), 12);
	TestEqual(TEXT("Confirmation countdown clamps at zero"), UABTSGameViewportClient::ComputeConfirmationSecondsRemaining(12.0, 13.0), 0);
	TestEqual(TEXT("Startup progress begins at zero"),
		UABTSGameViewportClient::ComputeStartupLoadingProgress(0.0, false), 0.0f);
	TestEqual(TEXT("Startup progress follows the 30 second target"),
		UABTSGameViewportClient::ComputeStartupLoadingProgress(15.0, false), 0.5f);
	TestEqual(TEXT("Startup progress remains capped before Ready"),
		UABTSGameViewportClient::ComputeStartupLoadingProgress(90.0, false), 0.92f);
	TestEqual(TEXT("Startup progress completes only at Ready"),
		UABTSGameViewportClient::ComputeStartupLoadingProgress(1.0, true), 1.0f);
	TestFalse(TEXT("Presentation never opens before authoritative world Ready"),
		UABTSGameViewportClient::IsStartupPresentationReady(false, true, 8));
	TestFalse(TEXT("Presentation never opens onto a missing front-end surface"),
		UABTSGameViewportClient::IsStartupPresentationReady(true, false, 8));
	TestFalse(TEXT("First complete front-end draw stays behind the loading cover"),
		UABTSGameViewportClient::IsStartupPresentationReady(true, true, 1));
	TestTrue(TEXT("Second complete draw atomically exposes the warm entry frame"),
		UABTSGameViewportClient::IsStartupPresentationReady(true, true, 2));
	TestNotNull(TEXT("Engine constructed the configured ABTS settings class"), UABTSGameUserSettings::Get());
	FString ViewportClass;
	GConfig->GetString(TEXT("/Script/Engine.Engine"), TEXT("GameViewportClientClassName"), ViewportClass, GEngineIni);
	TestEqual(TEXT("Shared viewport client is globally bound"), ViewportClass, FString(TEXT("/Script/ABTSRuntime.ABTSGameViewportClient")));
	return true;
}

#endif
