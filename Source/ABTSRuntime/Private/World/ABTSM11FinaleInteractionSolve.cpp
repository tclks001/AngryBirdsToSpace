// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleInteractionSystem.h"

#include "ABTSRuntime.h"
#include "Async/Async.h"
#include "Components/CapsuleComponent.h"
#include "Camera/ABTSM11FinaleFlightCamera.h"
#include "HAL/PlatformTime.h"
#include "Player/ABTSM25BirdCharacter.h"
#include "World/ABTSM11FinaleLayoutCertification.h"
#include "World/ABTSM11FinaleSystem.h"
#include "World/ABTSM11GravityAssistSolver.h"
#include "World/ABTSM51WorldActors.h"

namespace
{
	bool SameSolvedInput(
		const FABTSM11FinaleLaunchInput& A,
		const FABTSM11FinaleLaunchInput& B)
	{
		return A.YawDegrees == B.YawDegrees
			&& A.PitchDegrees == B.PitchDegrees
			&& A.Power == B.Power;
	}
}

void AABTSM11FinaleInteractionSystem::SetNextReleaseCameraDirectorModeOverride(
	const EABTSM11FinaleCameraDirectorMode Mode)
{
	NextReleaseCameraDirectorModeOverride = static_cast<int8>(Mode);
}

struct FABTSM11PreviewSolvePayload
{
	int64 Revision = INDEX_NONE;
	FABTSM11FinaleLaunchInput Input;
	FABTSM11TrajectoryResult QualifiedResult;
	FABTSM11PrefixClassification Classification;
	FABTSM11TrajectoryResult SameInputPhysicalResult;
	double SubmittedPlatformSeconds = 0.0;
	double SolveDurationMilliseconds = 0.0;
	bool bHasSameInputPhysicalResult = false;
	bool bSolved = false;
	FString Failure;
};

struct FABTSM11NominalSolvePayload
{
	FABTSM11TrajectoryResult Result;
	bool bSolved = false;
	FString Failure;
};

struct FABTSM11F4GuidanceSolvePayload
{
	FABTSM11F4GuidanceTarget Target;
	double SolveDurationMilliseconds = 0.0;
	bool bSolved = false;
	FString Failure;
};

void AABTSM11FinaleInteractionSystem::QueuePreviewSolveIfNeeded()
{
	if (!ABTSM11CanStartLatestOnlyPreview(
			bPreviewDirty,
			bPreviewSolveInFlight)
		|| !IsValid(FinaleSystem)
		|| (InteractionState != EABTSM11FinaleInteractionState::Aiming
			&& InteractionState
				!= EABTSM11FinaleInteractionState::ReleasePending))
	{
		return;
	}
	const FABTSM11FinaleLaunchInput Input =
		InteractionState
			== EABTSM11FinaleInteractionState::ReleasePending
		? FrozenReleaseInput
		: Stabilizer.GetControlledInput();
	FABTSM11TrajectoryRequest QualifiedRequest;
	FString BuildFailure;
	if (!FinaleSystem->BuildRequest(
		Input,
		0x7u,
		QualifiedRequest,
		&BuildFailure))
	{
		FailInteraction(FString::Printf(
			TEXT("PreviewRequest:%s"),
			*BuildFailure));
		return;
	}

	const FABTSM11FinaleLayoutPreset Preset =
		FinaleSystem->GetLayoutPreset();
	const int64 SubmittedRevision = AimRevision;
	const double SubmittedPlatformSeconds = FPlatformTime::Seconds();
	const bool bEditorCandidate =
		FinaleSystem->IsEditorCandidateMode();
	bPreviewDirty = false;
	bPreviewSolveInFlight = true;
	PreviewSolveFuture = Async(
		EAsyncExecution::ThreadPool,
		[
			Preset,
			QualifiedRequest,
			Input,
			SubmittedRevision,
			SubmittedPlatformSeconds,
			bEditorCandidate]()
		{
			const double SolveStartSeconds = FPlatformTime::Seconds();
			TSharedPtr<FABTSM11PreviewSolvePayload> Payload =
				MakeShared<FABTSM11PreviewSolvePayload>();
			Payload->Revision = SubmittedRevision;
			Payload->Input = Input;
			Payload->SubmittedPlatformSeconds =
				SubmittedPlatformSeconds;
			Payload->bSolved =
				FABTSM11GravityAssistSolver::Solve(
					QualifiedRequest,
					Payload->QualifiedResult,
					&Payload->Failure);
			if (Payload->bSolved)
			{
				Payload->Classification =
					FABTSM11PrefixClassifier::Classify(
						Preset,
						Payload->QualifiedResult,
						0x7u);
				if (!bEditorCandidate
					&& Payload->Classification.IsF(4))
				{
					FABTSM11TrajectoryRequest PhysicalRequest;
					if (Preset.BuildPhysicalPlaybackRequest(
						Input,
						0x7u,
						PhysicalRequest,
						&Payload->Failure))
					{
						Payload->bHasSameInputPhysicalResult =
							FABTSM11GravityAssistSolver::Solve(
								PhysicalRequest,
								Payload->SameInputPhysicalResult,
								&Payload->Failure);
					}
				}
			}
			Payload->SolveDurationMilliseconds =
				(FPlatformTime::Seconds() - SolveStartSeconds) * 1000.0;
			return Payload;
		});
}

