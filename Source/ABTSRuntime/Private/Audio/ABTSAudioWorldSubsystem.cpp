// Copyright Epic Games, Inc. All Rights Reserved.

#include "Audio/ABTSAudioWorldSubsystem.h"

#include "ABTSRuntime.h"
#include "Audio/ABTSAudioSettings.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Sound/SoundWave.h"

namespace
{
	constexpr float PullAttackSeconds = 0.012f;
	constexpr float ReleaseSnapAttackSeconds = 0.008f;
	constexpr float ReleaseResonanceAttackSeconds = 0.012f;

	template <typename T>
	T* Load(const TSoftObjectPtr<T>& Asset)
	{
		return Asset.IsNull() ? nullptr : Asset.LoadSynchronous();
	}

	void LoadArray(const TArray<TSoftObjectPtr<USoundBase>>& Sources, TArray<TObjectPtr<USoundBase>>& Destinations)
	{
		Destinations.Reset(Sources.Num());
		for (const TSoftObjectPtr<USoundBase>& Source : Sources)
		{
			if (USoundBase* Sound = Load(Source)) Destinations.Add(Sound);
		}
	}

	template <typename T>
	T* LoadOptional(const TSoftObjectPtr<T>& Asset)
	{
		if (Asset.IsNull() || !FPackageName::DoesPackageExist(Asset.ToSoftObjectPath().GetLongPackageName())) return nullptr;
		return Asset.LoadSynchronous();
	}

	bool PrimeLatencyCriticalSound(USoundBase* Sound)
	{
		if (Sound == nullptr) return false;
		if (USoundWave* Wave = Cast<USoundWave>(Sound))
		{
			// These short interaction sounds must not wait for their first input
			// event to request the first streaming chunk. In Editor, RetainOnLoad
			// intentionally becomes PrimeOnLoad; packaged builds retain the chunk.
			Wave->OverrideLoadingBehavior(ESoundWaveLoadingBehavior::RetainOnLoad);
		}
		UGameplayStatics::PrimeSound(Sound);
		return true;
	}

	void StartPreparedAudioComponent(
		UAudioComponent* Component,
		const FVector& WorldLocation,
		const float TargetVolume,
		const float Pitch,
		const float AttackSeconds)
	{
		if (!IsValid(Component) || TargetVolume <= KINDA_SMALL_NUMBER) return;
		Component->Stop();
		Component->SetWorldLocation(WorldLocation);
		Component->SetPitchMultiplier(FMath::Clamp(Pitch, 0.25f, 4.0f));
		// FadeIn owns a separate ActiveSound fader that starts at zero. Keep the
		// component multiplier at the requested level and fade that fader to one;
		// setting the multiplier itself to zero would keep the sound silent.
		Component->SetVolumeMultiplier(TargetVolume);
		Component->FadeIn(FMath::Max(0.0f, AttackSeconds), 1.0f);
	}
}

