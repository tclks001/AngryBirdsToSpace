// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleInteractionSystem.h"

#include "ABTSRuntime.h"
#include "Async/Async.h"
#include "Components/CapsuleComponent.h"
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

struct FABTSM11PreviewSolvePayload
{
	int64 Revision = INDEX_NONE;
	FABTSM11FinaleLaunchInput Input;
	FABTSM11TrajectoryResult QualifiedResult;
	FABTSM11PrefixClassification Classification;
	FABTSM11TrajectoryResult SameInputPhysicalResult;
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

void AABTSM11FinaleInteractionSystem::QueuePreviewSolveIfNeeded()
{
	if (!bPreviewDirty
		|| bPreviewSolveInFlight
		|| !IsValid(FinaleSystem)
		|| (InteractionState != EABTSM11FinaleInteractionState::Aiming
			&& InteractionState
				!= EABTSM11FinaleInteractionState::ReleasePending)
		|| PreviewSubmitAccumulatorSeconds
			< PreviewSubmitIntervalSeconds)
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
	bPreviewDirty = false;
	bPreviewSolveInFlight = true;
	PreviewSubmitAccumulatorSeconds = 0.0;
	PreviewSolveFuture = Async(
		EAsyncExecution::ThreadPool,
		[
			Preset,
			QualifiedRequest,
			Input,
			SubmittedRevision]()
		{
			TSharedPtr<FABTSM11PreviewSolvePayload> Payload =
				MakeShared<FABTSM11PreviewSolvePayload>();
			Payload->Revision = SubmittedRevision;
			Payload->Input = Input;
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
				if (Payload->Classification.IsF(4))
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
			return Payload;
		});
}

void AABTSM11FinaleInteractionSystem::QueueNominalPhysicalSolve()
{
	if (bNominalSolveInFlight
		|| bNominalPhysicalReady
		|| !IsValid(FinaleSystem))
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
	if (PreviewSolveFuture.IsValid()
		&& PreviewSolveFuture.IsReady())
	{
		HandlePreviewSolveCompleted(
			PreviewSolveFuture.Consume());
	}
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

	const bool bStale = Payload->Revision != AimRevision
		|| !SameSolvedInput(
			Payload->Input,
			InteractionState
				== EABTSM11FinaleInteractionState::ReleasePending
				? FrozenReleaseInput
				: Stabilizer.GetControlledInput());
	if (bStale)
	{
		bPreviewDirty = true;
		return;
	}

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
		PreviewSubmitAccumulatorSeconds = PreviewSubmitIntervalSeconds;
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
	if (!PreviewPlaybackPlan.Build(
		FinaleSystem->GetLayoutPreset(),
		LatestQualifiedResult,
		CurrentClassification,
		Physical,
		Nominal))
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
	MarkTargetCaptureDirty();
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
			|| !PreviewPlaybackPlan.bPhysicalTargetHit))
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
		ReleasedPlaybackPlan.bPhysicalTargetHit
		? ReleasedPlaybackPlan.DurationSeconds
		: ABTSM11ResolveFailurePresentationEndTime(
			FinaleSystem->GetLayoutPreset(),
			LatestQualifiedResult,
			ReleasedPlaybackPlan,
			BirdClearanceCM);
	if (!ReleasedPlaybackPlan.bPhysicalTargetHit)
	{
		const double FailureDurationCap =
			FMath::Max(1.0, MaximumFailureFlightDisplaySeconds)
			* FMath::Max(0.1, PlaybackTimeScale);
		PlaybackPresentationEndTimeSeconds = FMath::Min(
			PlaybackPresentationEndTimeSeconds,
			ReleasedPlaybackPlan.Points[0].TimeSeconds
				+ FailureDurationCap);
	}
	InteractionState = EABTSM11FinaleInteractionState::Launched;
	if (IsValid(ActiveCord))
	{
		ActiveCord->ResetPouchVisualToRest();
	}
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-C][Release] Source=0x%016llx Plan=0x%016llx F4=%d Physical=%d Transfer=%d PresentationEnd=%.3f"),
		ReleasedPlaybackPlan.ReleasedTrajectoryHash,
		ReleasedPlaybackPlan.PlanHash,
		ReleasedPlaybackPlan.bQualifiedF4 ? 1 : 0,
		ReleasedPlaybackPlan.bPhysicalTargetHit ? 1 : 0,
		ReleasedPlaybackPlan.bUsesVisibleTerminalTransfer ? 1 : 0,
		PlaybackPresentationEndTimeSeconds);
	return true;
}
