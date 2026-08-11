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

	TAutoConsoleVariable<int32> CVarABTSM11CameraDirectorM3Enabled(
		TEXT("abts.M11.CameraDirector.M3.Enabled"),
		0,
		TEXT("Enable the M11 M3 three-assist camera director.\n")
		TEXT("0: preserve the legacy/M2 scope (default)\n")
		TEXT("1: direct Assist1/2/3 and their Handoff windows"),
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

	bool ApplyIncomingShot(
		const double PlaybackSeconds,
		const int32 IncomingAssistIndex,
		const int32 OutgoingAssistIndex,
		const double RevealStartSeconds,
		const double BridgeEndAbsoluteSeconds,
		const double AcquireEndAbsoluteSeconds,
		const double EntryStartAbsoluteSeconds,
		const FABTSM11TrajectoryEvent& IncomingEnter,
		const FABTSM11TrajectoryEvent& IncomingClosest,
		FABTSM11FinaleCameraStageSelection& Selection)
	{
		const double ShotDurationSeconds = FMath::Max(
			IncomingEnter.TimeSeconds - RevealStartSeconds,
			UE_DOUBLE_SMALL_NUMBER);
		const double ShotElapsedSeconds = FMath::Clamp(
			PlaybackSeconds - RevealStartSeconds,
			0.0,
			ShotDurationSeconds);
		const double BridgeEndSeconds = FMath::Clamp(
			BridgeEndAbsoluteSeconds - RevealStartSeconds,
			0.0,
			ShotDurationSeconds);
		const double RevealEndSeconds = FMath::Clamp(
			AcquireEndAbsoluteSeconds - RevealStartSeconds,
			BridgeEndSeconds,
			ShotDurationSeconds);
		const double EntryStartSeconds = FMath::Clamp(
			EntryStartAbsoluteSeconds - RevealStartSeconds,
			RevealEndSeconds,
			ShotDurationSeconds);
		if (PlaybackSeconds >= EntryStartAbsoluteSeconds)
		{
			Selection.ShotPhase =
				EABTSM11FinaleCameraShotPhase::IncomingEntryMatch;
			Selection.ShotPhaseProgress = ResolveStageProgress(
				ShotElapsedSeconds,
				EntryStartSeconds,
				ShotDurationSeconds);
			Selection.ShotPhaseDurationSeconds = FMath::Max(
				0.0, ShotDurationSeconds - EntryStartSeconds);
		}
		else if (OutgoingAssistIndex > 0
			&& ShotElapsedSeconds < BridgeEndSeconds)
		{
			Selection.ShotPhase =
				EABTSM11FinaleCameraShotPhase::DualBodyBridge;
			Selection.ShotPhaseProgress = ResolveStageProgress(
				ShotElapsedSeconds,
				0.0,
				BridgeEndSeconds);
			Selection.ShotPhaseDurationSeconds = BridgeEndSeconds;
		}
		else if (ShotElapsedSeconds < RevealEndSeconds)
		{
			Selection.ShotPhase =
				EABTSM11FinaleCameraShotPhase::IncomingReveal;
			Selection.ShotPhaseProgress = ResolveStageProgress(
				ShotElapsedSeconds,
				BridgeEndSeconds,
				RevealEndSeconds);
			Selection.ShotPhaseDurationSeconds = FMath::Max(
				0.0, RevealEndSeconds - BridgeEndSeconds);
		}
		else
		{
			Selection.ShotPhase =
				EABTSM11FinaleCameraShotPhase::IncomingTrack;
			Selection.ShotPhaseProgress = ResolveStageProgress(
				ShotElapsedSeconds,
				RevealEndSeconds,
				EntryStartSeconds);
			Selection.ShotPhaseDurationSeconds = FMath::Max(
				0.0,
				EntryStartSeconds - RevealEndSeconds);
		}
		Selection.ShotProgress = ResolveStageProgress(
			PlaybackSeconds,
			RevealStartSeconds,
			IncomingEnter.TimeSeconds);
		Selection.ShotDurationSeconds = ShotDurationSeconds;
		const double ApproachDurationSeconds = FMath::Max(
			IncomingClosest.TimeSeconds - IncomingEnter.TimeSeconds,
			UE_DOUBLE_SMALL_NUMBER);
		// The incoming shot covers 1.65 R. Approach begins with a 0.6
		// normalized slope across 1.90 R; match their world-time speeds.
		Selection.ShotEndSlope = FMath::Clamp(
			(1.90 * 0.60 / ApproachDurationSeconds)
				* (ShotDurationSeconds / 1.65),
			0.0,
			2.0);
		Selection.FramingAssistIndex = IncomingAssistIndex;
		Selection.OutgoingAssistIndex = OutgoingAssistIndex;
		Selection.IncomingAssistIndex = IncomingAssistIndex;
		Selection.FramingTargetLabel = FString::Printf(
			TEXT("Assist%d"), IncomingAssistIndex);
		switch (Selection.ShotPhase)
		{
		case EABTSM11FinaleCameraShotPhase::DualBodyBridge:
			Selection.FramingTargetLabel = FString::Printf(
				TEXT("Assist%d+Assist%d"),
				OutgoingAssistIndex,
				IncomingAssistIndex);
			Selection.ShotReason = FString::Printf(
				TEXT("Assist%dToAssist%dDualBodyBridge"),
				OutgoingAssistIndex,
				IncomingAssistIndex);
			break;
		case EABTSM11FinaleCameraShotPhase::IncomingReveal:
			Selection.ShotReason = FString::Printf(
				TEXT("Assist%dAcquire"), IncomingAssistIndex);
			break;
		case EABTSM11FinaleCameraShotPhase::IncomingTrack:
			Selection.ShotReason = FString::Printf(
				TEXT("Assist%dTrack"), IncomingAssistIndex);
			break;
		default:
			Selection.ShotReason = FString::Printf(
				TEXT("Assist%dEntryMatch"), IncomingAssistIndex);
			break;
		}
		return true;
	}

	bool ApplyM3ShotPlan(
		const double PlaybackSeconds,
		const FABTSM11TrajectoryResult& Result,
		const FABTSM11FinaleCameraShotSettings& Settings,
		const FABTSM11FinaleCameraShotPlan* FrozenShotPlan,
		FABTSM11FinaleCameraStageSelection& Selection)
	{
		FABTSM11FinaleCameraShotPlan LocalShotPlan;
		const FABTSM11FinaleCameraShotPlan* ActiveShotPlan = FrozenShotPlan;
		if (ActiveShotPlan == nullptr)
		{
			if (!LocalShotPlan.Build(Result, Settings))
			{
				return false;
			}
			ActiveShotPlan = &LocalShotPlan;
		}
		if (!ActiveShotPlan->IsUsableFor(Result))
		{
			return false;
		}
		const FABTSM11TrajectoryEvent* FirstEnter = Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::AssistEnter,
			1);
		const FABTSM11TrajectoryEvent* FirstClosest = Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::ClosestApproach,
			1);
		if (FirstEnter == nullptr || FirstClosest == nullptr)
		{
			return false;
		}
		if (PlaybackSeconds < FirstEnter->TimeSeconds)
		{
			// Launch is the virtual source anchor for the first encounter. Starting
			// at playback zero removes the candidate-duration-dependent dead zone
			// while alpha zero still preserves the exact launch composition.
			return ApplyIncomingShot(
				PlaybackSeconds,
				1,
				0,
				0.0,
				0.0,
				FMath::Min(Settings.IncomingAcquireSeconds,
					FirstEnter->TimeSeconds),
				FMath::Max(0.0,
					FirstEnter->TimeSeconds - Settings.EntryMatchSeconds),
				*FirstEnter,
				*FirstClosest,
				Selection);
		}
		for (int32 IncomingAssistIndex = 2;
			IncomingAssistIndex <= FABTSM11GravityScenario::AssistCount;
			++IncomingAssistIndex)
		{
			const int32 OutgoingAssistIndex = IncomingAssistIndex - 1;
			const FABTSM11TrajectoryEvent* OutgoingClosest =
				Result.FindAssistEvent(
					EABTSM11TrajectoryEventType::ClosestApproach,
					OutgoingAssistIndex);
			const FABTSM11TrajectoryEvent* OutgoingExit =
				Result.FindAssistEvent(
					EABTSM11TrajectoryEventType::AssistExit,
					OutgoingAssistIndex);
			const FABTSM11TrajectoryEvent* IncomingEnter =
				Result.FindAssistEvent(
					EABTSM11TrajectoryEventType::AssistEnter,
					IncomingAssistIndex);
			const FABTSM11TrajectoryEvent* IncomingClosest =
				Result.FindAssistEvent(
					EABTSM11TrajectoryEventType::ClosestApproach,
					IncomingAssistIndex);
			if (OutgoingClosest == nullptr || OutgoingExit == nullptr
				|| IncomingEnter == nullptr || IncomingClosest == nullptr)
			{
				return false;
			}

			const FABTSM11FinaleCameraTransitionTiming* Timing =
				ActiveShotPlan->FindTransition(IncomingAssistIndex);
			if (Timing == nullptr)
			{
				return false;
			}
			if (PlaybackSeconds < Timing->OutgoingStartSeconds
				|| PlaybackSeconds >= IncomingEnter->TimeSeconds)
			{
				continue;
			}

			if (PlaybackSeconds < Timing->RevealStartSeconds)
			{
				Selection.ShotPhase =
					EABTSM11FinaleCameraShotPhase::OutgoingHold;
				Selection.ShotProgress = ResolveStageProgress(
					PlaybackSeconds,
					Timing->OutgoingStartSeconds,
					Timing->RevealStartSeconds);
				Selection.ShotDurationSeconds = FMath::Max(
					0.0,
					Timing->RevealStartSeconds
						- Timing->OutgoingStartSeconds);
				Selection.ShotPhaseProgress = Selection.ShotProgress;
				Selection.ShotPhaseDurationSeconds =
					Selection.ShotDurationSeconds;
				Selection.ShotEndSlope = 0.0;
				Selection.FramingAssistIndex = OutgoingAssistIndex;
				Selection.OutgoingAssistIndex = OutgoingAssistIndex;
				Selection.IncomingAssistIndex = IncomingAssistIndex;
				Selection.FramingTargetLabel = FString::Printf(
					TEXT("Assist%d"), OutgoingAssistIndex);
				Selection.ShotReason = FString::Printf(
					TEXT("Assist%dDepartureHold"), OutgoingAssistIndex);
				return true;
			}

			return ApplyIncomingShot(
				PlaybackSeconds,
				IncomingAssistIndex,
				OutgoingAssistIndex,
				Timing->RevealStartSeconds,
				Timing->BridgeEndSeconds,
				Timing->AcquireEndSeconds,
				Timing->EntryStartSeconds,
				*IncomingEnter,
				*IncomingClosest,
				Selection);
		}
		return true;
	}
}

