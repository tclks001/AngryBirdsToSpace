// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "UObject/UObjectGlobals.h"
#include "World/ABTSM11FinaleBirdTrailComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11FinaleBirdTrailHistoryTest,
	"ABTS.M11C.FinaleBirdTrail.History",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FABTSM11FinaleBirdTrailHistoryTest::RunTest(
	const FString& Parameters)
{
	UABTSM11FinaleBirdTrailComponent* SingleStepTrail =
		NewObject<UABTSM11FinaleBirdTrailComponent>();
	UABTSM11FinaleBirdTrailComponent* SplitStepTrail =
		NewObject<UABTSM11FinaleBirdTrailComponent>();
	TestNotNull(TEXT("Single-step trail is constructible without an asset"),
		SingleStepTrail);
	TestNotNull(TEXT("Split-step trail is constructible without an asset"),
		SplitStepTrail);
	if (SingleStepTrail == nullptr || SplitStepTrail == nullptr)
	{
		return false;
	}

	const FVector InitialEmitterPosition(10.0, 20.0, 30.0);
	const FVector MidEmitterPosition = InitialEmitterPosition
		+ FVector(320.0, 0.0, 0.0);
	const FVector FinalEmitterPosition = InitialEmitterPosition
		+ FVector(640.0, 0.0, 0.0);
	constexpr float TotalDeltaSeconds = 0.10f;
	constexpr float BirdSpeedCMPerSecond = 6400.0f;

	SingleStepTrail->BeginTrail(InitialEmitterPosition);
	SplitStepTrail->BeginTrail(InitialEmitterPosition);
	TestTrue(TEXT("Trail starts active"), SingleStepTrail->IsTrailActive());
	TestEqual(TEXT("Procedural soft sprite is the default renderer"),
		SingleStepTrail->GetTrailRenderMode(),
		1);
	TestTrue(TEXT("Begin creates the asset-free sprite texture"),
		SingleStepTrail->HasGeneratedSpriteTexture());
	TestEqual(TEXT("Generated sprite texture has the frozen resolution"),
		SingleStepTrail->GetGeneratedSpriteTextureSize(),
		64);
	TestTrue(TEXT("Halo is wider than the contrast shell"),
		SingleStepTrail->GetTrailHaloDiameterPixels()
			> SingleStepTrail->GetTrailContrastShellDiameterPixels());
	TestTrue(TEXT("Contrast shell is wider than the luminous core"),
		SingleStepTrail->GetTrailContrastShellDiameterPixels()
			> SingleStepTrail->GetTrailCoreDiameterPixels());
	TestTrue(TEXT("Contrast shell has bounded non-zero opacity"),
		SingleStepTrail->GetTrailContrastShellOpacity() > 0.0f
			&& SingleStepTrail->GetTrailContrastShellOpacity() <= 1.0f);
	TestTrue(TEXT("Core intensity exceeds halo intensity"),
		SingleStepTrail->GetTrailCoreIntensity()
			> SingleStepTrail->GetTrailHaloIntensity());
	TestTrue(TEXT("Composite core has bounded non-zero opacity"),
		SingleStepTrail->GetTrailCoreCompositeOpacity() > 0.0f
			&& SingleStepTrail->GetTrailCoreCompositeOpacity() <= 1.0f);
	TestTrue(TEXT("Sprite softness is a normalized value"),
		SingleStepTrail->GetTrailSpriteSoftness() >= 0.0f
			&& SingleStepTrail->GetTrailSpriteSoftness() <= 1.0f);
	TestTrue(TEXT("Particles expand without reversing size"),
		SingleStepTrail->GetTrailExpansionFraction() >= 0.0f);
	TestTrue(TEXT("Core persists longer than the halo"),
		SingleStepTrail->GetTrailCoreFadeExponent()
			< SingleStepTrail->GetTrailHaloFadeExponent());
	const TCHAR* AppearanceCVarNames[] =
	{
		TEXT("abts.M11.FinaleBirdTrail.RenderMode"),
		TEXT("abts.M11.FinaleBirdTrail.HaloDiameterPixels"),
		TEXT("abts.M11.FinaleBirdTrail.ContrastShellDiameterPixels"),
		TEXT("abts.M11.FinaleBirdTrail.CoreDiameterPixels"),
		TEXT("abts.M11.FinaleBirdTrail.HaloIntensity"),
		TEXT("abts.M11.FinaleBirdTrail.ContrastShellOpacity"),
		TEXT("abts.M11.FinaleBirdTrail.CoreIntensity"),
		TEXT("abts.M11.FinaleBirdTrail.CoreCompositeOpacity"),
		TEXT("abts.M11.FinaleBirdTrail.Softness"),
		TEXT("abts.M11.FinaleBirdTrail.ExpansionFraction"),
		TEXT("abts.M11.FinaleBirdTrail.CoreFadeExponent"),
		TEXT("abts.M11.FinaleBirdTrail.HaloFadeExponent"),
	};
	for (const TCHAR* AppearanceCVarName : AppearanceCVarNames)
	{
		TestNotNull(
			FString::Printf(TEXT("Appearance CVar is registered: %s"),
				AppearanceCVarName),
			IConsoleManager::Get().FindConsoleVariable(AppearanceCVarName));
	}
	TestEqual(TEXT("Begin stores the emitter without a velocity-less cluster"),
		SingleStepTrail->GetTrailSampleCount(), 0);

	SingleStepTrail->AdvanceTrail(
		FinalEmitterPosition,
		TotalDeltaSeconds);
	SplitStepTrail->AdvanceTrail(
		MidEmitterPosition,
		TotalDeltaSeconds * 0.5f);
	SplitStepTrail->AdvanceTrail(
		FinalEmitterPosition,
		TotalDeltaSeconds * 0.5f);

	TestTrue(TEXT("Arc-length motion emits multiple independent particles"),
		SingleStepTrail->GetTrailSampleCount() >= 8);
	TestEqual(TEXT("Split updates preserve the arc-length remainder"),
		SplitStepTrail->GetTrailSampleCount(),
		SingleStepTrail->GetTrailSampleCount());
	TestTrue(TEXT("Particle ages remain chronological"),
		SingleStepTrail->AreTrailSampleAgesOrdered());
	TestTrue(TEXT("Newest emitter follows the supplied world position"),
		SingleStepTrail->GetNewestSampleWorldPosition().Equals(
			FinalEmitterPosition,
			0.01));

	bool bSplitStepStateMatches = true;
	for (int32 SampleIndex = 0;
		bSplitStepStateMatches
			&& SampleIndex < SingleStepTrail->GetTrailSampleCount();
		++SampleIndex)
	{
		bSplitStepStateMatches =
			SingleStepTrail->GetTrailSampleWorldPosition(SampleIndex).Equals(
				SplitStepTrail->GetTrailSampleWorldPosition(SampleIndex),
				0.01)
			&& SingleStepTrail->GetTrailSampleEmissionWorldPosition(SampleIndex)
				.Equals(
					SplitStepTrail->GetTrailSampleEmissionWorldPosition(
						SampleIndex),
					0.01)
			&& SingleStepTrail->GetTrailSampleWorldVelocity(SampleIndex).Equals(
				SplitStepTrail->GetTrailSampleWorldVelocity(SampleIndex),
				0.01)
			&& FMath::IsNearlyEqual(
				SingleStepTrail->GetTrailSampleAgeSeconds(SampleIndex),
				SplitStepTrail->GetTrailSampleAgeSeconds(SampleIndex),
				0.0001f)
			&& FMath::IsNearlyEqual(
				SingleStepTrail->GetTrailSampleLifetimeSeconds(SampleIndex),
				SplitStepTrail->GetTrailSampleLifetimeSeconds(SampleIndex),
				0.0001f);
	}
	TestTrue(TEXT("Subframe birth state is invariant to frame subdivision"),
		bSplitStepStateMatches);
	TestTrue(TEXT("Distance remainder is invariant to frame subdivision"),
		FMath::IsNearlyEqual(
			SingleStepTrail->GetDistanceUntilNextEmissionCM(),
			SplitStepTrail->GetDistanceUntilNextEmissionCM(),
			0.01f));

	const float NominalSpacingCM =
		SingleStepTrail->GetNominalSampleSpacingCM();
	const float JitterFraction =
		SingleStepTrail->GetBlueNoiseSiteJitterFraction();
	const float MinimumSpacingCM = NominalSpacingCM
		* (1.0f - 2.0f * JitterFraction);
	const float MaximumSpacingCM = NominalSpacingCM
		* (1.0f + 2.0f * JitterFraction);
	float PreviousSpacingCM = -1.0f;
	bool bSpacingVaries = false;
	bool bHasLateralMotion = false;
	float SlowestLateralSpeedCMPerSecond = TNumericLimits<float>::Max();
	float FastestLateralSpeedCMPerSecond = 0.0f;
	float SlowestParticleLifetimeSeconds = 0.0f;
	float FastestParticleLifetimeSeconds = 0.0f;
	for (int32 SampleIndex = 0;
		SampleIndex < SingleStepTrail->GetTrailSampleCount();
		++SampleIndex)
	{
		const FVector EmissionPosition =
			SingleStepTrail->GetTrailSampleEmissionWorldPosition(SampleIndex);
		TestTrue(TEXT("Emission anchors stay on the travelled segment"),
			FMath::Abs(EmissionPosition.Y - InitialEmitterPosition.Y) < 0.01
				&& FMath::Abs(EmissionPosition.Z - InitialEmitterPosition.Z)
					< 0.01);
		if (SampleIndex > 0)
		{
			const FVector PreviousEmissionPosition =
				SingleStepTrail->GetTrailSampleEmissionWorldPosition(
					SampleIndex - 1);
			const float SpacingCM = static_cast<float>(FVector::Distance(
				PreviousEmissionPosition,
				EmissionPosition));
			TestTrue(TEXT("Blue-noise interval respects its hard lower bound"),
				SpacingCM + 0.01f >= MinimumSpacingCM);
			TestTrue(TEXT("Blue-noise interval respects its hard upper bound"),
				SpacingCM <= MaximumSpacingCM + 0.01f);
			if (PreviousSpacingCM > 0.0f)
			{
				bSpacingVaries |= FMath::Abs(SpacingCM - PreviousSpacingCM)
					> 0.5f;
			}
			PreviousSpacingCM = SpacingCM;
		}

		const FVector LateralVelocity =
			SingleStepTrail->GetTrailSampleWorldVelocity(SampleIndex);
		const float LateralSpeedCMPerSecond = static_cast<float>(
			LateralVelocity.Size());
		TestTrue(TEXT("Particle velocity stays in the trajectory normal plane"),
			FMath::Abs(LateralVelocity.X) < 0.01);
		TestTrue(TEXT("Particle velocity respects the one-fifth speed clamp"),
			LateralSpeedCMPerSecond
				<= BirdSpeedCMPerSecond
					* SingleStepTrail->GetMaximumLateralSpeedFraction()
					+ 0.01f);
		bHasLateralMotion |= LateralSpeedCMPerSecond > 1.0f;
		if (LateralSpeedCMPerSecond < SlowestLateralSpeedCMPerSecond)
		{
			SlowestLateralSpeedCMPerSecond = LateralSpeedCMPerSecond;
			SlowestParticleLifetimeSeconds =
				SingleStepTrail->GetTrailSampleLifetimeSeconds(SampleIndex);
		}
		if (LateralSpeedCMPerSecond > FastestLateralSpeedCMPerSecond)
		{
			FastestLateralSpeedCMPerSecond = LateralSpeedCMPerSecond;
			FastestParticleLifetimeSeconds =
				SingleStepTrail->GetTrailSampleLifetimeSeconds(SampleIndex);
		}
	}
	TestTrue(TEXT("One-dimensional site jitter breaks equal spacing"),
		bSpacingVaries);
	TestTrue(TEXT("Two-dimensional normal distribution produces lateral motion"),
		bHasLateralMotion);
	TestTrue(TEXT("Faster lateral particles receive shorter lifetimes"),
		FastestLateralSpeedCMPerSecond > SlowestLateralSpeedCMPerSecond
			&& FastestParticleLifetimeSeconds
				< SlowestParticleLifetimeSeconds);
	TestTrue(TEXT("Oldest subframe particle has already aged"),
		SingleStepTrail->GetOldestSampleAgeSeconds()
			>= TotalDeltaSeconds - 0.001f);
	TestTrue(TEXT("Subframe ages vary across one update"),
		SingleStepTrail->GetTrailSampleAgeSeconds(0)
			> SingleStepTrail->GetTrailSampleAgeSeconds(
				SingleStepTrail->GetTrailSampleCount() - 1));

	UABTSM11FinaleBirdTrailComponent* DistributionTrail =
		NewObject<UABTSM11FinaleBirdTrailComponent>();
	TestNotNull(TEXT("Distribution trail is constructible"),
		DistributionTrail);
	if (DistributionTrail != nullptr)
	{
		DistributionTrail->BeginTrail(FVector::ZeroVector);
		DistributionTrail->AdvanceTrail(
			FVector(4800.0, 0.0, 0.0),
			0.1f);
		int32 CoreParticleCount = 0;
		int32 EscapingParticleCount = 0;
		const float DistributionSpeedCMPerSecond = 48000.0f;
		const float MaximumLateralSpeedCMPerSecond =
			DistributionSpeedCMPerSecond
			* DistributionTrail->GetMaximumLateralSpeedFraction();
		for (int32 SampleIndex = 0;
			SampleIndex < DistributionTrail->GetTrailSampleCount();
			++SampleIndex)
		{
			const float SpeedRatio = static_cast<float>(
				DistributionTrail->GetTrailSampleWorldVelocity(SampleIndex).Size())
				/ MaximumLateralSpeedCMPerSecond;
			CoreParticleCount += SpeedRatio < 0.40f ? 1 : 0;
			EscapingParticleCount += SpeedRatio > 0.65f ? 1 : 0;
		}
		TestTrue(TEXT("Gaussian normal-plane motion keeps a dense slow core"),
			CoreParticleCount
				> DistributionTrail->GetTrailSampleCount() / 3);
		TestTrue(TEXT("Gaussian tail retains a sparse escaping population"),
			EscapingParticleCount > 0
				&& EscapingParticleCount
					< DistributionTrail->GetTrailSampleCount() / 3);
	}

	TArray<FVector> DeterministicPositions;
	TArray<FVector> DeterministicVelocities;
	for (int32 SampleIndex = 0;
		SampleIndex < SingleStepTrail->GetTrailSampleCount();
		++SampleIndex)
	{
		DeterministicPositions.Add(
			SingleStepTrail->GetTrailSampleWorldPosition(SampleIndex));
		DeterministicVelocities.Add(
			SingleStepTrail->GetTrailSampleWorldVelocity(SampleIndex));
	}
	SingleStepTrail->ClearTrail();
	SingleStepTrail->BeginTrail(InitialEmitterPosition);
	SingleStepTrail->AdvanceTrail(
		FinalEmitterPosition,
		TotalDeltaSeconds);
	bool bResetIsDeterministic = SingleStepTrail->GetTrailSampleCount()
		== DeterministicPositions.Num();
	for (int32 SampleIndex = 0;
		bResetIsDeterministic && SampleIndex < DeterministicPositions.Num();
		++SampleIndex)
	{
		bResetIsDeterministic = DeterministicPositions[SampleIndex].Equals(
			SingleStepTrail->GetTrailSampleWorldPosition(SampleIndex),
			0.01)
			&& DeterministicVelocities[SampleIndex].Equals(
				SingleStepTrail->GetTrailSampleWorldVelocity(SampleIndex),
				0.01);
	}
	TestTrue(TEXT("Blue-noise and Gaussian samples repeat after reset"),
		bResetIsDeterministic);

	SingleStepTrail->AdvanceTrail(
		FinalEmitterPosition,
		SingleStepTrail->GetBaseTrailLifetimeSeconds() + 0.01f);
	TestEqual(TEXT("Per-particle expiry removes the stationary history"),
		SingleStepTrail->GetTrailSampleCount(), 0);
	TestTrue(TEXT("Expiry preserves chronological order"),
		SingleStepTrail->AreTrailSampleAgesOrdered());

	UABTSM11FinaleBirdTrailComponent* CapacityTrail =
		NewObject<UABTSM11FinaleBirdTrailComponent>();
	TestNotNull(TEXT("Capacity trail is constructible"), CapacityTrail);
	if (CapacityTrail != nullptr)
	{
		CapacityTrail->BeginTrail(FVector::ZeroVector);
		bool bReachedCapacity = false;
		for (int32 StepIndex = 1; StepIndex <= 700; ++StepIndex)
		{
			CapacityTrail->AdvanceTrail(
				FVector(StepIndex * 100.0, 0.0, 0.0),
				0.0001f);
			bReachedCapacity |= CapacityTrail->GetTrailSampleCount()
				== CapacityTrail->GetMaximumTrailSampleCount();
			if (CapacityTrail->GetTrailSampleCount()
				> CapacityTrail->GetMaximumTrailSampleCount())
			{
				AddError(TEXT("Particle history exceeded its hard cap"));
				break;
			}
		}
		TestTrue(TEXT("High-density motion exercises the hard cap"),
			bReachedCapacity);
		TestTrue(TEXT("Capacity pruning preserves chronological order"),
			CapacityTrail->AreTrailSampleAgesOrdered());
	}

	SingleStepTrail->ClearTrail();
	TestFalse(TEXT("Clear stops the trail"),
		SingleStepTrail->IsTrailActive());
	TestEqual(TEXT("Clear removes all samples"),
		SingleStepTrail->GetTrailSampleCount(), 0);
	return true;
}

#endif