bool UABTSAudioWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UABTSAudioWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (IsRunningCommandlet() || FParse::Param(FCommandLine::Get(), TEXT("NoSound")) || InWorld.GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	LoadCatalog();
	SlingshotPrimeRequestCount = 0;
	SlingshotPrimeRequestCount += PrimeLatencyCriticalSound(PullSound) ? 1 : 0;
	SlingshotPrimeRequestCount += PrimeLatencyCriticalSound(ReleaseSnapSound) ? 1 : 0;
	SlingshotPrimeRequestCount += PrimeLatencyCriticalSound(ReleaseResonanceSound) ? 1 : 0;
	if (MasterMix)
	{
		UGameplayStatics::PushSoundMixModifier(&InWorld, MasterMix);
	}
	SetCategoryVolumes(RuntimeMusicVolume, RuntimeSFXVolume, RuntimeUIVolume, RuntimeAmbienceVolume, 0.0f);

	MusicComponents.Reset(MusicSounds.Num());
	float InitialTargets[4];
	GetMusicStemTargets(MusicState, InitialTargets[0], InitialTargets[1], InitialTargets[2], InitialTargets[3]);
	const UABTSAudioSettings* Settings = GetDefault<UABTSAudioSettings>();
	const float DirectCategoryVolume = MasterMix && MusicClass ? 1.0f : RuntimeMusicVolume;
	int32 LoadedMusicStemCount = 0;
	int32 ActiveMusicStemCount = 0;
	for (int32 Index = 0; Index < MusicSounds.Num(); ++Index)
	{
		USoundBase* Sound = MusicSounds[Index];
		if (Sound)
		{
			++LoadedMusicStemCount;
			// A silent stem must keep advancing so that later state changes reveal
			// the same musical position instead of restarting the wave.
			Sound->VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
		}
		UAudioComponent* Component = CreatePersistentComponent(Sound, MusicClass, false);
		MusicComponents.Add(Component);
		if (Component && Index < UE_ARRAY_COUNT(InitialTargets))
		{
			const float InitialVolume = InitialTargets[Index] * Settings->MusicVolume * DirectCategoryVolume;
			Component->FadeIn(0.0f, InitialVolume);
			ActiveMusicStemCount += Component->IsActive() ? 1 : 0;
		}
	}
	// Allocate and register latency-critical interaction components while the
	// world is becoming ready, rather than on the player's first pull/release.
	PullComponent = CreatePersistentComponent(PullSound, SFXClass, true);
	ReleaseSnapComponent = CreatePersistentComponent(ReleaseSnapSound, SFXClass, true);
	ReleaseResonanceComponent = CreatePersistentComponent(ReleaseResonanceSound, SFXClass, true);
	const int32 PreparedSlingshotComponentCount =
		(IsValid(PullComponent) ? 1 : 0)
		+ (IsValid(ReleaseSnapComponent) ? 1 : 0)
		+ (IsValid(ReleaseResonanceComponent) ? 1 : 0);
	bAudioReady = true;
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][Audio] Ready MusicLoaded=%d MusicComponents=%d MusicActive=%d State=%d Targets=(%.2f,%.2f,%.2f,%.2f) SoundClasses=%d MasterMix=%d SlingshotPrepared=%d PrimeRequested=%d"),
		LoadedMusicStemCount,
		MusicComponents.Num(),
		ActiveMusicStemCount,
		static_cast<int32>(MusicState),
		InitialTargets[0], InitialTargets[1], InitialTargets[2], InitialTargets[3],
		MusicClass && SFXClass && UIClass && AmbienceClass ? 4 : 0,
		MasterMix ? 1 : 0,
		PreparedSlingshotComponentCount,
		SlingshotPrimeRequestCount);
}

void UABTSAudioWorldSubsystem::Deinitialize()
{
	StopSlingshotPull(0.0f);
	if (IsValid(PullComponent)) PullComponent->DestroyComponent();
	if (IsValid(ReleaseSnapComponent)) ReleaseSnapComponent->DestroyComponent();
	if (IsValid(ReleaseResonanceComponent)) ReleaseResonanceComponent->DestroyComponent();
	PullComponent = nullptr;
	ReleaseSnapComponent = nullptr;
	ReleaseResonanceComponent = nullptr;
	for (UAudioComponent* Component : MusicComponents)
	{
		if (IsValid(Component)) Component->DestroyComponent();
	}
	MusicComponents.Reset();
	if (MasterMix && GetWorld()) UGameplayStatics::PopSoundMixModifier(GetWorld(), MasterMix);
	bAudioReady = false;
	Super::Deinitialize();
}