bool FABTSM11FinaleCameraShotSettings::IsUsable() const
{
	return FMath::IsFinite(IncomingRevealLeadSeconds)
		&& IncomingRevealLeadSeconds > 0.0
		&& FMath::IsFinite(IncomingAcquireSeconds)
		&& IncomingAcquireSeconds > 0.0
		&& FMath::IsFinite(DualBodyBridgeSeconds)
		&& DualBodyBridgeSeconds > 0.0
		&& FMath::IsFinite(MinimumDepartureHoldSeconds)
		&& MinimumDepartureHoldSeconds >= 0.0
		&& FMath::IsFinite(ForegroundTransitClearProgress)
		&& ForegroundTransitClearProgress > 0.0
		&& ForegroundTransitClearProgress < 1.0
		&& FMath::IsFinite(OutgoingReleaseSeconds)
		&& OutgoingReleaseSeconds > 0.0
		&& FMath::IsFinite(MinimumOutgoingReleaseSeconds)
		&& MinimumOutgoingReleaseSeconds > 0.0
		&& MinimumOutgoingReleaseSeconds <= OutgoingReleaseSeconds
		&& FMath::IsFinite(MinimumIncomingTrackSeconds)
		&& MinimumIncomingTrackSeconds > 0.0
		&& FMath::IsFinite(EntryMatchSeconds)
		&& EntryMatchSeconds > 0.0
		&& IncomingRevealLeadSeconds
			> FMath::Max(
				OutgoingReleaseSeconds,
				DualBodyBridgeSeconds + IncomingAcquireSeconds
					+ MinimumIncomingTrackSeconds)
				+ EntryMatchSeconds;
}

