// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/ABTSM11FinaleCameraDirector.h"

#include "HAL/IConsoleManager.h"
#include "World/ABTSM11GravityAssistTypes.h"

namespace
{
	TAutoConsoleVariable<int32> CVarABTSM11CameraDirectorM2Enabled(
		TEXT("abts.M11.CameraDirector.M2.Enabled"),
		0,
		TEXT("Enable the M11 M2 Assist1 Cruise/Approach/Periapsis camera director.\n")
		TEXT("0: legacy flight camera (default)\n")
		TEXT("1: M2 Assist1 dual-subject direction"),
		ECVF_Default);

	double ResolveStageProgress(
		const double TimeSeconds,
		const double StartSeconds,
		const double EndSeconds)
	{
		const double Duration = EndSeconds - StartSeconds;
		if (!FMath::IsFinite(TimeSeconds)
			|| !FMath::IsFinite(Duration)
			|| Duration <= UE_DOUBLE_SMALL_NUMBER)
		{
			return 0.0;
		}
		return FMath::Clamp(
			(TimeSeconds - StartSeconds) / Duration,
			0.0,
			1.0);
	}
}

bool FABTSM11FinaleCameraStageSelection::IsUsable() const
{
	return Stage != EABTSM11FinaleCameraStage::Unavailable
		&& FMath::IsFinite(StageProgress)
		&& StageProgress >= 0.0
		&& StageProgress <= 1.0
		&& !TargetLabel.IsEmpty()
		&& !Reason.IsEmpty();
}

bool FABTSM11FinaleCameraStageSelection::IsM2Assist1Window() const
{
	return AssistIndex == 1
		&& !bTargetIsUFO
		&& (Stage == EABTSM11FinaleCameraStage::CruiseToBody
			|| Stage == EABTSM11FinaleCameraStage::Approach
			|| Stage == EABTSM11FinaleCameraStage::Periapsis);
}

bool FABTSM11FinaleCameraDirectorSample::IsUsable() const
{
	const FVector ScreenRight = EncounterScreenRight.GetSafeNormal();
	const FVector ScreenUp = EncounterScreenUp.GetSafeNormal();
	return Selection.IsUsable()
		&& FMath::IsFinite(TargetCenter.X)
		&& FMath::IsFinite(TargetCenter.Y)
		&& FMath::IsFinite(TargetCenter.Z)
		&& FMath::IsFinite(TargetRadiusCM)
		&& TargetRadiusCM > 1.0
		&& FMath::IsFinite(BirdRadiusCM)
		&& BirdRadiusCM > 1.0
		&& FMath::IsFinite(EncounterScreenRight.X)
		&& FMath::IsFinite(EncounterScreenRight.Y)
		&& FMath::IsFinite(EncounterScreenRight.Z)
		&& FMath::IsFinite(EncounterScreenUp.X)
		&& FMath::IsFinite(EncounterScreenUp.Y)
		&& FMath::IsFinite(EncounterScreenUp.Z)
		&& !ScreenRight.IsNearlyZero()
		&& !ScreenUp.IsNearlyZero()
		&& FMath::Abs(FVector::DotProduct(
			ScreenRight,
			ScreenUp)) <= 1.0e-3f;
}

const TCHAR* ABTSM11FinaleCameraDirector::StageLabel(
	const EABTSM11FinaleCameraStage Stage)
{
	switch (Stage)
	{
	case EABTSM11FinaleCameraStage::PreLaunch:
		return TEXT("PreLaunch");
	case EABTSM11FinaleCameraStage::CruiseToBody:
		return TEXT("CruiseToBody");
	case EABTSM11FinaleCameraStage::Approach:
		return TEXT("Approach");
	case EABTSM11FinaleCameraStage::Periapsis:
		return TEXT("Periapsis");
	case EABTSM11FinaleCameraStage::Handoff:
		return TEXT("Handoff");
	case EABTSM11FinaleCameraStage::FinalApproach:
		return TEXT("FinalApproach");
	case EABTSM11FinaleCameraStage::Terminal:
		return TEXT("Terminal");
	default:
		return TEXT("Unavailable");
	}
}