void UABTSAudioWorldSubsystem::LoadCatalog()
{
	const UABTSAudioSettings* Settings = GetDefault<UABTSAudioSettings>();
	MusicSounds = {Load(Settings->BassStem), Load(Settings->HarmonyStem), Load(Settings->MelodyStem), Load(Settings->PercussionStem)};
	PullSound = Load(Settings->PullLoop);
	ReleaseSnapSound = Load(Settings->ReleaseSnap);
	ReleaseResonanceSound = Load(Settings->ReleaseResonance);
	LoadArray(Settings->WoodImpacts, WoodImpactSounds);
	LoadArray(Settings->StoneImpacts, StoneImpactSounds);
	LoadArray(Settings->MetalImpacts, MetalImpactSounds);
	LoadArray(Settings->GlassImpacts, GlassImpactSounds);
	ExplosionBodySound = Load(Settings->ExplosionBody);
	ExplosionTailSound = Load(Settings->ExplosionLowTail);
	UISounds = {Load(Settings->UIOpen), Load(Settings->UIClose), Load(Settings->UISelect), Load(Settings->UIConfirm), Load(Settings->UIError), Load(Settings->UITick)};
	MusicClass = LoadOptional(Settings->MusicSoundClass);
	SFXClass = LoadOptional(Settings->SFXSoundClass);
	UIClass = LoadOptional(Settings->UISoundClass);
	AmbienceClass = LoadOptional(Settings->AmbienceSoundClass);
	MasterMix = LoadOptional(Settings->MasterSoundMix);
}

UAudioComponent* UABTSAudioWorldSubsystem::CreatePersistentComponent(USoundBase* Sound, USoundClass* SoundClass, const bool bSpatialized)
{
	UWorld* World = GetWorld();
	if (World == nullptr || Sound == nullptr) return nullptr;
	UAudioComponent* Component = NewObject<UAudioComponent>(World);
	Component->SetAutoActivate(false);
	Component->bAutoDestroy = false;
	Component->bAllowSpatialization = bSpatialized;
	Component->SetUISound(false);
	Component->SetSound(Sound);
	Component->SoundClassOverride = SoundClass;
	Component->RegisterComponentWithWorld(World);
	return Component;
}

void UABTSAudioWorldSubsystem::PlayOneShot(
	USoundBase* Sound,
	USoundClass* SoundClass,
	const FVector& WorldLocation,
	const bool bSpatialized,
	const float Volume,
	const float Pitch)
{
	UWorld* World = GetWorld();
	if (World == nullptr || Sound == nullptr || Volume <= KINDA_SMALL_NUMBER) return;
	UAudioComponent* Component = NewObject<UAudioComponent>(World);
	Component->SetAutoActivate(false);
	Component->bAutoDestroy = true;
	Component->bAllowSpatialization = bSpatialized;
	Component->SetUISound(!bSpatialized);
	Component->SetWorldLocation(WorldLocation);
	Component->SetSound(Sound);
	Component->SoundClassOverride = SoundClass;
	Component->SetVolumeMultiplier(Volume);
	Component->SetPitchMultiplier(FMath::Clamp(Pitch, 0.25f, 4.0f));
	Component->RegisterComponentWithWorld(World);
	Component->Play();
}

void UABTSAudioWorldSubsystem::GetMusicStemTargets(
	const EABTSMusicState State,
	float& OutBass,
	float& OutHarmony,
	float& OutMelody,
	float& OutPercussion)
{
	switch (State)
	{
	case EABTSMusicState::Approach: OutBass = 0.65f; OutHarmony = 0.82f; OutMelody = 0.0f; OutPercussion = 0.0f; break;
	case EABTSMusicState::Aim: OutBass = 0.72f; OutHarmony = 0.82f; OutMelody = 0.0f; OutPercussion = 0.18f; break;
	case EABTSMusicState::Destruction: OutBass = 0.88f; OutHarmony = 0.76f; OutMelody = 0.35f; OutPercussion = 0.82f; break;
	case EABTSMusicState::Satellite: OutBass = 0.55f; OutHarmony = 0.70f; OutMelody = 0.50f; OutPercussion = 0.12f; break;
	case EABTSMusicState::Finale: OutBass = 0.92f; OutHarmony = 0.88f; OutMelody = 0.90f; OutPercussion = 0.92f; break;
	case EABTSMusicState::Explore:
	default: OutBass = 0.0f; OutHarmony = 0.78f; OutMelody = 0.0f; OutPercussion = 0.0f; break;
	}
}

