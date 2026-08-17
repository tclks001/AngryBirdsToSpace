// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinalePostHitCinematicTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSM11FinalePostHitTimelineTest,
	"ABTS.M11D.PostHit.Timeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSM11FinalePostHitTimelineTest::RunTest(const FString& Parameters)
{
	using Evaluator = FABTSM11FinalePostHitCinematicEvaluator;
	TestEqual(TEXT("Post-hit duration"), Evaluator::DurationSeconds, 18.0f);
	TestEqual(
		TEXT("0s is impact"),
		Evaluator::ResolvePhase(0.0f),
		EABTSM11FinalePostHitPhase::Impact);
	TestEqual(
		TEXT("1.2s starts rescue"),
		Evaluator::ResolvePhase(Evaluator::ImpactEndSeconds),
		EABTSM11FinalePostHitPhase::Rescue);
	TestEqual(
		TEXT("5.5s starts reformation"),
		Evaluator::ResolvePhase(Evaluator::RescueEndSeconds),
		EABTSM11FinalePostHitPhase::Reformation);
	TestEqual(
		TEXT("7s starts five-bird orbit"),
		Evaluator::ResolvePhase(Evaluator::ReformationEndSeconds),
		EABTSM11FinalePostHitPhase::FiveBirdOrbit);
	TestEqual(
		TEXT("14s starts ending"),
		Evaluator::ResolvePhase(Evaluator::OrbitEndSeconds),
		EABTSM11FinalePostHitPhase::Ending);
	TestEqual(
		TEXT("18s is complete"),
		Evaluator::ResolvePhase(Evaluator::DurationSeconds),
		EABTSM11FinalePostHitPhase::Complete);

	const EABTSM11FinalePostHitAudioCue BeforeImpact =
		Evaluator::ResolveCrossedAudioCues(0.0f, 0.17f);
	TestEqual(
		TEXT("Physical contact frame has no immediate semantic cue"),
		BeforeImpact,
		EABTSM11FinalePostHitAudioCue::None);
	const EABTSM11FinalePostHitAudioCue Impact =
		Evaluator::ResolveCrossedAudioCues(0.17f, 0.19f);
	TestTrue(
		TEXT("Visible break triggers impact audio"),
		EnumHasAnyFlags(
			Impact,
			EABTSM11FinalePostHitAudioCue::ImpactBreak));
	TestFalse(
		TEXT("Visible break does not trigger completion ding"),
		EnumHasAnyFlags(
			Impact,
			EABTSM11FinalePostHitAudioCue::Completion));
	const EABTSM11FinalePostHitAudioCue Completion =
		Evaluator::ResolveCrossedAudioCues(14.19f, 14.21f);
	TestTrue(
		TEXT("Completion confirmation belongs to the ending"),
		EnumHasAnyFlags(
			Completion,
			EABTSM11FinalePostHitAudioCue::Completion));

	const FABTSM11FinalePostHitUFOPose Intact = Evaluator::EvaluateUFO(0.0f);
	const FABTSM11FinalePostHitUFOPose Broken = Evaluator::EvaluateUFO(0.3f);
	const FABTSM11FinalePostHitUFOPose Cleared = Evaluator::EvaluateUFO(4.0f);
	TestTrue(TEXT("UFO starts intact"), Intact.bIntactVisible);
	TestFalse(TEXT("UFO starts without broken shell"), Intact.bBrokenVisible);
	TestFalse(TEXT("Intact shell hides after break"), Broken.bIntactVisible);
	TestTrue(TEXT("Broken shell appears after break"), Broken.bBrokenVisible);
	TestTrue(TEXT("Impact flash is visible"), Broken.FlashAlpha > 0.0f);
	TestFalse(TEXT("UFO remnants clear before reunion"), Cleared.bBrokenVisible);
	TestTrue(
		TEXT("Broken shell remains visible through the live Chaos window"),
		Evaluator::EvaluateUFO(3.69f).bBrokenVisible);

	const FABTSM11FinalePostHitBirdPose WhiteAtImpact =
		Evaluator::EvaluateBird(0.0f, EABTSM11FinalePostHitBird::White);
	const FABTSM11FinalePostHitBirdPose WhiteReleased =
		Evaluator::EvaluateBird(
			Evaluator::RescueEndSeconds,
			EABTSM11FinalePostHitBird::White);
	TestEqual(
		TEXT("White bird reacts to impact"),
		WhiteAtImpact.AnimationCue,
		EABTSM11FinalePostHitAnimationCue::Damage);
	TestTrue(
		TEXT("White bird exits at least 650cm along the rescue direction"),
		WhiteReleased.LocalPosition.X >= 650.0f);
	TestEqual(
		TEXT("White bird uses flight after release"),
		WhiteReleased.AnimationCue,
		EABTSM11FinalePostHitAnimationCue::Fly);

	TArray<FVector> OrbitPositions;
	for (int32 Index = 0;
		Index < static_cast<int32>(EABTSM11FinalePostHitBird::Count);
		++Index)
	{
		const FABTSM11FinalePostHitBirdPose Pose = Evaluator::EvaluateBird(
			8.0f,
			static_cast<EABTSM11FinalePostHitBird>(Index));
		OrbitPositions.Add(Pose.LocalPosition);
		const FVector Relative = Pose.LocalPosition - Evaluator::GetOrbitCenter();
		const float ScreenPlaneRadius = FVector2D(Relative.Y, Relative.Z).Size();
		TestTrue(
			TEXT("Each reunited bird stays on the 260cm presentation orbit"),
			FMath::IsNearlyEqual(
				ScreenPlaneRadius,
				Evaluator::FiveBirdOrbitRadiusCM,
				0.1f));
	}
	for (int32 Index = 0; Index < OrbitPositions.Num(); ++Index)
	{
		for (int32 Other = Index + 1; Other < OrbitPositions.Num(); ++Other)
		{
			TestTrue(
				TEXT("Five-bird orbit has no duplicate member position"),
				FVector::Distance(
					OrbitPositions[Index],
					OrbitPositions[Other]) > 250.0f);
		}
	}

	for (const float Boundary : {
		Evaluator::ImpactEndSeconds,
		Evaluator::RescueEndSeconds,
		Evaluator::ReformationEndSeconds,
		Evaluator::OrbitEndSeconds})
	{
		for (int32 Index = 0;
			Index < static_cast<int32>(EABTSM11FinalePostHitBird::Count);
			++Index)
		{
			const EABTSM11FinalePostHitBird Bird =
				static_cast<EABTSM11FinalePostHitBird>(Index);
			const FVector Before = Evaluator::EvaluateBird(
				Boundary - 0.001f,
				Bird).LocalPosition;
			const FVector After = Evaluator::EvaluateBird(
				Boundary + 0.001f,
				Bird).LocalPosition;
			TestTrue(
				TEXT("Bird pose remains continuous across phase boundary"),
				FVector::Distance(Before, After) < 5.0f);
		}
	}

	const FABTSM11FinalePostHitCameraPose OrbitCamera =
		Evaluator::EvaluateCamera(8.0f);
	const FABTSM11FinalePostHitCameraPose EndingCamera =
		Evaluator::EvaluateCamera(17.9f);
	TestTrue(
		TEXT("Orbit camera remains finite"),
		!OrbitCamera.LocalPosition.ContainsNaN()
			&& !OrbitCamera.LocalLookAt.ContainsNaN());
	TestTrue(
		TEXT("Ending camera pulls farther away"),
		FVector::Distance(
			EndingCamera.LocalPosition,
			Evaluator::GetOrbitCenter())
			> FVector::Distance(
				OrbitCamera.LocalPosition,
				Evaluator::GetOrbitCenter()));
	TestTrue(
		TEXT("Ending declares fade progression for Integration"),
		EndingCamera.FadeToBlackAlpha > 0.9f);

	const FABTSM11FinalePostHitLightingPose ImpactLighting =
		Evaluator::EvaluateLighting(0.0f);
	const FABTSM11FinalePostHitLightingPose ReunionLighting =
		Evaluator::EvaluateLighting(Evaluator::ReunionCueSeconds);
	const FABTSM11FinalePostHitLightingPose EndingLighting =
		Evaluator::EvaluateLighting(Evaluator::DurationSeconds);
	TestTrue(
		TEXT("Cinematic exposure lifts the finale above its shared space bias"),
		Evaluator::CinematicExposureBias > 0.0f);
	TestTrue(
		TEXT("Three-point rig positions remain finite"),
		!ImpactLighting.KeyLocalPosition.ContainsNaN()
			&& !ImpactLighting.FillLocalPosition.ContainsNaN()
			&& !ImpactLighting.RimLocalPosition.ContainsNaN());
	TestTrue(
		TEXT("Persistent key, fill and rim prevent unlit black faces"),
		ImpactLighting.KeyIntensity > 0.0f
			&& ImpactLighting.FillIntensity > 0.0f
			&& ImpactLighting.RimIntensity > 0.0f);
	TestTrue(
		TEXT("Reunion briefly emphasizes the rim"),
		ReunionLighting.RimIntensity > ImpactLighting.RimIntensity);
	TestTrue(
		TEXT("Ending retains a readable rim before Integration fade"),
		EndingLighting.RimIntensity
			>= ImpactLighting.RimIntensity * 0.70f);
	return true;
}

#endif