void AABTSM11FinaleInteractionSystem::QueueNominalPhysicalSolve()
{
	if (bNominalSolveInFlight
		|| bNominalPhysicalReady
		|| !IsValid(FinaleSystem)
		|| FinaleSystem->IsEditorCandidateMode())
	{
		return;
	}
	FABTSM11TrajectoryRequest Request;
	FString BuildFailure;
	const FABTSM11FinaleLayoutPreset Preset =
		FinaleSystem->GetLayoutPreset();
	if (!FinaleSystem->BuildPhysicalPlaybackRequest(
		Preset.NominalInput,
		Request,
		&BuildFailure))
	{
		FailInteraction(FString::Printf(
			TEXT("NominalPhysicalRequest:%s"),
			*BuildFailure));
		return;
	}

	bNominalSolveInFlight = true;
	NominalSolveFuture = Async(
		EAsyncExecution::ThreadPool,
		[Request]()
		{
			TSharedPtr<FABTSM11NominalSolvePayload> Payload =
				MakeShared<FABTSM11NominalSolvePayload>();
			Payload->bSolved =
				FABTSM11GravityAssistSolver::Solve(
					Request,
					Payload->Result,
					&Payload->Failure);
			return Payload;
		});
}

void AABTSM11FinaleInteractionSystem::QueueF4GuidanceSolve()
{
	if (bF4GuidanceInFlight
		|| F4GuidanceTarget.bValid
		|| !IsValid(FinaleSystem))
	{
		return;
	}
	const FABTSM11FinaleLayoutPreset Preset =
		FinaleSystem->GetLayoutPreset();
	F4GuidanceFailure.Reset();
	bF4GuidanceInFlight = true;
	F4GuidanceFuture = Async(
		EAsyncExecution::ThreadPool,
		[Preset]()
		{
			const double StartSeconds = FPlatformTime::Seconds();
			TSharedPtr<FABTSM11F4GuidanceSolvePayload> Payload =
				MakeShared<FABTSM11F4GuidanceSolvePayload>();
			Payload->bSolved = FABTSM11F4GuidanceBuilder::Build(
				Preset,
				Payload->Target,
				&Payload->Failure);
			Payload->SolveDurationMilliseconds =
				(FPlatformTime::Seconds() - StartSeconds) * 1000.0;
			return Payload;
		});
}

void AABTSM11FinaleInteractionSystem::DrainCompletedSolves()
{
	check(IsInGameThread());
	// Poll futures only from the Actor's native Tick. A ThreadPool completion
	// must never create a GameThread task carrying the worker's empty FAppTime
	// inherited context into SceneCapture or component render updates.
	if (NominalSolveFuture.IsValid()
		&& NominalSolveFuture.IsReady())
	{
		HandleNominalSolveCompleted(
			NominalSolveFuture.Consume());
	}
	if (F4GuidanceFuture.IsValid()
		&& F4GuidanceFuture.IsReady())
	{
		HandleF4GuidanceSolveCompleted(
			F4GuidanceFuture.Consume());
	}
	if (PreviewSolveFuture.IsValid()
		&& PreviewSolveFuture.IsReady())
	{
		HandlePreviewSolveCompleted(
			PreviewSolveFuture.Consume());
	}
}

