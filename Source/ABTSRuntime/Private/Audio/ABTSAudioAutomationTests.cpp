// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Audio/ABTSAudioSettings.h"
#include "Audio/ABTSAudioWorldSubsystem.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSAudioMappingTest,
	"ABTS.Audio.ReleaseAndMusicMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSAudioMappingTest::RunTest(const FString& Parameters)
{
	const float ShortCordPitch = UABTSAudioWorldSubsystem::ComputeCordPitchMultiplier(80.0f);
	const float ReferenceCordPitch = UABTSAudioWorldSubsystem::ComputeCordPitchMultiplier(120.0f);
	const float LongCordPitch = UABTSAudioWorldSubsystem::ComputeCordPitchMultiplier(180.0f);
	TestTrue(TEXT("Shorter unloaded cord has a higher fixed pitch"),
		ShortCordPitch > ReferenceCordPitch && ReferenceCordPitch > LongCordPitch);
	TestEqual(TEXT("Reference cord uses the source pitch"), ReferenceCordPitch, 1.0f);
	TestTrue(TEXT("Pull loop volume rises with pull power"),
		UABTSAudioWorldSubsystem::ComputePullLoopVolumeMultiplier(0.0f)
		< UABTSAudioWorldSubsystem::ComputePullLoopVolumeMultiplier(0.5f)
		&& UABTSAudioWorldSubsystem::ComputePullLoopVolumeMultiplier(0.5f)
		< UABTSAudioWorldSubsystem::ComputePullLoopVolumeMultiplier(1.0f));

	float Bass = 0.0f;
	float Harmony = 0.0f;
	float Melody = 0.0f;
	float Percussion = 0.0f;
	UABTSAudioWorldSubsystem::GetMusicStemTargets(EABTSMusicState::Explore, Bass, Harmony, Melody, Percussion);
	TestTrue(TEXT("Explore is harmony-led"), Harmony > 0.0f && Bass == 0.0f && Melody == 0.0f && Percussion == 0.0f);
	UABTSAudioWorldSubsystem::GetMusicStemTargets(EABTSMusicState::Finale, Bass, Harmony, Melody, Percussion);
	TestTrue(TEXT("Finale enables every stem"), Bass > 0.0f && Harmony > 0.0f && Melody > 0.0f && Percussion > 0.0f);

	const UABTSAudioSettings* Settings = GetDefault<UABTSAudioSettings>();
	TestFalse(TEXT("Bass default path is configured"), Settings->BassStem.IsNull());
	TestFalse(TEXT("Harmony default path is configured"), Settings->HarmonyStem.IsNull());
	TestFalse(TEXT("Melody default path is configured"), Settings->MelodyStem.IsNull());
	TestFalse(TEXT("Percussion default path is configured"), Settings->PercussionStem.IsNull());
	TestNotNull(TEXT("Bass stem loads"), Settings->BassStem.LoadSynchronous());
	TestNotNull(TEXT("Harmony stem loads"), Settings->HarmonyStem.LoadSynchronous());
	TestNotNull(TEXT("Melody stem loads"), Settings->MelodyStem.LoadSynchronous());
	TestNotNull(TEXT("Percussion stem loads"), Settings->PercussionStem.LoadSynchronous());
	TestNotNull(TEXT("Slingshot pull loop loads"), Settings->PullLoop.LoadSynchronous());
	TestNotNull(TEXT("Slingshot release snap loads"), Settings->ReleaseSnap.LoadSynchronous());
	TestNotNull(TEXT("Slingshot release resonance loads"), Settings->ReleaseResonance.LoadSynchronous());
	TestNotNull(TEXT("Music Sound Class loads"), Settings->MusicSoundClass.LoadSynchronous());
	TestNotNull(TEXT("Master Sound Mix loads"), Settings->MasterSoundMix.LoadSynchronous());
	TestEqual(TEXT("Each core material has three variants"), Settings->WoodImpacts.Num(), 3);
	TestEqual(TEXT("Each core material has three variants"), Settings->StoneImpacts.Num(), 3);
	TestEqual(TEXT("Each core material has three variants"), Settings->MetalImpacts.Num(), 3);
	TestEqual(TEXT("Each core material has three variants"), Settings->GlassImpacts.Num(), 3);
	return true;
}

#endif
