// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "UI/ABTSGameUserSettings.h"

#include "Misc/AutomationTest.h"

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
	return true;
}

#endif