FABTSM11FinaleCameraStageSelection
ABTSM11FinaleCameraDirector::ResolveStage(
	const bool bLaunched,
	const bool bTargetHit,
	const double PlaybackSeconds,
	const FABTSM11TrajectoryResult* Result)
{
	FABTSM11FinaleCameraStageSelection Selection;
	if (!bLaunched && !bTargetHit)
	{
		return Selection;
	}
	if (bTargetHit)
	{
		Selection.Stage = EABTSM11FinaleCameraStage::Terminal;
		Selection.AssistIndex = 0;
		Selection.StageProgress = 1.0;
		Selection.TargetLabel = TEXT("UFO");
		Selection.Reason = TEXT("TargetHit");
		Selection.bTargetIsUFO = true;
		return Selection;
	}
	if (Result == nullptr || Result->ValidationHash == 0
		|| !FMath::IsFinite(PlaybackSeconds))
	{
		Selection.Stage = EABTSM11FinaleCameraStage::Unavailable;
		Selection.Reason = TEXT("AuthorityEventsUnavailable");
		return Selection;
	}

	double PreviousExitSeconds = 0.0;
	for (int32 AssistIndex = 1;
		AssistIndex <= FABTSM11GravityScenario::AssistCount;
		++AssistIndex)
	{
		const FABTSM11TrajectoryEvent* Enter = Result->FindAssistEvent(
			EABTSM11TrajectoryEventType::AssistEnter,
			AssistIndex);
		const FABTSM11TrajectoryEvent* Closest = Result->FindAssistEvent(
			EABTSM11TrajectoryEventType::ClosestApproach,
			AssistIndex);
		const FABTSM11TrajectoryEvent* Exit = Result->FindAssistEvent(
			EABTSM11TrajectoryEventType::AssistExit,
			AssistIndex);
		if (Enter == nullptr || Closest == nullptr || Exit == nullptr
			|| Enter->TimeSeconds > Closest->TimeSeconds
			|| Closest->TimeSeconds > Exit->TimeSeconds)
		{
			Selection.Stage = EABTSM11FinaleCameraStage::Unavailable;
			Selection.Reason = FString::Printf(
				TEXT("Assist%dEventsIncomplete"),
				AssistIndex);
			return Selection;
		}
		Selection.AssistIndex = AssistIndex;
		Selection.TargetLabel = FString::Printf(
			TEXT("Assist%d"), AssistIndex);
		if (PlaybackSeconds < Enter->TimeSeconds)
		{
			Selection.Stage = AssistIndex == 1
				? EABTSM11FinaleCameraStage::CruiseToBody
				: EABTSM11FinaleCameraStage::Handoff;
			Selection.StageProgress = ResolveStageProgress(
				PlaybackSeconds,
				PreviousExitSeconds,
				Enter->TimeSeconds);
			Selection.Reason = AssistIndex == 1
				? TEXT("LaunchToAssist1")
				: FString::Printf(
					TEXT("Assist%dExitToAssist%d"),
					AssistIndex - 1,
					AssistIndex);
			return Selection;
		}
		if (PlaybackSeconds <= Closest->TimeSeconds)
		{
			Selection.Stage = EABTSM11FinaleCameraStage::Approach;
			Selection.StageProgress = ResolveStageProgress(
				PlaybackSeconds,
				Enter->TimeSeconds,
				Closest->TimeSeconds);
			Selection.Reason = FString::Printf(
				TEXT("Assist%dEnter"), AssistIndex);
			return Selection;
		}
		if (PlaybackSeconds <= Exit->TimeSeconds)
		{
			Selection.Stage = EABTSM11FinaleCameraStage::Periapsis;
			Selection.StageProgress = ResolveStageProgress(
				PlaybackSeconds,
				Closest->TimeSeconds,
				Exit->TimeSeconds);
			Selection.Reason = FString::Printf(
				TEXT("Assist%dClosestApproach"), AssistIndex);
			return Selection;
		}
		PreviousExitSeconds = Exit->TimeSeconds;
	}

	Selection.Stage = EABTSM11FinaleCameraStage::FinalApproach;
	Selection.AssistIndex = 0;
	Selection.StageProgress = 0.0;
	Selection.TargetLabel = TEXT("UFO");
	Selection.Reason = TEXT("Assist3Exit");
	Selection.bTargetIsUFO = true;
	return Selection;
}

bool ABTSM11FinaleCameraDirector::BuildAssistEncounterBasis(
	const FVector& TargetCenter,
	const FVector& EnterPosition,
	const FVector& ClosestPosition,
	const FVector& ClosestVelocity,
	const FVector& ExitPosition,
	FVector& OutScreenRight,
	FVector& OutScreenUp)
{
	OutScreenRight = FVector::ZeroVector;
	OutScreenUp = FVector::ZeroVector;
	const auto IsFiniteVector = [](const FVector& Value)
	{
		return FMath::IsFinite(Value.X)
			&& FMath::IsFinite(Value.Y)
			&& FMath::IsFinite(Value.Z);
	};
	if (!IsFiniteVector(TargetCenter)
		|| !IsFiniteVector(EnterPosition)
		|| !IsFiniteVector(ClosestPosition)
		|| !IsFiniteVector(ClosestVelocity)
		|| !IsFiniteVector(ExitPosition))
	{
		return false;
	}

	const FVector ClosestRadial =
		(ClosestPosition - TargetCenter).GetSafeNormal();
	if (ClosestRadial.IsNearlyZero())
	{
		return false;
	}
	FVector ChronologicalRight = FVector::VectorPlaneProject(
		ClosestVelocity,
		ClosestRadial).GetSafeNormal();
	const FVector EventChord = ExitPosition - EnterPosition;
	if (ChronologicalRight.IsNearlyZero())
	{
		ChronologicalRight = FVector::VectorPlaneProject(
			EventChord,
			ClosestRadial).GetSafeNormal();
	}
	if (ChronologicalRight.IsNearlyZero())
	{
		return false;
	}
	if (!EventChord.IsNearlyZero()
		&& FVector::DotProduct(ChronologicalRight, EventChord) < 0.0f)
	{
		ChronologicalRight *= -1.0f;
	}

	OutScreenRight = ChronologicalRight;
	// Keep the closest radial in depth instead of mapping it to screen-down.
	// With camera forward facing back along ClosestRadial, this normal makes
	// chronological velocity project to screen-right and lets the foreground
	// bird cross the planet disc rather than orbiting around its silhouette.
	OutScreenUp = FVector::CrossProduct(
		-ClosestRadial,
		ChronologicalRight).GetSafeNormal();
	return FMath::Abs(FVector::DotProduct(
		OutScreenRight,
		OutScreenUp)) <= 1.0e-3f
		&& !FVector::CrossProduct(
			OutScreenUp,
			OutScreenRight).IsNearlyZero();
}

void ABTSM11FinaleCameraDirector::SetM2Enabled(const bool bEnabled)
{
	CVarABTSM11CameraDirectorM2Enabled->Set(
		bEnabled ? 1 : 0,
		ECVF_SetByCode);
}

bool ABTSM11FinaleCameraDirector::IsM2Enabled()
{
	return CVarABTSM11CameraDirectorM2Enabled.GetValueOnGameThread() != 0;
}