void UABTSAudioWorldSubsystem::SetMusicState(const EABTSMusicState NewState, float FadeSeconds)
{
	MusicState = NewState;
	if (!bAudioReady) return;
	const UABTSAudioSettings* Settings = GetDefault<UABTSAudioSettings>();
	if (FadeSeconds < 0.0f) FadeSeconds = Settings->MusicFadeSeconds;
	float Targets[4];
	GetMusicStemTargets(NewState, Targets[0], Targets[1], Targets[2], Targets[3]);
	for (int32 Index = 0; Index < MusicComponents.Num() && Index < UE_ARRAY_COUNT(Targets); ++Index)
	{
		if (UAudioComponent* Component = MusicComponents[Index])
		{
			const float DirectCategoryVolume = MasterMix && MusicClass ? 1.0f : RuntimeMusicVolume;
			Component->AdjustVolume(FMath::Max(0.0f, FadeSeconds), Targets[Index] * Settings->MusicVolume * DirectCategoryVolume);
		}
	}
}

float UABTSAudioWorldSubsystem::ComputeCordPitchMultiplier(const float RestCordLengthCM)
{
	constexpr float ReferenceCordLengthCM = 120.0f;
	const float SafeLengthCM = FMath::IsFinite(RestCordLengthCM) && RestCordLengthCM > KINDA_SMALL_NUMBER
		? RestCordLengthCM
		: ReferenceCordLengthCM;
	// Stylized string rule: pitch is an instance property derived only from
	// unloaded cord length. Pull power never changes pitch.
	return FMath::Clamp(ReferenceCordLengthCM / SafeLengthCM, 0.67f, 1.50f);
}

float UABTSAudioWorldSubsystem::ComputePullLoopVolumeMultiplier(const float PullPowerAlpha)
{
	return FMath::Lerp(0.10f, 0.32f, FMath::Clamp(PullPowerAlpha, 0.0f, 1.0f));
}

void UABTSAudioWorldSubsystem::UpdateSlingshotPull(
	const FVector& WorldLocation,
	const float RestCordLengthCM,
	const float PullPowerAlpha)
{
	if (!bAudioReady || PullSound == nullptr) return;
	const float Alpha = FMath::Clamp(PullPowerAlpha, 0.0f, 1.0f);
	const float Pitch = ComputeCordPitchMultiplier(RestCordLengthCM);
	const float DirectCategoryVolume = MasterMix && SFXClass ? 1.0f : RuntimeSFXVolume;
	const float Volume = ComputePullLoopVolumeMultiplier(Alpha)
		* GetDefault<UABTSAudioSettings>()->SFXVolume
		* DirectCategoryVolume;
	if (!IsValid(PullComponent))
	{
		PullComponent = CreatePersistentComponent(PullSound, SFXClass, true);
	}
	if (PullComponent)
	{
		const EAudioComponentPlayState PlayState = PullComponent->GetPlayState();
		if (PlayState == EAudioComponentPlayState::Stopped
			|| PlayState == EAudioComponentPlayState::Paused
			|| PlayState == EAudioComponentPlayState::FadingOut)
		{
			StartPreparedAudioComponent(PullComponent, WorldLocation, Volume, Pitch, PullAttackSeconds);
			UE_LOG(LogABTSRuntime, Log,
				TEXT("[ABTS][Audio][SlingshotPull] Start CordLengthCM=%.2f Pitch=%.3f PullPower=%.3f Volume=%.3f Preconfigured=1 Prepared=1 PrimeRequested=%d AttackMS=%.1f"),
				RestCordLengthCM, Pitch, Alpha, Volume,
				SlingshotPrimeRequestCount == 3 ? 1 : 0,
				PullAttackSeconds * 1000.0f);
		}
		else
		{
			PullComponent->SetWorldLocation(WorldLocation);
			PullComponent->SetPitchMultiplier(Pitch);
			PullComponent->SetVolumeMultiplier(Volume);
		}
	}
}