void AABTSM11FinaleInteractionSystem::HandleF4GuidanceSolveCompleted(
	TSharedPtr<FABTSM11F4GuidanceSolvePayload> Payload)
{
	check(IsInGameThread());
	bF4GuidanceInFlight = false;
	if (!Payload.IsValid()
		|| !Payload->bSolved
		|| !IsValid(FinaleSystem)
		|| !Payload->Target.IsValid(
			FinaleSystem->GetLayoutPreset().LaunchModel))
	{
		F4GuidanceTarget = FABTSM11F4GuidanceTarget();
		F4GuidanceFailure = Payload.IsValid()
			? Payload->Failure
			: TEXT("MissingF4GuidancePayload");
		UE_LOG(
			LogABTSRuntime,
			Warning,
			TEXT("[ABTS][M11-C][F4Guide] Unavailable Failure=%s"),
			*F4GuidanceFailure);
		return;
	}
	F4GuidanceTarget = Payload->Target;
	F4GuidanceFailure.Reset();
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-C][F4Guide] Ready Yaw=%.6f Pitch=%.6f Power=%.6f Samples=%d F4=%d Truncated=%d DurationMS=%.3f"),
		F4GuidanceTarget.Input.YawDegrees,
		F4GuidanceTarget.Input.PitchDegrees,
		F4GuidanceTarget.Input.Power,
		F4GuidanceTarget.SampleCount,
		F4GuidanceTarget.F4SampleCount,
		F4GuidanceTarget.bSearchTruncated ? 1 : 0,
		Payload->SolveDurationMilliseconds);
}

void AABTSM11FinaleInteractionSystem::HandlePreviewSolveCompleted(
	TSharedPtr<FABTSM11PreviewSolvePayload> Payload)
{
	check(IsInGameThread());
	bPreviewSolveInFlight = false;
	if (InteractionState != EABTSM11FinaleInteractionState::Aiming
		&& InteractionState
			!= EABTSM11FinaleInteractionState::ReleasePending)
	{
		// Exit/recovery invalidates presentation publication. The worker owns
		// only pure data, so a late result can be discarded without cleanup.
		return;
	}
	if (!Payload.IsValid() || !Payload->bSolved)
	{
		const FString Reason = Payload.IsValid()
			? Payload->Failure
			: TEXT("MissingPreviewPayload");
		if (InteractionState
			== EABTSM11FinaleInteractionState::ReleasePending)
		{
			FailInteraction(FString::Printf(
				TEXT("ReleaseSolve:%s"),
				*Reason));
		}
		else
		{
			RuntimeFailure = FString::Printf(
				TEXT("PreviewSolve:%s"),
				*Reason);
		}
		return;
	}

	const bool bInputMatches = SameSolvedInput(
		Payload->Input,
		InteractionState
			== EABTSM11FinaleInteractionState::ReleasePending
			? FrozenReleaseInput
			: Stabilizer.GetControlledInput());
	const bool bStale = !ABTSM11CanPublishLatestOnlyPreview(
		Payload->Revision,
		AimRevision,
		bInputMatches);
	if (bStale)
	{
		++DiscardedPreviewSolveCount;
		bPreviewDirty = true;
		QueuePreviewSolveIfNeeded();
		return;
	}

	LastPreviewSolveMilliseconds =
		Payload->SolveDurationMilliseconds;
	LastPreviewLatencyMilliseconds =
		(FPlatformTime::Seconds()
			- Payload->SubmittedPlatformSeconds) * 1000.0;
	LatestSolvedRevision = Payload->Revision;
	LatestSolvedInput = Payload->Input;
	LatestQualifiedResult = MoveTemp(Payload->QualifiedResult);
	CurrentClassification = Payload->Classification;
	bLatestPhysicalResultAvailable =
		Payload->bHasSameInputPhysicalResult;
	if (bLatestPhysicalResultAvailable)
	{
		LatestSameInputPhysicalResult =
			MoveTemp(Payload->SameInputPhysicalResult);
	}
	else
	{
		LatestSameInputPhysicalResult.Reset();
	}
	RuntimeFailure = ABTSM11FailureReasonLabel(
		ABTSM11ClassifyFailure(
			LatestQualifiedResult,
			CurrentClassification));
	const FABTSM11TrajectoryResult& TargetSelectionResult =
		bLatestPhysicalResultAvailable
			&& (CurrentClassification.ValidAssistMask & 0x7u) == 0x7u
		? LatestSameInputPhysicalResult
		: LatestQualifiedResult;
	PreviewSelection = TargetSelector.Update(
		0.0,
		FinaleSystem->GetLayoutPreset(),
		TargetSelectionResult,
		CurrentClassification);
	RebuildPublishedPreview();

	if (InteractionState
		== EABTSM11FinaleInteractionState::ReleasePending)
	{
		FinalizePendingRelease();
	}
	if (bPreviewDirty)
	{
		QueuePreviewSolveIfNeeded();
	}
}

