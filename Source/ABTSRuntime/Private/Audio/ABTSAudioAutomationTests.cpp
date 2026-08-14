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
	TestTrue(TEXT("Faster movement shortens the distance between footsteps"),
		UABTSAudioWorldSubsystem::ComputeFootstepSpacingCM(100.0f)
		> UABTSAudioWorldSubsystem::ComputeFootstepSpacingCM(600.0f));
	TestEqual(TEXT("Footstep cadence is one quarter of the original low-speed rate"),
		UABTSAudioWorldSubsystem::ComputeFootstepSpacingCM(60.0f), 440.0f);
	TestEqual(TEXT("Footstep cadence is one quarter of the original high-speed rate"),
		UABTSAudioWorldSubsystem::ComputeFootstepSpacingCM(680.0f), 272.0f);
	TestTrue(TEXT("Faster movement raises footstep volume"),
		UABTSAudioWorldSubsystem::ComputeFootstepVolumeMultiplier(100.0f)
		< UABTSAudioWorldSubsystem::ComputeFootstepVolumeMultiplier(600.0f));
	TestEqual(TEXT("Low-speed footstep volume is reduced by half"),
		UABTSAudioWorldSubsystem::ComputeFootstepVolumeMultiplier(60.0f), 0.12f);
	TestEqual(TEXT("High-speed footstep volume is reduced by half"),
		UABTSAudioWorldSubsystem::ComputeFootstepVolumeMultiplier(680.0f), 0.27f);
	TestTrue(TEXT("Harder landings are louder"),
		UABTSAudioWorldSubsystem::ComputeLandingVolumeMultiplier(200.0f)
		< UABTSAudioWorldSubsystem::ComputeLandingVolumeMultiplier(800.0f));
	TestTrue(TEXT("Bridge semantics select wood footsteps"),
		UABTSAudioWorldSubsystem::ResolveFootstepSurfaceFromSemanticName(TEXT("ABTSM8BridgeActor"))
		== EABTSFootstepSurface::Wood);
	TestTrue(TEXT("Unclassified terrain defaults to grass footsteps"),
		UABTSAudioWorldSubsystem::ResolveFootstepSurfaceFromSemanticName(TEXT("ABTSM3PlanetTerrain"))
		== EABTSFootstepSurface::Grass);

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
	TestNotNull(TEXT("Red bird chirp loads"), Settings->RedBirdChirp.LoadSynchronous());
	TestNotNull(TEXT("Blue bird chirp loads"), Settings->BlueBirdChirp.LoadSynchronous());
	TestNotNull(TEXT("Yellow bird chirp loads"), Settings->YellowBirdChirp.LoadSynchronous());
	TestNotNull(TEXT("Black bird chirp loads"), Settings->BlackBirdChirp.LoadSynchronous());
	TestTrue(TEXT("Red bird uses licensed 404729 chirp"),
		Settings->RedBirdChirp.ToSoftObjectPath().ToString().Contains(TEXT("404729")));
	TestTrue(TEXT("Blue bird uses licensed 404725 chirp"),
		Settings->BlueBirdChirp.ToSoftObjectPath().ToString().Contains(TEXT("404725")));
	TestTrue(TEXT("Yellow bird uses licensed 404726 chirp"),
		Settings->YellowBirdChirp.ToSoftObjectPath().ToString().Contains(TEXT("404726")));
	TestTrue(TEXT("Black bird uses licensed 404724 chirp"),
		Settings->BlackBirdChirp.ToSoftObjectPath().ToString().Contains(TEXT("404724")));
	TestNotNull(TEXT("Pickup sound loads"), Settings->Pickup.LoadSynchronous());
	TestNotNull(TEXT("Music Sound Class loads"), Settings->MusicSoundClass.LoadSynchronous());
	TestNotNull(TEXT("Master Sound Mix loads"), Settings->MasterSoundMix.LoadSynchronous());
	TestEqual(TEXT("Grass footsteps have three variants"), Settings->GrassFootsteps.Num(), 3);
	TestEqual(TEXT("Wood footsteps have three variants"), Settings->WoodFootsteps.Num(), 3);
	for (const TSoftObjectPtr<USoundBase>& Footstep : Settings->GrassFootsteps)
	{
		TestNotNull(TEXT("Grass footstep loads"), Footstep.LoadSynchronous());
	}
	for (const TSoftObjectPtr<USoundBase>& Footstep : Settings->WoodFootsteps)
	{
		TestNotNull(TEXT("Wood footstep loads"), Footstep.LoadSynchronous());
	}
	TestEqual(TEXT("Each core material has three variants"), Settings->WoodImpacts.Num(), 3);
	TestEqual(TEXT("Each core material has three variants"), Settings->StoneImpacts.Num(), 3);
	TestEqual(TEXT("Each core material has three variants"), Settings->MetalImpacts.Num(), 3);
	TestEqual(TEXT("Each core material has three variants"), Settings->GlassImpacts.Num(), 3);
	return true;
}

#endif
