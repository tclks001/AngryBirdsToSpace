// Copyright Epic Games, Inc. All Rights Reserved.

#include "World/ABTSM11FinaleInteractionSystem.h"

#include "ABTSRuntime.h"
#include "Async/Async.h"
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
	TWeakObjectPtr<AABTSM11FinaleInteractionSystem> WeakThis(this);
	Async(
		EAsyncExecution::ThreadPool,
		[
			WeakThis,
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
			AsyncTask(
				ENamedThreads::GameThread,
				[WeakThis, Payload]()
				{
					if (WeakThis.IsValid())
					{
						WeakThis->HandlePreviewSolveCompleted(Payload);
					}
				});
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
	TWeakObjectPtr<AABTSM11FinaleInteractionSystem> WeakThis(this);
	Async(
		EAsyncExecution::ThreadPool,
		[WeakThis, Request]()
		{
			TSharedPtr<FABTSM11NominalSolvePayload> Payload =
				MakeShared<FABTSM11NominalSolvePayload>();
			Payload->bSolved =
				FABTSM11GravityAssistSolver::Solve(
					Request,
					Payload->Result,
					&Payload->Failure);
			AsyncTask(
				ENamedThreads::GameThread,
				[WeakThis, Payload]()
				{
					if (WeakThis.IsValid())
					{
						WeakThis->HandleNominalSolveCompleted(Payload);
					}
				});
		});
}

void AABTSM11FinaleInteractionSystem::HandlePreviewSolveCompleted(
	TSharedPtr<FABTSM11PreviewSolvePayload> Payload)
{
	bPreviewSolveInFlight = false;
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
	UpdateTargetCapture();
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
	InteractionState = EABTSM11FinaleInteractionState::Launched;
	if (IsValid(ActiveCord))
	{
		ActiveCord->ResetPouchVisualToRest();
	}
	UE_LOG(
		LogABTSRuntime,
		Log,
		TEXT("[ABTS][M11-C][Release] Source=0x%016llx Plan=0x%016llx F4=%d Physical=%d Transfer=%d"),
		ReleasedPlaybackPlan.ReleasedTrajectoryHash,
		ReleasedPlaybackPlan.PlanHash,
		ReleasedPlaybackPlan.bQualifiedF4 ? 1 : 0,
		ReleasedPlaybackPlan.bPhysicalTargetHit ? 1 : 0,
		ReleasedPlaybackPlan.bUsesVisibleTerminalTransfer ? 1 : 0);
	return true;
}