void AABTSM11FinaleInteractionSystem::HandleNominalSolveCompleted(
	TSharedPtr<FABTSM11NominalSolvePayload> Payload)
{
	check(IsInGameThread());
	bNominalSolveInFlight = false;
	if (!Payload.IsValid()
		|| !Payload->bSolved
		|| !IsValid(FinaleSystem)
		|| !Payload->Result.DidHitTarget()
		|| Payload->Result.ValidationHash
			!= FinaleSystem->GetLayoutPreset()
				.PhysicalPlaybackTrajectoryHash)
	{
		FailInteraction(FString::Printf(
			TEXT("NominalPhysicalSolve:%s"),
			Payload.IsValid()
				? *Payload->Failure
				: TEXT("MissingPayload")));
		return;
	}
	NominalPhysicalResult = MoveTemp(Payload->Result);
	bNominalPhysicalReady = true;
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-C][Playback] NominalReady Hash=0x%016llx Points=%d"),
		NominalPhysicalResult.ValidationHash,
		NominalPhysicalResult.Points.Num());
	if (DoesInputMatchLatestSolve())
	{
		RebuildPublishedPreview();
	}
	if (InteractionState
		== EABTSM11FinaleInteractionState::ReleasePending)
	{
		FinalizePendingRelease();
	}
}

void AABTSM11FinaleInteractionSystem::RebuildPublishedPreview()
{
	if (!IsValid(FinaleSystem)
		|| LatestQualifiedResult.ValidationHash == 0)
	{
		return;
	}
	const FABTSM11TrajectoryResult* Physical =
		bLatestPhysicalResultAvailable
			? &LatestSameInputPhysicalResult
			: nullptr;
	const FABTSM11TrajectoryResult* Nominal =
		bNominalPhysicalReady ? &NominalPhysicalResult : nullptr;
	const bool bBuilt = FinaleSystem->IsEditorCandidateMode()
		? PreviewPlaybackPlan.BuildCandidatePresentationContact(
			FinaleSystem->GetLayoutPreset(),
			LatestQualifiedResult,
			CurrentClassification)
		: PreviewPlaybackPlan.Build(
			FinaleSystem->GetLayoutPreset(),
			LatestQualifiedResult,
			CurrentClassification,
			Physical,
			Nominal);
	if (!bBuilt)
	{
		const FString PlanFailure = PreviewPlaybackPlan.Failure;
		if (InteractionState
				== EABTSM11FinaleInteractionState::ReleasePending
			&& CurrentClassification.IsF(4)
			&& bNominalPhysicalReady)
		{
			FailInteraction(FString::Printf(
				TEXT("ReleasePlaybackPlan:%s"),
				*PlanFailure));
			return;
		}
		// While the certified nominal cache is still arriving, publish the
		// exact player-qualified path but keep Release fail-closed.
		FABTSM11PrefixClassification Fallback =
			CurrentClassification;
		Fallback.HighestPrefixLevel = static_cast<uint8>(
			FMath::Min<int32>(
				Fallback.HighestPrefixLevel,
				3));
		if (!PreviewPlaybackPlan.Build(
			FinaleSystem->GetLayoutPreset(),
			LatestQualifiedResult,
			Fallback,
			nullptr,
			nullptr))
		{
			RuntimeFailure = PreviewPlaybackPlan.Failure;
			return;
		}
		RuntimeFailure = PlanFailure;
	}
	FABTSM11OrbitalDiagramBuilder::Build(
		FinaleSystem->GetLayoutPreset(),
		FinaleSystem->GetFinaleFrame(),
		PreviewPlaybackPlan.Points,
		LatestQualifiedResult.ValidationHash,
		DiagramSnapshot);
	RebuildHudPublishedData();
	if (!bTargetCaptureInitialized)
	{
		MarkTargetCaptureDirty();
	}
}

