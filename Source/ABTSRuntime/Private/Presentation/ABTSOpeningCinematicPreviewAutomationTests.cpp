// Copyright Epic Games, Inc. All Rights Reserved.

#include "Presentation/ABTSOpeningCinematicTypes.h"
#include "Presentation/ABTSCinematicPlaybackPolicy.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FABTSOpeningCinematicTimelineTest,
	"ABTS.Presentation.Opening.Timeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FABTSOpeningCinematicTimelineTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("RC9 Development default does not skip cinematics"),
		FABTSCinematicPlaybackPolicy::ResolveSkipRequest(false, false));
	TestTrue(TEXT("A later Development debug request may skip cinematics"),
		FABTSCinematicPlaybackPolicy::ResolveSkipRequest(true, false));
	TestFalse(TEXT("Shipping always plays cinematics even when skip is requested"),
		FABTSCinematicPlaybackPolicy::ResolveSkipRequest(true, true));

	TestEqual(TEXT("Opening duration"), FABTSOpeningCinematicEvaluator::DurationSeconds, 42.0f);
	TestEqual(TEXT("0s establish phase"), FABTSOpeningCinematicEvaluator::ResolvePhase(0.0f), EABTSOpeningPhase::Establish);
	TestEqual(TEXT("4s circle phase"), FABTSOpeningCinematicEvaluator::ResolvePhase(4.0f), EABTSOpeningPhase::CirclePlay);
	TestEqual(TEXT("12s close-up phase"), FABTSOpeningCinematicEvaluator::ResolvePhase(12.0f), EABTSOpeningPhase::WhiteBirdCloseUp);
	TestEqual(TEXT("21s capture phase"), FABTSOpeningCinematicEvaluator::ResolvePhase(21.0f), EABTSOpeningPhase::Capture);
	TestEqual(TEXT("35s handoff phase"), FABTSOpeningCinematicEvaluator::ResolvePhase(35.0f), EABTSOpeningPhase::Handoff);
	TestEqual(TEXT("42s complete phase"), FABTSOpeningCinematicEvaluator::ResolvePhase(42.0f), EABTSOpeningPhase::Complete);

	TArray<FVector> InitialPositions;
	for (int32 Index = 0; Index < static_cast<int32>(EABTSOpeningBird::Count); ++Index)
	{
		const FABTSOpeningBirdPose Pose = FABTSOpeningCinematicEvaluator::EvaluateBird(
			0.0f, static_cast<EABTSOpeningBird>(Index));
		InitialPositions.Add(Pose.LocalPosition);
		TestTrue(TEXT("Initial bird is on the 300cm ring"),
			FMath::IsNearlyEqual(Pose.LocalPosition.Size(), FABTSOpeningCinematicEvaluator::CircleRadiusCM, 0.1f));
		TestEqual(TEXT("Initial bird is idle"), Pose.AnimationCue, EABTSOpeningAnimationCue::Idle);
	}
	for (int32 Index = 0; Index < InitialPositions.Num(); ++Index)
	{
		const int32 Next = (Index + 1) % InitialPositions.Num();
		TestTrue(TEXT("Initial neighbours satisfy minimum separation"),
			FVector::Distance(InitialPositions[Index], InitialPositions[Next]) >= 180.0f);
	}

	const FABTSOpeningBirdPose MovingRed = FABTSOpeningCinematicEvaluator::EvaluateBird(8.0f, EABTSOpeningBird::Red);
	const FABTSOpeningBirdPose MovingWhite = FABTSOpeningCinematicEvaluator::EvaluateBird(8.0f, EABTSOpeningBird::White);
	TestEqual(TEXT("Circle red uses Move"), MovingRed.AnimationCue, EABTSOpeningAnimationCue::Move);
	TestEqual(TEXT("Circle white uses Move"), MovingWhite.AnimationCue, EABTSOpeningAnimationCue::Move);
	TestFalse(TEXT("White and red use distinct running phases"), MovingWhite.LocalPosition.Equals(MovingRed.LocalPosition, 0.1f));

	const FVector CaptureBase = FABTSOpeningCinematicEvaluator::GetWhiteBirdCaptureBase();
	const FABTSOpeningBirdPose CapturedWhite = FABTSOpeningCinematicEvaluator::EvaluateBird(26.99f, EABTSOpeningBird::White);
	TestTrue(TEXT("Capture leaves horizontal position unchanged"),
		FVector2D(CapturedWhite.LocalPosition).Equals(FVector2D(CaptureBase), 0.5f));
	TestTrue(TEXT("Capture raises white bird"), CapturedWhite.LocalPosition.Z > 470.0f);
	TestEqual(TEXT("Capture uses Fly"), CapturedWhite.AnimationCue, EABTSOpeningAnimationCue::Fly);

	const FABTSOpeningUFOPose CaptureUFO = FABTSOpeningCinematicEvaluator::EvaluateUFO(24.0f);
	TestTrue(TEXT("UFO visible during capture"), CaptureUFO.bVisible);
	TestTrue(TEXT("Beam visible during capture"), CaptureUFO.bCaptureBeamVisible);
	TestTrue(TEXT("UFO hovers above capture base"),
		FVector2D(CaptureUFO.LocalPosition).Equals(FVector2D(CaptureBase), 0.1f));

	const FABTSOpeningBirdPose HandoffRed = FABTSOpeningCinematicEvaluator::EvaluateBird(42.0f, EABTSOpeningBird::Red);
	const FABTSOpeningBirdPose HandoffWhite = FABTSOpeningCinematicEvaluator::EvaluateBird(42.0f, EABTSOpeningBird::White);
	TestTrue(TEXT("Red reaches front of handoff diamond"), HandoffRed.LocalPosition.Equals(FVector(180.0f, 0.0f, 0.0f), 0.1f));
	TestFalse(TEXT("White is hidden after departure"), HandoffWhite.bVisible);
	TestFalse(TEXT("UFO is hidden after departure"), FABTSOpeningCinematicEvaluator::EvaluateUFO(35.0f).bVisible);

	const FABTSOpeningCameraPose CaptureCamera = FABTSOpeningCinematicEvaluator::EvaluateCamera(24.0f);
	TestEqual(TEXT("Capture camera FOV"), CaptureCamera.FieldOfViewDegrees, 55.0f);
	return true;
}

#endif