bool FABTSM11FinaleCameraShotSettings::BuildPlaybackClockSettings(
	const double PlaybackTimeScale,
	FABTSM11FinaleCameraShotSettings& OutSettings) const
{
	if (!IsUsable()
		|| !FMath::IsFinite(PlaybackTimeScale)
		|| PlaybackTimeScale <= 0.0)
	{
		return false;
	}

	OutSettings = *this;
	OutSettings.IncomingRevealLeadSeconds *= PlaybackTimeScale;
	OutSettings.IncomingAcquireSeconds *= PlaybackTimeScale;
	OutSettings.DualBodyBridgeSeconds *= PlaybackTimeScale;
	OutSettings.MinimumDepartureHoldSeconds *= PlaybackTimeScale;
	OutSettings.OutgoingReleaseSeconds *= PlaybackTimeScale;
	OutSettings.MinimumOutgoingReleaseSeconds *= PlaybackTimeScale;
	OutSettings.MinimumIncomingTrackSeconds *= PlaybackTimeScale;
	OutSettings.EntryMatchSeconds *= PlaybackTimeScale;
	return OutSettings.IsUsable();
}

bool FABTSM11FinaleCameraTransitionTiming::IsUsable() const
{
	return OutgoingAssistIndex >= 1
		&& IncomingAssistIndex == OutgoingAssistIndex + 1
		&& FMath::IsFinite(OutgoingStartSeconds)
		&& FMath::IsFinite(RevealStartSeconds)
		&& FMath::IsFinite(BridgeEndSeconds)
		&& FMath::IsFinite(AcquireEndSeconds)
		&& FMath::IsFinite(EntryStartSeconds)
		&& FMath::IsFinite(IncomingEnterSeconds)
		&& OutgoingStartSeconds < RevealStartSeconds
		&& RevealStartSeconds < BridgeEndSeconds
		&& BridgeEndSeconds < AcquireEndSeconds
		&& AcquireEndSeconds < EntryStartSeconds
		&& EntryStartSeconds < IncomingEnterSeconds;
}