void AABTSM11FinaleInteractionSystem::RebuildHudPublishedData()
{
	check(IsInGameThread());
	/*
	 * A direct physical F4 playback is already the authoritative trajectory
	 * the player will see.  The HUD used to always rebuild from the qualified
	 * solve and only consumed PlaybackPlan for a visible terminal transfer;
	 * that silently replaced an accepted circular physical tail with the
	 * qualified solver's sparse display polyline.  Keep transfer plans on the
	 * qualified source (AppendPlaybackExtension below owns their handoff), but
	 * otherwise publish the exact same-input physical authority.
	 */
	const FABTSM11TrajectoryResult& HudAuthorityResult =
		bLatestPhysicalResultAvailable
		&& PreviewPlaybackPlan.bPhysicalTargetHit
		&& !PreviewPlaybackPlan.bUsesVisibleTerminalTransfer
		&& LatestSameInputPhysicalResult.ValidationHash != 0
		&& PreviewPlaybackPlan.PhysicalTrajectoryHash
			== LatestSameInputPhysicalResult.ValidationHash
		? LatestSameInputPhysicalResult
		: LatestQualifiedResult;
	if (!IsValid(FinaleSystem)
		|| !FABTSM11OrbitalSceneBuilder::Build(
			FinaleSystem->GetLayoutPreset(),
			HudAuthorityResult,
			HudOrbitalScene,
			900))
	{
		HudOrbitalScene = FABTSM11OrbitalSceneSnapshot();
		HudOverviewProjection = FABTSM11OverviewProjection();
		HudProbeProjection = FABTSM11ProbeProjection();
		return;
	}
	if (!HudOverviewView.bValid)
	{
		const FVector3d InitialAxisX = DiagramSnapshot.bValid
			? DiagramSnapshot.PlaneAxisX
			: FVector3d::ForwardVector;
		const FVector3d InitialAxisY = DiagramSnapshot.bValid
			? DiagramSnapshot.PlaneAxisY
			: FVector3d::RightVector;
		if (!HudOverviewView.InitializeFromScene(
			HudOrbitalScene,
			InitialAxisX,
			InitialAxisY))
		{
			HudOverviewProjection = FABTSM11OverviewProjection();
			return;
		}
		InitialHudOverviewView = HudOverviewView;
	}

	if (PreviewPlaybackPlan.bUsesVisibleTerminalTransfer)
	{
		// Projection cannot enlarge a three-dimensional chord error. This
		// conservative world-space threshold therefore keeps the deletion-only
		// HUD polyline within 0.35 px even at the maximum overview zoom and a
		// substantially larger diagram than the current layout permits.
		constexpr double MaximumCurveErrorPixels = 0.35;
		constexpr double MaximumSupportedDiagramRadiusPixels = 512.0;
		constexpr double MaximumOverviewZoom = 4.0;
		const double MaximumChordErrorCM = MaximumCurveErrorPixels
			* HudOverviewView.ProjectionScaleCM
			/ (MaximumSupportedDiagramRadiusPixels * MaximumOverviewZoom);
		FABTSM11OrbitalSceneBuilder::AppendPlaybackExtension(
			PreviewPlaybackPlan,
			HudOrbitalScene,
			720,
			MaximumChordErrorCM);
	}

	if (!FABTSM11OverviewProjector::Build(
		HudOrbitalScene,
		HudOverviewView,
		HudOverviewProjection))
	{
		HudOverviewProjection = FABTSM11OverviewProjection();
		return;
	}
	++HudOverviewRevision;
	UE_LOG(
		LogABTSRuntime,
		Verbose,
		TEXT("[ABTS][M11-C][HUD] OrbitalAuthority Source=0x%016llx Physical=%d Transfer=%d"),
		HudAuthorityResult.ValidationHash,
		&HudAuthorityResult == &LatestSameInputPhysicalResult ? 1 : 0,
		PreviewPlaybackPlan.bUsesVisibleTerminalTransfer ? 1 : 0);

	if (HudTrajectoryProbe.bValid)
	{
		if (!FABTSM11TrajectoryProbeResolver::Resolve(
			HudOrbitalScene,
			HudTrajectoryProbe,
			HudProbeProjection))
		{
			HudProbeProjection = FABTSM11ProbeProjection();
		}
		++HudProbeRevision;
	}
}