void UABTSAudioWorldSubsystem::StopSlingshotPull(const float FadeSeconds)
{
	if (!IsValid(PullComponent)) return;
	if (FadeSeconds <= 0.0f) PullComponent->Stop();
	else PullComponent->FadeOut(FadeSeconds, 0.0f);
}

void UABTSAudioWorldSubsystem::PlaySlingshotRelease(
	const FVector& WorldLocation,
	const float RestCordLengthCM,
	const float PullPowerAlpha)
{
	StopSlingshotPull();
	const UABTSAudioSettings* Settings = GetDefault<UABTSAudioSettings>();
	const float DirectCategoryVolume = MasterMix && SFXClass ? 1.0f : RuntimeSFXVolume;
	const float PullPower = FMath::Clamp(PullPowerAlpha, 0.0f, 1.0f);
	const float SnapVolume = Settings->SFXVolume * DirectCategoryVolume * FMath::Lerp(0.72f, 1.0f, PullPower);
	const float ResonanceVolume = Settings->SFXVolume * DirectCategoryVolume * FMath::Lerp(0.55f, 0.90f, PullPower);
	const float ResonancePitch = ComputeCordPitchMultiplier(RestCordLengthCM);
	if (!IsValid(ReleaseSnapComponent))
	{
		ReleaseSnapComponent = CreatePersistentComponent(ReleaseSnapSound, SFXClass, true);
	}
	if (!IsValid(ReleaseResonanceComponent))
	{
		ReleaseResonanceComponent = CreatePersistentComponent(ReleaseResonanceSound, SFXClass, true);
	}
	StartPreparedAudioComponent(
		ReleaseSnapComponent, WorldLocation, SnapVolume, 1.0f, ReleaseSnapAttackSeconds);
	StartPreparedAudioComponent(
		ReleaseResonanceComponent, WorldLocation, ResonanceVolume, ResonancePitch, ReleaseResonanceAttackSeconds);
	UE_LOG(LogABTSRuntime, Log,
		TEXT("[ABTS][Audio][SlingshotRelease] CordLengthCM=%.2f ResonancePitch=%.3f PullPower=%.3f SnapVolume=%.3f ResonanceVolume=%.3f Prepared=%d PrimeRequested=%d AttackMS=(%.1f,%.1f)"),
		RestCordLengthCM,
		ResonancePitch,
		PullPower,
		SnapVolume,
		ResonanceVolume,
		IsValid(ReleaseSnapComponent) && IsValid(ReleaseResonanceComponent) ? 1 : 0,
		SlingshotPrimeRequestCount == 3 ? 1 : 0,
		ReleaseSnapAttackSeconds * 1000.0f,
		ReleaseResonanceAttackSeconds * 1000.0f);
	SetMusicState(EABTSMusicState::Destruction);
}

USoundBase* UABTSAudioWorldSubsystem::SelectImpact(const EABTSM6ImpactMaterial Material)
{
	const TArray<TObjectPtr<USoundBase>>* Candidates = &StoneImpactSounds;
	switch (Material)
	{
	case EABTSM6ImpactMaterial::Wood:
	case EABTSM6ImpactMaterial::Building: Candidates = &WoodImpactSounds; break;
	case EABTSM6ImpactMaterial::Iron: Candidates = &MetalImpactSounds; break;
	case EABTSM6ImpactMaterial::Glass: Candidates = &GlassImpactSounds; break;
	default: break;
	}
	if (Candidates->IsEmpty()) return nullptr;
	return (*Candidates)[ImpactVariantCounter++ % Candidates->Num()];
}