void FABTSM11FinaleCameraShotPlan::Reset()
{
	ReleasedTrajectoryHash = 0;
	bUsesAdaptiveCompression = false;
	for (FABTSM11FinaleCameraTransitionTiming& Timing : Transitions)
	{
		Timing = FABTSM11FinaleCameraTransitionTiming();
	}
}

bool FABTSM11FinaleCameraShotPlan::Build(
	const FABTSM11TrajectoryResult& Result,
	const FABTSM11FinaleCameraShotSettings& Settings,
	FString* OutFailure)
{
	Reset();
	const auto Reject = [OutFailure](const FString& Reason)
	{
		if (OutFailure != nullptr)
		{
			*OutFailure = Reason;
		}
		return false;
	};
	if (Result.ValidationHash == 0 || !Settings.IsUsable())
	{
		return Reject(TEXT("M7ShotPlanAuthorityInvalid"));
	}

	for (int32 IncomingAssistIndex = 2;
		IncomingAssistIndex <= FABTSM11GravityScenario::AssistCount;
		++IncomingAssistIndex)
	{
		const int32 OutgoingAssistIndex = IncomingAssistIndex - 1;
		const FABTSM11TrajectoryEvent* OutgoingClosest =
			Result.FindAssistEvent(
				EABTSM11TrajectoryEventType::ClosestApproach,
				OutgoingAssistIndex);
		const FABTSM11TrajectoryEvent* OutgoingExit = Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::AssistExit,
			OutgoingAssistIndex);
		const FABTSM11TrajectoryEvent* IncomingEnter = Result.FindAssistEvent(
			EABTSM11TrajectoryEventType::AssistEnter,
			IncomingAssistIndex);
		if (OutgoingClosest == nullptr || OutgoingExit == nullptr
			|| IncomingEnter == nullptr)
		{
			return Reject(FString::Printf(
				TEXT("M7Assist%dEventsIncomplete"), IncomingAssistIndex));
		}

		FABTSM11FinaleCameraTransitionTiming& Timing =
			Transitions[IncomingAssistIndex - 2];
		Timing.OutgoingAssistIndex = OutgoingAssistIndex;
		Timing.IncomingAssistIndex = IncomingAssistIndex;
		Timing.IncomingEnterSeconds = IncomingEnter->TimeSeconds;
		const double ForegroundClearSeconds = FMath::Lerp(
			OutgoingClosest->TimeSeconds,
			OutgoingExit->TimeSeconds,
			Settings.ForegroundTransitClearProgress);
		const double MinimumTotalSeconds =
			Settings.MinimumOutgoingReleaseSeconds
				+ Settings.DualBodyBridgeSeconds
				+ Settings.IncomingAcquireSeconds
				+ Settings.MinimumIncomingTrackSeconds
				+ Settings.EntryMatchSeconds;
		const double AvailableSeconds =
			IncomingEnter->TimeSeconds - ForegroundClearSeconds;
		if (!FMath::IsFinite(AvailableSeconds)
			|| AvailableSeconds <= UE_DOUBLE_SMALL_NUMBER)
		{
			return Reject(FString::Printf(
				TEXT("M7Assist%dNoTransitionWindow"), IncomingAssistIndex));
		}

		double OutgoingDuration = 0.0;
		double BridgeDuration = Settings.DualBodyBridgeSeconds;
		double AcquireDuration = Settings.IncomingAcquireSeconds;
		double TrackDuration = Settings.MinimumIncomingTrackSeconds;
		double EntryDuration = Settings.EntryMatchSeconds;
		if (AvailableSeconds + UE_DOUBLE_SMALL_NUMBER >= MinimumTotalSeconds)
		{
			const double LatestRevealStartSeconds =
				IncomingEnter->TimeSeconds
					- (BridgeDuration + AcquireDuration
						+ TrackDuration + EntryDuration);
			const double EarliestRevealStartSeconds = FMath::Max3(
				OutgoingClosest->TimeSeconds
					+ Settings.MinimumDepartureHoldSeconds,
				IncomingEnter->TimeSeconds
					- Settings.IncomingRevealLeadSeconds,
				ForegroundClearSeconds
					+ Settings.MinimumOutgoingReleaseSeconds);
			const double RevealStartSeconds = FMath::Min(
				FMath::Max(
					EarliestRevealStartSeconds,
					ForegroundClearSeconds
						+ Settings.OutgoingReleaseSeconds),
				LatestRevealStartSeconds);
			Timing.OutgoingStartSeconds = FMath::Max(
				ForegroundClearSeconds,
				RevealStartSeconds - Settings.OutgoingReleaseSeconds);
			OutgoingDuration =
				RevealStartSeconds - Timing.OutgoingStartSeconds;
		}
		else
		{
			// A valid gameplay path must not fail because its presentation window
			// is shorter than the preferred cut. Compress every protected phase
			// proportionally while retaining strict phase ordering and continuity.
			const double Compression = AvailableSeconds / MinimumTotalSeconds;
			OutgoingDuration =
				Settings.MinimumOutgoingReleaseSeconds * Compression;
			BridgeDuration *= Compression;
			AcquireDuration *= Compression;
			TrackDuration *= Compression;
			EntryDuration *= Compression;
			Timing.OutgoingStartSeconds = ForegroundClearSeconds;
			Timing.bAdaptiveCompression = true;
			bUsesAdaptiveCompression = true;
		}

		Timing.RevealStartSeconds =
			Timing.OutgoingStartSeconds + OutgoingDuration;
		Timing.BridgeEndSeconds =
			Timing.RevealStartSeconds + BridgeDuration;
		Timing.AcquireEndSeconds =
			Timing.BridgeEndSeconds + AcquireDuration;
		Timing.EntryStartSeconds =
			IncomingEnter->TimeSeconds - EntryDuration;
		if (!Timing.IsUsable())
		{
			return Reject(FString::Printf(
				TEXT("M7Assist%dScheduleRejected"), IncomingAssistIndex));
		}
	}
	ReleasedTrajectoryHash = Result.ValidationHash;
	return IsUsableFor(Result);
}