bool AABTSM11FinaleInteractionSystem::FinalizePendingRelease()
{
	if (InteractionState
			!= EABTSM11FinaleInteractionState::ReleasePending
		|| !DoesInputMatchLatestSolve()
		|| !SameSolvedInput(
			FrozenReleaseInput,
			LatestSolvedInput))
	{
		return false;
	}
	if (CurrentClassification.IsF(4)
		&& (!PreviewPlaybackPlan.bQualifiedF4
			|| (FinaleSystem->IsEditorCandidateMode()
				? (!PreviewPlaybackPlan.bCandidateQualifiedIntercept
					|| !PreviewPlaybackPlan.bPhysicalTargetHit)
				: !PreviewPlaybackPlan.bPhysicalTargetHit)))
	{
		return false;
	}
	if (PreviewPlaybackPlan.Points.Num() < 2
		|| PreviewPlaybackPlan.ReleasedTrajectoryHash
			!= LatestQualifiedResult.ValidationHash)
	{
		FailInteraction(TEXT("ReleasePreviewIdentityMismatch"));
		return false;
	}

	ReleasedPlaybackPlan = PreviewPlaybackPlan;
	FString FormationPathFailure;
	if (!FormationPath.Build(ReleasedPlaybackPlan, &FormationPathFailure))
	{
		ReleasedPlaybackPlan.Reset();
		FailInteraction(
			FormationPathFailure.IsEmpty()
				? TEXT("FormationPathBuildRejected")
				: FormationPathFailure);
		return false;
	}
	ReleasedCameraTrajectoryResult = LatestQualifiedResult;
	ReleasedCameraDirectorMode = NextReleaseCameraDirectorModeOverride
		>= 0
		? static_cast<EABTSM11FinaleCameraDirectorMode>(
			NextReleaseCameraDirectorModeOverride)
		: ABTSM11FinaleCameraDirector::ResolveProductionDirectorMode();
	ReleasedCameraShotPlan.Reset();
	bCameraDirectorFallbackLogged = false;
	FABTSM11FinaleCameraShotSettings PlaybackShotSettings;
	FString CameraScheduleFailure;
	const double CameraPlaybackScale =
		FinaleSystem->IsEditorCandidateMode()
			? 1.0
			: FMath::Max(0.1, PlaybackTimeScale);
	const FABTSM11FinaleCameraShotSettings PresentationShotSettings =
		IsValid(FlightCamera)
			? FlightCamera->GetM3ShotSettings()
			: FABTSM11FinaleCameraShotSettings();
	const bool bCameraScheduleBuilt =
		PresentationShotSettings.BuildPlaybackClockSettings(
			CameraPlaybackScale,
			PlaybackShotSettings)
		&& ReleasedCameraShotPlan.Build(
			ReleasedCameraTrajectoryResult,
			PlaybackShotSettings,
			&CameraScheduleFailure);
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-C][M7] ReleaseCameraPlan Source=0x%016llx Built=%d Adaptive=%d DirectorMode=%s CaptureOverride=%d Failure=%s"),
		ReleasedCameraTrajectoryResult.ValidationHash,
		bCameraScheduleBuilt ? 1 : 0,
		ReleasedCameraShotPlan.bUsesAdaptiveCompression ? 1 : 0,
		ABTSM11FinaleCameraDirector::DirectorModeLabel(
			ReleasedCameraDirectorMode),
		NextReleaseCameraDirectorModeOverride >= 0 ? 1 : 0,
		CameraScheduleFailure.IsEmpty() ? TEXT("None") : *CameraScheduleFailure);
	PlaybackElapsedSeconds =
		ReleasedPlaybackPlan.Points[0].TimeSeconds;
	double BirdClearanceCM = 50.0;
	if (IsValid(AttemptBird)
		&& IsValid(AttemptBird->GetCapsuleComponent()))
	{
		BirdClearanceCM =
			AttemptBird->GetCapsuleComponent()
				->GetScaledCapsuleHalfHeight()
			+ 10.0;
	}
	PlaybackPresentationEndTimeSeconds =
		(ReleasedPlaybackPlan.bPhysicalTargetHit
			|| ReleasedPlaybackPlan.bCandidateQualifiedIntercept)
		? ReleasedPlaybackPlan.DurationSeconds
		: ABTSM11ResolveFailurePresentationEndTime(
			FinaleSystem->GetLayoutPreset(),
			LatestQualifiedResult,
			ReleasedPlaybackPlan,
			BirdClearanceCM);
	const double EffectivePlaybackTimeScale =
		FinaleSystem->IsEditorCandidateMode()
			? 1.0
			: FMath::Max(0.1, PlaybackTimeScale);
	if (!ReleasedPlaybackPlan.bPhysicalTargetHit
		&& !ReleasedPlaybackPlan.bCandidateQualifiedIntercept)
	{
		const double FailureDurationCap =
			FMath::Max(1.0, MaximumFailureFlightDisplaySeconds)
			* EffectivePlaybackTimeScale;
		PlaybackPresentationEndTimeSeconds = FMath::Min(
			PlaybackPresentationEndTimeSeconds,
			ReleasedPlaybackPlan.Points[0].TimeSeconds
				+ FailureDurationCap);
	}
	FABTSM11FailurePresentationConfig DesiredFailureConfig;
	DesiredFailureConfig.ReadableHoldSeconds =
		FailureReadableHoldSeconds;
	DesiredFailureConfig.FadeToBlackSeconds =
		FailureFadeToBlackSeconds;
	DesiredFailureConfig.BlackHoldSeconds = FailureBlackHoldSeconds;
	DesiredFailureConfig.FadeFromBlackSeconds =
		FailureFadeFromBlackSeconds;
	ReleasedFailurePresentationConfig = DesiredFailureConfig;
	FailurePresentationStartTimeSeconds =
		PlaybackPresentationEndTimeSeconds;
	if (!ReleasedPlaybackPlan.bPhysicalTargetHit
		&& !ReleasedPlaybackPlan.bCandidateQualifiedIntercept
		&& !ABTSM11ResolveFailurePresentationSchedule(
			ReleasedPlaybackPlan.Points[0].TimeSeconds,
			PlaybackPresentationEndTimeSeconds,
			EffectivePlaybackTimeScale,
			DesiredFailureConfig,
			FailurePresentationStartTimeSeconds,
			ReleasedFailurePresentationConfig))
	{
		FailInteraction(TEXT("FailurePresentationScheduleRejected"));
		return false;
	}
	InteractionState = EABTSM11FinaleInteractionState::Launched;
	if (IsValid(ActiveCord))
	{
		ActiveCord->ResetPouchVisualToRest();
	}
	PlayFinaleAudioCue(EABTSM11FinaleAudioCue::ReleaseCommitted);
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-C][Release] Source=0x%016llx Plan=0x%016llx F4=%d CandidateQualified=%d Physical=%d Transfer=%d FailureStart=%.3f PresentationEnd=%.3f"),
		ReleasedPlaybackPlan.ReleasedTrajectoryHash,
		ReleasedPlaybackPlan.PlanHash,
		ReleasedPlaybackPlan.bQualifiedF4 ? 1 : 0,
		ReleasedPlaybackPlan.bCandidateQualifiedIntercept ? 1 : 0,
		ReleasedPlaybackPlan.bPhysicalTargetHit ? 1 : 0,
		ReleasedPlaybackPlan.bUsesVisibleTerminalTransfer ? 1 : 0,
		FailurePresentationStartTimeSeconds,
		PlaybackPresentationEndTimeSeconds);
	return true;
}