void UABTSAudioWorldSubsystem::PlayImpact(
	const FVector& WorldLocation,
	const EABTSM6ImpactMaterial Material,
	const float NormalSpeedCMPerSec)
{
	const UABTSAudioSettings* Settings = GetDefault<UABTSAudioSettings>();
	if (!bAudioReady || NormalSpeedCMPerSec < Settings->MinimumImpactSpeedCMPerSec || GetWorld() == nullptr) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (const double* LastTime = LastImpactTimeByMaterial.Find(Material))
	{
		if (Now - *LastTime < Settings->ImpactCooldownSeconds) return;
	}
	LastImpactTimeByMaterial.Add(Material, Now);
	const float Strength = FMath::GetMappedRangeValueClamped(
		FVector2D(Settings->MinimumImpactSpeedCMPerSec, Settings->FullVolumeImpactSpeedCMPerSec),
		FVector2D(0.22f, 1.0f), NormalSpeedCMPerSec);
	const float Pitch = FMath::Lerp(1.08f, 0.92f, Strength);
	const float DirectCategoryVolume = MasterMix && SFXClass ? 1.0f : RuntimeSFXVolume;
	PlayOneShot(SelectImpact(Material), SFXClass, WorldLocation, true, Settings->SFXVolume * DirectCategoryVolume * Strength, Pitch);
}

void UABTSAudioWorldSubsystem::PlayExplosion(const FVector& WorldLocation, const bool bLarge)
{
	const UABTSAudioSettings* Settings = GetDefault<UABTSAudioSettings>();
	const float DirectCategoryVolume = MasterMix && SFXClass ? 1.0f : RuntimeSFXVolume;
	PlayOneShot(ExplosionBodySound, SFXClass, WorldLocation, true, Settings->SFXVolume * DirectCategoryVolume, bLarge ? 0.92f : 1.0f);
	if (bLarge) PlayOneShot(ExplosionTailSound, SFXClass, WorldLocation, true, Settings->SFXVolume * DirectCategoryVolume * 0.72f, 0.88f);
}

void UABTSAudioWorldSubsystem::PlayUIEvent(const EABTSUIAudioEvent Event)
{
	const int32 Index = static_cast<int32>(Event);
	if (!UISounds.IsValidIndex(Index)) return;
	const float DirectCategoryVolume = MasterMix && UIClass ? 1.0f : RuntimeUIVolume;
	PlayOneShot(UISounds[Index], UIClass, FVector::ZeroVector, false,
		GetDefault<UABTSAudioSettings>()->UIVolume * DirectCategoryVolume);
}

void UABTSAudioWorldSubsystem::SetCategoryVolumes(
	const float Music,
	const float SFX,
	const float UI,
	const float Ambience,
	const float FadeSeconds)
{
	RuntimeMusicVolume = FMath::Clamp(Music, 0.0f, 1.0f);
	RuntimeSFXVolume = FMath::Clamp(SFX, 0.0f, 1.0f);
	RuntimeUIVolume = FMath::Clamp(UI, 0.0f, 1.0f);
	RuntimeAmbienceVolume = FMath::Clamp(Ambience, 0.0f, 1.0f);
	if (MasterMix && GetWorld())
	{
		if (MusicClass) UGameplayStatics::SetSoundMixClassOverride(GetWorld(), MasterMix, MusicClass, RuntimeMusicVolume, 1.0f, FadeSeconds, true);
		if (SFXClass) UGameplayStatics::SetSoundMixClassOverride(GetWorld(), MasterMix, SFXClass, RuntimeSFXVolume, 1.0f, FadeSeconds, true);
		if (UIClass) UGameplayStatics::SetSoundMixClassOverride(GetWorld(), MasterMix, UIClass, RuntimeUIVolume, 1.0f, FadeSeconds, true);
		if (AmbienceClass) UGameplayStatics::SetSoundMixClassOverride(GetWorld(), MasterMix, AmbienceClass, RuntimeAmbienceVolume, 1.0f, FadeSeconds, true);
	}
	if (bAudioReady) SetMusicState(MusicState, FadeSeconds);
}