bool FABTSM11FinaleCameraShotPlan::IsUsableFor(
	const FABTSM11TrajectoryResult& Result) const
{
	if (ReleasedTrajectoryHash == 0
		|| ReleasedTrajectoryHash != Result.ValidationHash)
	{
		return false;
	}
	for (const FABTSM11FinaleCameraTransitionTiming& Timing : Transitions)
	{
		if (!Timing.IsUsable())
		{
			return false;
		}
	}
	return true;
}

const FABTSM11FinaleCameraTransitionTiming*
FABTSM11FinaleCameraShotPlan::FindTransition(
	const int32 IncomingAssistIndex) const
{
	for (const FABTSM11FinaleCameraTransitionTiming& Timing : Transitions)
	{
		if (Timing.IncomingAssistIndex == IncomingAssistIndex)
		{
			return &Timing;
		}
	}
	return nullptr;
}

bool FABTSM11FinaleCameraStageSelection::IsUsable() const
{
	return Stage != EABTSM11FinaleCameraStage::Unavailable
		&& FMath::IsFinite(StageProgress)
		&& StageProgress >= 0.0
		&& StageProgress <= 1.0
		&& FMath::IsFinite(StageDurationSeconds)
		&& StageDurationSeconds >= 0.0
		&& FMath::IsFinite(ShotProgress)
		&& ShotProgress >= 0.0
		&& ShotProgress <= 1.0
		&& FMath::IsFinite(ShotDurationSeconds)
		&& ShotDurationSeconds >= 0.0
		&& FMath::IsFinite(ShotPhaseProgress)
		&& ShotPhaseProgress >= 0.0
		&& ShotPhaseProgress <= 1.0
		&& FMath::IsFinite(ShotPhaseDurationSeconds)
		&& ShotPhaseDurationSeconds >= 0.0
		&& FMath::IsFinite(ShotEndSlope)
		&& ShotEndSlope >= 0.0
		&& FramingAssistIndex >= 0
		&& OutgoingAssistIndex >= 0
		&& OutgoingAssistIndex <= FABTSM11GravityScenario::AssistCount
		&& IncomingAssistIndex >= 0
		&& IncomingAssistIndex <= FABTSM11GravityScenario::AssistCount
		&& static_cast<uint8>(EndpointAuthority)
			<= static_cast<uint8>(
				EABTSM11FinaleCameraEndpointAuthority::PhysicalContact)
		&& (OutgoingAssistIndex == 0
			|| (IncomingAssistIndex > OutgoingAssistIndex
				&& IncomingAssistIndex != OutgoingAssistIndex))
		&& !TargetLabel.IsEmpty()
		&& !FramingTargetLabel.IsEmpty()
		&& !Reason.IsEmpty()
		&& !ShotReason.IsEmpty();
}

bool FABTSM11FinaleCameraStageSelection::IsM3IncomingShot() const
{
	return ShotPhase == EABTSM11FinaleCameraShotPhase::DualBodyBridge
		|| ShotPhase == EABTSM11FinaleCameraShotPhase::IncomingReveal
		|| ShotPhase == EABTSM11FinaleCameraShotPhase::IncomingTrack
		|| ShotPhase
			== EABTSM11FinaleCameraShotPhase::IncomingEntryMatch;
}

