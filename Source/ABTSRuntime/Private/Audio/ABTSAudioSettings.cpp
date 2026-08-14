// Copyright Epic Games, Inc. All Rights Reserved.

#include "Audio/ABTSAudioSettings.h"

namespace
{
	template <typename T>
	TSoftObjectPtr<T> Asset(const TCHAR* Path)
	{
		return TSoftObjectPtr<T>(FSoftObjectPath(Path));
	}
}

UABTSAudioSettings::UABTSAudioSettings()
{
	BassStem = Asset<USoundBase>(TEXT("/Game/Audio/Music/Bass.Bass"));
	HarmonyStem = Asset<USoundBase>(TEXT("/Game/Audio/Music/Harmony.Harmony"));
	MelodyStem = Asset<USoundBase>(TEXT("/Game/Audio/Music/Melody.Melody"));
	PercussionStem = Asset<USoundBase>(TEXT("/Game/Audio/Music/Percussion.Percussion"));

	PullLoop = Asset<USoundBase>(TEXT("/Game/SoundEffects/Looped_Rubber-y_Stretch.Looped_Rubber-y_Stretch"));
	ReleaseSnap = Asset<USoundBase>(TEXT("/Game/SoundEffects/pluck_001.pluck_001"));
	ReleaseResonance = Asset<USoundBase>(TEXT("/Game/SoundEffects/Elastic_band_c_note.Elastic_band_c_note"));

	WoodImpacts = {
		Asset<USoundBase>(TEXT("/Game/SoundEffects/impactWood_medium_000.impactWood_medium_000")),
		Asset<USoundBase>(TEXT("/Game/SoundEffects/impactWood_medium_001.impactWood_medium_001")),
		Asset<USoundBase>(TEXT("/Game/SoundEffects/impactWood_medium_002.impactWood_medium_002"))};
	StoneImpacts = {
		Asset<USoundBase>(TEXT("/Game/SoundEffects/impactPlank_medium_000.impactPlank_medium_000")),
		Asset<USoundBase>(TEXT("/Game/SoundEffects/impactPlank_medium_001.impactPlank_medium_001")),
		Asset<USoundBase>(TEXT("/Game/SoundEffects/impactPlank_medium_002.impactPlank_medium_002"))};
	MetalImpacts = {
		Asset<USoundBase>(TEXT("/Game/SoundEffects/impactMetal_medium_000.impactMetal_medium_000")),
		Asset<USoundBase>(TEXT("/Game/SoundEffects/impactMetal_medium_001.impactMetal_medium_001")),
		Asset<USoundBase>(TEXT("/Game/SoundEffects/impactMetal_medium_002.impactMetal_medium_002"))};
	GlassImpacts = {
		Asset<USoundBase>(TEXT("/Game/SoundEffects/impactGlass_medium_000.impactGlass_medium_000")),
		Asset<USoundBase>(TEXT("/Game/SoundEffects/impactGlass_medium_001.impactGlass_medium_001")),
		Asset<USoundBase>(TEXT("/Game/SoundEffects/impactGlass_medium_002.impactGlass_medium_002"))};

	ExplosionBody = Asset<USoundBase>(TEXT("/Game/SoundEffects/explosionCrunch_000.explosionCrunch_000"));
	ExplosionLowTail = Asset<USoundBase>(TEXT("/Game/SoundEffects/lowFrequency_explosion_000.lowFrequency_explosion_000"));
	RedBirdChirp = Asset<USoundBase>(TEXT("/Game/SoundEffects/Birds/404729__owlstorm__retro-video-game-sfx-bird-chirp-5.404729__owlstorm__retro-video-game-sfx-bird-chirp-5"));
	BlueBirdChirp = Asset<USoundBase>(TEXT("/Game/SoundEffects/Birds/404725__owlstorm__retro-video-game-sfx-bird-chirp-3.404725__owlstorm__retro-video-game-sfx-bird-chirp-3"));
	YellowBirdChirp = Asset<USoundBase>(TEXT("/Game/SoundEffects/Birds/404726__owlstorm__retro-video-game-sfx-bird-chirp-2.404726__owlstorm__retro-video-game-sfx-bird-chirp-2"));
	BlackBirdChirp = Asset<USoundBase>(TEXT("/Game/SoundEffects/Birds/404724__owlstorm__retro-video-game-sfx-bird-chirp-4.404724__owlstorm__retro-video-game-sfx-bird-chirp-4"));
	GrassFootsteps = {
		Asset<USoundBase>(TEXT("/Game/SoundEffects/footstep_grass_000.footstep_grass_000")),
		Asset<USoundBase>(TEXT("/Game/SoundEffects/footstep_grass_001.footstep_grass_001")),
		Asset<USoundBase>(TEXT("/Game/SoundEffects/footstep_grass_002.footstep_grass_002"))};
	WoodFootsteps = {
		Asset<USoundBase>(TEXT("/Game/SoundEffects/footstep_wood_000.footstep_wood_000")),
		Asset<USoundBase>(TEXT("/Game/SoundEffects/footstep_wood_001.footstep_wood_001")),
		Asset<USoundBase>(TEXT("/Game/SoundEffects/footstep_wood_002.footstep_wood_002"))};
	Pickup = Asset<USoundBase>(TEXT("/Game/SoundEffects/confirmation_001.confirmation_001"));
	UIOpen = Asset<USoundBase>(TEXT("/Game/SoundEffects/open_001.open_001"));
	UIClose = Asset<USoundBase>(TEXT("/Game/SoundEffects/close_001.close_001"));
	UISelect = Asset<USoundBase>(TEXT("/Game/SoundEffects/select_002.select_002"));
	UIConfirm = Asset<USoundBase>(TEXT("/Game/SoundEffects/confirmation_001.confirmation_001"));
	UIError = Asset<USoundBase>(TEXT("/Game/SoundEffects/error_003.error_003"));
	UITick = Asset<USoundBase>(TEXT("/Game/SoundEffects/tick_001.tick_001"));

	MusicSoundClass = Asset<USoundClass>(TEXT("/Game/Audio/Infrastructure/SC_ABTS_Music.SC_ABTS_Music"));
	SFXSoundClass = Asset<USoundClass>(TEXT("/Game/Audio/Infrastructure/SC_ABTS_SFX.SC_ABTS_SFX"));
	UISoundClass = Asset<USoundClass>(TEXT("/Game/Audio/Infrastructure/SC_ABTS_UI.SC_ABTS_UI"));
	AmbienceSoundClass = Asset<USoundClass>(TEXT("/Game/Audio/Infrastructure/SC_ABTS_Ambience.SC_ABTS_Ambience"));
	MasterSoundMix = Asset<USoundMix>(TEXT("/Game/Audio/Infrastructure/SM_ABTS_Master.SM_ABTS_Master"));
}