bool FABTSM11FinaleCameraStageSelection::IsM3DualBodyBridge() const
{
	return ShotPhase == EABTSM11FinaleCameraShotPhase::DualBodyBridge;
}

bool FABTSM11FinaleCameraStageSelection::IsM3InterBodyTransition() const
{
	return OutgoingAssistIndex >= 1
		&& OutgoingAssistIndex <= FABTSM11GravityScenario::AssistCount
		&& IncomingAssistIndex == OutgoingAssistIndex + 1
		&& (ShotPhase == EABTSM11FinaleCameraShotPhase::OutgoingHold
			|| ShotPhase == EABTSM11FinaleCameraShotPhase::DualBodyBridge
			|| ShotPhase == EABTSM11FinaleCameraShotPhase::IncomingReveal
			|| ShotPhase == EABTSM11FinaleCameraShotPhase::IncomingTrack
			|| ShotPhase
				== EABTSM11FinaleCameraShotPhase::IncomingEntryMatch);
}

bool FABTSM11FinaleCameraStageSelection::IsM3IncomingAcquire() const
{
	return ShotPhase == EABTSM11FinaleCameraShotPhase::IncomingReveal;
}

bool FABTSM11FinaleCameraStageSelection::IsM3TransitionShot() const
{
	return ShotPhase != EABTSM11FinaleCameraShotPhase::Authority;
}

bool FABTSM11FinaleCameraStageSelection::IsM4TerminalWindow() const
{
	return bTerminalTransition
		|| (bTargetIsUFO
			&& (Stage == EABTSM11FinaleCameraStage::FinalApproach
				|| Stage == EABTSM11FinaleCameraStage::Terminal));
}

bool FABTSM11FinaleCameraStageSelection::IsM4TerminalTransition() const
{
	return bTerminalTransition
		&& ShotPhase == EABTSM11FinaleCameraShotPhase::TerminalAcquire;
}

bool FABTSM11FinaleCameraStageSelection::IsM3AssistWindow() const
{
	return FramingAssistIndex >= 1
		&& FramingAssistIndex <= FABTSM11GravityScenario::AssistCount
		&& !bTargetIsUFO
		&& (Stage == EABTSM11FinaleCameraStage::CruiseToBody
			|| Stage == EABTSM11FinaleCameraStage::Handoff
			|| Stage == EABTSM11FinaleCameraStage::Approach
			|| Stage == EABTSM11FinaleCameraStage::Periapsis);
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
		&& (Selection.OutgoingAssistIndex == 0
			|| (FMath::IsFinite(OutgoingTargetCenter.X)
				&& FMath::IsFinite(OutgoingTargetCenter.Y)
				&& FMath::IsFinite(OutgoingTargetCenter.Z)
				&& FMath::IsFinite(OutgoingTargetRadiusCM)
				&& OutgoingTargetRadiusCM > 1.0))
		&& (Selection.OutgoingAssistIndex == 0
			|| (FMath::IsFinite(IncomingTargetCenter.X)
				&& FMath::IsFinite(IncomingTargetCenter.Y)
				&& FMath::IsFinite(IncomingTargetCenter.Z)
				&& FMath::IsFinite(IncomingTargetRadiusCM)
				&& IncomingTargetRadiusCM > 1.0))
		&& FMath::IsFinite(BirdRadiusCM)
		&& BirdRadiusCM > 1.0
		&& (!Selection.IsM4TerminalWindow()
			|| (FMath::IsFinite(TerminalTargetCenter.X)
				&& FMath::IsFinite(TerminalTargetCenter.Y)
				&& FMath::IsFinite(TerminalTargetCenter.Z)
				&& FMath::IsFinite(TerminalTargetRadiusCM)
				&& TerminalTargetRadiusCM > 1.0
				&& FMath::IsFinite(TerminalScreenRight.X)
				&& FMath::IsFinite(TerminalScreenRight.Y)
				&& FMath::IsFinite(TerminalScreenRight.Z)
				&& FMath::IsFinite(TerminalScreenUp.X)
				&& FMath::IsFinite(TerminalScreenUp.Y)
				&& FMath::IsFinite(TerminalScreenUp.Z)
				&& !TerminalScreenRight.IsNearlyZero()
				&& !TerminalScreenUp.IsNearlyZero()
				&& FMath::Abs(FVector::DotProduct(
					TerminalScreenRight.GetSafeNormal(),
					TerminalScreenUp.GetSafeNormal())) <= 1.0e-3f))
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

const TCHAR* ABTSM11FinaleCameraDirector::ShotPhaseLabel(
	const EABTSM11FinaleCameraShotPhase ShotPhase)
{
	switch (ShotPhase)
	{
	case EABTSM11FinaleCameraShotPhase::OutgoingHold:
		return TEXT("OutgoingHold");
	case EABTSM11FinaleCameraShotPhase::DualBodyBridge:
		return TEXT("DualBodyBridge");
	case EABTSM11FinaleCameraShotPhase::IncomingReveal:
		return TEXT("IncomingReveal");
	case EABTSM11FinaleCameraShotPhase::IncomingTrack:
		return TEXT("IncomingTrack");
	case EABTSM11FinaleCameraShotPhase::TerminalAcquire:
		return TEXT("TerminalAcquire");
	case EABTSM11FinaleCameraShotPhase::TerminalTrack:
		return TEXT("TerminalTrack");
	case EABTSM11FinaleCameraShotPhase::IncomingEntryMatch:
		return TEXT("IncomingEntryMatch");
	default:
		return TEXT("Authority");
	}
}

const TCHAR* ABTSM11FinaleCameraDirector::EndpointAuthorityLabel(
	const EABTSM11FinaleCameraEndpointAuthority EndpointAuthority)
{
	switch (EndpointAuthority)
	{
	case EABTSM11FinaleCameraEndpointAuthority::CandidateQualified:
		return TEXT("CandidateQualified");
	case EABTSM11FinaleCameraEndpointAuthority::PhysicalContact:
		return TEXT("PhysicalContact");
	default:
		return TEXT("None");
	}
}

FABTSM11FinaleCameraStageSelection
ABTSM11FinaleCameraDirector::ResolveStage(
	const bool bLaunched,
	const bool bTargetHit,
	const double PlaybackSeconds,
	const FABTSM11TrajectoryResult* Result,
	const bool bUseM3ShotPlan,
	const FABTSM11FinaleCameraShotSettings* M3ShotSettings,
	const FABTSM11FinaleCameraShotPlan* FrozenShotPlan)
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
		Selection.FramingAssistIndex = 0;
		Selection.StageProgress = 1.0;
		Selection.TargetLabel = TEXT("UFO");
		Selection.FramingTargetLabel = TEXT("UFO");
		Selection.Reason = TEXT("TargetHit");
		Selection.ShotPhase = EABTSM11FinaleCameraShotPhase::TerminalTrack;
		Selection.ShotReason = TEXT("UFOTerminalHold");
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
	const FABTSM11FinaleCameraShotSettings DefaultM3ShotSettings;
	const FABTSM11FinaleCameraShotSettings* ActiveM3ShotSettings =
		bUseM3ShotPlan
			? (M3ShotSettings != nullptr
				? M3ShotSettings
				: &DefaultM3ShotSettings)
			: nullptr;
	if (ActiveM3ShotSettings != nullptr
		&& !ActiveM3ShotSettings->IsUsable())
	{
		Selection.Stage = EABTSM11FinaleCameraStage::Unavailable;
		Selection.Reason = TEXT("M3ShotSettingsInvalid");
		return Selection;
	}
	const auto ApplyShotPlan = [&]() -> bool
	{
		return ActiveM3ShotSettings == nullptr
			|| ApplyM3ShotPlan(
				PlaybackSeconds,
				*Result,
				*ActiveM3ShotSettings,
				FrozenShotPlan,
				Selection);
	};

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
		Selection.FramingAssistIndex = AssistIndex;
		Selection.TargetLabel = FString::Printf(
			TEXT("Assist%d"), AssistIndex);
		Selection.FramingTargetLabel = Selection.TargetLabel;
		if (PlaybackSeconds < Enter->TimeSeconds)
		{
			Selection.Stage = AssistIndex == 1
				? EABTSM11FinaleCameraStage::CruiseToBody
				: EABTSM11FinaleCameraStage::Handoff;
			Selection.StageProgress = ResolveStageProgress(
				PlaybackSeconds,
				PreviousExitSeconds,
				Enter->TimeSeconds);
			Selection.StageDurationSeconds = FMath::Max(
				0.0,
				Enter->TimeSeconds - PreviousExitSeconds);
			Selection.Reason = AssistIndex == 1
				? TEXT("LaunchToAssist1")
				: FString::Printf(
					TEXT("Assist%dExitToAssist%d"),
					AssistIndex - 1,
					AssistIndex);
			if (!ApplyShotPlan())
			{
				Selection.Stage = EABTSM11FinaleCameraStage::Unavailable;
				Selection.Reason = TEXT("M3ShotEventsIncomplete");
			}
			return Selection;
		}
		if (PlaybackSeconds <= Closest->TimeSeconds)
		{
			Selection.Stage = EABTSM11FinaleCameraStage::Approach;
			Selection.StageProgress = ResolveStageProgress(
				PlaybackSeconds,
				Enter->TimeSeconds,
				Closest->TimeSeconds);
			Selection.StageDurationSeconds = FMath::Max(
				0.0,
				Closest->TimeSeconds - Enter->TimeSeconds);
			Selection.Reason = FString::Printf(
				TEXT("Assist%dEnter"), AssistIndex);
			if (!ApplyShotPlan())
			{
				Selection.Stage = EABTSM11FinaleCameraStage::Unavailable;
				Selection.Reason = TEXT("M3ShotEventsIncomplete");
			}
			return Selection;
		}
		if (PlaybackSeconds <= Exit->TimeSeconds)
		{
			Selection.Stage = EABTSM11FinaleCameraStage::Periapsis;
			Selection.StageProgress = ResolveStageProgress(
				PlaybackSeconds,
				Closest->TimeSeconds,
				Exit->TimeSeconds);
			Selection.StageDurationSeconds = FMath::Max(
				0.0,
				Exit->TimeSeconds - Closest->TimeSeconds);
			Selection.Reason = FString::Printf(
				TEXT("Assist%dClosestApproach"), AssistIndex);
			if (!ApplyShotPlan())
			{
				Selection.Stage = EABTSM11FinaleCameraStage::Unavailable;
				Selection.Reason = TEXT("M3ShotEventsIncomplete");
			}
			else if (ActiveM3ShotSettings != nullptr
				&& AssistIndex == FABTSM11GravityScenario::AssistCount
				&& Selection.StageProgress + UE_DOUBLE_SMALL_NUMBER
					>= ActiveM3ShotSettings->ForegroundTransitClearProgress)
			{
				Selection.ShotPhase =
					EABTSM11FinaleCameraShotPhase::TerminalAcquire;
				Selection.ShotPhaseProgress = ResolveStageProgress(
					Selection.StageProgress,
					ActiveM3ShotSettings->ForegroundTransitClearProgress,
					1.0);
				Selection.ShotProgress = Selection.ShotPhaseProgress;
				Selection.ShotPhaseDurationSeconds =
					FMath::Max(0.0,
						Exit->TimeSeconds - Closest->TimeSeconds)
					* (1.0
						- ActiveM3ShotSettings->ForegroundTransitClearProgress);
				Selection.ShotDurationSeconds =
					Selection.ShotPhaseDurationSeconds;
				Selection.ShotReason = TEXT("Assist3ToUFOAcquire");
				Selection.bTerminalTransition = true;
			}
			return Selection;
		}
		PreviousExitSeconds = Exit->TimeSeconds;
	}

	Selection.Stage = EABTSM11FinaleCameraStage::FinalApproach;
	Selection.AssistIndex = 0;
	Selection.FramingAssistIndex = 0;
	Selection.StageProgress = 0.0;
	Selection.ShotPhase = EABTSM11FinaleCameraShotPhase::TerminalTrack;
	Selection.ShotReason = TEXT("UFOFinalApproach");
	Selection.TargetLabel = TEXT("UFO");
	Selection.FramingTargetLabel = TEXT("UFO");
	Selection.Reason = TEXT("Assist3Exit");
	Selection.bTargetIsUFO = true;
	Selection.bTerminalTransition = true;
	return Selection;
}

bool ABTSM11FinaleCameraDirector::ApplyM4TerminalTimeline(
	const double PlaybackSeconds,
	const double TerminalStartSeconds,
	const double TerminalEndSeconds,
	FABTSM11FinaleCameraStageSelection& InOutSelection)
{
	if (!FMath::IsFinite(PlaybackSeconds)
		|| !FMath::IsFinite(TerminalStartSeconds)
		|| !FMath::IsFinite(TerminalEndSeconds)
		|| TerminalEndSeconds <= TerminalStartSeconds
		|| (InOutSelection.Stage
				!= EABTSM11FinaleCameraStage::FinalApproach
			&& InOutSelection.Stage
				!= EABTSM11FinaleCameraStage::Terminal))
	{
		return false;
	}
	const double DurationSeconds = TerminalEndSeconds - TerminalStartSeconds;
	const double Progress = ResolveStageProgress(
		PlaybackSeconds,
		TerminalStartSeconds,
		TerminalEndSeconds);
	InOutSelection.StageProgress = Progress;
	InOutSelection.StageDurationSeconds = DurationSeconds;
	InOutSelection.ShotProgress = Progress;
	InOutSelection.ShotDurationSeconds = DurationSeconds;
	InOutSelection.ShotPhaseProgress = Progress;
	InOutSelection.ShotPhaseDurationSeconds = DurationSeconds;
	return InOutSelection.IsUsable();
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

void ABTSM11FinaleCameraDirector::SetM3Enabled(const bool bEnabled)
{
	CVarABTSM11CameraDirectorM3Enabled->Set(
		bEnabled ? 1 : 0,
		ECVF_SetByCode);
}

bool ABTSM11FinaleCameraDirector::IsM3Enabled()
{
	return CVarABTSM11CameraDirectorM3Enabled.GetValueOnGameThread() != 0;
}
